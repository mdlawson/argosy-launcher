#include "streaminglinearresampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

using libretrodroid::StreamingLinearResampler;

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

static std::vector<int16_t> signal(int frames, double inputRate) {
    std::vector<int16_t> samples(frames * 2);
    for (int frame = 0; frame < frames; ++frame) {
        samples[frame * 2] = std::lround(20000 * std::sin(frame * 6283.185307179586 / inputRate));
        samples[frame * 2 + 1] = std::lround(14000 * std::cos(frame * 19477.874452256 / inputRate));
    }
    return samples;
}

static int16_t referenceSample(const std::vector<int16_t>& input, double position, int channel) {
    position = std::clamp(position, 0.0, double(input.size() / 2 - 1));
    const int lower = std::floor(position);
    const int upper = std::min(lower + 1, int(input.size() / 2 - 1));
    const double fraction = position - lower;
    return std::lround(input[lower * 2 + channel] * (1.0 - fraction) +
                       input[upper * 2 + channel] * fraction);
}

static void partitionInvariant(int inputRate, int outputRate) {
    const auto input = signal(inputRate, inputRate);
    std::vector<int16_t> whole(outputRate * 2), chunks(outputRate * 2);
    const double ratio = double(inputRate) / outputRate;
    StreamingLinearResampler resampler;
    resampler.resample(input.data(), inputRate, whole.data(), outputRate, ratio);
    resampler.reset();
    double pending = 0.0;
    int consumed = 0;
    int block = 0;
    const int sizes[] {192, 1, 127, 240, 3, 960, 64, 511};
    for (int output = 0; output < outputRate;) {
        const int count = std::min(sizes[block++ % 8], outputRate - output);
        pending += count * ratio;
        const int available = std::lround(pending);
        pending -= available;
        require(consumed + available <= inputRate, "input accounting overflow");
        resampler.resample(input.data() + consumed * 2, available,
                           chunks.data() + output * 2, count, ratio);
        consumed += available;
        output += count;
    }
    require(consumed == inputRate, "input accounting lost samples");
    for (int output = 0; output < outputRate; ++output) {
        for (int channel = 0; channel < 2; ++channel) {
            const int index = output * 2 + channel;
            require(std::abs(int(whole[index]) - int(chunks[index])) <= 1,
                    "callback partition changed the waveform");
            require(std::abs(int(chunks[index]) - int(referenceSample(input, output * ratio - 2, channel))) <= 1,
                    "output deviated from continuous interpolation");
        }
    }
}

static void varyingRate() {
    const auto input = signal(96000, 32040);
    StreamingLinearResampler resampler;
    double pending = 0.0;
    double position = -2.0;
    int consumed = 0;
    for (int block = 0; block < 200; ++block) {
        const int count = 192 + (block % 7) * 13;
        const double ratio = (32040.0 / 48000) * (1.0 + 0.02 * std::sin(block * 0.07));
        pending += count * ratio;
        const int available = std::lround(pending);
        pending -= available;
        std::vector<int16_t> output(count * 2);
        resampler.resample(input.data() + consumed * 2, available, output.data(), count, ratio);
        for (int frame = 0; frame < count; ++frame) {
            for (int channel = 0; channel < 2; ++channel) {
                require(std::abs(int(output[frame * 2 + channel]) -
                    int(referenceSample(input, position, channel))) <= 1,
                    "rate adjustment lost fractional position");
            }
            position += ratio;
        }
        consumed += available;
    }
}

static void resetAndEmptyInput() {
    StreamingLinearResampler resampler;
    int16_t output[4] {};
    resampler.resample(nullptr, 0, output, 2, 0.1);
    require(output[0] == 0 && output[1] == 0 && output[2] == 0 && output[3] == 0,
            "empty input was not silent");
    const int16_t input[] {32767, -32768, 32767, -32768};
    resampler.reset();
    resampler.resample(input, 2, output, 2, 1.0);
    require(output[0] == 32767 && output[1] == -32768 &&
            output[2] == 32767 && output[3] == -32768, "full-scale stereo was corrupted");
    resampler.reset();
    resampler.resample(nullptr, 0, output, 2, 0.1);
    require(output[0] == 0 && output[1] == 0, "reset retained old audio");
}

int main() {
    try {
        partitionInvariant(32040, 48000);
        partitionInvariant(32000, 48000);
        partitionInvariant(44100, 48000);
        partitionInvariant(48000, 48000);
        partitionInvariant(96000, 48000);
        partitionInvariant(8000, 48000);
        varyingRate();
        resetAndEmptyInput();
        std::puts("PASS: callback partition invariance, continuous reference, changing rates, stereo, reset, empty input");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
