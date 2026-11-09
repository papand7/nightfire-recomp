// Optional firmer movement. Keep the native collision, step, water and ladder handling.
// A9990 starts a jump (eax actor, ecx object); AB5D0 integrates gravity and movement.
#include <cmath>
#include <cstdlib>

#include "common/nightfire.h"
#include "core/log.h"
#include "core/netplay.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr float kGravity = 1.6f;
constexpr uint32_t kGravityY = 0x1F664C;

bool OnFoot(uint32_t obj) {
    if (!obj) return false;
    const int posture = kernel::G<int16_t>(obj + 0xD2);
    return posture == 0 || posture == 4;  // standing/crouching, not water, ladders or scripted movement
}
}

namespace nf {
bool TightMovement() {
    if (netplay::Active()) return (netplay::SessionRules() & 1) != 0;
    bool host;
    return NetMovement(host) ? host : tight_movement;
}
bool CanChangeMovement() { return !netplay::Active() && !NetGuest(); }
}

X86_EXTERN_FUNC(__imp__sub_000A9990);
X86_FUNC(sub_000A9990) {
    const uint32_t actor = ctx.eax, obj = ctx.ecx;
    const bool improve = nf::TightMovement() && OnFoot(obj);
    const bool grounded = improve && kernel::G<uint8_t>(actor + 0x8DC) == 0;
    __imp__sub_000A9990(ctx, base);
    if (grounded && kernel::G<uint8_t>(actor + 0x8DC) == 1) {
        // Scale the launch speed by sqrt(gravity): retain jump height with a shorter arc.
        kernel::G<float>(actor + 0x40) *= std::sqrt(kGravity);
    }
}

X86_EXTERN_FUNC(__imp__sub_000AB5D0);
X86_FUNC(sub_000AB5D0) {
    const uint32_t obj = kernel::R32(ctx.esp + 4);
    const bool improve = nf::TightMovement() && OnFoot(obj);
    const float gravity = kernel::G<float>(kGravityY);
    if (improve) kernel::G<float>(kGravityY) = gravity * kGravity;
    __imp__sub_000AB5D0(ctx, base);
    kernel::G<float>(kGravityY) = gravity;
    // Diagnostic samples use the game's simulation time, independent of rendering FPS.
    static const bool trace = std::getenv("NF_MOVEMENT_TRACE") != nullptr;
    if (trace && obj) {
        const uint32_t actor = kernel::R32(obj + 0xBC);
        if (kernel::G<int8_t>(actor + 0x8DE) == 0)
            LOGR("nightfire: movement: tight %d dt %.6f y %.6f vy %.6f air %u x %.6f z %.6f",
                 improve, kernel::G<float>(0x17C104), kernel::G<float>(obj + 0x28),
                 kernel::G<float>(actor + 0x40), kernel::G<uint8_t>(actor + 0x8DC),
                 kernel::G<float>(obj + 0x24), kernel::G<float>(obj + 0x2C));
    }
}
