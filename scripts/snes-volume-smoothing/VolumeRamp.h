#ifndef ARGOSY_CORE_VOLUME_RAMP_H
#define ARGOSY_CORE_VOLUME_RAMP_H

#include <stdint.h>

class CoreVolumeRamp {
public:
    void reset(int volume) {
        current = target = int64_t(volume) * 65536;
        remaining = 0;
    }

    int scale(int sample, int volume) {
        const int64_t requested = int64_t(volume) * 65536;
        if (target != requested) {
            target = requested;
            remaining = 64;
        }
        if (remaining > 0) {
            current += (target - current) / remaining;
            --remaining;
        }
        return int((int64_t(sample) * current) >> 23);
    }

private:
    int64_t current = 0;
    int64_t target = 0;
    int remaining = 0;
};

#endif
