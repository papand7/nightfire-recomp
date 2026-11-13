// LAN play's state hash (core/netplay.h: the copies compare it once a second and log a desync): every entity's place
// and health. The edicts are at [0x329390], 0x2E0 each, 1024 of them; an edict's entity is its +0x278 (0 a free
// slot), the entity's origin three floats at +0xA8 (src/bond/pickup_physics.cpp), the edict's health a float at +0x2C4.
// The players are the first edicts (player 1 at 0, src/bond/hit_feedback.cpp), the bots and everything else after.
#include "bond/addr.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "common/auf.h"
#include "xdk/d3d8.h"
#include "core/log.h"
#include "core/netplay.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kGentities = A(0x329390), kGentitySize = 0x2E0, kMaxEntities = 1024;
constexpr uint32_t kEntity = 0x278, kOrigin = 0xA8, kHealth = 0x2C4;
constexpr uint32_t kGuestTop = 0x08000000;  // the console's 128 MB: past it a pointer is not one

void Mix(uint64_t& h, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001B3ull;
}

uint64_t Hash() {
    uint64_t h = 0xCBF29CE484222325ull;
    const uint32_t edicts = kernel::R32(kGentities);
    if (!edicts || edicts >= kGuestTop) return h;
    uint32_t live = 0;
    for (uint32_t k = 0; k < kMaxEntities; k++) {
        const uint32_t edict = edicts + k * kGentitySize, entity = kernel::R32(edict + kEntity);
        if (!entity || entity >= kGuestTop) continue;
        live++;
        uint8_t data[16];
        std::memcpy(data, &kernel::G<uint8_t>(entity + kOrigin), 12);
        std::memcpy(data + 12, &kernel::G<uint8_t>(edict + kHealth), 4);
        Mix(h, &k, 4);
        Mix(h, data, sizeof data);
    }
    Mix(h, &live, 4);
    // the first hashes in the log with the first two edicts' places (where two copies part, compared by eye)
    static int said = 0;
    if (said < 6 && live) {
        said++;
        float o[2][3] = {};
        for (uint32_t k = 0; k < 2; k++)
            if (const uint32_t e = kernel::R32(edicts + k * kGentitySize + kEntity); e && e < kGuestTop)
                std::memcpy(o[k], &kernel::G<uint8_t>(e + kOrigin), 12);
        LOGR("auf: network play: frame %llu, %u entities, player 1 at (%.1f %.1f %.1f), player 2 at (%.1f %.1f %.1f)",
             (unsigned long long)netplay::FrameCount(), live, o[0][0], o[0][1], o[0][2], o[1][0], o[1][1], o[1][2]);
    }
    return h;
}
}  // namespace

// srand (the C library's, its state the calling thread's): the game seeds it with the performance counter at boot
// (0x25E82) and as a match starts (0x26039). In LAN play that counter is the frames' plus a little each read, and how many
// reads a loading wait takes is the PC's own: the seed is the frame's alone there, the same on every copy.
X86_EXTERN_FUNC(__imp__sub_00168E29);
X86_FUNC(sub_00168E29) {
    if (netplay::Active()) {
        const uint32_t seed = 0x5EED0000u ^ uint32_t(netplay::FrameCount());
        static int said = 0;
        if (said++ < 8) LOGR("auf: network play: srand(%08X) at frame %llu", seed, (unsigned long long)netplay::FrameCount());
        kernel::W32(ctx.esp + 4, seed);
    }
    __imp__sub_00168E29(ctx, base);
}

// The game's frame (its main loop at 0x269E0: a state change's enter function, which loads a level and presents loading
// frames of its own, then 0x25CA0 measures the frame's time, then the state's frame draws and presents): Tick here keeps
// a loading screen's presents out of the lockstep, as each PC presents its own number of them (core/netplay.h).
X86_EXTERN_FUNC(__imp__sub_00025CA0);
X86_FUNC(sub_00025CA0) {
    netplay::Tick();
    __imp__sub_00025CA0(ctx, base);
}

// Each player sees only their own view in LAN play. The split screen's views (0x26DA1 calls 0x263D0 for each local
// player, edi the player) are set up by 0x15E140 (cdecl: view, views, player): 2 views halve the height, 3 or 4 the width
// too. This copy's player gets the whole screen (views 1); the others' views are still made (their HUD and the pause
// menu's tick run there, the game's logic) but drawn into a pixel off the screen: 0x15E050 (esi the rect x, y, w, h floats, edi
// the player) makes the camera and the viewport from that rect.
X86_EXTERN_FUNC(__imp__sub_0015E140);
static bool g_pixel_view = false;  // the view being made is another player's split screen view
static bool g_other_view = false;  // 0x263D0 runs for another player's view (from its start or its 0x15E140 to its end)
static bool g_own_view = false;    // 0x263D0 runs for this copy's own player's view
// AUF_NET_VIEW (diagnostic) bits: 1 the other views' HUD menu (0x33EE0) not drawn, 2 their camera (0x15E050) not made
static int NetView() {
    static const int v = [] {
        const char* e = std::getenv("AUF_NET_VIEW");
        return e && *e ? std::atoi(e) : 0;
    }();
    return v;
}
X86_FUNC(sub_0015E140) {
    const bool split = netplay::Active() && netplay::Players() > 1 && kernel::R32(ctx.esp + 8) > 1;
    const bool mine = kernel::R32(ctx.esp + 12) == uint32_t(netplay::Me());
    if (split && mine) kernel::W32(ctx.esp + 8, 1);
    g_pixel_view = split && !mine;
    g_other_view = g_pixel_view;
    // its draws dropped until its 0x263D0 returns: the view's own 2D fills (a red one over the bottom half) are drawn
    // at the split screen's place whatever the viewport
    d3d::SetDrawsOff(g_other_view);
    __imp__sub_0015E140(ctx, base);
    g_pixel_view = false;
}
X86_EXTERN_FUNC(__imp__sub_0015E050);
X86_FUNC(sub_0015E050) {
    if (g_pixel_view && (NetView() & 2)) {
        ctx.esp += 4;
        return;
    }
    if (g_pixel_view) {  // off the screen: the view's full rectangle fill (a red one seen) starts at its x, y too
        kernel::G<float>(ctx.esi + 0) = 4000.0f;
        kernel::G<float>(ctx.esi + 4) = 4000.0f;
        kernel::G<float>(ctx.esi + 8) = 1.0f;
        kernel::G<float>(ctx.esi + 12) = 1.0f;
    }
    __imp__sub_0015E050(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_000263D0);
X86_FUNC(sub_000263D0) {
    // another player's view (edi its index) drawn nowhere from its start (0x33F40 places its HUD before 0x15E140)
    const bool lan = netplay::Active() && netplay::Players() > 1;
    if (lan && ctx.edi != uint32_t(netplay::Me())) {
        g_other_view = true;
        d3d::SetDrawsOff(true);
    }
    g_own_view = lan && ctx.edi == uint32_t(netplay::Me());
    __imp__sub_000263D0(ctx, base);
    g_own_view = false;
    if (g_other_view) d3d::SetDrawsOff(false);
    g_other_view = false;
}
// 0x518F0 (eax the HUD) draws the split screen's frame in the player's colour around the view (a red 640x240 one for
// player 1, the other views drawn over its inside): with this copy's view the whole screen, none
X86_EXTERN_FUNC(__imp__sub_000518F0);
X86_FUNC(sub_000518F0) {
    if (g_own_view) {
        ctx.esp += 4;  // cdecl: the return address only
        return;
    }
    __imp__sub_000518F0(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_00033EE0);
X86_FUNC(sub_00033EE0) {
    if (g_other_view && (NetView() & 1)) {
        ctx.esp += 4;
        return;
    }
    __imp__sub_00033EE0(ctx, base);
}
// (Drawing this copy's view last instead, 0x263D0 put off until after the last player's, made the copies part: that
// function also runs each player's HUD and pause menu logic, whose order matters. 2026-11-13.)

namespace auf {
// bond_eng.xbe each frame (hooks.cpp): the hash handed over once
void NetplayBond() {
    if (g_other_view) d3d::SetDrawsOff(false);
    g_other_view = false;
    CoopFrame();
    static bool set = false;
    if (set || !netplay::Active()) return;
    set = true;
    netplay::SetStateHash(Hash);
    LOGR("auf: network play: the copies compare every entity's place and health once a second");
}
}  // namespace auf
