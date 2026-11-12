// Pickup physics (nf::pickup_physics, on by default, off in network and LAN play): dropped weapons, ammo and other
// pickups (object class 0x2F) fly, bounce, slide, tumble and come to rest instead of dropping straight down.
//
// The game's pickups (static, verified in play): made by 0xA7380 (13 args; its +0x18 state 0 falling, 1 resting, 2
// taken and waiting to respawn), dropped by 0xA7F10(matrix*, weapon, ammo, type) from an enemy's death (0x3DE00: a
// grenade on some levels), a player's death in multiplayer (0xAC390: the held weapon and its ammo, at the gun's
// matrix) and 0x67690. The object's place is its matrix's translation (obj+0xA0, the matrix at obj+0x70; the spawner
// sets +0xD6 bits 0x04, the matrix kept as it is, and 0x40, +0xA0 copied to +0x24 after the thinks); +0x60 is its
// bounds' centre (+0x54 through the matrix), +0x6C their radius. Its data ([obj+0xBC]): +0x00 a velocity (world units a
// frame), +0x0C the place less the centre at the drop, +0x18 the state, +0x1A type, +0x1C item, +0x1E ammo, +0x24 and
// +0x26 the respawn time and a countdown to removal, +0x30 a spin while resting (multiplayer). Its think 0xA84D0
// (class table 0x163B98): in state 0 the velocity takes gravity (0x1F6648 by (1/60)^2), the place its velocity, a ray
// (0x2C4E0, mask 0x50D) from the last place (+0x30) to the new one lowered by the model's +0x20 lands it (state 1,
// 0xA1310: its multiplayer slot); then in every state but 2: the spin, the pickup test (a player within +0x6C + 1.1
// of +0x60 in sight: 0xA7710 takes it, state 2 or removed) and the countdown. Setting +0xD6 bit 0x20 has the object
// loop (0x2DD00) rebuild +0x60 (0x2DB20) and put it in its room (0x2D3A0, 0x21110: the safe move) after the thinks.
//
// Here (the think's hook): a falling pickup, or one we set moving, is ours: its centre a sphere of radius about 0.12
// m, its velocity taking gravity, a ray along each frame's move (a wall, floor or step crossed: bounce
// v -= (1 + e)(v.n)n, friction along the surface, a spin from the scrape) and a ray down to the ground under it
// (resting on it, sliding down a slope). It tumbles (a turn of its matrix about its centre) while it flies and lies
// down flat on the ground's normal (its model's up, the matrix's second row, along the normal) as it slows; slow on
// ground flatter than 45 degrees for a moment (or after 10 s) it is the game's again: state 1 (and 0xA1310, as the
// game's own landing). While it is ours the game's think still runs for it as a resting pickup (its spin off): the
// pickup test and the countdown go on.
// Drops get a toss (0xA7F10's hook): up and out with a spin, random but the same for the same object and frame.
// A resting pickup a player walks into (one it does not take: full ammo) is pushed along by the player's motion; an
// explosion (0x690A0, src/game/fx.cpp) throws the pickups within its radius away from it. The level's own pickups
// (made resting by the placement 0xA4BC0) too: each one's matrix when first seen resting is kept, and one taken goes
// back there while hidden (state 2; the game's respawn, 0xA84D0's state 2, only shows it again), one lost (ending
// off the ground or 150 m away) at once.
// NF_PICKUP_PHYSICS=0/1 overrides the setting; NF_PICKUP_TRACE=1 logs each pickup taken, its bounces and its rest;
// NF_PICKUP_TEST=<weapon> (diagnostic) drops that weapon (0xA7F10, 30 rounds) in front of player 1 every 4 s
// (NF_PICKUP_TEST_AT=ahead,up metres; NF_PICKUP_TOSS=k scales the toss sideways; NF_PICKUP_BLAST=1 an explosion
// beside each 2 s later; the first 8 s into a level); NF_PICKUP_LEVEL_TEST=1 pushes the nearest level pickup,
// takes it and follows its respawn.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "common/nightfire.h"
#include "core/log.h"
#include "core/netplay.h"
#include "game/bones.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
using namespace bones;

constexpr uint32_t kClass = 0x2F, kFrameCounter = 0x1F65BC, kPlayers = 0x1F6654;
constexpr float kDt = 1 / 60.0f;            // the game's think steps a frame of 1/60 s (its pickups' own fall too)
constexpr float kGravity = 9.8f;            // world units (metres) a second squared, should the game's (0x1F664C) be odd
constexpr float kBounce = 0.3f;             // the share of the speed into a surface kept out of it
constexpr float kScrape = 0.25f;            // the share of the speed along a surface lost at a bounce
constexpr float kRoll = 5.0f;               // the speed along the ground lost a second while sliding (m/s a second)
constexpr float kRestSpeed = 0.35f;         // m/s: slower than this on flat ground it may rest
constexpr int kRestFrames = 8, kMaxFrames = 600;

struct Item {
    uint32_t data = 0, seen = 0;
    V vel, spin;                // m/s; rad/s about the axis
    V from;                     // its centre when taken
    int age = 0, still = 0, bounces = 0;
    bool resting = false;       // the game's state was 1 when we took it (pushed): it stays 1
    bool lost = false;          // came to its end off the ground or far away
};
std::unordered_map<uint32_t, Item> g_items;

// the level's own pickups' places (the matrix each had resting when first seen, before any push): one taken comes
// back there when it respawns (state 2), one lost (off the ground, out of the level) at once. Forgotten with the level
// (its id and its world)
constexpr uint32_t kLevelId = 0x1F658C;
struct Spawn {
    uint32_t data = 0;
    M m;
};
std::unordered_map<uint32_t, Spawn> g_spawns;
uint32_t g_spawn_level = 0;

bool Trace() {
    static const bool on = [] {
        const char* e = std::getenv("NF_PICKUP_TRACE");
        return e && *e == '1';
    }();
    return on;
}

bool On() { return nf::pickup_physics && !netplay::Active() && !nf::NetOn(); }

uint32_t Hash(uint32_t x) {
    x ^= x >> 16, x *= 0x7FEB352Du, x ^= x >> 15, x *= 0x846CA68Bu, x ^= x >> 16;
    return x;
}
float Unit01(uint32_t& seed) { return float((seed = Hash(seed + 0x9E3779B9u)) >> 8) / float(1u << 24); }

// the game's ray with its pickups' mask (0x50D: the level and the objects in it), past the pickup itself
Hit Cast(X86Context& ctx, uint32_t obj, V a, V b) {
    static const uint32_t scratch = mem::AllocHostContiguous(32, 16);
    const uint32_t s = scratch, e = scratch + 12, list = scratch + 24;
    SetF(s, a.x), SetF(s + 4, a.y), SetF(s + 8, a.z);
    SetF(e, b.x), SetF(e + 4, b.y), SetF(e + 8, b.z);
    kernel::G<uint32_t>(list) = 0;
    Hit h;
    const uint32_t hit = kernel::CallGuest(ctx, 0x2C4E0, {s, e, R32(obj + kZone), obj, 0, list, 0, 0x50D, 0}) & 0xFF;
    const uint32_t c = R32(list);
    if (hit && Plausible(c)) {
        h.at = {F(c + 0x38), F(c + 0x3C), F(c + 0x40)};
        h.n = Unit({F(c + 0x10), F(c + 0x14), F(c + 0x18)});
        h.ok = Finite(h.at) && Finite(h.n) && Len(h.n) > 0.5f;
        if (!h.ok) h.n = {0, 1, 0};
    }
    if (c) kernel::CallGuest(ctx, 0x294D0, {list});
    return h;
}

// the matrix at obj+0x70 (rows +0x70, +0x80, +0x90; the place +0xA0) and the bounds' centre in the model (its +0x0C:
// 0x2DB20 puts it through the matrix to +0x60)
M Matrix(uint32_t obj) { return Load(obj + 0x70); }
V LocalCentre(uint32_t obj) {
    const uint32_t model = R32(obj + 0xB4);
    if (!Plausible(model) || (kernel::G<uint8_t>(obj + 0xD6) & 0x08)) return {};
    const V c{F(model + 0x0C), F(model + 0x10), F(model + 0x14)};
    return Finite(c) && Len(c) < 5 ? c : V{};
}
float Radius(uint32_t obj) {
    const uint32_t model = R32(obj + 0xB4);
    const float r = Plausible(model) ? std::abs(F(model + 0x20)) : 0.1f;
    return std::clamp(std::isfinite(r) ? r : 0.1f, 0.06f, 0.2f);
}

// the pickup put with its centre at c and its rows m.r: the matrix, the place (+0x24 too), the centre (+0x60),
// and +0xD6 bit 0x20 (the object loop puts it in its room after the thinks)
void Put(uint32_t obj, M m, V c) {
    m.t = c - Rows(LocalCentre(obj), m.r);
    if (!Finite(m.t)) return;
    Store(obj + 0x70, m);
    SetF(obj + kPos, m.t.x), SetF(obj + kPos + 4, m.t.y), SetF(obj + kPos + 8, m.t.z);
    SetF(obj + 0x60, c.x), SetF(obj + 0x64, c.y), SetF(obj + 0x68, c.z);
    kernel::G<uint8_t>(obj + 0xD6) |= 0x20;
}

M Turned(const M& m, V axis, float angle) {  // m's rows turned about axis (unit) by angle
    if (std::abs(angle) < 1e-6f) return m;
    const V a = Unit(axis);
    // Rodrigues in row form, as bones::TurnAbout
    const float c = std::cos(angle), s = std::sin(angle), k = 1 - c, x = a.x, y = a.y, z = a.z;
    V q[3] = {{c + x * x * k, x * y * k + z * s, x * z * k - y * s},
              {y * x * k - z * s, c + y * y * k, y * z * k + x * s},
              {z * x * k + y * s, z * y * k - x * s, c + z * z * k}};
    M o = m;
    for (int i = 0; i < 3; i++) o.r[i] = Rows(m.r[i], q);
    return o;
}

// the share (0..1) of the turn taking its model's up (the second row) to the normal n
M Flatten(const M& m, V n, float share) {
    const V up = Unit(m.r[1]);
    const V axis = Cross(up, n);
    const float s = Len(axis), angle = std::atan2(s, Dot(up, n));
    if (s < 1e-5f) return m;
    return Turned(m, axis * (1 / s), angle * share);
}

void Start(uint32_t obj, Item& it, V vel, V spin, bool resting) {
    it = Item{};
    it.data = R32(obj + kActor), it.seen = R32(kFrameCounter);
    it.vel = vel, it.spin = spin, it.resting = resting;
    it.from = {F(obj + 0x60), F(obj + 0x64), F(obj + 0x68)};
    if (Trace()) {
        const uint32_t d = it.data;
        const M m = Matrix(obj);
        const V c = m.t + Rows(LocalCentre(obj), m.r);
        LOGR("pickup: %08X centre %.2f %.2f %.2f from its matrix (the game's %.2f %.2f %.2f), place %.2f %.2f %.2f", obj,
             c.x, c.y, c.z, F(obj + 0x60), F(obj + 0x64), F(obj + 0x68), m.t.x, m.t.y, m.t.z);
        LOGR("pickup: %08X taken (%s): item %u type %u ammo %u at %.2f %.2f %.2f, velocity %.2f %.2f %.2f, radius %.2f",
             obj, resting ? "pushed" : "falling", kernel::G<uint16_t>(d + 0x1C), kernel::G<uint16_t>(d + 0x1A),
             kernel::G<uint16_t>(d + 0x1E), F(obj + 0x60), F(obj + 0x64), F(obj + 0x68), vel.x, vel.y, vel.z,
             Radius(obj));
    }
}

// a frame of an item's flight: true when it has come to rest
bool Step(X86Context& ctx, uint32_t obj, Item& it) {
    M m = Matrix(obj);
    const float r = Radius(obj);
    V c = m.t + Rows(LocalCentre(obj), m.r);
    if (!Finite(c) || !Finite(it.vel)) return true;
    const V was = c;
    const float g = -F(0x1F664C);  // the game's gravity (9.8)
    it.vel.y -= (std::isfinite(g) && g > 4 && g < 30 ? g : kGravity) * kDt;
    V move = it.vel * kDt;
    // along the move (two passes: a bounce off a wall into the floor)
    for (int pass = 0; pass < 2 && Len(move) > 1e-5f; pass++) {
        const V dir = Unit(move);
        const Hit h = Cast(ctx, obj, c, c + move + dir * r);
        if (!h.ok) {
            c = c + move;
            break;
        }
        V n = Dot(h.n, dir) > 0 ? h.n * -1 : h.n;
        // the centre a radius off the surface where it met it, the rest of the move reflected
        const float travel = std::max(0.0f, Len(h.at - c) - r);
        c = c + dir * travel;
        if (const float d = Dot(c - h.at, n); d < r) c = c + n * (r - d);
        const float vn = Dot(it.vel, n);
        if (vn < 0) {
            const V along = it.vel - n * vn;
            const float hard = -vn;
            it.vel = along * (1 - kScrape) - n * (vn * kBounce);
            // the scrape sets it spinning (as it rolls along the surface), a hard hit more
            it.spin = it.spin * 0.5f + Cross(n, along) * (0.5f / r) * (1 - kScrape);
            if (hard > 1.0f) {
                it.bounces++;
                if (Trace())
                    LOGR("pickup: %08X bounce %d at %.2f %.2f %.2f, normal %.2f %.2f %.2f, speed in %.2f, out %.2f %.2f %.2f",
                         obj, it.bounces, c.x, c.y, c.z, n.x, n.y, n.z, hard, it.vel.x, it.vel.y, it.vel.z);
            }
        }
        const float left = std::max(0.0f, Len(move) - travel);
        move = Unit(it.vel) * std::min(left, Len(it.vel) * kDt);
    }
    // the ground under it: rest on it, slide down it
    bool ground = false;
    V gn{0, 1, 0};
    {
        const Hit g = Cast(ctx, obj, c + V{0, r * 0.5f + 0.02f, 0}, c - V{0, r + 0.04f, 0});
        if (g.ok && g.n.y > 0.3f) {
            gn = g.n;
            const float d = Dot(c - g.at, gn);
            if (d < r + 0.02f) {
                ground = true;
                if (d < r) c = c + gn * (r - d);
                if (const float vn = Dot(it.vel, gn); vn < 0) it.vel = it.vel - gn * vn;
                V along = it.vel - gn * Dot(it.vel, gn);
                const float speed = Len(along), lose = kRoll * kDt;
                // gravity's pull along the slope stays (it slides down a steep one); friction holds it on a gentle one
                along = speed > lose ? along * (1 - lose / speed) : V{};
                if (gn.y > 0.7f && speed <= lose) it.vel = V{};
                else it.vel = along + gn * Dot(it.vel, gn);
                it.spin = it.spin * 0.85f;
            }
        }
    }
    // tumbling: turned about its centre; on the ground lying down on it
    const float w = Len(it.spin);
    if (w > 1e-3f) m = Turned(m, it.spin * (1 / w), w * kDt);
    if (ground) m = Flatten(m, gn, 0.2f);
    it.spin = it.spin * 0.995f;
    if (!Finite(c)) c = was;
    Put(obj, m, c);
    // the game's velocity (a frame's travel) kept as ours, should the game take it falling again
    SetF(it.data + 0, it.vel.x * kDt), SetF(it.data + 4, it.vel.y * kDt), SetF(it.data + 8, it.vel.z * kDt);
    it.still = ground && gn.y > 0.7f && Len(it.vel) < kRestSpeed ? it.still + 1 : 0;
    if (Trace() && it.age % 15 == 0)
        LOGR("pickup: %08X frame %d at %.2f %.2f %.2f velocity %.2f %.2f %.2f spin %.1f%s", obj, it.age, c.x, c.y, c.z,
             it.vel.x, it.vel.y, it.vel.z, w, ground ? " (on the ground)" : "");
    if (Len(c - it.from) > 150) {  // never seen: fallen out of the world; the game's again where it is
        if (Trace()) LOGR("pickup: %08X left %.0f m from its start: given back", obj, Len(c - it.from));
        it.age = kMaxFrames;
    }
    it.lost = !ground || Len(c - it.from) > 150;
    if (++it.age > kMaxFrames || it.still > kRestFrames) {
        if (ground) Put(obj, Flatten(Matrix(obj), gn, 1.0f), c);  // lying flat
        SetF(it.data + 0, 0), SetF(it.data + 4, 0), SetF(it.data + 8, 0);
        if (Trace())
            LOGR("pickup: %08X rests after %d frames, %d bounces, at %.2f %.2f %.2f (ground normal %.2f %.2f %.2f)", obj,
                 it.age, it.bounces, c.x, c.y, c.z, gn.x, gn.y, gn.z);
        return true;
    }
    return false;
}

// the players' motion (their places a frame apart), for the pushes
struct Mover {
    uint32_t obj = 0, frame = 0;
    V at, vel;
};
Mover g_movers[4];
void TrackPlayers() {
    const uint32_t frame = R32(kFrameCounter);
    for (uint32_t p = 0; p < 4; p++) {
        Mover& mv = g_movers[p];
        const uint32_t obj = R32(kPlayers + 4 * p);
        if (!Plausible(obj)) {
            mv = Mover{};
            continue;
        }
        if (mv.frame == frame && mv.obj == obj) continue;
        const V at{F(obj + kPos), F(obj + kPos + 4), F(obj + kPos + 8)};
        if (mv.obj == obj && frame - mv.frame >= 1 && frame - mv.frame <= 3 && Finite(at)) {
            V v = (at - mv.at) * (1.0f / (float(frame - mv.frame) * kDt));
            if (Len(v) > 15) v = V{};  // a teleport
            mv.vel = v;
        } else {
            mv.vel = V{};
        }
        mv.obj = obj, mv.frame = frame, mv.at = at;
    }
}

// a resting pickup a player walks into: pushed along (true: it is ours now)
bool Pushed(uint32_t obj, uint32_t data, V& vel, V& spin) {
    if (kernel::G<uint32_t>(obj + 0xCC) & 0x10) return false;  // hidden (taken, waiting)
    TrackPlayers();
    const V c{F(obj + 0x60), F(obj + 0x64), F(obj + 0x68)};
    for (const Mover& mv : g_movers) {
        if (!mv.obj || kernel::G<uint16_t>(mv.obj + 0xD0) != 1 || kernel::G<uint8_t>(mv.obj + 0xDB) == 0x12) continue;
        const V d = c - mv.at;
        const V flat{d.x, 0, d.z}, pv{mv.vel.x, 0, mv.vel.z};
        // (a player's place is about a metre over its feet)
        if (d.y < -1.8f || d.y > 0.6f || Len(flat) > 0.6f || Len(pv) < 0.8f || Dot(flat, pv) <= 0) continue;
        vel = pv + V{0, 1.0f, 0};
        if (Trace())
            LOGR("pickup: %08X pushed by player %08X moving %.2f %.2f m/s, %.2f m off", obj, mv.obj, pv.x, pv.z, Len(flat));
        spin = Cross(V{0, 1, 0}, Unit(pv)) * 6.0f;
        return true;
    }
    return false;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000A84D0);
X86_FUNC(sub_000A84D0) {  // a pickup's think (cdecl: the object)
    const uint32_t obj = R32(ctx.esp + 4);
    if (!On() || !Plausible(obj) || kernel::G<uint8_t>(obj + 0xDB) != kClass || !Plausible(R32(obj + kActor))) {
        if (!g_items.empty() && !On()) g_items.clear();
        __imp__sub_000A84D0(ctx, base);
        return;
    }
    const uint32_t data = R32(obj + kActor), frame = R32(kFrameCounter);
    const uint32_t state_va = data + 0x18;
    const uint16_t state = kernel::G<uint16_t>(state_va);
    // a level started (its id, or its world [0x1F6674] made anew on a restart): its pickups' places anew
    if (const uint32_t level = R32(kLevelId) ^ (R32(0x1F6674) * 0x9E3779B1u); level != g_spawn_level)
        g_spawns.clear(), g_spawn_level = level;
    auto found = g_items.find(obj);
    auto spawn = g_spawns.find(obj);
    // (an object made anew: another pickup)
    if (spawn != g_spawns.end() && spawn->second.data != data)
        g_spawns.erase(spawn), spawn = g_spawns.end();
    if (spawn == g_spawns.end() && state == 1 && found == g_items.end()) {  // resting where the level put it
        spawn = g_spawns.emplace(obj, Spawn{data, Matrix(obj)}).first;
    }
    if (state == 2 && spawn != g_spawns.end() && Len(Matrix(obj).t - spawn->second.m.t) > 0.01f) {
        // taken and waiting to respawn (hidden): back to its place now, so it reappears there
        const M& m = spawn->second.m;
        Put(obj, m, m.t + Rows(LocalCentre(obj), m.r));
        if (Trace()) LOGR("pickup: %08X taken: back to its spawn at %.2f %.2f %.2f for its respawn", obj, m.t.x, m.t.y, m.t.z);
    }
    if (found != g_items.end() && (found->second.data != data || state == 2 || frame - found->second.seen > 30)) {
        g_items.erase(found);
        found = g_items.end();
    }
    if (found == g_items.end()) {
        V vel, spin;
        if (state == 0) {  // falling the game's way: ours, with its velocity (a frame's travel)
            vel = V{F(data), F(data + 4), F(data + 8)} * (1 / kDt);
            if (!Finite(vel) || Len(vel) > 40) vel = V{};
            Start(obj, g_items[obj], vel, V{}, false);
        } else if (state == 1 && Pushed(obj, data, vel, spin)) {
            Start(obj, g_items[obj], vel, spin, true);
        } else {
            __imp__sub_000A84D0(ctx, base);
            return;
        }
        found = g_items.find(obj);
    }
    Item& it = found->second;
    it.seen = frame;
    const bool rests = Step(ctx, obj, it);
    // the game's think as for a resting pickup (the pickup test, the countdown), its spin off while it tumbles
    const uint16_t spin = kernel::G<uint16_t>(data + 0x30);
    const bool resting = it.resting;
    kernel::G<uint16_t>(data + 0x30) = 0;
    kernel::G<uint16_t>(state_va) = 1;
    __imp__sub_000A84D0(ctx, base);
    if (R32(obj + kActor) != data) {  // (never seen: the object made anew)
        g_items.erase(obj);
        return;
    }
    kernel::G<uint16_t>(data + 0x30) = spin;
    const bool taken = kernel::G<uint16_t>(state_va) != 1 || (kernel::G<uint8_t>(obj + 0xDA) & 1);
    if (taken) {
        if (Trace()) LOGR("pickup: %08X picked up while moving (state %u)", obj, kernel::G<uint16_t>(state_va));
        g_items.erase(obj);
        return;
    }
    if (rests) {
        if (it.lost && spawn != g_spawns.end()) {  // a level pickup lost: back to its place
            const M& m = spawn->second.m;
            Put(obj, m, m.t + Rows(LocalCentre(obj), m.r));
            if (Trace()) LOGR("pickup: %08X lost: back to its spawn at %.2f %.2f %.2f", obj, m.t.x, m.t.y, m.t.z);
        }
        g_items.erase(obj);
        if (!resting) kernel::CallGuest(ctx, 0xA1310, {obj});  // the game's landing: its multiplayer slot
        return;
    }
    if (!resting) kernel::G<uint16_t>(state_va) = 0;
}

X86_EXTERN_FUNC(__imp__sub_000A7F10);
X86_FUNC(sub_000A7F10) {  // a pickup dropped (cdecl: matrix*, weapon, ammo, type; returns the object)
    const uint32_t weapon = R32(ctx.esp + 8) & 0xFFFF;
    __imp__sub_000A7F10(ctx, base);
    const uint32_t obj = ctx.eax;
    if (!On() || !Plausible(obj) || kernel::G<uint8_t>(obj + 0xDB) != kClass) return;
    const uint32_t data = R32(obj + kActor);
    if (!Plausible(data) || kernel::G<uint16_t>(data + 0x18) != 0) return;
    // a toss: up and out, random (the same for the same object, frame and weapon), with a spin
    uint32_t seed = obj ^ (R32(kFrameCounter) * 0x9E3779B1u) ^ (weapon << 20);
    const float angle = Unit01(seed) * 6.2831853f, out = 1.4f + 1.4f * Unit01(seed), up = 2.6f + 1.2f * Unit01(seed);
    static const float toss = [] {  // NF_PICKUP_TOSS=k (diagnostic): the toss's sideways speed scaled
        const char* e = std::getenv("NF_PICKUP_TOSS");
        return e && *e ? float(std::atof(e)) : 1.0f;
    }();
    const V vel{std::cos(angle) * out * toss, up, std::sin(angle) * out * toss};
    const V axis = Unit(V{Unit01(seed) - 0.5f, (Unit01(seed) - 0.5f) * 0.6f, Unit01(seed) - 0.5f});
    const V spin = axis * (5.0f + 6.0f * Unit01(seed));
    Start(obj, g_items[obj], vel, spin, false);
    ctx.eax = obj;
}

namespace nf {
// an explosion at pos (src/game/fx.cpp, 0x690A0): the pickups within its radius thrown away from it
void PickupBlast(const float pos[3], float radius) {
    if (!On() || !std::isfinite(radius)) return;
    const V at{pos[0], pos[1], pos[2]};
    const float reach = std::clamp(radius, 1.0f, 8.0f) * 1.5f;
    const uint32_t frame = R32(kFrameCounter);
    int n = 0;
    for (uint32_t o = R32(0x1DF34C); Plausible(o) && n < 4096; o = R32(o + 0x14), n++) {
        if (kernel::G<uint8_t>(o + 0xDB) != kClass || (kernel::G<uint32_t>(o + 0xCC) & 0x10)) continue;
        const uint32_t data = R32(o + kActor);
        if (!Plausible(data) || kernel::G<uint16_t>(data + 0x18) == 2) continue;
        const V c{F(o + 0x60), F(o + 0x64), F(o + 0x68)};
        const V d = c - at;
        const float dist = Len(d);
        if (!Finite(c) || dist > reach) continue;
        const float k = 1 - dist / reach;
        const V away = Unit(V{d.x, 0, d.z});
        V push = away * (1.5f + 5 * k) + V{0, 2 + 3.5f * k, 0};
        uint32_t seed = o ^ frame;
        const V spin = Unit(V{Unit01(seed) - 0.5f, Unit01(seed) - 0.5f, Unit01(seed) - 0.5f}) * (6 + 10 * k);
        auto it = g_items.find(o);
        if (it != g_items.end() && it->second.data == data) {
            it->second.vel = it->second.vel + push, it->second.spin = it->second.spin + spin;
            it->second.still = 0, it->second.age = 0;
        } else {
            Start(o, g_items[o], push, spin, kernel::G<uint16_t>(data + 0x18) == 1);
        }
        if (Trace()) LOGR("pickup: %08X thrown by an explosion %.1f m away (radius %.1f)", o, dist, radius);
    }
}

// NF_PICKUP_LEVEL_TEST=1 (diagnostic): 5 s into a level the level pickup nearest player 1 is pushed (as a player's
// push, 4 m/s away from player 1), 4 s later taken as the game takes a respawning one (state 2, hidden, its time at
// +0xC8; a respawn time of 1 if it had none), then followed until the game shows it again: where, against its spawn
void LevelTest(X86Context& ctx) {
    static const bool on = [] {
        const char* e = std::getenv("NF_PICKUP_LEVEL_TEST");
        return e && *e == '1';
    }();
    static uint32_t frames = 0, obj = 0, data = 0;
    if (!on || !On() || MenuUp()) return;
    (void)ctx;
    ++frames;
    const uint32_t me = R32(kPlayers);
    if (!Plausible(me)) return;
    const V at{F(me + kPos), F(me + kPos + 4), F(me + kPos + 8)};
    if (frames == 300) {
        float best = 1e9f;
        int count = 0;
        int n = 0;
        for (uint32_t o = R32(0x1DF34C); Plausible(o) && n < 4096; o = R32(o + 0x14), n++) {
            if (kernel::G<uint8_t>(o + 0xDB) != kClass || !g_spawns.count(o)) continue;
            const uint32_t d = R32(o + kActor);
            if (!Plausible(d) || kernel::G<uint16_t>(d + 0x18) != 1 || (kernel::G<uint32_t>(o + 0xCC) & 0x10)) continue;
            count++;
            const float dist = Len(V{F(o + 0x60), F(o + 0x64), F(o + 0x68)} - at);
            if (dist < best) best = dist, obj = o, data = d;
        }
        LOGR("pickup level test: %d level pickups resting; the nearest %08X at %.1f m", count, obj, best);
        if (!obj) return;
        LOGR("pickup level test: %08X item %u type %u ammo %u respawn %u, spawn %.2f %.2f %.2f", obj,
             kernel::G<uint16_t>(data + 0x1C), kernel::G<uint16_t>(data + 0x1A), kernel::G<uint16_t>(data + 0x1E),
             kernel::G<uint16_t>(data + 0x24), F(obj + 0xA0), F(obj + 0xA4), F(obj + 0xA8));
        const V c{F(obj + 0x60), F(obj + 0x64), F(obj + 0x68)};
        const V away = Unit(V{c.x - at.x, 0, c.z - at.z});
        Start(obj, g_items[obj], away * 4.0f + V{0, 1.5f, 0}, Cross(V{0, 1, 0}, away) * 6.0f, true);
        return;
    }
    if (!obj || R32(obj + kActor) != data || kernel::G<uint8_t>(obj + 0xDB) != kClass) return;
    const auto sp = g_spawns.find(obj);
    const V spawn = sp != g_spawns.end() ? sp->second.m.t : V{};
    const V now{F(obj + 0xA0), F(obj + 0xA4), F(obj + 0xA8)};
    const uint16_t state = kernel::G<uint16_t>(data + 0x18);
    const bool hidden = kernel::G<uint32_t>(obj + 0xCC) & 0x10;
    if (frames == 540) {
        LOGR("pickup level test: %08X pushed to %.2f %.2f %.2f, %.2f m from its spawn (state %u); taken now", obj, now.x,
             now.y, now.z, Len(now - spawn), state);
        if (!kernel::G<uint16_t>(data + 0x24)) kernel::G<uint16_t>(data + 0x24) = 1;
        kernel::G<uint16_t>(data + 0x18) = 2;
        kernel::G<uint32_t>(obj + 0xCC) |= 0x10;
        kernel::G<uint32_t>(obj + 0xC8) = R32(0x1F65B4);
        return;
    }
    if (frames > 540 && frames % 30 == 0) {
        LOGR("pickup level test: %08X state %u%s at %.2f %.2f %.2f, %.2f m from its spawn", obj, state,
             hidden ? " hidden" : "", now.x, now.y, now.z, Len(now - spawn));
        if (state == 1 && !hidden) {
            LOGR("pickup level test: %08X respawned %s", obj, Len(now - spawn) < 0.01f ? "at its spawn" : "ELSEWHERE");
            obj = 0;
        }
    }
}

// NF_PICKUP_TEST=<weapon> (diagnostic): that weapon dropped (0xA7F10, 30 rounds) 2 m in front of player 1 and 1.3 m
// up, every 4 s (from the 2D pass); when its model is not in the level, the next weapon whose model is
void PickupTest(X86Context& ctx) {
    static const int weapon = [] {
        const char* e = std::getenv("NF_PICKUP_TEST");
        return e && *e ? std::atoi(e) : 0;
    }();
    static uint32_t frames = 0, last = 0;
    LevelTest(ctx);
    if (weapon <= 0 || MenuUp()) return;
    ++frames;
    // NF_PICKUP_BLAST=1: 2 s after each drop an explosion (0x690A0, radius 2) 1 m past where it lies
    static const bool blast = std::getenv("NF_PICKUP_BLAST") != nullptr;
    if (blast && last && frames % 240 == 120 && Plausible(R32(last + kActor)) &&
        kernel::G<uint8_t>(last + 0xDB) == kClass) {
        static const uint32_t at = mem::AllocHostContiguous(16, 16);
        const uint32_t cam = R32(0x1F661C);
        const V fwd = Unit(Plausible(cam) ? V{F(cam + 0x128), 0, F(cam + 0x130)} : V{0, 0, 1});
        const V p = V{F(last + 0x60), F(last + 0x64), F(last + 0x68)} + fwd * 1.0f;
        SetF(at, p.x), SetF(at + 4, p.y), SetF(at + 8, p.z);
        const uint32_t r = std::bit_cast<uint32_t>(2.0f);
        kernel::CallGuest(ctx, 0x690A0, {last, at, 0x29D71C, r, r, 0x6000052, 0, 0xFF, 0xFF, 0x64, 0, 0});
        LOGR("pickup test: an explosion at %.2f %.2f %.2f", p.x, p.y, p.z);
    }
    if (frames < 300 || frames % 240) return;
    const uint32_t me = R32(kPlayers);
    if (!Plausible(me)) return;
    static const uint32_t mat = mem::AllocHostContiguous(0x40, 16);
    std::memcpy(kernel::HostPtr(mat), kernel::HostPtr(me + 0x70), 0x40);
    const uint32_t cam = R32(0x1F661C);
    const V fwd = Unit(Plausible(cam) ? V{F(cam + 0x128), 0, F(cam + 0x130)} : V{0, 0, 1});
    static float ahead = 2.0f, up = 1.3f;
    static const bool read = [] {  // NF_PICKUP_TEST_AT=ahead,up (metres)
        if (const char* e = std::getenv("NF_PICKUP_TEST_AT")) std::sscanf(e, "%f,%f", &ahead, &up);
        return true;
    }();
    (void)read;
    const V to = V{F(me + kPos), F(me + kPos + 4), F(me + kPos + 8)} + fwd * ahead + V{0, up, 0};
    SetF(mat + 0x30, to.x), SetF(mat + 0x34, to.y), SetF(mat + 0x38, to.z);
    // the weapon asked for; else (its model not in this level) the next whose model is, from 1 on
    static int next = 1;
    int id = weapon;
    uint32_t obj = kernel::CallGuest(ctx, 0xA7F10, {mat, uint32_t(id), 30, 0});
    for (int tries = 0; !obj && tries < 0x71; tries++, next = next % 0x71 + 1)
        if (next != weapon) obj = kernel::CallGuest(ctx, 0xA7F10, {mat, uint32_t(id = next), 30, 0});
    if (obj) next = next % 0x71 + 1;
    last = obj;
    LOGR("pickup test: weapon %d dropped at %.2f %.2f %.2f: object %08X (gravity %.3f)", id, to.x, to.y, to.z, obj,
         F(0x1F664C));
}
}  // namespace nf
