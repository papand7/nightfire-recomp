// LAN play (the runtime's core/netplay.h: every copy runs the whole game in lockstep, only the controllers travel).
// What this port adds to the game that changes what happens in it must be the same on every copy: the host's choices
// travel in the handshake (netplay::SetSessionRules) and every copy plays by them from its first frame; what one
// player's keyboard does to the game on that copy alone (photo mode's pause, the restart key, the modern menus' mouse,
// the keyboard's own handling) is off. The state hash that finds a copy gone its own way is bond_eng.xbe's
// (src/bond/netplay_hash.cpp).
#include <cstdint>

#include "common/auf.h"
#include "core/log.h"
#include "core/netplay.h"

namespace auf {
namespace {
// the rules word: kValid set by this port's hosts; the rest a bit per option
enum : uint32_t {
    kAutoAim = 1u << 0,
    kAimDownSights = 1u << 1,
    kUncapped = 1u << 2,
    kSkipCutscenes = 1u << 3,
    kRagdolls = 1u << 4,
    kValid = 1u << 31,
};

uint32_t Mine() {
    return kValid | (auto_aim ? kAutoAim : 0) | (aim_down_sights ? kAimDownSights : 0) | (uncapped ? kUncapped : 0) |
           (skip_cutscenes ? kSkipCutscenes : 0) | (ragdolls ? kRagdolls : 0);
}

void Started() {
    const uint32_t r = netplay::SessionRules();
    if (r & kValid) {
        if (r != Mine()) LOGR("auf: network play: the host's options in force (%08X, this PC's %08X)", r, Mine());
        auto_aim = r & kAutoAim;
        aim_down_sights = r & kAimDownSights;
        uncapped = r & kUncapped;
        skip_cutscenes = r & kSkipCutscenes;
        ragdolls = r & kRagdolls;
    }
    // one copy's keyboard alone: off
    photo_mode = false;
    restart_key = 0;
    pickup_physics = false;
    LOGR("auf: network play: player %d of %d; auto-aim %d, aim down sights %d, high frame rate %d, skip cutscenes %d, "
         "ragdolls %d; photo mode, the restart key and pickup physics off",
         netplay::Me() + 1, netplay::Players(), auto_aim, aim_down_sights, uncapped, skip_cutscenes, ragdolls);
}
}  // namespace

void NetplayInit() {
    if (!netplay::Configured()) return;
    netplay::SetSessionRules(Mine());
    netplay::OnStart(Started);
}

}  // namespace auf
