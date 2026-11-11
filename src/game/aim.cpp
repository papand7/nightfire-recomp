// Aim down sights (nf::aim_down_sights): L (the right mouse button) raises the gun to the middle of the view and zooms
// in a little, as in a PC shooter, instead of the game's manual aim (the view held still, a red crosshair moved by the
// stick).
//
// 0xDE720 puts L in the controls' slot 19 (+0x60, style 0, 1 and 2); the weapon update 0xBA8F0 (cdecl: the player's
// object) turns it into bit 0 of the weapon state's word +0x50 ([obj+0xB8]): held, or toggled on each press when the
// controls' byte +3 is set. That bit holds the turn (0xA9CF0), moves the crosshair (0xB7610, actor +0xD8/+0xDC), aims
// off centre (0xB9950, +0xE0/+0xE4), draws it red (0xB4B60) and turns the gun toward it (0xB7EB0). Here slot 19 is
// cleared after 0xDE720 (before 0x6CF50 makes the held and pressed bits from it), so the bit never sets and shots go
// through the middle; a scoped weapon (the weapon table 0x18CFA0, 0x10C an entry, +0x68 bit 0x40) keeps the game's
// scope. While aiming:
// - the zoom: the target +0x864 at kZoom, its most +0x868 (the weapon's +0x88, 1 for most guns) raised to it before
//   0xB7190 clamps and eases the zoom +0x860 toward it (the camera's divisor, 0xA8BC6)
// - the gun: 0xBD740 (cdecl: the object) sets its model's position ([actor+0x778] +0x24, +0x28, +0x2C) each frame
//   (0xBCAA0: the weapon's offset and the bob); kSight is added after it, eased in and out
// - the crosshair (the actor's first HUD sprite [[+0x770]], drawn by 0xB6190 when the controls' +8 is set) hidden
// NF_ADS_TEST=1 (diagnostic): player 1 aims for 240 frames of each 480.
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <imgui.h>

#include "app/app.h"
#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;

constexpr uint32_t kControls = 0x1FE6D0, kControlSize = 0x158, kAimSlot = 0x60, kToggle = 3;
constexpr uint32_t kWeapons = 0x18CFA0, kWeaponSize = 0x10C, kWeaponFlags = 0x68, kScoped = 0x40, kWeaponZoom = 0x88;
constexpr uint32_t kActor = 0xBC, kWeaponState = 0xB8, kWeaponIndex = 0x52, kPlayerIndex = 0x8DE;
constexpr uint32_t kZoomTarget = 0x864, kZoomMost = 0x868, kGun = 0x778, kGunPos = 0x24, kHud = 0x770;
constexpr float kZoom = 1.3f;  // the view's angle divided by this (iron sights: nf::ads_zoom's percent in its place)
// the weapons by the game's own names (its text, read from a running game 2026-11-11; the model each draws, +0xDC):
// 10-12 the Kowloon Type 40 (model 0x51), 13 the same two at once (flag 0x40000), 28-29 the Frinesi Automatic 12,
// 50-51 the Phoenix Samurai
constexpr int kDualKowloon = 13;
// each weapon's sights brought to the middle of the view (its model's frame: +x left, +y up): the gun's place at the
// hip (0xBCAA0) moved by its sight's place on screen there, at the model's depth. Each weapon its own: models that
// shared one (the silenced pistols, 18 and 19) sat up to 100 px apart. Measured from aimed NF_ADS_CAL shots
// (2026-11-05, a 1920x1080 view: the front sight's or the rear notch's pixels from the middle; 2 cm at a pistol's
// depth is about 45 px, measured again per gun from pictures with it 2 cm left and up). An optic
// (a sight that hides the view: the scoped rifles' that are not the game's scopes, the launchers) zooms by the
// weapon's own amount and the gun is put out of sight at full aim, the crosshair left on
struct Sight {
    float x, y;
    float zoom;   // the view's when aiming (the game's own for an optic)
    bool optic;
};
Sight SightOf(int idx) {
    switch (idx) {
        case 1: return {0.264f, 0.135f, kZoom, false};                          // the P2K
        case 2: return {0.2665f, 0.1285f, kZoom, false};                        // tuned: +2.5 mm, -6.5 mm
        case 3: return {0.270f, 0.129f, kZoom, false};
        case 4: return {0.2665f, 0.128f, kZoom, false};                         // tuned: +0.5 mm
        case 5: return {0.266f, 0.128f, kZoom, false};
        case 6: return {0.266f, 0.108f, kZoom, false};                          // 6-9 silenced: the rear notch (6 tuned: -2 mm)
        case 7: case 9: return {0.266f, 0.108f, kZoom, false};
        case 8: return {0.267f, 0.108f, kZoom, false};
        case 10: case 11: return {0.215f, 0.070f, kZoom, false};
        case 12: return {0.214f, 0.074f, kZoom, false};                         // tuned: -1 mm, +4 mm
        case 13: return {0.222f, 0.090f, kZoom, false};
        case 14: case 15: return {0.358f, 0.100f, kZoom, false};
        case 16: return {0.359f, 0.102f, kZoom, false};
        case 18: return {0.233f, 0.121f, kZoom, false};
        case 19: return {0.200f, 0.073f, kZoom, false};
        case 20: return {0.2585f, 0.146f, kZoom, false};                        // tuned: +5.5 mm, +3 mm
        case 21: return {0.254f, 0.144f, kZoom, false};
        case 22: return {0.2515f, 0.0855f, kZoom, false};                       // tuned: +1.5 mm, +1.5 mm
        case 23: return {0.250f, 0.084f, kZoom, false};
        case 24: case 25: return {0.230f, 0.076f, 3.0f, true};                // the rifle with a scope
        case 28: case 29: return {0.206f, 0.041f, kZoom, false};                // the receiver's top
        case 42: case 43: return {0.0f, 0.0f, 2.0f, true};
        case 44: case 45: case 46: case 47: case 48: case 49: return {0.0f, 0.0f, 1.6f, true};  // the launchers
        case 50: return {0.160f, 0.153f, kZoom, false};
        default: return {0.25f, 0.10f, kZoom, false};
    }
}
constexpr float kEase = 0.125f;                        // of the way in or out a frame

// each weapon's own correction (metres, +x left, +y up), from the sight tuner: sight_offsets.txt beside the exe,
// a line a weapon: "index x_mm y_mm z_mm yaw pitch roll" (z: + farther from the eye; the turns in degrees, added to
// the gun's own at +0x40, +0x3C, +0x44, which 0xB7EB0 sets before the gun is placed; a line without them reads as 0);
// read once, written by the tuner's Enter
float g_tune[64][6] = {};
constexpr float kDeg = 0.017453293f;
std::filesystem::path TunePath() { return app::ExeDir() / "sight_offsets.txt"; }
void LoadTune() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    // the tuner's results built in (2026-11-11: x_mm y_mm z_mm, yaw pitch roll degrees); a file's line replaces one
    static const struct { int idx; float v[6]; } kBuiltIn[] = {
        {10, {13.5f, 1.5f, 0, -1.70f, 0.80f, 0}},  // the Kowloon Type 40
        {12, {14.0f, -4.5f, 0, -1.50f, 0, 0}},     // the Kowloon Type 40, its other mode
        {14, {2.0f, -7.0f, 0, 0, 0, 0}},
        {18, {-3.0f, 15.0f, 0, 0, 0, 0}},          // the submachine gun
        {28, {16.5f, -2.0f, 0, 0, 0, 0}},          // the Frinesi Automatic 12
    };
    for (const auto& b : kBuiltIn)
        for (int k = 0; k < 6; k++) g_tune[b.idx][k] = k < 3 ? b.v[k] / 1000.0f : b.v[k] * kDeg;
    if (FILE* f = std::fopen(TunePath().string().c_str(), "r")) {
        char line[128];
        while (std::fgets(line, sizeof line, f)) {
            int idx;
            float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0;
            if (std::sscanf(line, "%d %f %f %f %f %f %f", &idx, &x, &y, &z, &yaw, &pitch, &roll) >= 3 && idx >= 0 && idx < 64) {
                g_tune[idx][0] = x / 1000.0f, g_tune[idx][1] = y / 1000.0f, g_tune[idx][2] = z / 1000.0f;
                g_tune[idx][3] = yaw * kDeg, g_tune[idx][4] = pitch * kDeg, g_tune[idx][5] = roll * kDeg;
            }
        }
        std::fclose(f);
        LOGR("nightfire: sight offsets read from %s", TunePath().string().c_str());
    }
}
bool SaveTune() {
    FILE* f = std::fopen(TunePath().string().c_str(), "w");
    if (!f) return false;
    for (int i = 0; i < 64; i++)
        if (g_tune[i][0] != 0 || g_tune[i][1] != 0 || g_tune[i][2] != 0 || g_tune[i][3] != 0 || g_tune[i][4] != 0 ||
            g_tune[i][5] != 0)
            std::fprintf(f, "%d %.1f %.1f %.1f %.2f %.2f %.2f\n", i, g_tune[i][0] * 1000, g_tune[i][1] * 1000,
                         g_tune[i][2] * 1000, g_tune[i][3] / kDeg, g_tune[i][4] / kDeg, g_tune[i][5] / kDeg);
    std::fclose(f);
    return true;
}
struct Tuner {
    int weapon = -1;            // the weapon held
    int64_t saved_at = -100000;  // when Enter saved (the panel says so a while)
    bool was[6] = {};            // (unused, unused,) Enter, Delete, comma, full stop last frame
} g_tuner;

struct Player {
    bool held = false;    // L held last frame (the toggle's edge)
    bool aiming = false;
    bool was = false;     // the zoom set for aiming (put back once let go)
    bool scoped = true;   // the weapon has a scope (0xBA8F0's last frame): the game's own manual aim
    float t = 0;          // the gun's way to the middle
    bool optic = false;   // looking through an optic: its gun out of sight, the crosshair kept
} g_players[4];

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

uint32_t g_test_frames = 0;  // NF_ADS_WEAPONS's count
uint32_t TestPeriod() { return std::getenv("NF_ADS_CAL") ? 400 : 200; }  // frames a weapon

// NF_ADS_WEAPONS=N (diagnostic): player 1 aims all the time and holds each weapon from N on in turn, 200 frames each
// (given by 0xBA5E0 and asked for at the weapon state's +0x53, as a pickup does: the gun's state machine draws it)
int WeaponsTest() {
    static const int on = [] {
        const char* e = std::getenv("NF_ADS_WEAPONS");
        return e && *e ? std::atoi(e) : 0;
    }();
    return on;
}

bool Test() {
    static const bool on = [] {
        const char* e = std::getenv("NF_ADS_TEST");
        return e && *e == '1';
    }();
    return on;
}

uint32_t Weapon(uint32_t obj) {
    uint32_t state = R32(obj + kWeaponState);
    return kWeapons + uint32_t(int32_t(kernel::G<int8_t>(state + kWeaponIndex))) * kWeaponSize;
}

// the player of a character's object, else -1
int PlayerOf(uint32_t obj) {
    uint32_t actor = obj ? R32(obj + kActor) : 0;
    if (!actor || !R32(obj + kWeaponState)) return -1;
    int p = kernel::G<int8_t>(actor + kPlayerIndex);
    return p >= 0 && p < 4 ? p : -1;
}
}  // namespace

namespace nf {
// after 0xDE720 filled the controls (src/game/controls.cpp): L aims down the sights instead of the game's manual aim
void AimDownSights(uint32_t count) {
    static uint32_t frame = 0;
    frame++;
    for (uint32_t i = 0; i < count && i < 4; i++) {
        Player& p = g_players[i];
        uint32_t c = kControls + i * kControlSize;
        float l = GetF(c + kAimSlot);
        if (Test() && i == 0) l = (frame / 240) % 2 ? 1.0f : 0.0f;
        if (WeaponsTest() && i == 0) l = 1.0f;
        if (i == 0 && sight_tuner && !p.scoped) l = 1.0f;  // the sight tuner holds the aim
        if (!aim_down_sights || p.scoped) {
            p.aiming = p.held = false;
            continue;
        }
        bool held = l > 0.5f;
        if (kernel::G<uint8_t>(c + kToggle)) {
            if (held && !p.held) p.aiming = !p.aiming;
        } else {
            p.aiming = held;
        }
        p.held = held;
        SetF(c + kAimSlot, 0);
    }
}
// NF_ADS_FIRE=1 (diagnostic): while player 1 aims, its current weapon is fired every 60 frames from its eye straight
// along its view (the spawner 0x211F0, as a shot), and the bullet's direction logged beside the view's forward
// direction: a difference is the aim going wrong. Its shots hit whatever is there (no ammo needed).
// XBE_CAM_TRACE=1 (diagnostic): twice a second, player 1's own camera (its place and forward, +0x138 and +0x128) logged, to
// set beside the renderer's "camera trace" line (the camera the picture is drawn from)
void CameraTrace() {
    static const bool on = std::getenv("XBE_CAM_TRACE") != nullptr;
    static uint32_t frame = 0;
    if (!on || ++frame % 30) return;
    const uint32_t cam = kernel::R32(0x1F661C);
    if (!cam) return;
    LOGR("camera trace: the game's camera at %.3f %.3f %.3f, facing %.4f %.4f %.4f", GetF(cam + 0x138), GetF(cam + 0x13C),
         GetF(cam + 0x140), GetF(cam + 0x128), GetF(cam + 0x12C), GetF(cam + 0x130));
}

void AimFireTest(X86Context& ctx) {
    CameraTrace();
    static const bool on = [] { const char* e = std::getenv("NF_ADS_FIRE"); return e && *e == '1'; }();
    static uint32_t frame = 0;
    if (!on || !g_players[0].aiming) return;
    // (the view is set at one frame in each 60 and the shot made 30 frames later, when the picture has caught up)
    const uint32_t tick = frame++ % 60;
    const uint32_t obj = kernel::R32(0x1F6654), actor = obj ? kernel::R32(obj + kActor) : 0;
    if (!actor || !R32(obj + kWeaponState)) return;
    const uint32_t entry = Weapon(obj);
    // NF_ADS_PITCH=1 (with NF_ADS_FIRE): the view is swept up and down (0, +0.3, -0.3, +0.6, -0.6, ... a quarter turn is 1)
    static const bool sweep = [] { const char* e = std::getenv("NF_ADS_PITCH"); return e && *e == '1'; }();
    static uint32_t shot = 0;
    static const float steps[] = {0.0f, 0.3f, -0.3f, 0.6f, -0.6f, 0.9f, -0.9f};
    if (sweep && tick == 0) SetF(actor + 0x838, steps[shot % 7]), shot++;
    if (tick != 30) return;
    const float yaw = GetF(obj + 0x40), pitch = GetF(actor + 0x838) * 1.5707964f;  // (the pitch as the mouse sets it)
    const float fwd[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    static uint32_t vec = mem::AllocHostContiguous(32, 16);
    const float eye[3] = {GetF(obj + 0x24), GetF(obj + 0x28) + 1.5f, GetF(obj + 0x2C)};
    std::memcpy(kernel::HostPtr(vec), eye, 12);
    std::memcpy(kernel::HostPtr(vec + 12), fwd, 12);
    // the game's own fire (0xBAE50, EAX = the shooter): it makes each shot's origin and direction, and the spawner logs
    // them (NF_SHOT_TRACE): compare with the view's forward
    const X86Context saved = ctx;
    ctx.eax = obj;
    kernel::CallGuest(ctx, 0xBAE50, {entry});
    ctx = saved;
    const uint32_t cam = kernel::R32(0x1F661C);  // the camera the picture is drawn from (hit_feedback.cpp's forward)
    LOGR("nightfire: ADS fire: the game's fire called; camera forward %+.4f %+.4f %+.4f, pitch value %.2f",
         cam ? GetF(cam + 0x128) : 0.0f, cam ? GetF(cam + 0x12C) : 0.0f, cam ? GetF(cam + 0x130) : 0.0f,
         GetF(actor + 0x838));
}
bool AimingDownSights() { return g_players[0].aiming || g_players[0].t > 0; }
}  // namespace nf

X86_EXTERN_FUNC(__imp__sub_000BA8F0);
X86_FUNC(sub_000BA8F0) {  // a player's weapon update (cdecl: the object)
    uint32_t obj = R32(ctx.esp + 4);
    int i = PlayerOf(obj);
    if (i >= 0) {
        Player& p = g_players[i];
        // the game's scoped weapons keep its scope; the thrown ones (52-56: the grenades, NF_ADS_WEAPONS shots
        // 2026-11-05) and the dual-wield Kowloon (13: two guns, no sights to line up) have none: L is the game's own
        // aim for them. The Phoenix Samurai (50, 51) keeps the game's own scope too
        int idx = kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex);
        p.scoped = (R32(Weapon(obj) + kWeaponFlags) & kScoped) || (idx >= 52 && idx <= 56) || idx == kDualKowloon;
        if (p.scoped) p.aiming = false;
    }
    __imp__sub_000BA8F0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_000B7190);
X86_FUNC(sub_000B7190) {  // a player's zoom eased (cdecl: the object)
    uint32_t obj = R32(ctx.esp + 4);
    int i = PlayerOf(obj);
    if (i >= 0) {
        Player& p = g_players[i];
        uint32_t actor = R32(obj + kActor);
        if (p.aiming) {
            const Sight sight = SightOf(kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex));
            // iron sights zoom by the setting (More Settings' Aim Zoom, nf::ads_zoom), an optic by its own amount
            float zoom = sight.optic ? sight.zoom : std::clamp(float(nf::ads_zoom) / 100.0f, 1.0f, 3.0f);
            SetF(actor + kZoomMost, std::max(GetF(actor + kZoomMost), zoom));
            SetF(actor + kZoomTarget, zoom);
            if (!p.was) LOGR("nightfire: player %d aims down the sights", i + 1);
            p.was = true;
        } else if (p.was) {
            p.was = false;
            SetF(actor + kZoomTarget, 1.0f);
            SetF(actor + kZoomMost, GetF(Weapon(obj) + kWeaponZoom));
        }
    }
    __imp__sub_000B7190(ctx, base);
}

namespace {
// The sight tuner (nf::sight_tuner, debug): player 1 aims all the time and is given every weapon with sights of ours
// once a level (0xBA5E0; the scoped and thrown ones have none), switched with the game's own keys; the arrow keys move the held weapon (0.5 mm a frame, Shift 0.1 mm), Enter
// saves every weapon's place to sight_offsets.txt (applied from then on), Delete puts the held one back. The keys do not
// reach the game meanwhile (input::HoldBackToolKeys).
int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
// a weapon the tuner offers: one with sights of ours (not the game's scoped ones but the Samurai, not the thrown ones
// or the dual-wield Kowloon), whose model this level has loaded: drawing one it has not crashes (0x118F0, the gun's
// instance null). The model: the entry's +0xDC, as 0x11440 resolves it (0x11350 swaps it for a level's own, EAX in and
// out; 0x6B500 the loaded model or 0)
bool Usable(int idx) {
    const uint32_t w = kWeapons + uint32_t(idx) * kWeaponSize;
    return kernel::G<uint16_t>(w) && !(R32(w + kWeaponFlags) & kScoped) && !(idx >= 52 && idx <= 56) &&
           idx != kDualKowloon;
}
bool Loaded(X86Context& ctx, int idx) {
    const uint32_t model = R32(kWeapons + uint32_t(idx) * kWeaponSize + 0xDC);
    if (!model) return false;
    const X86Context saved = ctx;
    ctx.eax = model;
    kernel::CallGuest(ctx, 0x11350, {});
    const uint32_t mine = ctx.eax;
    const uint32_t found = kernel::CallGuest(ctx, 0x6B500, {mine});
    ctx = saved;
    return found != 0;
}
void SightTunerFrame(X86Context& ctx, uint32_t obj, int held) {
    if (!nf::sight_tuner) {
        if (g_tuner.weapon != -1) input::HoldBackToolKeys(false), g_tuner.weapon = -1;
        return;
    }
    input::HoldBackToolKeys(true);
    g_tuner.weapon = held;
    const bool focused = input::GameFocused();
    const bool now[6] = {false, false, focused && input::KeyDown(0x0D), focused && input::KeyDown(0x2E),
                         focused && input::KeyDown(0xBC), focused && input::KeyDown(0xBE)};
    auto pressed = [&](int k) { return now[k] && !g_tuner.was[k]; };
    // every weapon given once a level (0xBA5E0: the actor, the index, ammo), switched with the game's own keys
    static uint32_t given_for = 0;
    if (given_for != R32(obj + kActor)) {
        given_for = R32(obj + kActor);
        const uint32_t eax = ctx.eax;
        int n = 0;
        std::string here, missing;
        for (int idx = 0; idx < 64; idx++) {
            if (!Usable(idx)) continue;
            if (!Loaded(ctx, idx)) {
                missing += " " + std::to_string(idx);
                continue;
            }
            kernel::CallGuest(ctx, 0xBA5E0, {R32(obj + kActor), uint32_t(idx), 999}), n++;
            here += " " + std::to_string(idx);
        }
        ctx.eax = eax;
        LOGR("nightfire: sight tuner: %d weapons given:%s; not in this level:%s", n, here.c_str(), missing.c_str());
        for (int idx = 0; idx < 0x48; idx++) {  // the whole table, to see what is left out and why
            const uint32_t w = kWeapons + uint32_t(idx) * kWeaponSize;
            if (kernel::G<uint16_t>(w))
                LOGR("nightfire: weapon table %2d: id %u, flags %08X, zoom %.2f, base %d: %s", idx, kernel::G<uint16_t>(w),
                     R32(w + kWeaponFlags), GetF(w + kWeaponZoom), kernel::G<uint8_t>(w + 4),
                     Usable(idx) ? "in the tuner" : (R32(w + kWeaponFlags) & kScoped) ? "left out: scoped" : "left out: thrown");
        }
    }
    // comma and full stop: the weapon before and after in the table, every one (the game's own switch reaches only the
    // base weapons, not the variants such as the silenced ones), asked for as a pickup does
    if (pressed(4) || pressed(5)) {
        const int dir = pressed(5) ? 1 : 63;
        for (int tries = 1, idx = held < 0 ? 0 : held; tries <= 64; tries++) {
            idx = (idx + dir) % 64;
            if (!Usable(idx) || !Loaded(ctx, idx)) continue;
            kernel::G<int8_t>(R32(obj + kWeaponState) + 0x53) = int8_t(idx);
            break;
        }
    }
    if (held >= 0 && held < 64 && focused) {
        const float step = (input::KeyDown(0x10) ? 0.1f : 0.5f) / 1000.0f;  // Shift: finer
        if (input::KeyDown(0x25)) g_tune[held][0] += step;  // left
        if (input::KeyDown(0x27)) g_tune[held][0] -= step;  // right
        if (input::KeyDown(0x26)) g_tune[held][1] += step;  // up
        if (input::KeyDown(0x28)) g_tune[held][1] -= step;  // down
        if (input::KeyDown(0x24)) g_tune[held][2] -= step;  // Home: nearer (seen: + is farther)
        if (input::KeyDown(0x23)) g_tune[held][2] += step;  // End: farther
        // the turns: 0.1 degree a frame (Shift 0.02). J L about the up axis, I K up and down, U O the roll
        const float turn = (input::KeyDown(0x10) ? 0.02f : 0.1f) * kDeg;
        if (input::KeyDown('J')) g_tune[held][3] -= turn;
        if (input::KeyDown('L')) g_tune[held][3] += turn;
        if (input::KeyDown('I')) g_tune[held][4] -= turn;
        if (input::KeyDown('K')) g_tune[held][4] += turn;
        if (input::KeyDown('U')) g_tune[held][5] -= turn;
        if (input::KeyDown('O')) g_tune[held][5] += turn;
        if (pressed(3)) for (float& v : g_tune[held]) v = 0;
    }
    if (pressed(2)) {
        if (SaveTune()) g_tuner.saved_at = NowMs(), LOGR("nightfire: sight tuner: saved to %s", TunePath().string().c_str());
    }
    for (int k = 0; k < 6; k++) g_tuner.was[k] = now[k];
}
// its panel (from nf::GameUi): the true centre (where the shots go) and the held weapon's place
void SightTunerUi() {
    if (!nf::sight_tuner || g_tuner.weapon < 0) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
    dl->AddCircleFilled(c, 3.5f, IM_COL32(0, 0, 0, 200));
    dl->AddCircleFilled(c, 2.5f, IM_COL32(255, 40, 40, 255));
    const int w = g_tuner.weapon;
    char line[512];
    std::snprintf(line, sizeof line,
                  "SIGHT TUNER  weapon %d   left/right %+.1f mm   up/down %+.1f mm   farther %+.1f mm   "
                  "turn %+.2f   tilt %+.2f   roll %+.2f deg\n"
                  ", and . pick any weapon (the variants too)   Arrows move   Home/End nearer/farther   J L turn   I K tilt   "
                  "U O roll   (Shift finer)   Enter save all   Delete reset\n"
                  "Put the sights on the red dot: that is where the shots go.%s",
                  w, g_tune[w][0] * 1000, g_tune[w][1] * 1000, g_tune[w][2] * 1000, g_tune[w][3] / kDeg,
                  g_tune[w][4] / kDeg, g_tune[w][5] / kDeg,
                  NowMs() - g_tuner.saved_at < 2500 ? "\nSaved to sight_offsets.txt" : "");
    const ImVec2 at(vp->Pos.x + 24, vp->Pos.y + vp->Size.y * 0.72f);
    const ImVec2 size = ImGui::CalcTextSize(line);
    dl->AddRectFilled(ImVec2(at.x - 10, at.y - 8), ImVec2(at.x + size.x + 10, at.y + size.y + 8), IM_COL32(0, 0, 0, 170), 6);
    dl->AddText(at, IM_COL32(255, 255, 255, 255), line);
}
const bool g_tuner_ui_set = (nf::g_sight_tuner_ui = SightTunerUi, true);
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000BD740);
X86_FUNC(sub_000BD740) {  // the first person gun placed (cdecl: the object)
    uint32_t obj = R32(ctx.esp + 4);
    __imp__sub_000BD740(ctx, base);
    int i = PlayerOf(obj);
    if (i < 0) return;
    if (i == 0) nf::WeaponKeys(ctx, obj);  // the number keys (src/game/weapon_keys.cpp)
    if (i == 0) nf::ThrowFrame();
    if (i == 0 && WeaponsTest()) {
        static uint32_t idx = uint32_t(WeaponsTest()) - 1;
        int f = int(g_test_frames % TestPeriod()) + 200 - int(TestPeriod());  // its pictures (the shot file): a as it is, b, c NF_ADS_CAL
        if ((f == 130 || f == 160 || f == 190) && std::getenv("NF_ADS_SHOTS")) {
            if (FILE* sf = std::fopen("shot", "w"))
                std::fprintf(sf, "%s\\w_%02u%c.png\n", std::getenv("NF_ADS_SHOTS"), idx, 'a' + (f - 130) / 30), std::fclose(sf);
        }
        if (++g_test_frames % TestPeriod() == 0) {
            uint32_t state = R32(obj + kWeaponState);
            for (int tries = 0; tries < 64 && state; tries++) {
                idx = (idx + 1) % 64;
                uint32_t w = kWeapons + idx * kWeaponSize;
                if (!kernel::G<uint16_t>(w) || !Loaded(ctx, int(idx))) continue;  // no weapon there, or not in this level
                if (const char* only = std::getenv("NF_ADS_ONLY")) {  // NF_ADS_ONLY=,2,3,: those weapons alone
                    char key[8];
                    std::snprintf(key, sizeof key, ",%u,", idx);
                    if (!std::strstr(only, key)) continue;
                }
                uint32_t eax = ctx.eax;  // given (0xBA5E0: the actor, the index, ammo), then asked for (+0x53)
                kernel::CallGuest(ctx, 0xBA5E0, {R32(obj + kActor), idx, 999});
                ctx.eax = eax;
                kernel::G<int8_t>(state + 0x53) = int8_t(idx);
                LOGR("nightfire: ADS test: weapon %u (id %u, flags %08X, zoom %.2f)", idx, kernel::G<uint16_t>(w),
                     R32(w + kWeaponFlags), GetF(w + kWeaponZoom));
                break;
            }
        }
    }
    Player& p = g_players[i];
    p.t = std::clamp(p.t + (p.aiming ? kEase : -kEase), 0.0f, 1.0f);
    uint32_t gun = R32(R32(obj + kActor) + kGun);
    if (i == 0 && gun && std::getenv("NF_GUN_TRACE")) {  // diagnostic: the gun's place (its model's) each frame it moves
        static float last[3] = {};
        const float now[3] = {GetF(gun + kGunPos), GetF(gun + kGunPos + 4), GetF(gun + kGunPos + 8)};
        if (std::fabs(now[0] - last[0]) + std::fabs(now[1] - last[1]) + std::fabs(now[2] - last[2]) > 0.004f) {
            LOGR("nightfire: gun at %.3f %.3f %.3f (aim %.2f)", now[0], now[1], now[2], p.t);
            logx::Flush();
        }
        std::memcpy(last, now, sizeof last);
    }
    if (i == 0 && WeaponsTest() && gun) {  // the test: alive, and each weapon's own place (before ours) logged
        SetF(R32(obj + kActor) + 0x824, 100.0f);
        static int last = -1;
        int held = kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex);
        static int settle = 0;
        if (held != last) last = held, settle = 0;
        if (++settle == TestPeriod() - 10)
            LOGR("nightfire: ADS test: weapon %d at %.3f %.3f %.3f, turned %.4f %.4f %.4f (aim off %.3f %.3f, sway %.4f)",
                 held, GetF(gun + kGunPos), GetF(gun + kGunPos + 4), GetF(gun + kGunPos + 8), GetF(gun + 0x3C),
                 GetF(gun + 0x40), GetF(gun + 0x44), GetF(R32(obj + kActor) + 0xE0), GetF(R32(obj + kActor) + 0xE4),
                 GetF(R32(obj + kActor) + 0x874));
    }
    if (i == 0) SightTunerFrame(ctx, obj, kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex));
    if (gun) nf::GunSway(i, gun, 1 - 0.8f * p.t * p.t * (3 - 2 * p.t));  // sway: a fifth of it when aimed
    if (!gun || p.t <= 0) return;
    float s = p.t * p.t * (3 - 2 * p.t);
    const int held_idx = kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex);
    Sight sight = SightOf(held_idx);
    LoadTune();
    if (held_idx >= 0 && held_idx < 64 && !sight.optic) sight.x += g_tune[held_idx][0], sight.y += g_tune[held_idx][1];
    const float near = held_idx >= 0 && held_idx < 64 && !sight.optic ? g_tune[held_idx][2] : 0.0f;
    if (WeaponsTest()) {  // NF_ADS_NUDGE=x,y (diagnostic): added to every sight, to measure the view's scale
        static float nx = 0, ny = 0;
        static const bool read = [] { if (const char* e = std::getenv("NF_ADS_NUDGE")) std::sscanf(e, "%f,%f", &nx, &ny); return true; }();
        (void)read;
        sight.x += nx, sight.y += ny;
        // NF_ADS_CAL=1: the gun 2 cm to the left (+x) for picture b and 2 cm up for c, the view's scale at its sight
        int f = int(g_test_frames % TestPeriod()) + 200 - int(TestPeriod());  // the last 200 frames
        if (std::getenv("NF_ADS_CAL") && f >= 145 && f < 175) sight.x += 0.02f;
        if (std::getenv("NF_ADS_CAL") && f >= 175) sight.y += 0.02f;
    }
    // the tuning in settings.toml (aim_sight_x_mm, aim_sight_y_mm): the sights moved for every weapon
    sight.x += app::Get().aim_sight_x_mm / 1000.0f * (sight.optic ? 0.0f : 1.0f);
    sight.y += app::Get().aim_sight_y_mm / 1000.0f * (sight.optic ? 0.0f : 1.0f);
    p.optic = sight.optic && p.t >= 1.0f;
    SetF(gun + kGunPos, GetF(gun + kGunPos) + sight.x * s);
    SetF(gun + kGunPos + 4, GetF(gun + kGunPos + 4) + (p.optic ? -50.0f : sight.y * s));  // an optic: out of sight
    SetF(gun + kGunPos + 8, GetF(gun + kGunPos + 8) + near * s);  // the tuner's farther (+) or nearer (-)
    if (held_idx >= 0 && held_idx < 64 && !sight.optic) {  // the tuner's turns, eased in with the aim
        SetF(gun + 0x40, GetF(gun + 0x40) + g_tune[held_idx][3] * s);
        SetF(gun + 0x3C, GetF(gun + 0x3C) + g_tune[held_idx][4] * s);
        SetF(gun + 0x44, GetF(gun + 0x44) + g_tune[held_idx][5] * s);
    }
}

X86_EXTERN_FUNC(__imp__sub_000B6190);
X86_FUNC(sub_000B6190) {  // a player's HUD (cdecl: the actor, ...): no crosshair while aiming
    uint32_t actor = R32(ctx.esp + 4);
    nf::CoopHud(actor, false);  // story co-op: the full screen places back for the update
    __imp__sub_000B6190(ctx, base);
    nf::CoopHud(actor, true);   // then the places in the player's own view
    if (!actor || !R32(actor + kHud)) return;
    // a custom crosshair (Settings::crosshair_style, app/crosshair.cpp) replaces the game's, which stays hidden
    if (app::Get().crosshair_style != 0) {
        if (uint32_t sprite = R32(R32(actor + kHud))) kernel::G<uint8_t>(sprite + 0x38) = 0xFF;
        return;
    }
    int i = kernel::G<int8_t>(actor + kPlayerIndex);
    if (i < 0 || i >= 4 || !g_players[i].aiming || g_players[i].optic) return;
    if (uint32_t sprite = R32(R32(actor + kHud))) kernel::G<uint8_t>(sprite + 0x38) = 0xFF;
}
