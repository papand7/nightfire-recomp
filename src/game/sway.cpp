// Weapon sway and head bob (nf::view_motion), very subtle: the original has neither.
//
// The player's camera is placed by 0x24530 (cdecl: the object, the camera) at the end of the first person camera's
// update: its eye at camera +0x138 (0xA89C4: the object's place, its height +0x898 less the crouch +0x89C). After it
// the eye is moved by a bob that follows the distance walked (a small dip at each step, a smaller sway to the side),
// fading in with the walking speed and out in the air. The gun (src/game/aim.cpp, after 0xBD740 placed it) lags a
// little behind the view's turn and sways with the same steps; aiming down the sights keeps a fifth of it.
#include <algorithm>
#include <cmath>
#include <cstring>

#include <cstdlib>

#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;

constexpr uint32_t kActor = 0xBC, kPos = 0x24, kYaw = 0x40, kPitch = 0x838, kPlayerIndex = 0x8DE, kEye = 0x138;
constexpr float kStep = 2.8f;          // metres a full bob (two steps): the pace of a jog, not a sprint of tiny steps
constexpr float kBobUp = 0.022f;       // the eye's dip at a step, metres
constexpr float kBobSide = 0.010f;     // its sway to the side
constexpr float kRunSpeed = 4.0f;      // metres a second at which the bob is whole
constexpr float kGunLag = 0.035f;      // the gun's lag per radian a frame of turn
constexpr float kGunLagMost = 0.012f;
constexpr float kGunBob = 0.006f;

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

struct Motion {
    float last[3] = {};
    float last_yaw = 0, last_pitch = 0;
    bool seen = false;
    float phase = 0;    // radians, a step each pi
    float amount = 0;   // 0 standing to 1 running, eased
    float lag_x = 0, lag_y = 0;
} g_motion[4];
}  // namespace

namespace nf {


// after 0xBD740 placed player i's gun (src/game/aim.cpp): its lag and step sway, `keep` of it (aiming: less)
void GunSway(int i, uint32_t gun, float keep) {
    if (!view_motion || i < 0 || i >= 4 || !gun) return;
    const Motion& m = g_motion[i];
    float bx = std::sin(m.phase) * kGunBob * m.amount, by = -std::fabs(std::cos(m.phase)) * kGunBob * 0.7f * m.amount;
    SetF(gun + kPos, GetF(gun + kPos) + (m.lag_x + bx) * keep);
    SetF(gun + kPos + 4, GetF(gun + kPos + 4) + (m.lag_y + by) * keep);
}
}  // namespace nf

X86_EXTERN_FUNC(__imp__sub_00024530);
X86_FUNC(sub_00024530) {  // a player's camera placed from its object (cdecl: the object, the camera)
    uint32_t obj = R32(ctx.esp + 4), cam = R32(ctx.esp + 8);
    __imp__sub_00024530(ctx, base);
    uint32_t actor = obj ? R32(obj + kActor) : 0;
    if (!nf::view_motion || !actor || !cam) return;
    int i = kernel::G<int8_t>(actor + kPlayerIndex);
    if (i < 0 || i >= 4) return;
    Motion& m = g_motion[i];
    float p[3] = {GetF(obj + kPos), GetF(obj + kPos + 4), GetF(obj + kPos + 8)};
    float yaw = GetF(obj + kYaw), pitch = GetF(actor + kPitch);
    if (!m.seen) std::memcpy(m.last, p, sizeof p), m.last_yaw = yaw, m.last_pitch = pitch, m.seen = true;
    float dx = p[0] - m.last[0], dz = p[2] - m.last[2], dy = p[1] - m.last[1];
    float walked = std::sqrt(dx * dx + dz * dz);
    std::memcpy(m.last, p, sizeof p);
    if (walked > 1.0f) walked = 0;  // a respawn or a teleport
    float speed = walked * 60.0f, airborne = std::fabs(dy) > 0.03f ? 1.0f : 0.0f;
    float want = std::clamp(speed / kRunSpeed, 0.0f, 1.0f) * (1 - airborne);
    m.amount += (want - m.amount) * 0.12f;
    m.phase += walked / kStep * 6.2831853f;
    if (m.phase > 62.831853f) m.phase -= 62.831853f;
    // the gun's lag: the opposite way of the turn, easing back
    float dyaw = std::remainder(yaw - m.last_yaw, 6.2831853f), dpitch = pitch - m.last_pitch;
    m.last_yaw = yaw, m.last_pitch = pitch;
    if (std::fabs(dyaw) > 0.5f) dyaw = 0;  // a respawn's turn
    m.lag_x = std::clamp(m.lag_x * 0.85f + dyaw * kGunLag, -kGunLagMost, kGunLagMost);
    m.lag_y = std::clamp(m.lag_y * 0.85f - dpitch * kGunLag, -kGunLagMost, kGunLagMost);
    // the eye's bob: down at each step, a little to the side
    float up = -std::fabs(std::sin(m.phase)) * kBobUp * m.amount, side = std::sin(m.phase * 0.5f) * kBobSide * m.amount;
    static int tr = 0;
    if (i == 0 && std::getenv("NF_SWAY_TRACE") && ++tr % 20 == 0)  // diagnostic: three times a second
        LOGR("nightfire: sway: speed %.2f m/s, amount %.2f, eye %+.3f %+.3f, gun lag %+.4f %+.4f", speed, m.amount, up,
             side, m.lag_x, m.lag_y);
    SetF(cam + kEye + 4, GetF(cam + kEye + 4) + up);
    SetF(cam + kEye, GetF(cam + kEye) + std::cos(yaw) * side);
    SetF(cam + kEye + 8, GetF(cam + kEye + 8) - std::sin(yaw) * side);
}
