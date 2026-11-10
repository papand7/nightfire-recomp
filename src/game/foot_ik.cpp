// Feet on the ground (nf::foot_ik): the characters' legs bent so each foot stands on the ground under it, on slopes,
// steps and uneven snow, where the game's walk animations (made on flat ground) left one foot in the air or in the
// ground. The original has none: its feet follow the animation and the body's height comes from its capsule.
//
// A character's pose is solved once a frame by 0xDD290 (cdecl: the object, its skeleton = [obj+0xB8] + 0x58, a char
// "skin too"): the bones' world matrices at [skel+0x40] (0x3C bytes each: three rows of three floats at +0, +0x10,
// +0x20, the place at +0x30; a point p goes to x*R0 + y*R1 + z*R2 + T), flag 4 at skel+0x50 once they are made, then
// the skin matrices from them (flag 8). The hook has the world pose made first (the original, no skin), bends the
// legs there, then lets the original make the skin matrices from it. The human skeleton (73 bones: its model at
// [skel+0x1C], the parents at [model+0x20], low 7 bits, 0x7F none) has the pelvis 0, a leg 49 thigh, 50 knee, 51
// ankle, 52 toe, the other 53-56; every bone hangs from the pelvis.
//
// The ground: the game's ray (0x2C4E0, cdecl: start*, end*, the object's zone [obj+0x20], the object to pass through,
// 0, Contact** list, 0, mask, flags; the player's floor probe's mask 0x0C, flags 8), its nearest contact first (the
// hit at +0x38, the normal at +0x10), the list freed by 0x294D0(&list). Under the character's middle (the ground the
// animation stood on) and under each ankle: a foot over lower ground is lowered by the difference (the pelvis with it,
// so the leg reaches), one over higher ground raised, the knee bending in the plane it bent in, the foot tilted a
// little with the slope. Eased over a few frames; none in the air or dead.
// NF_IK_TRACE=1 logs the numbers every 5 s; NF_IK=0/1 overrides the setting.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "common/nightfire.h"
#include "game/bones.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
using namespace bones;

constexpr uint32_t kLegs[2][4] = {{49, 50, 51, 52}, {53, 54, 55, 56}};
constexpr float kMostDown = 0.45f, kMostUp = 0.35f;  // how far a foot follows the ground, world units
constexpr float kEase = 0.3f;                        // a frame's share of the way to the new bend
constexpr float kTilt = 0.6f;                        // how much of the slope a foot takes

// the ground straight down from above a point
struct Floor {
    bool ok = false;
    float y = 0;
    V n{0, 1, 0};
};
Floor Ground(X86Context& ctx, uint32_t obj, V from, float up, float down) {
    const Hit h = Ray(ctx, obj, from + V{0, up, 0}, from - V{0, down, 0});
    Floor g;
    g.ok = h.ok && h.n.y > 0.5f;
    if (g.ok) g.y = h.at.y, g.n = h.n;
    return g;
}

struct Feet {
    float delta[2] = {};  // each foot's ground against the middle's, eased
    float drop = 0;       // the pelvis lowered by, eased
    V n[2] = {{0, 1, 0}, {0, 1, 0}};
    uint32_t seen = 0;
};
std::unordered_map<uint32_t, Feet> g_feet;
uint32_t g_frame = 0;
bool g_inner = false;
float g_fit = 0;  // the largest miss of a turned knee and ankle against where they were solved to be (a check)

void BendLegs(X86Context& ctx, uint32_t obj, uint32_t skel, uint32_t parents) {
    static const bool trace = [] {
        const char* e = std::getenv("NF_IK_TRACE");
        return e && *e == '1';
    }();
    const uint32_t world = R32(skel + kWorld);
    if (!Plausible(world)) return;
    // dead (its death playing) or no actor: left as animated
    uint32_t actor = R32(obj + kActor);
    if (Plausible(actor) && F(actor + 0x90) <= 0) return;
    M old[kBones], now[kBones];
    for (uint32_t i = 0; i < kBones; i++) old[i] = now[i] = Load(world + i * kStride);
    const V place{F(obj + kPos), F(obj + kPos + 4), F(obj + kPos + 8)};
    if (!Finite(place) || !Finite(old[51].t) || !Finite(old[55].t)) return;

    Feet& ft = g_feet[obj];
    ft.seen = g_frame;
    // the ground the animation stood on (under the middle) and under each foot
    const Floor mid = Ground(ctx, obj, place, 0.3f, 3.0f);
    float want[2] = {0, 0};
    V normal[2] = {{0, 1, 0}, {0, 1, 0}};
    const float lowest = std::min(old[51].t.y, old[55].t.y);
    const bool standing = mid.ok && lowest - mid.y < 0.35f;  // in the air: the feet well above the ground
    if (standing)
        for (int l = 0; l < 2; l++) {
            const V ankle = old[kLegs[l][2]].t;
            const Floor g = Ground(ctx, obj, ankle, 0.6f, 0.9f);
            if (!g.ok) continue;
            want[l] = std::clamp(g.y - mid.y, -kMostDown, kMostUp);
            normal[l] = g.n;
        }
    for (int l = 0; l < 2; l++) {
        ft.delta[l] += (want[l] - ft.delta[l]) * kEase;
        ft.n[l] = Unit(ft.n[l] + (normal[l] - ft.n[l]) * kEase);
    }
    ft.drop += (std::min({0.0f, want[0], want[1]}) - ft.drop) * kEase;
    if (std::abs(ft.delta[0]) + std::abs(ft.delta[1]) + std::abs(ft.drop) < 1e-3f) return;

    // the pelvis, and every bone with it, lowered for the lower foot
    for (uint32_t i = 0; i < kBones; i++) now[i].t.y += ft.drop;
    bool moved[kBones] = {};
    for (int l = 0; l < 2; l++) {
        const uint32_t a = kLegs[l][0], b = kLegs[l][1], c = kLegs[l][2], d = kLegs[l][3];
        const V A = now[a].t, B = now[b].t, C = now[c].t;
        const V T = C + V{0, ft.delta[l] - ft.drop, 0};  // the ankle where the ground wants it
        const float l1 = Len(B - A), l2 = Len(C - B);
        if (l1 < 1e-4f || l2 < 1e-4f) continue;
        V to = T - A;
        float dist = std::clamp(Len(to), std::abs(l1 - l2) + 1e-3f, (l1 + l2) * 0.999f);
        V dir = Unit(to);
        // the knee bends where it bent: the old knee off the hip-to-ankle line
        V bend = Unit((B - A) - dir * Dot(B - A, dir));
        if (Len(bend) < 0.5f) bend = Unit(Cross(dir, V{0, 0, 1}));
        float cosa = std::clamp((l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist), -1.0f, 1.0f);
        const V B2 = A + dir * (l1 * cosa) + bend * (l1 * std::sqrt(1 - cosa * cosa));
        const V C2 = A + dir * dist;
        // the thigh turned about the hip, the shin then about the new knee
        const M hip = TurnAbout(Unit(B - A), Unit(B2 - A), A);
        const M thigh = Then(now[a], hip);
        const V shin = Unit(Rows(C - B, hip.r));  // the shin as the thigh's turn carried it
        const M knee = Then(Then(now[b], hip), TurnAbout(shin, Unit(C2 - B2), B2));
        const M carried = Then(Then(now[c], hip), TurnAbout(shin, Unit(C2 - B2), B2));  // the ankle the two turns carry
        g_fit = std::max(g_fit, Len(knee.t - B2) + Len(carried.t - C2));
        // the foot keeps its own turn, tilted a little with the slope, at the new ankle
        M foot = now[c];
        foot.t = C2;
        V up{0, 1, 0};
        V slope = Unit(up + (ft.n[l] - up) * kTilt);
        foot = Then(foot, TurnAbout(up, slope, C2));
        M ankle_inv;
        if (!Inverse(now[c], ankle_inv)) continue;
        const M toe = Then(Then(now[d], ankle_inv), foot);  // the toe stays where it was on the foot
        now[a] = thigh, now[b] = knee, now[c] = foot, now[d] = toe;
        moved[a] = moved[b] = moved[c] = moved[d] = true;
    }
    // anything else hanging from a moved leg bone follows it
    for (uint32_t i = 0; i < kBones; i++) {
        uint32_t p = kernel::G<uint8_t>(parents + i) & 0x7Fu;
        if (moved[i] || p >= kBones || !moved[p]) continue;
        M inv;
        if (!Inverse(old[p], inv)) continue;
        now[i] = Then(Then(old[i], inv), now[p]);
        moved[i] = true;
    }
    for (uint32_t i = 0; i < kBones; i++)
        if (Finite(now[i].t)) Store(world + i * kStride, now[i]);
    if (trace) {  // the most bent character since the last line
        static uint32_t next = 0;
        static float most = -1;
        static char line[256];
        float bent = std::max(std::abs(ft.delta[0]), std::abs(ft.delta[1]));
        if (bent > most) {
            most = bent;
            std::snprintf(line, sizeof(line), "%08X: place %.2f %.2f %.2f, ground %.2f, ankles %.2f %.2f, feet %+.3f "
                          "%+.3f, pelvis %+.3f, slopes %.2f %.2f",
                          obj, place.x, place.y, place.z, mid.y, old[51].t.y, old[55].t.y, ft.delta[0], ft.delta[1],
                          ft.drop, ft.n[0].y, ft.n[1].y);
        }
        if (g_frame >= next) {
            next = g_frame + 1500;
            LOGR("nightfire: foot IK, the most bent of %zu characters: %s; largest miss %.5f", g_feet.size(), line, g_fit);
            most = -1, g_fit = 0;
        }
    }
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000DD290);
X86_FUNC(sub_000DD290) {  // a character's pose: its bones' world matrices, then (a char) its skin matrices
    const uint32_t obj = R32(ctx.esp + 4), skel = R32(ctx.esp + 8);
    uint32_t parents = 0;
    if ((!nf::foot_ik && !nf::RagdollsOn()) || g_inner || !Plausible(obj) || !Plausible(skel) ||
        (kernel::G<uint32_t>(skel + kFlags) & 4) || !Human(skel, parents)) {
        __imp__sub_000DD290(ctx, base);
        return;
    }
    // the world pose made (no skin), the legs bent in it (or a dead enemy's body laid as it falls), then the original
    // makes the skin from it
    g_inner = true;
    kernel::CallGuest(ctx, 0xDD290, {obj, skel, 0});
    g_inner = false;
    g_frame++;
    if (kernel::G<uint32_t>(skel + kFlags) & 4) {
        if (!nf::Ragdoll(ctx, obj, skel, parents) && nf::foot_ik) BendLegs(ctx, obj, skel, parents);
    }
    if ((g_frame & 1023) == 0)
        for (auto it = g_feet.begin(); it != g_feet.end();) it = g_frame - it->second.seen > 20000 ? g_feet.erase(it) : std::next(it);
    __imp__sub_000DD290(ctx, base);
}
