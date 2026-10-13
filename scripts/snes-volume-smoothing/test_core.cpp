#include "apu/bapu/snes/snes.hpp"
#include "apu/apu.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

SSettings Settings {};
namespace SNES { CPU cpu; }

static void require(bool condition) {
    if (!condition) throw std::runtime_error("Core smoothing test failed");
}

static uint32_t read32(const uint8* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

static std::vector<int16> replay(std::vector<uint8> sound, size_t frames, bool snapshots, bool power) {
    if (power) SNES::dsp.power();
    uint8* position = sound.data();
    SNES::smp.load_state(&position);
    SNES::dsp.load_state(&position);
    require(position + 16 <= sound.data() + sound.size());
    SNES::dsp.clock = int32_t(read32(position + 8));
    std::copy_n(position + 12, 4, SNES::cpu.registers);
    std::array<int16, 4096> samples {};
    std::vector<int16> output;
    output.reserve(frames * 2);
    while (output.size() / 2 < frames) {
        const size_t requested = std::min(size_t(128), frames - output.size() / 2);
        SNES::dsp.spc_dsp.set_output(samples.data(), int(samples.size()));
        SNES::smp.clock -= int(requested * 32);
        SNES::smp.enter();
        SNES::dsp.synchronize();
        const int count = SNES::dsp.spc_dsp.sample_count();
        require(count > 0 && count <= int(samples.size()) && count % 2 == 0);
        const size_t copied = std::min(size_t(count / 2), frames - output.size() / 2);
        output.insert(output.end(), samples.begin(), samples.begin() + copied * 2);
        if (snapshots) {
            std::array<uint8, 66560> snapshot {};
            uint8* saved = snapshot.data();
            SNES::smp.save_state(&saved);
            SNES::dsp.save_state(&saved);
            require(saved == snapshot.data() + 66342);
        }
    }
    return output;
}

int main(int argc, char** argv) {
    try {
        std::fill(std::begin(Settings.ChannelsVolumePercent), std::end(Settings.ChannelsVolumePercent), 100);
        Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;
        SNES::smp.power();
        SNES::dsp.power();
        std::vector<uint8> sound(66560);
        size_t frames = 6408;
        if (argc == 3) {
            std::ifstream input(argv[1], std::ios::binary);
            sound.assign(std::istreambuf_iterator<char>(input), {});
            require(sound.size() == 66560);
            frames = 640800;
        } else {
            require(argc == 2 && std::string(argv[1]) == "--self-test");
            uint8* position = sound.data();
            SNES::smp.save_state(&position);
            SNES::dsp.save_state(&position);
        }
        const auto baseline = replay(sound, frames, false, true);
        require(replay(sound, frames, true, false) == baseline);
        SNES::dsp.spc_dsp.soft_reset();
        require(replay(sound, frames, false, false) == baseline);
        if (argc == 3) {
            std::ofstream output(argv[2], std::ios::binary);
            output.exceptions(std::ios::failbit | std::ios::badbit);
            output.write(reinterpret_cast<const char*>(baseline.data()), baseline.size() * sizeof(int16));
            output.close();
        } else {
            require(std::all_of(baseline.begin(), baseline.end(), [](int16 sample) { return sample == 0; }));
        }
        std::cout << "PASS: snapshot byte layout, periodic serialization, state reload, soft-reset/reload\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
