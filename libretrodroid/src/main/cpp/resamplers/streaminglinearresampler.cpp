#include "streaminglinearresampler.h"

#include <algorithm>
#include <cmath>

namespace libretrodroid {

void StreamingLinearResampler::reset() {
    position = -2.0;
    history.fill(0);
    initialized = false;
}

void StreamingLinearResampler::resample(const int16_t* source, int32_t inputFrames,
                                       int16_t* sink, int32_t sinkFrames,
                                       double inputFramesPerOutputFrame) {
    if (!initialized && inputFrames > 0) {
        history = {source[0], source[1], source[0], source[1], source[0], source[1]};
        initialized = true;
    }

    const auto sample = [&](int32_t frame, int channel) -> int16_t {
        if (frame < 0) {
            return history[std::clamp(frame + 3, 0, 2) * 2 + channel];
        }
        if (inputFrames == 0) {
            return history[4 + channel];
        }
        return source[std::min(frame, inputFrames - 1) * 2 + channel];
    };

    for (int32_t frame = 0; frame < sinkFrames; ++frame) {
        const auto lower = static_cast<int32_t>(std::floor(position));
        const double fraction = position - lower;
        for (int channel = 0; channel < 2; ++channel) {
            const double value = sample(lower, channel) * (1.0 - fraction) +
                                 sample(lower + 1, channel) * fraction;
            *sink++ = static_cast<int16_t>(std::lround(value));
        }
        position += inputFramesPerOutputFrame;
    }
    position -= inputFrames;

    if (inputFrames >= 3) {
        std::copy_n(source + (inputFrames - 3) * 2, 6, history.begin());
    } else if (inputFrames == 2) {
        history = {history[4], history[5], source[0], source[1], source[2], source[3]};
    } else if (inputFrames == 1) {
        history = {history[2], history[3], history[4], history[5], source[0], source[1]};
    }
}

}
