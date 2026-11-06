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
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;

constexpr uint32_t kInst = 0xB8, kZone = 0x20, kPos = 0x24, kActor = 0xBC;
constexpr uint32_t kSkel = 0x58, kModel = 0x1C, kWorld = 0x40, kFlags = 0x50, kParents = 0x20, kSkelId = 0x2C;
constexpr uint32_t kSkelTable = 0x1D7578;
constexpr uint32_t kBones = 73, kStride = 0x3C;
constexpr uint32_t kLegs[2][4] = {{49, 50, 51, 52}, {53, 54, 55, 56}};
constexpr float kMostDown = 0.45f, kMostUp = 0.35f;  // how far a foot follows the ground, world units
constexpr float kEase = 0.3f;                        // a frame's share of the way to the new bend
constexpr float kTilt = 0.6f;                        // how much of the slope a foot takes

float F(uint32_t va) { return kernel::G<float>(va); }
void SetF(uint32_t va, float v) { kernel::G<float>(va) = v; }
bool Plausible(uint32_t va) { return va >= 0x10000 && (va & 0x7FFFFFFF) < 0x8000000; }  // 0x8xxxxxxx: the mirror

struct V {
    float x = 0, y = 0, z = 0;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Len(V a) { return std::sqrt(Dot(a, a)); }
V Unit(V a) {
    float l = Len(a);
    return l > 1e-9f ? a * (1 / l) : V{};
}
bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// a bone's world matrix: rows r (its axes, scaled), place t; a point p goes to p.x r0 + p.y r1 + p.z r2 + t
struct M {
    V r[3];
    V t;
};
V Rows(V v, const V r[3]) { return r[0] * v.x + r[1] * v.y + r[2] * v.z; }
M Load(uint32_t va) {
    M m;
    for (uint32_t i = 0; i < 3; i++) m.r[i] = {F(va + 0x10 * i), F(va + 0x10 * i + 4), F(va + 0x10 * i + 8)};
    m.t = {F(va + 0x30), F(va + 0x34), F(va + 0x38)};
    return m;
}
void Store(uint32_t va, const M& m) {
    for (uint32_t i = 0; i < 3; i++)
        SetF(va + 0x10 * i, m.r[i].x), SetF(va + 0x10 * i + 4, m.r[i].y), SetF(va + 0x10 * i + 8, m.r[i].z);
    SetF(va + 0x30, m.t.x), SetF(va + 0x34, m.t.y), SetF(va + 0x38, m.t.z);
}
M Then(const M& a, const M& b) {  // a, then b
    M o;
    for (int i = 0; i < 3; i++) o.r[i] = Rows(a.r[i], b.r);
    o.t = Rows(a.t, b.r) + b.t;
    return o;
}
bool Inverse(const M& m, M& o) {
    const V a = m.r[0], b = m.r[1], c = m.r[2];
    V c0 = Cross(b, c), c1 = Cross(c, a), c2 = Cross(a, b);
    float det = Dot(a, c0);
    if (std::abs(det) < 1e-12f) return false;
    float k = 1 / det;
    // the inverse's rows: the columns of the cofactors over the determinant
    o.r[0] = V{c0.x, c1.x, c2.x} * k, o.r[1] = V{c0.y, c1.y, c2.y} * k, o.r[2] = V{c0.z, c1.z, c2.z} * k;
    o.t = Rows(m.t, o.r) * -1;
    return true;
}
// the rotation taking unit a to unit b, about pivot, as a matrix
M TurnAbout(V a, V b, V pivot) {
    M q;
    q.r[0] = {1, 0, 0}, q.r[1] = {0, 1, 0}, q.r[2] = {0, 0, 1};
    V axis = Cross(a, b);
    float s = Len(axis), c = std::clamp(Dot(a, b), -1.0f, 1.0f);
    if (s > 1e-6f) {
        axis = axis * (1 / s);
        float k = 1 - c, x = axis.x, y = axis.y, z = axis.z;
        // Rodrigues for row vectors (v' = v * R): the column form transposed
        q.r[0] = {c + x * x * k, x * y * k + z * s, x * z * k - y * s};
        q.r[1] = {y * x * k - z * s, c + y * y * k, y * z * k + x * s};
        q.r[2] = {z * x * k + y * s, z * y * k - x * s, c + z * z * k};
    }
    q.t = pivot - Rows(pivot, q.r);
    return q;
}

// the guest's scratch for the ray: start, end, the list pointer
uint32_t g_scratch = 0;
struct Hit {
    bool ok = false;
    float y = 0;
    V n{0, 1, 0};
};
Hit Ground(X86Context& ctx, uint32_t obj, V from, float up, float down) {
    Hit h;
    if (!g_scratch) g_scratch = mem::AllocHostContiguous(32, 16);
    const uint32_t s = g_scratch, e = g_scratch + 12, list = g_scratch + 24;
    SetF(s, from.x), SetF(s + 4, from.y + up), SetF(s + 8, from.z);
    SetF(e, from.x), SetF(e + 4, from.y - down), SetF(e + 8, from.z);
    kernel::G<uint32_t>(list) = 0;
    uint32_t hit = kernel::CallGuest(ctx, 0x2C4E0, {s, e, R32(obj + kZone), obj, 0, list, 0, 0x0C, 8}) & 0xFF;
    uint32_t c = R32(list);
    if (hit && Plausible(c)) {
        h.y = F(c + 0x3C);
        h.n = Unit({F(c + 0x10), F(c + 0x14), F(c + 0x18)});
        h.ok = std::isfinite(h.y) && Finite(h.n) && h.n.y > 0.5f;
        if (!h.ok) h.n = {0, 1, 0};
    }
    if (c) kernel::CallGuest(ctx, 0x294D0, {list});
    return h;
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

bool Human(uint32_t skel, uint32_t& parents) {
    uint32_t model = R32(skel + kModel);
    if (!Plausible(model)) return false;
    uint32_t table = R32(kSkelTable + 4 * kernel::G<uint8_t>(model + kSkelId));
    parents = R32(model + kParents);
    if (!Plausible(table) || !Plausible(parents) || kernel::G<uint8_t>(table + 2) != kBones) return false;
    auto parent = [&](uint32_t i) { return kernel::G<uint8_t>(parents + i) & 0x7Fu; };
    return parent(49) == 0 && parent(53) == 0 && parent(50) == 49 && parent(51) == 50 && parent(52) == 51 &&
           parent(54) == 53 && parent(55) == 54 && parent(56) == 55;
}

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
    const Hit mid = Ground(ctx, obj, place, 0.3f, 3.0f);
    float want[2] = {0, 0};
    V normal[2] = {{0, 1, 0}, {0, 1, 0}};
    const float lowest = std::min(old[51].t.y, old[55].t.y);
    const bool standing = mid.ok && lowest - mid.y < 0.35f;  // in the air: the feet well above the ground
    if (standing)
        for (int l = 0; l < 2; l++) {
            const V ankle = old[kLegs[l][2]].t;
            const Hit g = Ground(ctx, obj, ankle, 0.6f, 0.9f);
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
    if (!nf::foot_ik || g_inner || !Plausible(obj) || !Plausible(skel) || (kernel::G<uint32_t>(skel + kFlags) & 4) ||
        !Human(skel, parents)) {
        __imp__sub_000DD290(ctx, base);
        return;
    }
    // the world pose made (no skin), the legs bent in it, then the original makes the skin from it
    g_inner = true;
    kernel::CallGuest(ctx, 0xDD290, {obj, skel, 0});
    g_inner = false;
    g_frame++;
    if (kernel::G<uint32_t>(skel + kFlags) & 4) BendLegs(ctx, obj, skel, parents);
    if ((g_frame & 1023) == 0)
        for (auto it = g_feet.begin(); it != g_feet.end();) it = g_frame - it->second.seen > 20000 ? g_feet.erase(it) : std::next(it);
    __imp__sub_000DD290(ctx, base);
}
