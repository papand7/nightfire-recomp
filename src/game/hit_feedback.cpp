// Hit feedback (app::HitMarker): 0x3D900 (cdecl: the character hit's pair {object, data}, the hit, a scale) is every
// shot's damage to a character on foot, single player and multiplayer, bots too; it hands 0x3D670 the damage, the
// hit zone and the weapon. The hit: +8 the damage, +0x46 the zone (5 the head, 0x3D670 multiplies by 0x164074 for
// it), +0x4C the projectile, whose +0xBC's +0x24 is the shooter. The character's health is the data's +0x90 float
// (0x3D670 subtracts from it, at 0 or under it dies). The shooter is a player's when it is the object a player's
// camera (0x1F661C, 0-3) follows at +0x64 (measured: a two-player Fort Knox match, player 1's shots). A hit that
// takes health away marks that player's crosshair: the head amber, the health crossing 0 a kill. With blood on
// (gpu::GraphicsStyle::blood) it sprays there too, and a player hit bleeds at the edges of their view.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "app/app.h"
#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;
constexpr uint32_t kCameras = 0x1F661C, kFollowed = 0x64, kAspect = 0x100, kWidescreen = 0x1F6610;

float F32(uint32_t a) { return kernel::G<float>(a); }

// the player whose camera follows the shooter, -1 none
int PlayerOf(uint32_t shooter) {
    if (!shooter) return -1;
    for (int i = 0; i < 4; i++)
        if (uint32_t cam = R32(kCameras + 4 * i); cam && R32(cam + kFollowed) == shooter) return i;
    return -1;
}

// the centre of player i's view in the shown frame: a camera's aspect is the screen's alone, doubled for two players
// (the screen split across), else quarters
void ViewCentre(int i, float& x, float& y) {
    x = 0.5f, y = 0.5f;
    int players = 0;
    for (int p = 0; p < 4; p++)
        if (uint32_t cam = R32(kCameras + 4 * p); cam && R32(cam + kFollowed)) players = p + 1;
    if (players <= 1) return;
    float screen = R32(kWidescreen) ? 16.0f / 9.0f : 4.0f / 3.0f;
    uint32_t cam = R32(kCameras + 4 * i);
    float aspect = cam ? F32(cam + kAspect) : screen;
    if (players == 2 && aspect > screen * 1.5f) {
        y = i == 0 ? 0.25f : 0.75f;
        return;
    }
    x = (i & 1) ? 0.75f : 0.25f;
    y = (i & 2) ? 0.75f : 0.25f;
}

// player i's view in the shown frame: about its centre, the whole frame, half or a quarter of it
void ViewRect(int i, float& x0, float& y0, float& x1, float& y1) {
    float x, y;
    ViewCentre(i, x, y);
    float hw = x == 0.5f ? 0.5f : 0.25f, hh = y == 0.5f ? 0.5f : 0.25f;
    x0 = x - hw, y0 = y - hh, x1 = x + hw, y1 = y + hh;
}

void Bleed(int player, float lost) {
    float x0, y0, x1, y1;
    ViewRect(player, x0, y0, x1, y1);
    app::BloodHurt(x0, y0, x1, y1, lost / 40.0f);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_0003D900);
X86_FUNC(sub_0003D900) {
    uint32_t who = R32(ctx.esp + 4), hit = R32(ctx.esp + 8);
    if (nf::LanDamage(who, hit)) {  // LAN play: a puppet's health is its own copy's
        ctx.esp += 4;
        return;
    }
    // story co-op without friendly fire: one player's shot at the other does nothing
    if (nf::CoopSplit() && !nf::coop_friendly_fire && hit) {
        uint32_t p = R32(hit + 0x4C), o = p ? R32(p + 0xBC) : 0;
        int shooter = PlayerOf(o ? R32(o + 0x24) : 0), victim = PlayerOf(who ? R32(who) : 0);
        if (shooter >= 0 && victim >= 0 && shooter != victim) {
            ctx.esp += 4;
            return;
        }
    }
    uint32_t data = who ? R32(who + 4) : 0;
    float before = data ? F32(data + 0x90) : 0;
    __imp__sub_0003D900(ctx, base);
    if (!hit || !data || !(before > 0)) return;
    float after = F32(data + 0x90);
    if (!(after < before)) return;  // no health taken (an ally, a scripted character, the shot absorbed)
    if (int victim = PlayerOf(R32(who)); victim >= 0) Bleed(victim, before - after);
    uint32_t proj = R32(hit + 0x4C), owner = proj ? R32(proj + 0xBC) : 0;
    int player = PlayerOf(owner ? R32(owner + 0x24) : 0);
    if (player < 0 || PlayerOf(R32(who)) == player) return;  // not a player's shot, or their own
    int kind = after <= 0 ? 2 : kernel::G<uint16_t>(hit + 0x46) == 5 ? 1 : 0;
    float x, y;
    ViewCentre(player, x, y);
    static int logged = 0;
    if (logged < 3) {
        logged++;
        LOGR("hit feedback: player %d hit (%s), health %.1f -> %.1f, mark at %.2f %.2f", player + 1,
             kind == 2 ? "kill" : kind == 1 ? "head" : "body", before, after, x, y);
    }
    app::BloodSpray(x, y, kind == 2 ? 1.0f : kind == 1 ? 0.8f : 0.45f);
    app::HitMarker(x, y, kind);
}

namespace nf {
// LAN play: a hit of our player on another copy's player (src/game/lan_play.cpp), its mark as a hit here would show
void LanHitMark(int player, bool head) {
    float x, y;
    ViewCentre(player, x, y);
    app::BloodSpray(x, y, head ? 0.8f : 0.45f);
    app::HitMarker(x, y, head ? 1 : 0);
}
}  // namespace nf

// NF_HIT_TEST=1: each player view's mark (hit, head, kill in turn) every second, from the HUD pass (0xDAC20), to
// see and hear the feedback without a hit (and with blood on, its spray and the view bleeding)
namespace nf {
void HitTest() {
    static const bool on = [] {
        const char* e = std::getenv("NF_HIT_TEST");
        return e && *e == '1';
    }();
    static uint32_t frame = 0;
    if (!on || ++frame % 60) return;
    int players = 0;
    for (int p = 0; p < 4; p++)
        if (uint32_t cam = R32(kCameras + 4 * p); cam && R32(cam + kFollowed)) players = p + 1;
    for (int p = 0; p < std::max(players, 1); p++) {
        float x, y;
        ViewCentre(p, x, y);
        int kind = int(frame / 60 % 3);
        app::BloodSpray(x, y, kind == 2 ? 1.0f : kind == 1 ? 0.8f : 0.45f);
        if (kind == 2) Bleed(p, 30.0f);
        app::HitMarker(x, y, kind);
    }
}
}  // namespace nf
