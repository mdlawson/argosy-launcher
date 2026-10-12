#ifndef LIBRETRODROID_AUDIOCAPTURE_H
#define LIBRETRODROID_AUDIOCAPTURE_H

#ifndef NDEBUG
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace libretrodroid {

class AudioCapture {
public:
    AudioCapture(std::string directory, int32_t coreRate, int32_t outputRate,
                 unsigned seconds = 20);
    ~AudioCapture();
    bool start();
    void recordCore(const int16_t* data, size_t frames);
    void recordOutput(const int16_t* data, size_t frames);
    void saveWhenComplete();
    void abort();

private:
    enum class State { Idle, Recording, Complete, Aborted, Saving, Saved };
    static bool saveWav(const std::string& path, const std::vector<int16_t>& samples,
                        size_t frames, int32_t sampleRate);
    void record(const int16_t* data, size_t frames, bool output);

    std::string directory;
    int32_t coreRate;
    int32_t outputRate;
    std::vector<int16_t> coreSamples;
    std::vector<int16_t> outputSamples;
    size_t coreFrames = 0;
    size_t outputFrames = 0;
    size_t droppedCoreFrames = 0;
    std::atomic<State> state {State::Idle};
    std::atomic<unsigned> writers {0};
    std::thread savingThread;
};

}
#endif

#endif
