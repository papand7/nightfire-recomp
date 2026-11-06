// The mouse aims on foot as in a PC shooter: its movement turns and pitches Bond's view directly, not through the
// right stick (GameInfo::mouse_direct in default.xbe's exe; the driving levels keep the stick).
//
// The player's movement update 0xA9CF0 (cdecl: the actor, its object, a time scale) reads the turn, move and strafe
// controls (0x6CA20 slots 0-2) and writes the frame's turn at actor +0x28: radians added to the view's heading at the
// next step (turning right is negative), through an acceleration filter (0xB7420, +0x88C) and the stick's value
// squared, which is what made the mouse feel like a stick. The look update 0xB7610 adds the look control to the
// pitch at +0x838 (a quarter turn up is 1, the camera's angle pi/2 times it) and marks the frame looked (+0xF0 = 1:
// otherwise the view eases back level). After 0xA9CF0 for player 1 the mouse's movement since the last
// frame is added to both: so many radians a count at 100% sensitivity, less zoomed in (the camera's field of view
// over the game's 60 degrees, and a quarter of that again: Harry, aiming the sniper rifle and the rocket launcher).
// A frame where the game did not write the turn (a cutscene, another state) gets none.
// Where the player steers something else (a guided missile, a turret: 0xA9CF0 does not write the turn) the mouse plays
// the right stick in the controls 0xDE720 made (nf::MouseAsStick, from src/game/controls.cpp), 20 counts a frame
// full deflection. Auto-aim (the game's option, the byte at +1 of the player's controls, +2 in the mode 0x260018
// flags) is off while the mouse aims: the target search 0xB9950 sees it off for the call, the option stays as set;
// the homing weapons ('P', 'Q') still lock on.
// Headless (no mouse): NF_MOUSE_TEST=dx,dy,from,to plays that many counts a frame for frames from..to.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kTurn = 0x28, kPitch = 0x838, kLooked = 0xF0, kPlayerIndex = 0x8DE;
constexpr uint32_t kCameras = 0x1F661C, kCamFov = 0x104, kCamZoom = 0xF4;  // the angle, a zoom divisor (0xDD950)
constexpr float kRadiansPerCount = 0.0008f;  // at 100% sensitivity: 36 degrees an inch at 800 counts an inch
constexpr float kGameFov = 1.0471976f;       // the cameras' 60 degrees (0x25190's callers)
constexpr float kZoomedScale = 0.25f;        // zoomed in (a scope, the rocket launcher's sight): a quarter as fast more
constexpr float kZoomedBelow = 0.9f;  // the view's angle over the game's under which it is zoomed in
constexpr float kHalfPi = 1.5707964f;          // the pitch is kept over a quarter turn (the camera's is pi/2 times it)
constexpr uint32_t kUnset = 0x7FC0DEAD;      // a NaN: the turn as it was before the update, if it stays
constexpr uint32_t kControls = 0x1FE6D0, kControlSize = 0x158;  // per player (0xDE720's)
constexpr uint32_t kAutoAim = 1, kAutoAimAlt = 2;
constexpr float kCountsFull = 20;            // counts a frame playing the stick fully
uint32_t g_direct = 0;                       // frames since 0xA9CF0 last turned player 1 (the mouse aims directly)

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

// the mouse's counts since the last call, times the sensitivity
bool Counts(float& x, float& y) {
    static const struct Test {
        float dx = 0, dy = 0;
        uint32_t from = 0, to = 0;
    } test = [] {
        Test t;
        if (const char* e = std::getenv("NF_MOUSE_TEST"))
            std::sscanf(e, "%f,%f,%u,%u", &t.dx, &t.dy, &t.from, &t.to);
        return t;
    }();
    static uint32_t frame = 0;
    frame++;
    if (test.to) {
        bool on = frame >= test.from && frame <= test.to;
        x = on ? test.dx : 0, y = on ? test.dy : 0;
        return true;
    }
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.001f, 0.1f);
    last = now;
    if (!input::UsingKeyboard() || !input::TakeMouse(x, y)) return false;  // a controller in use: its own look
    x *= 2400 * dt, y *= 2400 * dt;  // TakeMouse's rate (2400 counts a second at 100% is 1) back to counts
    return true;
}

// player 1's view angle over the game's 60 degrees: under kZoomedBelow a scope or sight is zoomed in
float ViewZoom() {
    uint32_t cam = kernel::R32(kCameras);
    if (!cam) return 1.0f;
    float divisor = GetF(cam + kCamZoom);
    return std::clamp(GetF(cam + kCamFov) / (divisor > 0.01f ? divisor : 1.0f) / kGameFov, 0.02f, 2.0f);
}

// the mouse wheel through a scope: notches the input thread counted (nf::ScopeZoom), applied here on the game's
// thread. A weapon's zoom is the player's +0x860, which the first person camera copies into its divisor (+0xF4,
// 0xA8BC6) each frame; 0xB7190 eases it to the target +0x864, kept to 1 .. the weapon's most (+0x868, at most 50),
// which its zoom control (slot 6) multiplies while held, and leaving the scope sets it back to 1 (0xB9BD1). A notch
// multiplies the target, so the game's own easing, limits and reset apply
std::atomic<int> g_zoom_notches{0};
constexpr float kNotchStep = 1.25f;
constexpr uint32_t kZoomTarget = 0x864, kZoomMost = 0x868;  // the player's; the zoom itself +0x860

void WheelZoom(uint32_t actor) {
    int notches = g_zoom_notches.exchange(0);
    if (!notches || !nf::Zoomed()) return;
    float most = std::min(GetF(actor + kZoomMost), 50.0f);
    if (!(most > 1.0f)) return;  // a sight that does not zoom
    float target = std::clamp(GetF(actor + kZoomTarget) * std::pow(kNotchStep, float(notches)), 1.0f, most);
    SetF(actor + kZoomTarget, target);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000A9CF0);
X86_FUNC(sub_000A9CF0) {
    uint32_t actor = kernel::R32(ctx.esp + 4);
    bool mine = actor && kernel::G<int8_t>(actor + kPlayerIndex) == 0;
    uint32_t was = mine ? kernel::R32(actor + kTurn) : 0;
    if (mine) kernel::W32(actor + kTurn, kUnset);
    // the walk's speed (the third argument) raised while the player sprints
    float& speed = kernel::G<float>(ctx.esp + 12);
    float speed_was = speed, sprint = actor ? nf::SprintFactor(kernel::G<int8_t>(actor + kPlayerIndex)) : 1.0f;
    speed = speed_was * sprint;
    __imp__sub_000A9CF0(ctx, base);
    speed = speed_was;
    if (!mine) return;
    WheelZoom(actor);
    if (kernel::R32(actor + kTurn) == kUnset) {
        kernel::W32(actor + kTurn, was);
        return;
    }
    g_direct = 0;
    float x, y;
    if (!Counts(x, y)) return;
    kernel::G<uint8_t>(actor + kLooked) = 1;  // looking, as 0xB7610 marks the stick's: else the view eases back level
    if (x == 0 && y == 0) return;
    float zoom = ViewZoom();
    bool zoomed = nf::Zoomed();  // aiming down the sights: slower as zoomed in, not a scope's quarter more
    static bool was_zoomed = false;
    if (zoomed != was_zoomed) {
        was_zoomed = zoomed;
        LOGR("nightfire: %s (field of view %.2f of the game's 60 degrees)",
             zoomed ? "zoomed in: the mouse slows" : "zoomed out", zoom);
    }
    float k = kRadiansPerCount * std::min(zoom, 1.0f) * (zoomed ? kZoomedScale : 1.0f);
    SetF(actor + kTurn, GetF(actor + kTurn) - x * k);
    SetF(actor + kPitch, std::clamp(GetF(actor + kPitch) + y * k / kHalfPi, -1.0f, 1.0f));
    static bool said = false;
    if (!said) {
        said = true;
        LOGR("nightfire: the mouse turns and pitches the view directly (%.4f radians a count at 100%%)", kRadiansPerCount);
    }
}

namespace nf {
// Bond under the player's own control on foot (0xA9CF0 turned player 1 within two frames): not at a keypad, steering
// a missile or a turret, in a cutscene
bool PlayerFree() { return g_direct <= 2; }
bool Zoomed() { return ViewZoom() < kZoomedBelow && !AimingDownSights(); }
void ScopeZoom(int notches) { g_zoom_notches += notches; }

// GameInfo::view_turn (smooth turning): the turn the mouse's counts not yet read will make, as the update above makes
// it, and the view's half angle's tangent (60 degrees, zoomed)
bool ViewTurn(float& yaw, float& pitch, float& tan_half_fov) {
    if (!PlayerFree() || MenuUp() || !input::UsingKeyboard()) return false;
    float x, y;
    if (!input::PeekMouse(x, y)) return false;
    float zoom = ViewZoom();
    float k = kRadiansPerCount * std::min(zoom, 1.0f) * (Zoomed() ? kZoomedScale : 1.0f);
    yaw = x * k, pitch = y * k;
    tan_half_fov = std::tan(0.5f * kGameFov * std::min(zoom, 2.0f));
    return true;
}

// after 0xDE720 filled the controls: where the mouse does not turn the player itself, it plays the right stick
// (style 0's: slot 0 and 3 its x, 4 and 5 its y, -1 to 1)
void MouseAsStick() {
    if (++g_direct <= 2 || MenuUp()) return;  // 0xA9CF0 turned player 1 a frame ago: the mouse aims directly
    float x, y;
    if (!Counts(x, y) || (x == 0 && y == 0)) return;
    float sx = std::clamp(x / kCountsFull, -1.0f, 1.0f), sy = std::clamp(y / kCountsFull, -1.0f, 1.0f);
    for (uint32_t slot : {0u, 3u}) SetF(kControls + 0x14 + slot * 4, sx);
    for (uint32_t slot : {4u, 5u}) SetF(kControls + 0x14 + slot * 4, sy);
    static bool said = false;
    if (!said) {
        said = true;
        LOGR("nightfire: the mouse plays the right stick (the player steers something else: a guided missile, a turret)");
    }
}
}  // namespace nf

// auto-aim's target search (cdecl: the player's object; its actor at +0xBC, the player index at actor +0x8DE)
X86_EXTERN_FUNC(__imp__sub_000B9950);
X86_FUNC(sub_000B9950) {
    uint32_t obj = kernel::R32(ctx.esp + 4), actor = obj ? kernel::R32(obj + 0xBC) : 0;
    if (!actor || kernel::G<int8_t>(actor + kPlayerIndex) != 0 || !input::UsingKeyboard())
        return __imp__sub_000B9950(ctx, base);
    uint8_t& on = kernel::G<uint8_t>(kControls + kAutoAim), &alt = kernel::G<uint8_t>(kControls + kAutoAimAlt);
    uint8_t was = on, was_alt = alt;
    static bool said = false;
    if ((was || was_alt) && !said) {
        said = true;
        LOGR("nightfire: auto-aim off while the mouse aims (the game's option stays as set)");
    }
    on = alt = 0;
    __imp__sub_000B9950(ctx, base);
    on = was, alt = was_alt;
}
