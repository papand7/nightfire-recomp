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
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/app.h"
#include "common/nightfire.h"
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
        case 1: case 2: return {0.264f, 0.135f, kZoom, false};                  // the P2K
        case 3: return {0.270f, 0.129f, kZoom, false};
        case 4: return {0.266f, 0.128f, kZoom, false};
        case 5: return {0.266f, 0.128f, kZoom, false};
        case 6: return {0.268f, 0.108f, kZoom, false};                          // 6-9 silenced: the rear notch
        case 7: case 9: return {0.266f, 0.108f, kZoom, false};
        case 8: return {0.267f, 0.108f, kZoom, false};
        case 10: case 11: case 12: return {0.215f, 0.070f, kZoom, false};
        case 13: return {0.222f, 0.090f, kZoom, false};
        case 14: case 15: return {0.358f, 0.100f, kZoom, false};
        case 16: return {0.359f, 0.102f, kZoom, false};
        case 18: return {0.233f, 0.121f, kZoom, false};
        case 19: return {0.200f, 0.073f, kZoom, false};
        case 20: return {0.253f, 0.143f, kZoom, false};
        case 21: return {0.254f, 0.144f, kZoom, false};
        case 22: case 23: return {0.250f, 0.084f, kZoom, false};
        case 24: case 25: return {0.230f, 0.076f, 3.0f, true};                // the rifle with a scope
        case 28: case 29: return {0.206f, 0.041f, kZoom, false};                // the receiver's top
        case 42: case 43: return {0.0f, 0.0f, 2.0f, true};
        case 44: case 45: case 46: case 47: case 48: case 49: return {0.0f, 0.0f, 1.6f, true};  // the launchers
        case 50: return {0.160f, 0.153f, kZoom, false};
        default: return {0.25f, 0.10f, kZoom, false};
    }
}
constexpr float kEase = 0.125f;                        // of the way in or out a frame

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
void AimFireTest(X86Context& ctx) {
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
        // 2026-11-05) have no sights: L is the game's own aim for both
        int idx = kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex);
        p.scoped = (R32(Weapon(obj) + kWeaponFlags) & kScoped) || (idx >= 52 && idx <= 56);
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
                if (!kernel::G<uint16_t>(w)) continue;  // no weapon there
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
    if (gun) nf::GunSway(i, gun, 1 - 0.8f * p.t * p.t * (3 - 2 * p.t));  // sway: a fifth of it when aimed
    if (!gun || p.t <= 0) return;
    float s = p.t * p.t * (3 - 2 * p.t);
    Sight sight = SightOf(kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex));
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
    p.optic = sight.optic && p.t >= 1.0f;
    SetF(gun + kGunPos, GetF(gun + kGunPos) + sight.x * s);
    SetF(gun + kGunPos + 4, GetF(gun + kGunPos + 4) + (p.optic ? -50.0f : sight.y * s));  // an optic: out of sight
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
