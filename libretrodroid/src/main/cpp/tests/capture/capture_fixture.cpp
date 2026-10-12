#include "audiocapture.h"
#include "audiodiagnostics.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }
extern "C" int __android_log_write(int, const char*, const char*) { return 0; }
extern "C" int __system_property_get(const char*, char* value) { value[0] = 0; return 0; }

static void require(bool result) {
    if (!result) {
        throw std::runtime_error("capture state invariant failed");
    }
}

static std::vector<int16_t> signal(size_t frames) {
    std::vector<int16_t> samples(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        samples[i * 2] = int(i * 17 % 60000) - 30000;
        samples[i * 2 + 1] = 30000 - int(i * 31 % 60000);
    }
    return samples;
}

int main(int argc, char** argv) {
    require(argc == 2);
    using libretrodroid::AudioCapture;
    using libretrodroid::AudioDiagnostics;
    const std::filesystem::path root(argv[1]);
    for (const char* name : {"normal", "aborted", "overflow", "idle"}) {
        std::filesystem::create_directories(root / name);
    }
    AudioDiagnostics::setDirectory((root / "normal").string());
    AudioDiagnostics::beginSession();
    {
        AudioCapture capture((root / "normal").string(), 32040, 48000, 1);
        const auto input = signal(32040);
        const auto output = signal(48000);
        capture.recordCore(input.data(), 100);
        require(capture.start());
        require(!capture.start());
        std::thread coreWriter([&] {
            for (size_t i = 0; i < 32040; i += 257) {
                capture.recordCore(input.data() + i * 2, std::min(size_t(257), 32040 - i));
            }
        });
        capture.recordOutput(output.data(), 24000);
        capture.saveWhenComplete();
        require(!std::filesystem::exists(root / "normal/audio-core.wav"));
        require(!std::filesystem::exists(root / "normal/audio-output.wav"));
        coreWriter.join();
        for (size_t i = 24000; i < 48000; i += 144) {
            capture.recordOutput(output.data() + i * 2, std::min(size_t(144), 48000 - i));
        }
        capture.recordCore(input.data(), 100);
        capture.recordOutput(output.data(), 100);
        capture.saveWhenComplete();
        require(!capture.start());
    }
    AudioDiagnostics::setDirectory((root / "aborted").string());
    AudioDiagnostics::beginSession();
    {
        AudioCapture capture((root / "aborted").string(), 10, 10, 1);
        const auto samples = signal(5);
        require(capture.start());
        capture.recordCore(samples.data(), 5);
        capture.recordOutput(samples.data(), 5);
        capture.abort();
        capture.recordCore(samples.data(), 5);
        capture.recordOutput(samples.data(), 5);
        capture.saveWhenComplete();
    }
    AudioDiagnostics::setDirectory((root / "overflow").string());
    AudioDiagnostics::beginSession();
    {
        AudioCapture capture((root / "overflow").string(), 10, 10, 1);
        const auto samples = signal(40);
        require(capture.start());
        capture.recordCore(samples.data(), 40);
        capture.recordOutput(samples.data(), 10);
        capture.saveWhenComplete();
    }
    {
        AudioCapture capture((root / "idle").string(), 10, 10, 1);
        const auto samples = signal(10);
        capture.recordCore(samples.data(), 10);
        capture.recordOutput(samples.data(), 10);
        capture.saveWhenComplete();
    }
}
