// A character's bones (src/game/foot_ik.cpp, src/game/ragdoll.cpp): the world matrices' math, the human skeleton's
// check and the game's ground ray. See foot_ik.cpp for the layout.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace bones {
using kernel::R32;

constexpr uint32_t kInst = 0xB8, kZone = 0x20, kPos = 0x24, kActor = 0xBC;
constexpr uint32_t kSkel = 0x58, kModel = 0x1C, kWorld = 0x40, kFlags = 0x50, kParents = 0x20, kSkelId = 0x2C;
constexpr uint32_t kSkelTable = 0x1D7578;
constexpr uint32_t kBones = 73, kStride = 0x3C;

inline float F(uint32_t va) { return kernel::G<float>(va); }
inline void SetF(uint32_t va, float v) { kernel::G<float>(va) = v; }
inline bool Plausible(uint32_t va) { return va >= 0x10000 && (va & 0x7FFFFFFF) < 0x8000000; }  // 0x8xxxxxxx: the mirror

struct V {
    float x = 0, y = 0, z = 0;
};
inline V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V operator*(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V a) { return std::sqrt(Dot(a, a)); }
inline V Unit(V a) {
    float l = Len(a);
    return l > 1e-9f ? a * (1 / l) : V{};
}
inline bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// a bone's world matrix: rows r (its axes, scaled), place t; a point p goes to p.x r0 + p.y r1 + p.z r2 + t
struct M {
    V r[3];
    V t;
};
inline V Rows(V v, const V r[3]) { return r[0] * v.x + r[1] * v.y + r[2] * v.z; }
inline M Load(uint32_t va) {
    M m;
    for (uint32_t i = 0; i < 3; i++) m.r[i] = {F(va + 0x10 * i), F(va + 0x10 * i + 4), F(va + 0x10 * i + 8)};
    m.t = {F(va + 0x30), F(va + 0x34), F(va + 0x38)};
    return m;
}
inline void Store(uint32_t va, const M& m) {
    for (uint32_t i = 0; i < 3; i++)
        SetF(va + 0x10 * i, m.r[i].x), SetF(va + 0x10 * i + 4, m.r[i].y), SetF(va + 0x10 * i + 8, m.r[i].z);
    SetF(va + 0x30, m.t.x), SetF(va + 0x34, m.t.y), SetF(va + 0x38, m.t.z);
}
inline M Then(const M& a, const M& b) {  // a, then b
    M o;
    for (int i = 0; i < 3; i++) o.r[i] = Rows(a.r[i], b.r);
    o.t = Rows(a.t, b.r) + b.t;
    return o;
}
inline bool Inverse(const M& m, M& o) {
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
inline M TurnAbout(V a, V b, V pivot) {
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

// the game's ray (0x2C4E0) from a to b, past the object: its nearest contact (the point at +0x38, the normal at +0x10)
struct Hit {
    bool ok = false;
    V at;
    V n{0, 1, 0};
};
inline Hit Ray(X86Context& ctx, uint32_t obj, V a, V b) {
    static const uint32_t scratch = mem::AllocHostContiguous(32, 16);
    const uint32_t s = scratch, e = scratch + 12, list = scratch + 24;
    SetF(s, a.x), SetF(s + 4, a.y), SetF(s + 8, a.z);
    SetF(e, b.x), SetF(e + 4, b.y), SetF(e + 8, b.z);
    kernel::G<uint32_t>(list) = 0;
    Hit h;
    uint32_t hit = kernel::CallGuest(ctx, 0x2C4E0, {s, e, R32(obj + kZone), obj, 0, list, 0, 0x0C, 8}) & 0xFF;
    uint32_t c = R32(list);
    if (hit && Plausible(c)) {
        h.at = {F(c + 0x38), F(c + 0x3C), F(c + 0x40)};
        h.n = Unit({F(c + 0x10), F(c + 0x14), F(c + 0x18)});
        h.ok = Finite(h.at) && Finite(h.n) && Len(h.n) > 0.5f;
        if (!h.ok) h.n = {0, 1, 0};
    }
    if (c) kernel::CallGuest(ctx, 0x294D0, {list});
    return h;
}

// the 73-bone human skeleton (its parents at [model+0x20], low 7 bits): the legs from the pelvis 0
inline bool Human(uint32_t skel, uint32_t& parents) {
    uint32_t model = R32(skel + kModel);
    if (!Plausible(model)) return false;
    uint32_t table = R32(kSkelTable + 4 * kernel::G<uint8_t>(model + kSkelId));
    parents = R32(model + kParents);
    if (!Plausible(table) || !Plausible(parents) || kernel::G<uint8_t>(table + 2) != kBones) return false;
    auto parent = [&](uint32_t i) { return kernel::G<uint8_t>(parents + i) & 0x7Fu; };
    return parent(49) == 0 && parent(53) == 0 && parent(50) == 49 && parent(51) == 50 && parent(52) == 51 &&
           parent(54) == 53 && parent(55) == 54 && parent(56) == 55;
}
}  // namespace bones
