// The mouse aims the mounted guns (Paris's van, Alpine Escape's snowmobile) as in a PC shooter.
//
// The gun camera's update 0x857D0 (fastcall: the camera) adds the stick's rates to the aim (+0x1EC yaw, +0x1E8 pitch,
// in turns: up aims at a smaller pitch), clamps them to the mission's limits (0x83190) and builds the view. The rates
// come through a curve with a dead zone (0x81540), are smoothed (+0x290, +0x294), ramped while held (+0x1F0, +0x1F4)
// and scaled by the view's half angle (+0xB4: 33 degrees, about 2 zoomed), so the mouse as a stick lagged. While the
// keyboard and mouse play, the mouse's counts are added to the aim before the call and the stick's part is zeroed:
// so many turns a count (default.xbe's 0.0008 radians), slower in proportion zoomed and a quarter again (as on foot).
// Auto-aim (default.xbe's option, carried in the launch data to [0x2445D0]; read only by 0x82E00 from this update)
// pulls the crosshair onto a target: off for the call, and a lock it holds (byte [+0x354]+0xB) let go.
// (.local/driving_input_re.md)
#include <algorithm>
#include <chrono>
#include <cstring>

#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kPitch = 0x1E8, kYaw = 0x1EC, kHalfAngle = 0xB4, kAttached = 0x178, kLock = 0x354;
constexpr uint32_t kStickParts[] = {0x2A0, 0x2A4, 0x290, 0x294, 0x1F0, 0x1F4};
constexpr uint32_t kAutoAim = 0x2445D0, kGame = 0x239220, kState = 0x234E24;
constexpr float kTurnsPerCount = 0.0008f / 6.2831853f;
constexpr float kGameHalfAngle = 33.0f;
constexpr float kZoomedScale = 0.25f;

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

// the mouse's counts since the last call, times the sensitivity (input::TakeMouse's rate back to counts)
bool Counts(float& x, float& y) {
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.001f, 0.1f);
    last = now;
    if (!input::UsingKeyboard() || !input::TakeMouse(x, y)) return false;
    x *= 2400 * dt, y *= 2400 * dt;
    return true;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000857D0);
X86_FUNC(sub_000857D0) {
    uint32_t cam = ctx.ecx, game = kernel::R32(kGame);
    float x, y;
    if (!cam || !Counts(x, y)) return __imp__sub_000857D0(ctx, base);
    bool frozen = (game && kernel::R32(game + 0x47C)) || kernel::R32(kState) == 3 ||
                  kernel::G<uint8_t>(cam + kAttached);
    if (frozen) return __imp__sub_000857D0(ctx, base);  // paused, a movie, scripted: the counts are dropped
    float zoom = std::clamp(GetF(cam + kHalfAngle) / kGameHalfAngle, 0.02f, 2.0f);
    bool zoomed = zoom < 0.9f;
    float k = kTurnsPerCount * std::min(zoom, 1.0f) * (zoomed ? kZoomedScale : 1.0f);
    SetF(cam + kYaw, GetF(cam + kYaw) + x * k);
    SetF(cam + kPitch, GetF(cam + kPitch) - y * k);
    for (uint32_t f : kStickParts) SetF(cam + f, 0);
    uint32_t aim = kernel::R32(kAutoAim), lock = kernel::R32(cam + kLock);
    kernel::W32(kAutoAim, 0);
    if (lock) kernel::G<uint8_t>(lock + 0xB) = 0;
    __imp__sub_000857D0(ctx, base);
    for (uint32_t f : kStickParts) SetF(cam + f, 0);
    kernel::W32(kAutoAim, aim);
    static bool said = false;
    if (!said) {
        said = true;
        LOGR("nightfire: the mouse aims the mounted gun directly (auto-aim %s for it)", aim ? "off" : "already off");
    }
}
