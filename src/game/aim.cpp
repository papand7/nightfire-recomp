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
constexpr float kZoom = 1.3f;                          // the view's angle divided by this
// each weapon's sights brought to the middle of the view (its model's frame: +x left, +y up): the gun's place at the
// hip (0xBCAA0) moved by its sight's place on screen there, at the model's depth (NF_ADS_WEAPONS shots, 2026-11-05:
// the sight's pixels from the middle times the depth over the 1221-pixel focal length of a 1410-pixel view, then
// corrected from aimed shots at the zoomed 1587). An optic
// (a sight that hides the view: the scoped rifles' that are not the game's scopes, the launchers) zooms by the
// weapon's own amount and the gun is put out of sight at full aim, the crosshair left on
struct Sight {
    float x, y;
    float zoom;   // the view's when aiming (the game's own for an optic)
    bool optic;
};
Sight SightOf(int idx) {
    switch (idx) {
        case 1: case 2: return {0.25f, 0.11f, kZoom, false};                    // the P2K
        case 3: case 4: case 5: case 6: case 7: case 8: case 9:
            return {0.287f, 0.110f, kZoom, false};
        case 10: case 11: case 12: case 13: return {0.215f, 0.070f, kZoom, false};
        case 14: case 15: case 16: return {0.349f, 0.104f, kZoom, false};
        case 18: case 19: return {0.255f, 0.120f, kZoom, false};
        case 20: case 21: return {0.250f, 0.144f, kZoom, false};
        case 22: case 23: return {0.230f, 0.076f, kZoom, false};
        case 24: case 25: return {0.230f, 0.076f, 3.0f, true};                // the rifle with a scope
        case 28: case 29: return {0.166f, 0.068f, kZoom, false};
        case 42: case 43: return {0.0f, 0.0f, 2.0f, true};
        case 44: case 45: case 46: case 47: case 48: case 49: return {0.0f, 0.0f, 1.6f, true};  // the launchers
        case 50: return {0.160f, 0.153f, kZoom, false};
        default: return {0.25f, 0.10f, kZoom, false};
    }
}
constexpr float kEase = 0.125f;                        // of the way in or out a frame
constexpr uint32_t kCameras = 0x1F661C;                // the player views' cameras (a second: split screen)

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
            float zoom = SightOf(kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex)).zoom;
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
    if (i == 0 && WeaponsTest()) {
        static uint32_t frames = 0, idx = uint32_t(WeaponsTest()) - 1;
        if (frames % 200 == 195 && std::getenv("NF_ADS_SHOTS")) {  // its picture: the renderer's shot file
            if (FILE* sf = std::fopen("shot", "w")) std::fprintf(sf, "%s\\w_%02u.png\n", std::getenv("NF_ADS_SHOTS"), idx), std::fclose(sf);
        }
        if (++frames % 200 == 0) {
            uint32_t state = R32(obj + kWeaponState);
            for (int tries = 0; tries < 64 && state; tries++) {
                idx = (idx + 1) % 64;
                uint32_t w = kWeapons + idx * kWeaponSize;
                if (!kernel::G<uint16_t>(w)) continue;  // no weapon there
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
    if (i == 0) {  // player 1's sights: the focus blur, with one view only (split screen's would blur the others')
        bool one_view = !R32(kCameras + 4);
        float s = p.t * p.t * (3 - 2 * p.t);
        app::SetFocusBlur(nf::ads_focus_blur && one_view ? s : 0.0f);
    }
    uint32_t gun = R32(R32(obj + kActor) + kGun);
    if (i == 0 && WeaponsTest() && gun) {  // the test: alive, and each weapon's own place (before ours) logged
        SetF(R32(obj + kActor) + 0x824, 100.0f);
        static int last = -1;
        int held = kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex);
        static int settle = 0;
        if (held != last) last = held, settle = 0;
        if (++settle == 190)
            LOGR("nightfire: ADS test: weapon %d at %.3f %.3f %.3f", held, GetF(gun + kGunPos), GetF(gun + kGunPos + 4),
                 GetF(gun + kGunPos + 8));
    }
    if (gun) nf::GunSway(i, gun, 1 - 0.8f * p.t * p.t * (3 - 2 * p.t));  // sway: a fifth of it when aimed
    if (!gun || p.t <= 0) return;
    float s = p.t * p.t * (3 - 2 * p.t);
    Sight sight = SightOf(kernel::G<int8_t>(R32(obj + kWeaponState) + kWeaponIndex));
    p.optic = sight.optic && p.t >= 1.0f;
    SetF(gun + kGunPos, GetF(gun + kGunPos) + sight.x * s);
    SetF(gun + kGunPos + 4, GetF(gun + kGunPos + 4) + (p.optic ? -50.0f : sight.y * s));  // an optic: out of sight
}

X86_EXTERN_FUNC(__imp__sub_000B6190);
X86_FUNC(sub_000B6190) {  // a player's HUD (cdecl: the actor, ...): no crosshair while aiming
    uint32_t actor = R32(ctx.esp + 4);
    __imp__sub_000B6190(ctx, base);
    if (!actor || !R32(actor + kHud)) return;
    int i = kernel::G<int8_t>(actor + kPlayerIndex);
    if (i < 0 || i >= 4 || !g_players[i].aiming || g_players[i].optic) return;
    if (uint32_t sprite = R32(R32(actor + kHud))) kernel::G<uint8_t>(sprite + 0x38) = 0xFF;
}
