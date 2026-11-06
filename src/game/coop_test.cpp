// NF_COOP_TEST (diagnostic, no menus): a story mission with a second local player, to see what the levels do with
// one. A mission's start (0xBDDF0: the level, 0, 0) runs with one human [0x260044], one character [0x26002C] and the
// multiplayer flag [0x260018] clear. =1 raises the humans and characters to two (also held from the moment a mission
// is chosen, [0x1F6588] no longer the front end 0x7000048): the level still makes one player, full screen. =2 sets
// the multiplayer flag too, which the camera split takes (0x25E20: one camera a human only when it is set): the
// level then runs the multiplayer rules each frame (0xA2B80 on the scenario state [0x262920]) and crashes 8 s in on
// the multiplayer setup it never had (a null read under 0x9CC50). 2026-11-06, The Exchange.
#include <cstdint>
#include <cstdlib>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kMulti = 0x260018, kHumans = 0x260044, kMode = 0x26002C /* the characters: humans and bots */, kNextLevel = 0x1F6588;

int CoopTest() {
    static const int n = [] {
        const char* e = std::getenv("NF_COOP_TEST");
        return e && *e ? std::atoi(e) : 0;
    }();
    return n;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000BDDF0);
X86_FUNC(sub_000BDDF0) {  // a level started (the level, 0, 0): its players made from [0x260044] at its load
    uint32_t level = kernel::R32(ctx.esp + 4);
    __imp__sub_000BDDF0(ctx, base);
    if (!CoopTest()) return;
    LOGR("nightfire: coop test: level %08X starting, %u humans, mode %u", level, kernel::R32(kHumans), kernel::R32(kMode));
    if (kernel::R32(kMode) != 1 || kernel::R32(kHumans) != 1) return;
    kernel::W32(kHumans, 2);
    kernel::W32(kMode, 2);
    if (CoopTest() >= 2) kernel::W32(kMulti, 1);  // =2: the multiplayer flag too (one camera a player: 0x25E20)
    LOGR("nightfire: coop test: level %08X started with 2 players", level);
}

X86_EXTERN_FUNC(__imp__sub_0006CF50);
X86_FUNC(sub_0006CF50) {  // the players' pads read, each frame (menus too): the count held at two from the menus on
    uint32_t next = kernel::R32(kNextLevel);
    if (CoopTest() && next && next != 0x7000048 && kernel::R32(kHumans) != 2) {  // a mission chosen
        LOGR("nightfire: coop test: %u humans made 2 (level %08X, next %08X)", kernel::R32(kHumans), kernel::R32(0x1F658C), next);
        kernel::W32(kHumans, 2);
    }
    if (CoopTest() && next && next != 0x7000048 && kernel::R32(kMode) == 1) kernel::W32(kMode, 2);
    if (CoopTest() >= 2 && next && next != 0x7000048 && !kernel::R32(kMulti)) {
        LOGR("nightfire: coop test: the multiplayer flag set (next %08X)", next);
        kernel::W32(kMulti, 1);
    }
    __imp__sub_0006CF50(ctx, base);
}
