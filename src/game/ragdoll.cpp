// Ragdolls (nf::ragdolls, off by default): an enemy that dies goes limp and falls with its weight, the push of the shot
// that killed it and the ground and walls it meets, in place of the game's death animations. Only the bodies' look:
// the game's own death (its object, its fall and its removal) goes on underneath, and nothing is ever written to its
// state, so it is off in network play (where every copy must draw the same).
//
// From the pose solve's hook (src/game/foot_ik.cpp, 0xDD290): a human (the 73-bone skeleton, see bones.h) whose actor's
// health ([obj+0xBC]+0x90) is gone, not a player's. At its first dead frame its bones' world matrices are kept and
// 17 of its joints become points (the pelvis, chest and head, the shoulders, elbows and wrists, the hips, knees,
// ankles and toes), moving on as they moved the frame before (the living pose is kept a frame for that) plus the
// shot's push. Each game frame ([0x1F65BC]) the points fall (Verlet), hold their distances (the torso and head as one
// piece, the limbs' bones, knees and elbows not folding flat, knees not bending backward) and meet the level through
// the game's ray (0x2C4E0: the way each point moved, then the ground under it), with friction. Every bone's matrix
// is then its kept one moved as its joint moved: the torso turned with the frame of its points, each limb bone aimed
// from its joint at the next, the rest (hands, fingers, face, coat flaps) carried with their parents. A body that
// lies still for a moment (or after 8 s) sleeps: its last pose drawn as it is, no more rays.
// NF_RAGDOLL=0/1 overrides the setting; NF_RAGDOLL_TRACE=1 logs each body's start and sleep.
#include <bit>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "common/nightfire.h"
#include "core/log.h"
#include "core/netplay.h"
#include "game/bones.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
using namespace bones;

constexpr uint32_t kFrameCounter = 0x1F65BC, kPlayers = 0x1F6654;
// the points: the bones whose joints move freely
constexpr uint32_t kPt[] = {0, 3, 5, 19, 20, 21, 31, 32, 33, 49, 50, 51, 52, 53, 54, 55, 56};
constexpr int kPts = sizeof kPt / sizeof kPt[0];
enum : int { P_PELVIS, P_CHEST, P_HEAD, P_SHL, P_ELL, P_HANDL, P_SHR, P_ELR, P_HANDR,
             P_HIPL, P_KNEEL, P_ANKLEL, P_TOEL, P_HIPR, P_KNEER, P_ANKLER, P_TOER };
constexpr float kRadius[kPts] = {0.12f, 0.12f, 0.1f, 0.07f, 0.05f, 0.05f, 0.07f, 0.05f, 0.05f,
                                 0.08f, 0.06f, 0.05f, 0.04f, 0.08f, 0.06f, 0.05f, 0.04f};
constexpr float kGravity = 11.0f;     // world units (about metres) a second squared; a little more than Earth's reads well
constexpr float kDamping = 0.995f;    // the share of a frame's motion kept into the next
constexpr float kFriction = 0.55f;    // the share of the sliding kept by a point on the ground
constexpr float kStick = 0.004f;     // a frame's sliding under which a point on the ground stays put
constexpr int kIterations = 10;
constexpr float kKick = 3.5f;         // the killing shot's push, a second's travel at the point it hit
constexpr int kSleepFrames = 24, kMaxFrames = 480;

int Index(uint32_t bone) {
    for (int i = 0; i < kPts; i++)
        if (kPt[i] == bone) return i;
    return -1;
}

// the distances kept: a, b, and whether it is only a least distance (a knee or elbow not folding flat)
struct Link {
    int a, b;
    float least = 0;  // > 0: the share of the limb's full length it keeps at least
};
const std::vector<Link>& Links() {
    static const std::vector<Link> links = [] {
        std::vector<Link> l;
        const int torso[] = {P_PELVIS, P_CHEST, P_SHL, P_SHR, P_HIPL, P_HIPR};
        for (int i = 0; i < 6; i++)
            for (int j = i + 1; j < 6; j++) l.push_back({torso[i], torso[j]});
        for (int t : {P_CHEST, P_SHL, P_SHR, P_PELVIS}) l.push_back({P_HEAD, t});
        for (auto [a, b] : {std::pair{P_SHL, P_ELL}, {P_ELL, P_HANDL}, {P_SHR, P_ELR}, {P_ELR, P_HANDR},
                            {P_HIPL, P_KNEEL}, {P_KNEEL, P_ANKLEL}, {P_ANKLEL, P_TOEL}, {P_KNEEL, P_TOEL},
                            {P_HIPR, P_KNEER}, {P_KNEER, P_ANKLER}, {P_ANKLER, P_TOER}, {P_KNEER, P_TOER}})
            l.push_back({a, b});
        l.push_back({P_SHL, P_HANDL, 0.35f}), l.push_back({P_SHR, P_HANDR, 0.35f});
        l.push_back({P_HIPL, P_ANKLEL, 0.5f}), l.push_back({P_HIPR, P_ANKLER, 0.5f});
        // the limbs kept off the far side of the body a little: a hand or foot not through the other hip or shoulder
        l.push_back({P_HANDL, P_HIPR, 0.25f}), l.push_back({P_HANDR, P_HIPL, 0.25f});
        l.push_back({P_KNEEL, P_KNEER, 0.12f}), l.push_back({P_ANKLEL, P_ANKLER, 0.12f});
        return l;
    }();
    return links;
}
// the bone each moving bone aims at (its next joint), -1 none
int AimOf(uint32_t bone) {
    switch (bone) {
        case 19: return P_ELL;
        case 20: return P_HANDL;
        case 31: return P_ELR;
        case 32: return P_HANDR;
        case 49: return P_KNEEL;
        case 50: return P_ANKLEL;
        case 51: return P_TOEL;
        case 53: return P_KNEER;
        case 54: return P_ANKLER;
        case 55: return P_TOER;
        default: return -1;
    }
}

// the torso's frame from its points: across (the body's +x, the shoulders' and hips' left to right), up, forward
void Frame(const V p[kPts], V e[3]) {
    V up = Unit(p[P_CHEST] - p[P_PELVIS]);
    V side = (p[P_SHL] - p[P_SHR]) + (p[P_HIPR] - p[P_HIPL]);
    side = Unit(side - up * Dot(side, up));
    e[0] = side, e[1] = up, e[2] = Cross(side, up);
}
float Axis(V v, int j) { return j == 0 ? v.x : j == 1 ? v.y : v.z; }
// the turn taking frame e to frame f (rows: v' = v * R)
M Turn(const V e[3], const V f[3]) {
    M m;
    for (int j = 0; j < 3; j++) m.r[j] = f[0] * Axis(e[0], j) + f[1] * Axis(e[1], j) + f[2] * Axis(e[2], j);
    return m;
}

struct Body {
    uint32_t inst = 0, frame = 0, seen = 0;
    M kept[kBones], inv[kBones], out[kBones];
    V start[kPts], p[kPts], last[kPts];
    float rest[64];
    V side0[3];
    bool on[kPts];  // a point whose bone this model leaves unsolved (far off, at the origin) is left out
    V anchor[2];  // the pelvis and head half a second ago
    int still = 0, age = 0;
    bool asleep = false;
};
struct Living {
    uint32_t inst = 0, frame = 0;
    V p[kPts];
};
struct Shot {
    uint32_t frame = 0;
    V at, dir;
};
std::unordered_map<uint32_t, Body> g_bodies;
std::unordered_map<uint32_t, Living> g_living;
std::unordered_map<uint32_t, Shot> g_shots;

bool Trace() {
    static const bool on = [] {
        const char* e = std::getenv("NF_RAGDOLL_TRACE");
        return e && *e == '1';
    }();
    return on;
}

bool Player(uint32_t obj) {
    for (uint32_t p = 0; p < 4; p++)
        if (R32(kPlayers + 4 * p) == obj) return true;
    return false;
}

float Dt() {  // the time since the last step of any body: the frames' own pace, kept sensible
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last = Clock::now();
    static uint32_t frame = 0;
    static float dt = 1 / 60.0f;
    const uint32_t now_frame = R32(kFrameCounter);
    if (now_frame != frame) {
        const Clock::time_point now = Clock::now();
        dt = std::clamp(std::chrono::duration<float>(now - last).count(), 1 / 144.0f, 1 / 30.0f);
        last = now, frame = now_frame;
    }
    return dt;
}

void Start(Body& b, uint32_t obj, uint32_t world, uint32_t inst, uint32_t frame) {
    b = Body{};
    b.inst = inst, b.frame = frame;
    for (uint32_t i = 0; i < kBones; i++) {
        b.kept[i] = b.out[i] = Load(world + i * kStride);
        if (!Inverse(b.kept[i], b.inv[i])) b.inv[i] = M{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, b.kept[i].t * -1};
    }
    for (int i = 0; i < kPts; i++) {
        b.start[i] = b.p[i] = b.last[i] = b.kept[kPt[i]].t;
        b.on[i] = Finite(b.p[i]) && Len(b.p[i] - b.kept[0].t) < 2.5f;
        if (!b.on[i]) b.start[i] = b.p[i] = b.last[i] = b.kept[0].t;
    }
    for (int i : {P_PELVIS, P_CHEST, P_SHL, P_SHR, P_HIPL, P_HIPR})
        if (!b.on[i]) {  // no torso to lie with: left as the game has it
            b.asleep = true;
            if (Trace()) LOGR("ragdoll: %08X has no torso point %d: left to the game", obj, i);
            return;
        }
    const auto& links = Links();
    for (size_t i = 0; i < links.size() && i < 64; i++) {
        const Link& l = links[i];
        if (l.least > 0 && (l.a == P_SHL || l.a == P_SHR || l.a == P_HIPL || l.a == P_HIPR) &&
            (l.b == P_HANDL || l.b == P_HANDR || l.b == P_ANKLEL || l.b == P_ANKLER)) {
            // a limb's least reach: a share of its two bones' full length
            const int mid = l.b == P_HANDL ? P_ELL : l.b == P_HANDR ? P_ELR : l.b == P_ANKLEL ? P_KNEEL : P_KNEER;
            b.rest[i] = l.least * (Len(b.p[mid] - b.p[l.a]) + Len(b.p[l.b] - b.p[mid]));
        } else if (l.least > 0) {
            b.rest[i] = l.least;  // a plain least distance, world units
        } else {
            b.rest[i] = Len(b.p[l.b] - b.p[l.a]);
        }
    }
    Frame(b.p, b.side0);
    // moving on as it moved: the living pose a frame ago
    const float dt = Dt();
    if (auto it = g_living.find(obj); it != g_living.end() && it->second.inst == inst && frame - it->second.frame <= 2)
        for (int i = 0; i < kPts; i++) {
            V v = b.p[i] - it->second.p[i];
            const float most = 6.0f * dt;  // a teleport or a cut is no motion
            if (Len(v) > most) v = Unit(v) * most;
            b.last[i] = b.p[i] - v;
        }
    // the killing shot's push, most at the point it hit
    if (auto it = g_shots.find(obj); it != g_shots.end() && frame - it->second.frame <= 10) {
        const Shot& s = it->second;
        for (int i = 0; i < kPts; i++) {
            const float d = Len(b.p[i] - s.at);
            const float share = 0.25f + 0.75f * std::exp(-d * d / 0.08f);
            b.last[i] = b.last[i] - s.dir * (kKick * share * dt);
        }
        g_shots.erase(it);
    }
    if (Trace())
        for (int i = 0; i < kPts; i++)
            if (!b.on[i]) LOGR("ragdoll: %08X: bone %u is not solved in this model, left out", obj, kPt[i]);
    if (Trace())
        LOGR("ragdoll: %08X starts at %.2f %.2f %.2f (%zu bodies)", obj, b.p[P_PELVIS].x, b.p[P_PELVIS].y,
             b.p[P_PELVIS].z, g_bodies.size());
}

// a point's contact this frame: the plane it must stay on the near side of (its radius off)
struct Contact {
    bool on = false;
    V at, n;
};

// the level met this frame, through the game's ray: the way each point moved (a wall or floor crossed), else the ground
// under it (from above: one that sank in comes back up)
void Contacts(X86Context& ctx, uint32_t obj, const Body& b, Contact c[kPts]) {
    for (int i = 0; i < kPts; i++) {
        c[i] = Contact{};
        if (!b.on[i]) continue;
        const float r = kRadius[i];
        const V p = b.p[i], moved = p - b.last[i];
        if (Len(moved) > 0.005f) {
            const Hit h = Ray(ctx, obj, b.last[i], p + Unit(moved) * r);
            if (h.ok) c[i] = {true, h.at, Dot(h.n, moved) > 0 ? h.n * -1 : h.n};
        }
        if (c[i].on) continue;
        const Hit g = Ray(ctx, obj, p + V{0, 0.35f, 0}, p - V{0, r + 0.05f, 0});
        if (g.ok && g.n.y > 0.3f) c[i] = {true, g.at, g.n};
    }
}

void Planes(Body& b, const Contact c[kPts]) {
    for (int i = 0; i < kPts; i++)
        if (c[i].on)
            if (const float d = Dot(b.p[i] - c[i].at, c[i].n) - kRadius[i]; d < 0) b.p[i] = b.p[i] - c[i].n * d;
}

// the distances kept and the contacts held, together (each pass both: neither undoes the other into motion)
void Solve(Body& b, const Contact c[kPts]) {
    const auto& links = Links();
    for (int it = 0; it < kIterations; it++) {
        for (size_t i = 0; i < links.size() && i < 64; i++) {
            const Link& l = links[i];
            if (!b.on[l.a] || !b.on[l.b]) continue;
            V d = b.p[l.b] - b.p[l.a];
            const float len = Len(d);
            if (len < 1e-5f) continue;
            if (l.least > 0 && len >= b.rest[i]) continue;
            const V fix = d * ((len - b.rest[i]) / len * 0.5f);
            b.p[l.a] = b.p[l.a] + fix, b.p[l.b] = b.p[l.b] - fix;
        }
        Planes(b, c);
    }
    // the knees bend forward only: one bent backward of the hip-to-ankle line is straightened
    V e[3];
    Frame(b.p, e);
    for (auto [h, k, a] : {std::tuple{P_HIPL, P_KNEEL, P_ANKLEL}, {P_HIPR, P_KNEER, P_ANKLER}}) {
        if (!b.on[k] || !b.on[a]) continue;
        const V dir = Unit(b.p[a] - b.p[h]);
        const V off = (b.p[k] - b.p[h]) - dir * Dot(b.p[k] - b.p[h], dir);
        const V fwd = Unit(e[2] - dir * Dot(e[2], dir));
        if (const float back = Dot(off, fwd); back < 0) b.p[k] = b.p[k] - fwd * back;
    }
    Planes(b, c);
}

// a point touching a surface: no bounce off it, and its sliding slowed, or stopped when slow (a body at rest stays)
void Friction(Body& b, const Contact c[kPts]) {
    for (int i = 0; i < kPts; i++) {
        if (!c[i].on || Dot(b.p[i] - c[i].at, c[i].n) - kRadius[i] > 0.01f) continue;
        const V n = c[i].n, v = b.p[i] - b.last[i];
        const V along = v - n * Dot(v, n);
        b.p[i] = b.p[i] - (Len(along) < kStick ? along : along * (1 - kFriction));
        if (const float out = Dot(b.p[i] - b.last[i], n); out > 0) b.last[i] = b.last[i] + n * out;
    }
}

void Step(X86Context& ctx, uint32_t obj, Body& b) {
    const float dt = Dt();
    float most = 0;
    int which = -1;
    for (int i = 0; i < kPts; i++) {
        if (!b.on[i]) continue;
        const V now = b.p[i];
        b.p[i] = now + (now - b.last[i]) * kDamping + V{0, -kGravity * dt * dt, 0};
        b.last[i] = now;
    }
    Contact c[kPts];
    Contacts(ctx, obj, b, c);
    Solve(b, c);
    Friction(b, c);
    for (int i = 0; i < kPts; i++) {  // no point faster than a fall from a few metres: no constraint fight runs away
        const V v = b.p[i] - b.last[i];
        if (const float most_v = 9.0f * dt; Len(v) > most_v) b.last[i] = b.p[i] - Unit(v) * most_v;
    }
    for (int i = 0; i < kPts; i++) {
        if (!b.on[i]) continue;
        if (!Finite(b.p[i])) {  // never a broken body: back to where it was
            for (int j = 0; j < kPts; j++) b.p[j] = b.last[j];
            break;
        }
        if (Len(b.p[i] - b.last[i]) > most) most = Len(b.p[i] - b.last[i]), which = i;
    }
    // still: every point a while, or the torso gone nowhere in half a second (a limb on an edge, or a body wedged
    // against a wall, can tremble in place without end)
    b.still = most < 0.0015f ? b.still + 1 : 0;
    bool settled = false;
    if (b.age % 30 == 0) {
        settled = b.age > 0 && Len(b.p[P_PELVIS] - b.anchor[0]) < 0.02f && Len(b.p[P_HEAD] - b.anchor[1]) < 0.03f;
        b.anchor[0] = b.p[P_PELVIS], b.anchor[1] = b.p[P_HEAD];
    }
    if (Trace() && b.age % 20 == 0)
        LOGR("ragdoll: %08X frame %d: pelvis %.2f %.2f %.2f, head %.2f %.2f %.2f, left ankle y %.2f, moved %.4f (point %d)", obj, b.age,
             b.p[P_PELVIS].x, b.p[P_PELVIS].y, b.p[P_PELVIS].z, b.p[P_HEAD].x, b.p[P_HEAD].y, b.p[P_HEAD].z,
             b.p[P_ANKLEL].y, most, which);
    if (++b.age > kMaxFrames || b.still > kSleepFrames || settled) {
        b.asleep = true;
        if (Trace()) LOGR("ragdoll: %08X lies still after %d frames at %.2f %.2f %.2f", obj, b.age, b.p[P_PELVIS].x,
                          b.p[P_PELVIS].y, b.p[P_PELVIS].z);
    }
}

// every bone's matrix from the points: the kept one moved as its joint moved
void Pose(Body& b, uint32_t parents) {
    M delta[kBones];
    V e[3];
    Frame(b.p, e);
    const M turn = Turn(b.side0, e);
    for (uint32_t i = 0; i < kBones; i++) {
        const uint32_t parent = kernel::G<uint8_t>(parents + i) & 0x7Fu;
        M now;
        if (i == 0 || parent >= i) {  // the pelvis (and any root): the torso's turn about its point
            M d = turn;
            d.t = b.p[P_PELVIS] - Rows(b.start[P_PELVIS], turn.r);
            now = Then(b.kept[i], d);
        } else {
            now = Then(b.kept[i], delta[parent]);
            if (const int aim = AimOf(i); aim >= 0 && b.on[aim] && b.on[Index(i)]) {
                const int self = Index(i);
                now.t = b.p[self];
                const V was = Unit(Rows(b.start[aim] - b.start[self], delta[parent].r));
                now = Then(now, TurnAbout(was, Unit(b.p[aim] - b.p[self]), now.t));
            }
        }
        b.out[i] = now;
        delta[i] = Then(b.inv[i], now);
    }
}
}  // namespace

namespace nf {
bool RagdollsOn() { return ragdolls && !netplay::Active(); }

// a hit on a character (src/game/hit_feedback.cpp): where, and which way the round went (from its shooter)
void RagdollHit(uint32_t obj, uint32_t hit) {
    if (!RagdollsOn() || !Plausible(obj) || !Plausible(hit)) return;
    Shot s;
    s.frame = R32(kFrameCounter);
    s.at = {F(hit + 0x38), F(hit + 0x3C), F(hit + 0x40)};
    const uint32_t proj = R32(hit + 0x4C), data = Plausible(proj) ? R32(proj + 0xBC) : 0;
    const uint32_t shooter = Plausible(data) ? R32(data + 0x24) : 0;
    V dir;
    if (Plausible(shooter)) dir = Unit(s.at - V{F(shooter + kPos), F(shooter + kPos + 4), F(shooter + kPos + 8)});
    if (Len(dir) < 0.5f) dir = Unit(V{F(hit + 0x10), F(hit + 0x14), F(hit + 0x18)}) * -1;
    if (!Finite(s.at) || !Finite(dir) || Len(dir) < 0.5f) return;
    s.dir = Unit(dir + V{0, 0.15f, 0});  // a little up: a body thrown, not pressed into the floor
    g_shots[obj] = s;
}

// a human's world pose made this frame (the hook at 0xDD290): true when it is a dead enemy's body, laid here
bool Ragdoll(X86Context& ctx, uint32_t obj, uint32_t skel, uint32_t parents) {
    if (!RagdollsOn()) {
        if (!g_bodies.empty()) g_bodies.clear(), g_living.clear(), g_shots.clear();
        return false;
    }
    const uint32_t world = R32(skel + kWorld), inst = R32(obj + kInst), actor = R32(obj + kActor);
    if (!Plausible(world) || !Plausible(actor) || Player(obj)) return false;
    const uint32_t frame = R32(kFrameCounter);
    const bool dead = !(F(actor + 0x90) > 0);
    if (!dead) {
        g_bodies.erase(obj);
        Living& l = g_living[obj];
        l.inst = inst, l.frame = frame;
        for (int i = 0; i < kPts; i++) l.p[i] = {F(world + kPt[i] * kStride + 0x30), F(world + kPt[i] * kStride + 0x34),
                                                 F(world + kPt[i] * kStride + 0x38)};
        // forgotten when long unseen (a level's objects gone)
        if ((frame & 255) == 0)
            for (auto it = g_living.begin(); it != g_living.end();) it = frame - it->second.frame > 600 ? g_living.erase(it) : std::next(it);
        return false;
    }
    auto it = g_bodies.find(obj);
    if (it == g_bodies.end() || it->second.inst != inst || frame - it->second.seen > 36000) {
        Body& b = g_bodies[obj];
        Start(b, obj, world, inst, frame);
        g_living.erase(obj);
        it = g_bodies.find(obj);
    }
    Body& b = it->second;
    b.seen = frame;
    if (!b.asleep && frame != b.frame) {
        b.frame = frame;
        Step(ctx, obj, b);
        Pose(b, parents);
    }
    for (uint32_t i = 0; i < kBones; i++) Store(world + i * kStride, b.out[i]);
    return true;
}
}  // namespace nf

// NF_RAGDOLL_TEST=1 (diagnostic): every 4 s the nearest living enemy in sight of player 1 killed by a made-up shot through
// the game's own damage (0x3D900: the pair {object, data}, the hit, a scale), from the 2D pass (0xDAC20)
namespace nf {
void RagdollTest(X86Context& ctx) {
    static const bool on = [] {
        const char* e = std::getenv("NF_RAGDOLL_TEST");
        return e && *e == '1';
    }();
    static uint32_t frames = 0;
    if (!on || !RagdollsOn() || MenuUp() || ++frames < 1200 || frames % 240) return;
    const uint32_t me = R32(kPlayers), frame = R32(kFrameCounter);
    if (!Plausible(me)) return;
    const V at{F(me + kPos), F(me + kPos + 4), F(me + kPos + 8)};
    uint32_t best = 0;
    float near_d = 60;
    for (const auto& [obj, l] : g_living)
        if (frame - l.frame <= 2 && Len(l.p[P_CHEST] - at) < near_d) near_d = Len(l.p[P_CHEST] - at), best = obj;
    if (!best) {
        LOGR("ragdoll test: no living enemy in sight within 60 of %.1f %.1f %.1f", at.x, at.y, at.z);
        for (const auto& [obj, l] : g_living)
            LOGR("ragdoll test:   %08X %u frames ago at %.1f m", obj, frame - l.frame, Len(l.p[P_CHEST] - at));
        return;
    }
    // brought in front of player 1 first (NF_RAGDOLL_TEST_NEAR=0: where it is), killed a second later
    static uint32_t moved = 0, moved_at = 0;
    static const bool bring = [] {
        const char* e = std::getenv("NF_RAGDOLL_TEST_NEAR");
        return !e || *e != '0';
    }();
    if (bring && moved != best) {
        const uint32_t cam = R32(0x1F661C);
        V fwd = cam ? V{F(cam + 0x128), 0, F(cam + 0x130)} : V{0, 0, 1};
        fwd = Unit(fwd);
        const V to = at + fwd * 4.0f;
        SetF(best + kPos, to.x), SetF(best + kPos + 4, at.y), SetF(best + kPos + 8, to.z);
        moved = best, moved_at = frames;
        frames -= 180;  // back in a second
        LOGR("ragdoll test: %08X brought to %.1f %.1f %.1f", best, to.x, at.y, to.z);
        return;
    }
    (void)moved_at;
    static const uint32_t pair = mem::AllocHostContiguous(16, 16), hit = mem::AllocHostContiguous(0x80, 16);
    std::memset(kernel::HostPtr(hit), 0, 0x80);
    const V chest = g_living[best].p[P_CHEST], n = Unit(at - chest);
    SetF(hit + 8, 500.0f);
    kernel::G<uint16_t>(hit + 0x46) = 1;
    SetF(hit + 0x38, chest.x), SetF(hit + 0x3C, chest.y), SetF(hit + 0x40, chest.z);
    SetF(hit + 0x10, n.x), SetF(hit + 0x14, n.y), SetF(hit + 0x18, n.z);
    kernel::G<uint32_t>(pair) = best, kernel::G<uint32_t>(pair + 4) = R32(best + kActor);
    const float before = F(R32(best + kActor) + 0x90);
    RagdollHit(best, hit);  // (the made-up shot has no shooter: pushed away from player 1 by its normal)
    const X86Context saved = ctx;
    kernel::CallGuest(ctx, 0x3D900, {pair, hit, std::bit_cast<uint32_t>(1.0f)});
    ctx = saved;
    if (F(R32(best + kActor) + 0x90) > 0) SetF(R32(best + kActor) + 0x90, 0);  // the made-up shot taken by nothing: health gone itself
    LOGR("ragdoll test: %08X at %.1f m shot, health %.1f -> %.1f", best, near_d, before, F(R32(best + kActor) + 0x90));
}
}  // namespace nf
