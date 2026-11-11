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
#include <bit>
#include <cstdint>
#include <cstdlib>

#include "app/app.h"
#include "common/nightfire.h"
#include "core/log.h"
#include "core/netplay.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "gpu/style.h"
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
    nf::CameraShake(player, lost / 40.0f * 0.06f);  // about 6 cm for a hit of 40 health
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
            static int said = 0;
            if (said++ < 3) LOGR("hit feedback: co-op: player %d's shot at player %d stopped (no friendly fire)", shooter + 1, victim + 1);
            ctx.esp += 4;
            return;
        }
    }
    uint32_t data = who ? R32(who + 4) : 0;
    float before = data ? F32(data + 0x90) : 0;
    if (nf::CoopSplit() && hit && PlayerOf(who ? R32(who) : 0) >= 0) {
        uint32_t pj = R32(hit + 0x4C), ow = pj ? R32(pj + 0xBC) : 0;
        static int said = 0;
        if (said++ < 8) LOGR("hit feedback: co-op: a shot reaches player %d (from player %d, damage %.1f, health %.1f)",
                             PlayerOf(R32(who)) + 1, PlayerOf(ow ? R32(ow + 0x24) : 0) + 1, F32(hit + 8), before);
    }
    __imp__sub_0003D900(ctx, base);
    if (!hit || !data || !(before > 0)) return;
    float after = F32(data + 0x90);
    if (!(after < before)) return;  // no health taken (an ally, a scripted character, the shot absorbed)
    if (int victim = PlayerOf(R32(who)); victim >= 0) {
        Bleed(victim, before - after);
        if (nf::CoopSplit()) {
            uint32_t pj = R32(hit + 0x4C), ow = pj ? R32(pj + 0xBC) : 0;
            static int said = 0;
            if (said++ < 6) LOGR("hit feedback: co-op: player %d hit (by player %d), health %.1f -> %.1f", victim + 1,
                                 PlayerOf(ow ? R32(ow + 0x24) : 0) + 1, before, after);
        }
    }
    if (R32(hit + 0x4C)) nf::BloodHit(ctx, R32(who), hit);
    if (after <= 0) nf::RagdollHit(R32(who), hit);
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
    app::HitMarker(x, y, kind, before - after);
}

namespace {
uint32_t Env(const char* name, uint32_t fallback) {
    const char* e = std::getenv(name);
    return e && *e ? uint32_t(std::strtoul(e, nullptr, 0)) : fallback;
}
}  // namespace

namespace nf {
// Native sprite particles are depth-tested in the world and work in every renderer and split-screen view.
void BloodHit(X86Context& ctx, uint32_t victim, uint32_t hit) {
    const bool visible = gpu::Style().blood;
    // Lockstep copies must allocate the same native objects and consume the same game RNG,
    // even when one player hides blood locally. Hide rendering only after creation.
    if ((!visible && !netplay::Active()) || !victim || !hit) return;
    const uint32_t room = R32(victim + 0x20);
    if (!room || !mem::IsCommitted(room + 0x90) || !mem::IsCommitted(hit + 0x43)) return;
    float pos[3], normal[3];
    float length = 0;
    for (int k = 0; k < 3; k++) {
        pos[k] = F32(hit + 0x38 + k * 4);
        normal[k] = F32(hit + 0x10 + k * 4);
        if (!std::isfinite(pos[k]) || std::abs(pos[k]) > 100000) return;
        if (!std::isfinite(normal[k])) normal[k] = 0;
        length += normal[k] * normal[k];
    }
    if (length > 0.0001f && std::isfinite(length)) {
        for (float& n : normal) n /= std::sqrt(length);
    } else normal[0] = normal[2] = 0, normal[1] = 1;
    static const uint32_t scratch = mem::AllocHostContiguous(32, 16);
    static uint32_t seed = 0xB100D;
    auto random = [&] { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.0f; };
    const X86Context saved = ctx;
    int made = 0;
    for (int i = 0; i < 12; i++) {
        for (int k = 0; k < 3; k++) {
            kernel::G<float>(scratch + k * 4) = pos[k] + normal[k] * 0.025f;
            kernel::G<float>(scratch + 16 + k * 4) = normal[k] * (0.35f + random() * 0.7f) +
                (random() - 0.5f) * 1.1f + (k == 1 ? 0.2f : 0);
        }
        // NF_BLOOD_SPRITE / NF_BLOOD_MODE / NF_BLOOD_SCALE / NF_BLOOD_RGBA (diagnostic): another look to compare
        // the game's smoke puff 0x200001F, alpha blended (its character-hit puff 0x200021D is additive: it glowed like
        // sparks), dark red and opaque, fading out, three quarters of the old size
        static const uint32_t spr = Env("NF_BLOOD_SPRITE", 0x200001F), mode = Env("NF_BLOOD_MODE", 0),
                              rgba = Env("NF_BLOOD_RGBA", 0x8A0808FF);
        static const float scale = float(Env("NF_BLOOD_SCALE", 75)) / 100.0f;
        const float size = (i < 3 ? 0.065f : 0.015f + random() * 0.025f) * scale;
        const uint32_t sprite = kernel::CallGuest(ctx, 0xCD140, {room, scratch, 0, scratch + 16,
                std::bit_cast<uint32_t>(size), std::bit_cast<uint32_t>(size * 0.55f),
                spr, mode, rgba, (rgba & 0xFFFFFF00u) >> 1 & 0x7F7F7F00u, 18});
        if (sprite) {
            made++;
            if (!visible) kernel::W32(sprite + 0xCC, R32(sprite + 0xCC) | 0x10);
        }
    }
    ctx = saved;
    // blood that stays: a spatter on the surfaces round the hit and a pool under it (the renderer's, kept for a minute and
    // a half); only for us, never the game's state
#ifndef XBE_HEADLESS  // the headless harness has no renderer
    if (visible) gpu::AddBloodStain(pos, normal, 1.0f);
#endif
    static unsigned logged = 0;
    if (logged++ < 6) LOGR("blood: impact at %.2f %.2f %.2f, %d world particles", pos[0], pos[1], pos[2], made);
}
}

namespace nf {
// LAN play: a hit of our player on another copy's player (src/game/lan_play.cpp), its mark as a hit here would show
void LanHitMark(int player, bool head) {
    float x, y;
    ViewCentre(player, x, y);
    app::HitMarker(x, y, head ? 1 : 0);
}
}  // namespace nf

// NF_HIT_TEST=1: each player view's mark (hit, head, kill in turn) every second, from the HUD pass (0xDAC20), to
// see and hear the feedback without a hit (and with blood on, its spray and the view bleeding)
namespace nf {
void HitTest(X86Context& ctx) {
    static const bool on = [] {
        const char* e = std::getenv("NF_HIT_TEST");
        return e && (*e == '1' || *e == 'w');
    }();
    static uint32_t frame = 0;
    if (!on || ++frame % 60) return;
    const char* mode = std::getenv("NF_HIT_TEST");
    if (mode && *mode == 'w') {  // world-space effect diagnostic, no damage or screen overlay
        if (MenuUp()) return;
        const uint32_t cam = R32(kCameras), obj = cam ? R32(cam + kFollowed) : 0;
        if (!obj) return;
        static const uint32_t hit = mem::AllocHostContiguous(0x50, 16);
        for (int k = 0; k < 3; k++) {
            const float forward = F32(cam + 0x128 + k * 4);
            kernel::G<float>(hit + 0x38 + k * 4) = F32(cam + 0x138 + k * 4) + forward * 2;
            kernel::G<float>(hit + 0x10 + k * 4) = -forward;
        }
        BloodHit(ctx, obj, hit);
        return;
    }
    int players = 0;
    for (int p = 0; p < 4; p++)
        if (uint32_t cam = R32(kCameras + 4 * p); cam && R32(cam + kFollowed)) players = p + 1;
    for (int p = 0; p < std::max(players, 1); p++) {
        float x, y;
        ViewCentre(p, x, y);
        int kind = int(frame / 60 % 3);
        app::BloodSpray(x, y, kind == 2 ? 1.0f : kind == 1 ? 0.8f : 0.45f);
        if (kind == 2) Bleed(p, 30.0f);
        app::HitMarker(x, y, kind, 25.0f * (kind + 1));
    }
}
}  // namespace nf
