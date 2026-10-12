#include "audiodiagnostics.h"

#ifndef NDEBUG
#include <android/log.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {
    std::mutex reportMutex;
    std::string reportDirectory;
    FILE* reportFile = nullptr;
}
#endif

namespace libretrodroid {

void AudioDiagnostics::setDirectory(const std::string& directory) {
#ifndef NDEBUG
    std::lock_guard<std::mutex> lock(reportMutex);
    reportDirectory = directory;
#else
    (void) directory;
#endif
}

void AudioDiagnostics::beginSession() {
#ifndef NDEBUG
    std::string directory;
    {
        std::lock_guard<std::mutex> lock(reportMutex);
        if (reportFile) {
            std::fclose(reportFile);
        }
        directory = reportDirectory;
        reportFile = std::fopen((directory + "/audio-diagnostics.txt").c_str(), "w");
        if (!reportFile) {
            __android_log_print(ANDROID_LOG_ERROR, "libretrodroid",
                                "AudioDiag report open failed: %s", std::strerror(errno));
        }
    }
    log("AudioDiag report format=2 directory=%s", directory.c_str());
#endif
}

bool AudioDiagnostics::legacyRequested() {
#ifndef NDEBUG
    bool legacyMarker;
    bool streamingMarker;
    {
        std::lock_guard<std::mutex> lock(reportMutex);
        legacyMarker = !reportDirectory.empty() &&
            access((reportDirectory + "/audio-legacy").c_str(), F_OK) == 0;
        streamingMarker = !reportDirectory.empty() &&
            access((reportDirectory + "/audio-streaming").c_str(), F_OK) == 0;
    }
    char property[PROP_VALUE_MAX] {};
    __system_property_get("debug.argosy.audio.legacy", property);
    const bool result = legacyMarker || (!streamingMarker && property[0] == '1');
    log("AudioDiag selector legacy_marker=%d streaming_marker=%d property=%s selected=%s",
        legacyMarker, streamingMarker, property[0] ? property : "unset",
        result ? "legacy" : "streaming");
    return result;
#else
    return false;
#endif
}

void AudioDiagnostics::log(const char* format, ...) {
#ifndef NDEBUG
    char line[2048];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    __android_log_write(ANDROID_LOG_INFO, "libretrodroid", line);
    std::lock_guard<std::mutex> lock(reportMutex);
    if (reportFile) {
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::fprintf(reportFile, "%lld %s\n", (long long) milliseconds, line);
        std::fflush(reportFile);
    }
#else
    (void) format;
#endif
}

std::string AudioDiagnostics::directory() {
#ifndef NDEBUG
    std::lock_guard<std::mutex> lock(reportMutex);
    return reportDirectory;
#else
    return {};
#endif
}

bool AudioDiagnostics::consumeCaptureRequest() {
#ifndef NDEBUG
    const auto folder = directory();
    return !folder.empty() && std::remove((folder + "/audio-capture").c_str()) == 0;
#else
    return false;
#endif
}

}
