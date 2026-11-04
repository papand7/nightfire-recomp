// LAN play (sdk core/netplay.h): every copy runs the whole game from the same controllers. Its check that they still
// compute the same game, once a second: each player camera's (0x1F661C, 0-3) followed object (+0x64), its first 0x80
// bytes (its place and heading), and the character's health (the data at +0x90, hit_feedback.cpp), with the menu up.
#include <cstdint>

#include "common/nightfire.h"
#include "kernel/kernel.h"

namespace nf {

uint64_t NetStateHash() {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    auto mix = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) h = (h ^ ((v >> (i * 8)) & 0xFF)) * 1099511628211ull;
    };
    mix(kernel::R32(0x25F1D0 + 0x1BC));
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t cam = kernel::R32(0x1F661C + i * 4), obj = cam ? kernel::R32(cam + 0x64) : 0;
        mix(obj);
        if (obj)
            for (uint32_t o = 0; o < 0x80; o += 4) mix(kernel::R32(obj + o));
    }
    return h;
}

}  // namespace nf
