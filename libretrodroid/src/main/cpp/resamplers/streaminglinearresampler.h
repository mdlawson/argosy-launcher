#ifndef LIBRETRODROID_STREAMINGLINEARRESAMPLER_H
#define LIBRETRODROID_STREAMINGLINEARRESAMPLER_H

#include <array>
#include <cstdint>

namespace libretrodroid {

class StreamingLinearResampler {
public:
    void resample(const int16_t* source, int32_t inputFrames, int16_t* sink,
                  int32_t sinkFrames, double inputFramesPerOutputFrame);
    void reset();

private:
    double position = -2.0;
    std::array<int16_t, 6> history {};
    bool initialized = false;
};

}

#endif
