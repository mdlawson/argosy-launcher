#ifndef LIBRETRODROID_AUDIODIAGNOSTICS_H
#define LIBRETRODROID_AUDIODIAGNOSTICS_H

#include <string>

namespace libretrodroid {

class AudioDiagnostics {
public:
    static void setDirectory(const std::string& directory);
    static void beginSession();
    static bool legacyRequested();
    static void log(const char* format, ...) __attribute__((format(printf, 1, 2)));
};

}

#endif
