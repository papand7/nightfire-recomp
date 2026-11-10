// Diagnostic (NF_TELEPORT_AHEAD=<metres>): once a level is running (about 10 s after the menus), player 1 is moved that
// far straight ahead of where it faces, its place logged before and after. For walking up to a door without a route.
#include <cmath>
#include <cstdlib>

#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace nf {
void TeleportTest() {
    static const float ahead = [] {
        const char* e = std::getenv("NF_TELEPORT_AHEAD");
        return e && *e ? float(std::atof(e)) : 0.0f;
    }();
    static uint32_t frames = 0;
    static bool done = false;
    if (ahead == 0 || done) return;
    static uint32_t calls = 0;
    if (++calls % 600 == 1) logx::Flush(), LOGR("nightfire: teleport test: level %08X, player 1 %08X, frames %u", kernel::R32(0x1F658C), kernel::R32(0x1F6654), calls);
    const uint32_t level = kernel::R32(0x1F658C);  // the level running (the front end's is 07000048)
    const uint32_t obj = kernel::R32(0x1F6654);  // player 1's object
    if (!level || level == 0x07000048 || !obj || ++frames < 600) return;
    done = true;
    float x = kernel::G<float>(obj + 0x24), y = kernel::G<float>(obj + 0x28), z = kernel::G<float>(obj + 0x2C);
    const float yaw = kernel::G<float>(obj + 0x40);  // the heading: forward is (sin, 0, cos)
    logx::Flush();
    LOGR("nightfire: teleport test: from %.2f %.2f %.2f, heading %.2f, %.1f m ahead", x, y, z, yaw, ahead);
    kernel::G<float>(obj + 0x24) = x + std::sin(yaw) * ahead;
    kernel::G<float>(obj + 0x2C) = z + std::cos(yaw) * ahead;
    logx::Flush();
}
}  // namespace nf
