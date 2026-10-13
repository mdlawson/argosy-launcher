#include "VolumeRamp.h"
#include <stdexcept>
#include <iostream>

static void require(bool condition) {
    if (!condition) throw std::runtime_error("Volume ramp test failed");
}

int main() {
    CoreVolumeRamp left;
    CoreVolumeRamp right;
    left.reset(32);
    right.reset(-32);
    require(left.scale(4096, 32) == 1024);
    require(right.scale(4096, -32) == -1024);
    for (int i = 1; i <= 64; ++i) {
        require(left.scale(4096, 34) == 1024 + i);
        require(right.scale(4096, -32) == -1024);
    }
    require(left.scale(4096, 34) == 1088);
    for (int i = 1; i <= 64; ++i) require(left.scale(4096, 32) == 1088 - i);
    for (int i = 0; i < 12; ++i) left.scale(4096, 40);
    const int beforeRetarget = left.scale(4096, 40);
    const int afterRetarget = left.scale(4096, 20);
    require(afterRetarget < beforeRetarget && beforeRetarget - afterRetarget <= 16);
    for (int i = 1; i < 64; ++i) left.scale(4096, 20);
    require(left.scale(4096, 20) == 640);
    left.reset(-40);
    require(left.scale(-4096, -40) == 1280);
    require(left.scale(4096, -40) == -1280);
    left.reset(127);
    require(left.scale(32767, 127) == (32767 * 127 >> 7));
    left.reset(-128);
    require(left.scale(-32768, -128) == 32768);
    std::cout << "PASS: signed gain, exact ramp duration, stereo independence, retargeting, reseeding, endpoints\n";
}
