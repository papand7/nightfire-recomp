// Story co-op's play (nf::coop; the second player and the split views are made in coop_test.cpp). From the static RE in
// .local/coop_notes.md (2026-11-07):
// - the HUD: each player's is made by 0xB6AA0 from the story table (0x180EE0) and updated each frame by 0xB6190; its
//   sprites are drawn by the full screen camera 5 at their own places, so the elements laid out for one full screen
//   (layout flags 0: the radar ring at 50,345) sat at the same place for both players. They are drawn here in the
//   player's own view: each frame after the update their places scaled into its rectangle (cam +0xDC/+0xE4, +0xF8/+0xFC),
//   the full screen's put back before the next update.
// - the first person gun ([actor+0x778]) is drawn by every camera its +0xD4 mask has a bit for (0x1F): each player's
//   kept to its own camera (1 << p), so the other's no longer floats in a view.
// - the body: the walk set is chosen by 0xA91E0 / 0xA9350 and the one-shot moves by 0xA94C0 / 0xA9590, which take one
//   still pose without the multiplayer flag [0x260018]: set for those calls alone, so each player sees the other walk.
// - cutscenes: the game freezes, places and releases player 1 alone (0xABD70 obj, 0; 0xABBD0 obj, matrix|0, snap):
//   player 2 the same, a step to the side. The cutscene camera 4 gets a split view's aspect from 0x25190 (view.cpp).
// - player 2's death: 0xAC390 (the death test) sets the story's failure flags ([0x1DF196], event flag 6 at
//   [0x29A198]): kept as they were for player 2; its dead frames 0xAC690 (obj in eax) skipped, and after 3 s player 2
//   comes back beside player 1 as the multiplayer respawn does (0x9F0B0's steps, then 0xAC7E0 obj, place*, euler*, 0).
//   Player 1's death is the game's own (the mission failed).
// - triggers and objectives read player 1 alone (most of the level's logic): player 2 is kept near (moved beside player
//   1 after 3 s over 20 units away or outside the level's zones, never in a cutscene; 0x21110 relinks its zone).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;
using kernel::W32;

constexpr uint32_t kPlayers = 0x1F6654, kCameras = 0x1F661C, kWorld = 0x1F6674, kCutscene = 0x1F6678, kMulti = 0x260018;
constexpr uint32_t kActor = 0xBC, kPlace = 0x24, kEuler = 0x3C, kZone = 0x20, kState = 0xD0, kHud = 0x770, kGun = 0x778;
constexpr uint32_t kFailed = 0x1DF196, kEventFlags = 0x29A198;

float F(uint32_t va) { return kernel::G<float>(va); }
void SetF(uint32_t va, float v) { kernel::G<float>(va) = v; }
bool Plausible(uint32_t va) { return va >= 0x10000 && (va & 0x7FFFFFFF) < 0x8000000; }
bool On() { return nf::CoopSplit() != 0; }
// NF_COOP_OFF (a test): bits leave parts out: 1 the HUD, 2 the guns, 4 the body, 8 cutscenes and death, 16 the leash
int Off() {
    static const int v = [] {
        const char* e = std::getenv("NF_COOP_OFF");
        return e && *e ? std::atoi(e) : 0;
    }();
    return v;
}
uint32_t Player(int p) { return R32(kPlayers + 4 * uint32_t(p)); }
bool Dead(uint32_t obj) { uint32_t s = R32(obj + kState); return s == 2 || s == 3; }
uint32_t Scratch() {
    static uint32_t s = mem::AllocSystem(0x40);
    return s;
}

// ---- the HUD in each player's view
std::unordered_map<uint32_t, std::pair<int16_t, int16_t>> g_full;  // a sprite's full screen place

template <typename Fn>
void FullScreenSprites(uint32_t actor, Fn fn) {
    uint32_t hud = R32(actor + kHud);
    if (!Plausible(hud)) return;
    // the radar ring (element 1, laid out for one full screen at 50,345) alone: the other full screen elements are
    // overlays meant for the whole screen, hidden below it when off (scaling their places brought them into view)
    for (uint32_t i = 1; i < 2; i++) {
        uint32_t elem = hud + 8 + 0x20 * i, desc = R32(elem);
        if (!Plausible(desc) || (kernel::G<uint16_t>(desc + 0x18) & 0x198)) continue;  // laid out in the view already
        uint32_t list = R32(elem + 8), n = std::min<uint32_t>(R32(elem + 0x18), 64);
        if (!Plausible(list)) continue;
        for (uint32_t k = 0; k < n; k++)
            if (uint32_t s = R32(list + 4 * k); Plausible(s)) fn(s);
    }
}

// ---- moving player 2
void Beside(uint32_t lead, float out[3], float side) {
    float h = F(lead + 0x40);  // the heading: forward (sin h, 0, cos h)
    out[0] = F(lead + kPlace) + std::cos(h) * side;
    out[1] = F(lead + kPlace + 4);
    out[2] = F(lead + kPlace + 8) - std::sin(h) * side;
}
// obj's place set (its last, its actor's) and its zone relinked; false (put back) where the level has no zone
bool MoveTo(X86Context& ctx, uint32_t obj, const float pos[3]) {
    uint32_t actor = R32(obj + kActor);
    float was[3];
    for (int i = 0; i < 3; i++) was[i] = F(obj + kPlace + 4 * i);
    auto put = [&](const float* v) {
        for (int i = 0; i < 3; i++) {
            SetF(obj + kPlace + 4 * i, v[i]), SetF(obj + 0x30 + 4 * i, v[i]);
            if (Plausible(actor)) SetF(actor + 4 * i, v[i]);
        }
    };
    put(pos);
    if (kernel::CallGuest(ctx, 0x21110, {obj, 1, R32(kWorld)})) return true;
    put(was);
    kernel::CallGuest(ctx, 0x21110, {obj, 1, R32(kWorld)});
    return false;
}

// player 2 back beside player 1 (0x9F0B0's steps, then 0xAC7E0)
void Respawn(X86Context& ctx, uint32_t obj, uint32_t lead) {
    uint32_t actor = R32(obj + kActor);
    if (!Plausible(actor)) return;
    if (uint32_t hud = R32(actor + kHud); Plausible(hud)) kernel::CallGuest(ctx, 0xB1A60, {hud, 0xC, 0, 0});
    kernel::CallGuest(ctx, 0xABEF0, {actor, 0});
    SetF(actor + 0x840, 0), SetF(actor + 0x844, 0);  // no armour
    float health = 100.0f + float(int32_t(R32(0x25FE64 + 0x30)));
    uint32_t hb;
    std::memcpy(&hb, &health, 4);
    kernel::CallGuest(ctx, 0xAC050, {actor, hb});
    SetF(actor + 0x824, health), SetF(actor + 0x848, health);
    W32(actor + 0x110, 0), W32(actor + 0x10C, 0), W32(actor + 0x838, 0);
    kernel::G<uint8_t>(obj + 0xDB) = 3;
    W32(obj + 0xCC, R32(obj + 0xCC) & ~0x20u);
    kernel::CallGuest(ctx, 0x2CF10, {obj, obj + 0x70});
    const uint32_t s = Scratch();
    float pos[3];
    Beside(lead, pos, 1.2f);
    for (int i = 0; i < 3; i++) SetF(s + 4 * i, pos[i]), SetF(s + 0x10 + 4 * i, F(lead + kEuler + 4 * i));
    kernel::CallGuest(ctx, 0xAC7E0, {obj, s, s + 0x10, 0});
    kernel::CallGuest(ctx, 0x18690, {obj});
    LOGR("nightfire: co-op: player 2 back beside player 1 (health %.0f)", health);
}

uint32_t g_dead_frames = 0, g_far_frames = 0;
}  // namespace

namespace nf {
// aim.cpp's 0xB6190 hook: before the HUD's update the full screen places back, after it the places in the view
void CoopHud(uint32_t actor, bool after) {
    if (!On() || !Plausible(actor) || (Off() & 1)) return;
    int p = kernel::G<int8_t>(actor + 0x8DE);
    if (p < 0 || p > 1) return;
    if (!after) {
        FullScreenSprites(actor, [&](uint32_t s) {
            if (auto it = g_full.find(s); it != g_full.end())
                kernel::G<int16_t>(s + 0x24) = it->second.first, kernel::G<int16_t>(s + 0x26) = it->second.second;
        });
        return;
    }
    uint32_t cam = R32(kCameras + 4 * uint32_t(p));
    if (!Plausible(cam) || R32(kCutscene)) return;  // a cutscene: full screen
    const float x0 = F(cam + 0xDC), y0 = F(cam + 0xE4), w = F(cam + 0xF8), h = F(cam + 0xFC);
    if (!(w > 0 && h > 0) || (w >= 639 && h >= 479)) return;
    FullScreenSprites(actor, [&](uint32_t s) {
        int16_t x = kernel::G<int16_t>(s + 0x24), y = kernel::G<int16_t>(s + 0x26);
        if (x < -64 || x > 640 || y < -64 || y > 480) return;  // off the screen: kept there
        g_full[s] = {x, y};
        kernel::G<int16_t>(s + 0x24) = int16_t(std::lround(x0 + x * w / 640.0f));
        kernel::G<int16_t>(s + 0x26) = int16_t(std::lround(y0 + y * h / 480.0f));
    });
    if (g_full.size() > 4096) g_full.clear();
}

// each frame (coop_test.cpp's pad hook): the guns' views, player 2's respawn and leash
void CoopFrame(X86Context& ctx) {
    if (!On()) {
        g_dead_frames = g_far_frames = 0;
        return;
    }
    const uint32_t lead = Player(0), second = Player(1);
    for (int p = 0; p < 2 && !(Off() & 2); p++)
        if (uint32_t obj = Player(p); Plausible(obj))
            if (uint32_t actor = R32(obj + kActor); Plausible(actor))
                if (uint32_t gun = R32(actor + kGun); Plausible(gun)) kernel::G<uint16_t>(gun + 0xD4) = uint16_t(1u << p);
    if (!Plausible(lead) || !Plausible(second) || R32(kCutscene) || (Off() & 16)) return;
    if (Dead(second)) {
        if (++g_dead_frames > 180 && !Dead(lead)) Respawn(ctx, second, lead), g_dead_frames = 0;
        return;
    }
    g_dead_frames = 0;
    if (Dead(lead)) return;
    float dx = F(second + kPlace) - F(lead + kPlace), dy = F(second + kPlace + 4) - F(lead + kPlace + 4),
          dz = F(second + kPlace + 8) - F(lead + kPlace + 8);
    bool far = dx * dx + dy * dy + dz * dz > 20.0f * 20.0f || !R32(second + kZone);
    g_far_frames = far ? g_far_frames + 1 : 0;
    if (g_far_frames > 180) {
        float pos[3];
        Beside(lead, pos, 1.2f);
        bool ok = MoveTo(ctx, second, pos);
        if (!ok) Beside(lead, pos, -1.2f), ok = MoveTo(ctx, second, pos);
        LOGR("nightfire: co-op: player 2 brought to player 1 (%s)", ok ? "beside" : "no room there");
        g_far_frames = 0;
    }
}
}  // namespace nf

// ---- the body's walk and moves: chosen with the multiplayer flag set, as a match does. Off unless NF_COOP_BODY=1:
// in a story level it took the multiplayer's sets, which are not loaded there (the screen filled with a broken sprite)
bool BodyHack() {
    static const bool on = [] {
        const char* e = std::getenv("NF_COOP_BODY");
        return e && *e == '1';
    }();
    return on;
}
#define WITH_MULTI(addr)                                                            \
    X86_EXTERN_FUNC(__imp__sub_##addr);                                             \
    X86_FUNC(sub_##addr) {                                                          \
        uint32_t obj = R32(ctx.esp + 4);                                            \
        bool mine = On() && BodyHack() && obj && (obj == Player(0) || obj == Player(1));          \
        uint32_t was = R32(kMulti);                                                 \
        if (mine) W32(kMulti, 1);                                                   \
        __imp__sub_##addr(ctx, base);                                               \
        if (mine) W32(kMulti, was);                                                 \
    }
WITH_MULTI(000A91E0)
WITH_MULTI(000A9350)
WITH_MULTI(000A94C0)
WITH_MULTI(000A9590)

// ---- cutscenes: player 2 frozen and released with player 1
X86_EXTERN_FUNC(__imp__sub_000ABD70);
X86_FUNC(sub_000ABD70) {  // a player frozen for a script (obj, 0)
    uint32_t obj = R32(ctx.esp + 4), arg = R32(ctx.esp + 8);
    __imp__sub_000ABD70(ctx, base);
    if (On() && !(Off() & 8) && obj == Player(0) && Plausible(Player(1))) {
        uint32_t eax = ctx.eax;
        kernel::CallGuest(ctx, 0xABD70, {Player(1), arg});
        ctx.eax = eax;
    }
}

X86_EXTERN_FUNC(__imp__sub_000ABBD0);
X86_FUNC(sub_000ABBD0) {  // a player released (obj, matrix* or 0, snap): player 2 too, a step to the side
    uint32_t obj = R32(ctx.esp + 4), matrix = R32(ctx.esp + 8), snap = R32(ctx.esp + 12);
    __imp__sub_000ABBD0(ctx, base);
    const uint32_t second = Player(1);
    if (!On() || (Off() & 8) || obj != Player(0) || !Plausible(second)) return;
    uint32_t eax = ctx.eax;
    kernel::CallGuest(ctx, 0xABBD0, {second, matrix, snap});
    if (matrix) {
        float pos[3];
        Beside(obj, pos, 1.2f);
        if (!MoveTo(ctx, second, pos)) Beside(obj, pos, -1.2f), MoveTo(ctx, second, pos);
    }
    ctx.eax = eax;
}

// ---- player 2's death: no mission failure, a respawn (CoopFrame)
X86_EXTERN_FUNC(__imp__sub_000AC390);
X86_FUNC(sub_000AC390) {  // the death test (obj, class)
    uint32_t obj = R32(ctx.esp + 4);
    const bool second = On() && !(Off() & 8) && obj && obj == Player(1);
    const uint8_t failed = second ? kernel::G<uint8_t>(kFailed) : 0;
    const uint32_t flags = second ? R32(kEventFlags) : 0;
    __imp__sub_000AC390(ctx, base);
    if (second) kernel::G<uint8_t>(kFailed) = failed, W32(kEventFlags, flags);
}

X86_EXTERN_FUNC(__imp__sub_000AC690);
X86_FUNC(sub_000AC690) {  // a dead player's frame (obj in eax): the story's "mission failed" wait
    if (On() && !(Off() & 8) && ctx.eax && ctx.eax == Player(1)) {
        ctx.esp += 4;  // player 2: none (its respawn is CoopFrame's)
        return;
    }
    __imp__sub_000AC690(ctx, base);
}
