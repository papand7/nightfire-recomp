// NF_COOP_TEST (diagnostic, no menus): a story mission with a second local player, to see what the levels do with
// one. A mission's start (0xBDDF0: the level, 0, 0) runs with one human [0x260044], one character [0x26002C] and the
// multiplayer flag [0x260018] clear. =1 raises the humans and characters to two (also held from the moment a mission
// is chosen, [0x1F6588] no longer the front end 0x7000048): the level still makes one player, full screen. =2 sets
// the multiplayer flag too, which the camera split takes (0x25E20: one camera a human only when it is set): the
// level then runs the multiplayer rules each frame (0xA2B80 on the scenario state [0x262920]) and crashes 8 s in on
// the multiplayer setup it never had (a null read under 0x9CC50). 2026-11-06, The Exchange.
//
// =3: a second player made by hand. A story level makes its player in 0xAE870 (a start record of 0xCC bytes at
// 0x2774D0: its place, +0xC its facing, +0x1C its loadout) through 0xACAB0 (cdecl: the player, place*, facing*,
// loadout*: the character's object in [0x1F6654 + 4p], its actor in [0x2774B8 + 4p], the player's camera
// [0x1F661C + 4p] read and given the object). The cameras are made in 0x25D10, one a human only with the multiplayer
// flag, then laid out by 0x25B80 (camera, x, y, w, h: floats of the 640 x 480 screen; two players one above the other,
// side by side when [0x1F660C] is 1). So after 0xAE870 the second camera is made (0x25BF0 as 0x25D10 makes it) and the
// screen split, then the second player made a step beside the first.
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "common/nightfire.h"
#include "core/log.h"
#include "gpu/style.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kMulti = 0x260018, kHumans = 0x260044, kMode = 0x26002C /* the characters: humans and bots */, kNextLevel = 0x1F6588;

// the setting (nf::coop: story co-op, =3) or the test switch
int CoopTest() {
    static const int n = [] {
        const char* e = std::getenv("NF_COOP_TEST");
        return e && *e ? std::atoi(e) : 0;
    }();
    return n ? n : nf::coop ? 3 : 0;
}
int g_split = 0;  // the screen split now (nf::CoopSplit)
void Views(int split) {
    g_split = split;
    gpu::SetViewCount(split ? 2 : 1);
}
}  // namespace

int nf::CoopSplit() { return g_split; }

X86_EXTERN_FUNC(__imp__sub_000BDDF0);
X86_FUNC(sub_000BDDF0) {  // a level started (the level, 0, 0): its players made from [0x260044] at its load
    uint32_t level = kernel::R32(ctx.esp + 4);
    Views(0);  // one view until the level's cameras are split again
    __imp__sub_000BDDF0(ctx, base);
    if (!CoopTest()) return;
    LOGR("nightfire: coop test: level %08X starting, %u humans, mode %u", level, kernel::R32(kHumans), kernel::R32(kMode));
    if (kernel::R32(kMode) != 1 || kernel::R32(kHumans) != 1) return;
    kernel::W32(kHumans, 2);
    kernel::W32(kMode, 2);
    if (CoopTest() == 2) kernel::W32(kMulti, 1);  // =2: the multiplayer flag too (one camera a player: 0x25E20)
    LOGR("nightfire: coop test: level %08X started with 2 players", level);
}

X86_EXTERN_FUNC(__imp__sub_0006CF50);
X86_FUNC(sub_0006CF50) {  // the players' pads read, each frame (menus too): the count held at two from the menus on
    uint32_t next = kernel::R32(kNextLevel);
    // NF_LEVEL=<hex> (a test): whichever on-foot mission the menus start, this one instead (0x07000005 The Exchange)
    static const uint32_t forced = [] {
        const char* e = std::getenv("NF_LEVEL");
        return e && *e ? uint32_t(std::strtoul(e, nullptr, 16)) : 0u;
    }();
    if (forced && next && next != 0x7000048 && next != forced && (next >> 24) == 7) {
        LOGR("nightfire: NF_LEVEL: level %08X started as %08X", next, forced);
        kernel::W32(kNextLevel, forced);
        next = forced;
    }
    if (next == 0x7000048 && g_split) Views(0);  // back to the front end: one view
    // back to the front end: one human and one character again (the mission's choice raises them for the next)
    if (next == 0x7000048 && kernel::R32(kHumans) == 2 && CoopTest()) kernel::W32(kHumans, 1), kernel::W32(kMode, 1);
    nf::CoopFrame(ctx);
    if (CoopTest() && next && next != 0x7000048 && kernel::R32(kHumans) != 2) {  // a mission chosen
        LOGR("nightfire: coop test: %u humans made 2 (level %08X, next %08X)", kernel::R32(kHumans), kernel::R32(0x1F658C), next);
        kernel::W32(kHumans, 2);
    }
    if (CoopTest() && next && next != 0x7000048 && kernel::R32(kMode) == 1) kernel::W32(kMode, 2);
    if (CoopTest() == 2 && next && next != 0x7000048 && !kernel::R32(kMulti)) {
        LOGR("nightfire: coop test: the multiplayer flag set (next %08X)", next);
        kernel::W32(kMulti, 1);
    }
    __imp__sub_0006CF50(ctx, base);
}

namespace {
constexpr uint32_t kPlayerObjects = 0x1F6654, kCameras = 0x1F661C, kCameraWorld = 0x1F6674;
constexpr uint32_t kHalfW = 0x43A00000, kFullW = 0x44200000, kHalfH = 0x43700000, kFullH = 0x43F00000;  // floats

// the second camera, and the screen split for two (as 0x25D10 does with the multiplayer flag)
void SplitScreen(X86Context& ctx) {
    if (!kernel::R32(kCameras + 4))
        kernel::CallGuest(ctx, 0x25BF0, {1, kernel::R32(kCameraWorld), 0, 0, 0, 0x280, 0x1E0});
    bool side = kernel::R32(0x1F660C) == 1;
    uint32_t w = side ? kHalfW : kFullW, h = side ? kFullH : kHalfH;
    kernel::CallGuest(ctx, 0x25B80, {0, 0, 0, w, h});
    kernel::CallGuest(ctx, 0x25B80, {1, side ? kHalfW : 0, side ? 0 : kHalfH, w, h});
    Views(side ? 2 : 1);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00025D10);
X86_FUNC(sub_00025D10) {  // the cameras made for a level: two, split, in a story level under the test
    __imp__sub_00025D10(ctx, base);
    if (CoopTest() != 3 || kernel::R32(kMulti) || kernel::R32(kNextLevel) == 0x7000048) return;  // not the front end
    uint32_t eax = ctx.eax;
    SplitScreen(ctx);
    ctx.eax = eax;
    LOGR("nightfire: coop test: two cameras, the screen split");
}

X86_EXTERN_FUNC(__imp__sub_000AE870);
X86_FUNC(sub_000AE870) {  // a story level's player made at its start record
    __imp__sub_000AE870(ctx, base);
    uint32_t first = kernel::R32(kPlayerObjects);
    if (CoopTest() != 3 || kernel::R32(kMulti) || !first || kernel::R32(kPlayerObjects + 4)) return;
    uint32_t eax = ctx.eax;
    SplitScreen(ctx);
    // the second's start: the first's place a step to the side, its facing, the first start record's loadout
    static uint32_t start = mem::AllocSystem(0x40);
    for (int i = 0; i < 3; i++) kernel::W32(start + 4 * i, kernel::R32(first + 0x24 + 4 * i));
    for (int i = 0; i < 3; i++) kernel::W32(start + 0x10 + 4 * i, kernel::R32(first + 0x3C + 4 * i));
    float x;
    uint32_t xb = kernel::R32(start);
    std::memcpy(&x, &xb, 4);
    x += 1.2f;
    std::memcpy(&xb, &x, 4);
    kernel::W32(start, xb);
    // the first start record's loadout, without its condition (+0x2C: a flag in 0x1DF138 the start waits for)
    static uint32_t loadout = mem::AllocSystem(0xB0);
    for (uint32_t i = 0; i < 0xB0; i += 4) kernel::W32(loadout + i, kernel::R32(0x2774EC + i));
    LOGR("nightfire: coop test: start loadout +2C %08X +38 %08X", kernel::R32(loadout + 0x2C), kernel::R32(loadout + 0x38));
    kernel::W32(loadout + 0x2C, 0);
    uint32_t second = kernel::CallGuest(ctx, 0xACAB0, {1, start, start + 0x10, loadout});
    ctx.eax = eax;
    LOGR("nightfire: coop test: the second player made: %08X (the first %08X)", second, first);
}
