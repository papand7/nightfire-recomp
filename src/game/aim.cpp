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
constexpr float kSight[3] = {0.25f, 0.06f, 0.0f};      // the gun moved to the middle (its model's frame: +x left)
constexpr float kEase = 0.125f;                        // of the way in or out a frame
constexpr uint32_t kCameras = 0x1F661C;                // the player views' cameras (a second: split screen)

struct Player {
    bool held = false;    // L held last frame (the toggle's edge)
    bool aiming = false;
    bool was = false;     // the zoom set for aiming (put back once let go)
    bool scoped = true;   // the weapon has a scope (0xBA8F0's last frame): the game's own manual aim
    float t = 0;          // the gun's way to the middle
} g_players[4];

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

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
        p.scoped = R32(Weapon(obj) + kWeaponFlags) & kScoped;
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
            SetF(actor + kZoomMost, std::max(GetF(actor + kZoomMost), kZoom));
            SetF(actor + kZoomTarget, kZoom);
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
    Player& p = g_players[i];
    p.t = std::clamp(p.t + (p.aiming ? kEase : -kEase), 0.0f, 1.0f);
    if (i == 0) {  // player 1's sights: the focus blur, with one view only (split screen's would blur the others')
        bool one_view = !R32(kCameras + 4);
        float s = p.t * p.t * (3 - 2 * p.t);
        app::SetFocusBlur(nf::ads_focus_blur && one_view ? s : 0.0f);
    }
    uint32_t gun = R32(R32(obj + kActor) + kGun);
    if (!gun || p.t <= 0) return;
    float s = p.t * p.t * (3 - 2 * p.t);
    for (uint32_t k = 0; k < 3; k++) SetF(gun + kGunPos + k * 4, GetF(gun + kGunPos + k * 4) + kSight[k] * s);
}

X86_EXTERN_FUNC(__imp__sub_000B6190);
X86_FUNC(sub_000B6190) {  // a player's HUD (cdecl: the actor, ...): no crosshair while aiming
    uint32_t actor = R32(ctx.esp + 4);
    __imp__sub_000B6190(ctx, base);
    if (!actor || !R32(actor + kHud)) return;
    int i = kernel::G<int8_t>(actor + kPlayerIndex);
    if (i < 0 || i >= 4 || !g_players[i].aiming) return;
    if (uint32_t sprite = R32(R32(actor + kHud))) kernel::G<uint8_t>(sprite + 0x38) = 0xFF;
}
