#include "audiocapture.h"

#ifndef NDEBUG
#include "audiodiagnostics.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <system_error>

namespace libretrodroid {

AudioCapture::AudioCapture(std::string directory, int32_t coreRate, int32_t outputRate,
                           unsigned seconds)
    : directory(std::move(directory)), coreRate(coreRate), outputRate(outputRate),
      coreSamples(size_t(coreRate) * (seconds + 2) * 2),
      outputSamples(size_t(outputRate) * seconds * 2) {}

AudioCapture::~AudioCapture() {
    abort();
    while (writers.load() != 0) {
        std::this_thread::yield();
    }
    if (savingThread.joinable()) {
        savingThread.join();
    }
}

bool AudioCapture::start() {
    auto expected = State::Idle;
    return state.compare_exchange_strong(expected, State::Recording);
}

void AudioCapture::abort() {
    auto expected = state.load();
    while (expected == State::Idle || expected == State::Recording) {
        if (state.compare_exchange_weak(expected, State::Aborted)) {
            break;
        }
    }
}

void AudioCapture::recordCore(const int16_t* data, size_t frames) {
    record(data, frames, false);
}

void AudioCapture::recordOutput(const int16_t* data, size_t frames) {
    record(data, frames, true);
}

void AudioCapture::record(const int16_t* data, size_t frames, bool output) {
    if (state.load() != State::Recording) {
        return;
    }
    writers.fetch_add(1);
    if (state.load() == State::Recording) {
        auto& samples = output ? outputSamples : coreSamples;
        auto& count = output ? outputFrames : coreFrames;
        const size_t copied = std::min(frames, samples.size() / 2 - count);
        if (copied > 0) {
            std::memcpy(samples.data() + count * 2, data, copied * 2 * sizeof(int16_t));
        }
        count += copied;
        if (output && count == samples.size() / 2) {
            auto expected = State::Recording;
            state.compare_exchange_strong(expected, State::Complete);
        } else if (!output) {
            droppedCoreFrames += frames - copied;
        }
    }
    writers.fetch_sub(1);
}

void AudioCapture::saveWhenComplete() {
    const auto current = state.load();
    if (current != State::Complete && current != State::Aborted) {
        return;
    }
    while (writers.load() != 0) {
        std::this_thread::yield();
    }
    state.store(State::Saving);
    const bool complete = current == State::Complete;
    try {
        savingThread = std::thread([this, complete] {
            const bool coreSaved = saveWav(directory + "/audio-core.wav", coreSamples, coreFrames, coreRate);
            const bool outputSaved = saveWav(directory + "/audio-output.wav", outputSamples, outputFrames, outputRate);
            const bool success = complete && coreSaved && outputSaved && coreFrames > 0 && droppedCoreFrames == 0;
            AudioDiagnostics::log("AudioDiag capture saved success=%d core_hz=%d output_hz=%d core_frames=%llu output_frames=%llu dropped_core_frames=%llu complete=%d",
                success, coreRate, outputRate, (unsigned long long) coreFrames,
                (unsigned long long) outputFrames, (unsigned long long) droppedCoreFrames, complete);
            state.store(State::Saved);
        });
    } catch (const std::system_error& error) {
        AudioDiagnostics::log("AudioDiag capture saved success=0 error=%s", error.what());
        state.store(State::Saved);
    }
}

bool AudioCapture::saveWav(const std::string& path, const std::vector<int16_t>& samples,
                           size_t frames, int32_t sampleRate) {
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
    std::array<unsigned char, 44> header {};
    const auto put16 = [&](size_t offset, uint16_t value) {
        header[offset] = value;
        header[offset + 1] = value >> 8;
    };
    const auto put32 = [&](size_t offset, uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            header[offset + byte] = value >> (byte * 8);
        }
    };
    std::memcpy(header.data(), "RIFF", 4);
    put32(4, 36 + frames * 4);
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 2);
    put32(24, sampleRate);
    put32(28, sampleRate * 4);
    put16(32, 4);
    put16(34, 16);
    std::memcpy(header.data() + 36, "data", 4);
    put32(40, frames * 4);
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    const bool headerSaved = std::fwrite(header.data(), 1, header.size(), file) == header.size();
    const bool dataSaved = std::fwrite(samples.data(), sizeof(int16_t), frames * 2, file) == frames * 2;
    const bool closed = std::fclose(file) == 0;
    return headerSaved && dataSaved && closed;
}

}
#endif
