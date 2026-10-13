#include "apu/bapu/snes/snes.hpp"
#include "apu/apu.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

SSettings Settings {};
namespace SNES { CPU cpu; }

namespace {
constexpr int sampleRate = 32040;
constexpr int soundStateBytes = 66560;
constexpr int volumeRampFrames = 64;
int rampFrames;
size_t producedFrames;
FILE* traceFile;

struct VolumeRamp {
    int64_t current;
    int64_t target;
    int remaining;
    bool initialized;
};

VolumeRamp ramps[8][2] {};

uint32_t read32(const uint8* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}

namespace SNES {
int volume_amp(int output, int volume, int voice, int channel) {
    if (rampFrames == 0) return (output * volume) >> 7;
    auto& ramp = ramps[voice][channel];
    const int64_t target = int64_t(volume) * 65536;
    if (!ramp.initialized) ramp = {target, target, 0, true};
    if (ramp.target != target) {
        ramp.target = target;
        ramp.remaining = rampFrames;
    }
    if (ramp.remaining > 0) {
        ramp.current += (ramp.target - ramp.current) / ramp.remaining;
        --ramp.remaining;
    }
    return int((int64_t(output) * ramp.current) >> 23);
}

void trace_dsp_write(uint8 address, uint8 data) {
    const int old = dsp.spc_dsp.read(address);
    if (traceFile && address < 128 && address % 16 < 2 && old != data) {
        const size_t frame = producedFrames + dsp.spc_dsp.sample_count() / 2;
        std::fprintf(traceFile, "%zu,%u,%d,%d,%d\n", frame, address / 16 + 1,
                     address % 16, int8_t(old), int8_t(data));
    }
}
}

namespace {
std::vector<uint8> extractSoundState(const std::vector<uint8>& state) {
    const std::string magic = "#!s9xsnp:0014\n";
    if (state.size() < magic.size() || !std::equal(magic.begin(), magic.end(), state.begin())) {
        throw std::runtime_error("Expected an uncompressed Snes9x version 14 save state from the diagnostic APK.");
    }
    size_t position = magic.size();
    std::vector<uint8> sound;
    while (position < state.size()) {
        if (state.size() - position < 11 || state[position + 3] != ':' || state[position + 10] != ':') {
            throw std::runtime_error("Invalid save-state block header.");
        }
        size_t size = 0;
        for (size_t i = position + 4; i < position + 10; ++i) {
            if (state[i] < '0' || state[i] > '9') throw std::runtime_error("Invalid save-state block size.");
            size = size * 10 + state[i] - '0';
        }
        const bool isSound = state[position] == 'S' && state[position + 1] == 'N' && state[position + 2] == 'D';
        position += 11;
        if (size > state.size() - position) throw std::runtime_error("Truncated save-state block.");
        if (isSound) {
            if (!sound.empty() || size != soundStateBytes) throw std::runtime_error("Unexpected sound-state format.");
            sound.assign(state.begin() + position, state.begin() + position + size);
        }
        position += size;
    }
    if (sound.empty()) throw std::runtime_error("Save state has no sound-chip snapshot.");
    return sound;
}

std::vector<int16> replay(std::vector<uint8> sound, int smoothing, size_t frames) {
    std::fill(std::begin(Settings.ChannelsVolumePercent), std::end(Settings.ChannelsVolumePercent), 100);
    Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;
    SNES::dsp.power();
    uint8* position = sound.data();
    SNES::smp.load_state(&position);
    SNES::dsp.load_state(&position);
    if (position + 16 > sound.data() + sound.size()) throw std::runtime_error("Sound state ended unexpectedly.");
    SNES::dsp.clock = int32_t(read32(position + 8));
    std::copy_n(position + 12, 4, SNES::cpu.registers);
    rampFrames = smoothing;
    producedFrames = 0;
    std::memset(ramps, 0, sizeof(ramps));
    std::vector<int16> output;
    output.reserve(frames * 2);
    std::array<int16, 4096> samples {};
    while (producedFrames < frames) {
        const size_t requested = std::min(size_t(128), frames - producedFrames);
        SNES::dsp.spc_dsp.set_output(samples.data(), int(samples.size()));
        SNES::smp.clock -= int(requested * 32);
        SNES::smp.enter();
        SNES::dsp.synchronize();
        const int count = SNES::dsp.spc_dsp.sample_count();
        if (count <= 0 || count > int(samples.size()) || count % 2 != 0) throw std::runtime_error("Unexpected audio batch size.");
        const size_t copied = std::min(size_t(count / 2), frames - producedFrames);
        output.insert(output.end(), samples.begin(), samples.begin() + copied * 2);
        producedFrames += copied;
    }
    return output;
}

void saveWav(const std::filesystem::path& path, const std::vector<int16>& samples) {
    std::array<char, 44> header {};
    const auto put16 = [&](size_t offset, uint16_t value) {
        header[offset] = char(value);
        header[offset + 1] = char(value >> 8);
    };
    const auto put32 = [&](size_t offset, uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) header[offset + byte] = char(value >> (byte * 8));
    };
    std::memcpy(header.data(), "RIFF", 4);
    put32(4, uint32_t(36 + samples.size() * 2));
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 2);
    put32(24, sampleRate);
    put32(28, sampleRate * 4);
    put16(32, 4);
    put16(34, 16);
    std::memcpy(header.data() + 36, "data", 4);
    put32(40, uint32_t(samples.size() * 2));
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.write(header.data(), header.size());
    for (int16 sample : samples) {
        const std::array<char, 2> bytes {char(uint16_t(sample)), char(uint16_t(sample) >> 8)};
        output.write(bytes.data(), bytes.size());
    }
    output.close();
}

void require(bool condition) {
    if (!condition) throw std::runtime_error("Self-test failed.");
}

void selfTest() {
    rampFrames = 0;
    require(SNES::volume_amp(-4096, -40, 0, 0) == 1280);
    rampFrames = volumeRampFrames;
    std::memset(ramps, 0, sizeof(ramps));
    require(SNES::volume_amp(4096, 32, 0, 0) == 1024);
    for (int i = 1; i <= volumeRampFrames; ++i) require(SNES::volume_amp(4096, 34, 0, 0) == 1024 + i);
    require(SNES::volume_amp(4096, 34, 0, 0) == 1088);
    for (int i = 1; i <= volumeRampFrames; ++i) require(SNES::volume_amp(4096, 32, 0, 0) == 1088 - i);
    require(SNES::volume_amp(4096, -32, 1, 1) == -1024);
    SNES::smp.power();
    SNES::dsp.power();
    std::vector<uint8> sound(soundStateBytes);
    uint8* position = sound.data();
    SNES::smp.save_state(&position);
    SNES::dsp.save_state(&position);
    const auto audio = replay(sound, 0, 6408);
    require(audio.size() == 12816);
    require(std::all_of(audio.begin(), audio.end(), [](int16 sample) { return sample == 0; }));
    auto invalid = std::vector<uint8> {'#'};
    bool rejected = false;
    try { extractSoundState(invalid); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
    std::cout << "PASS: signed volume, exact 64-frame ramps, separate voices/channels, sound-state restore, silence replay, invalid input\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            selfTest();
            return 0;
        }
        if (argc != 2) throw std::runtime_error("Usage: CeresAudioCompare.exe ceres-snes9x.state");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Could not open the save-state file.");
        const auto size = std::filesystem::file_size(argv[1]);
        if (size > 4 * 1024 * 1024) throw std::runtime_error("Save state exceeds the supported size.");
        const std::vector<uint8> state((std::istreambuf_iterator<char>(input)), {});
        const auto sound = extractSoundState(state);
        const auto original = replay(sound, 0, sampleRate * 20);
        traceFile = std::fopen("ceres-volume-writes.csv", "w");
        if (!traceFile) throw std::runtime_error("Could not create the volume trace.");
        std::fprintf(traceFile, "frame,voice,channel,old_volume,new_volume\n");
        const auto smoothed = replay(sound, volumeRampFrames, sampleRate * 20);
        const bool traceSaved = std::fclose(traceFile) == 0;
        traceFile = nullptr;
        if (!traceSaved) throw std::runtime_error("Could not save the volume trace.");
        saveWav("ceres-original.wav", original);
        saveWav("ceres-smoothed.wav", smoothed);
        std::cout << "Saved ceres-original.wav and ceres-smoothed.wav (20 seconds).\n"
                     "Only voice-volume transitions are smoothed over 64 samples (about 2 ms).\n";
        return 0;
    } catch (const std::exception& error) {
        if (traceFile) std::fclose(traceFile);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
