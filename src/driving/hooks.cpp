// Hooks for Driving.xbe (the driving levels). A strong definition replaces the recompiled function at that address,
// as in src/game/hooks.cpp.

#include "common/nightfire.h"
#include "kernel/kernel.h"

namespace nf {
// which exe this is (Driving.xbe): src/common's key layouts and settings tables follow it
extern const bool kDriving = true;

// EA's in-game menus (Paused and its tabs, Mission Failed, the controller-removed menu): the HUD's byte +0x2B0, set
// by 0xDCAE0 (0xDCBBB) when one opens (START 0xE0278, a pad removed 0xE00F0, the mission's end 0xDF2E0) and cleared
// at 0xD9091 (.local/driving_input_re.md)
bool MenuGeometry(uint32_t&, std::vector<MenuWidget>&, uint32_t&) { return false; }

bool MenuUp() {
    uint32_t hud = kernel::R32(0x23F44C);
    return hud && kernel::G<uint8_t>(hud + 0x2B0) != 0;
}

// the menus show Enter and Backspace for A and B, but for the pause's CONTROLS tab (the HUD's page +0x470: MISSION 0,
// OBJECTIVES 1, CONTROLS 2, SCORE 3), whose lines name the keys played
bool MenuKeys() {
    uint32_t hud = kernel::R32(0x23F44C);
    return MenuUp() && kernel::R32(hud + 0x470) != 2;
}

// the control map (0x1E455C): +4 its mode, 1 a mounted gun (data\control\povcfgN.def), 0 the car (drivecfg.def), 2 none
// yet; +0xC the gun's style (the player's controller style)
uint64_t NetStateHash() { return 0; }  // no LAN play in the driving levels
bool PlayerFree() { return true; }  // default.xbe's questions (src/game/mouse.cpp)
bool Zoomed() { return false; }
void ScopeZoom(int) {}
bool ViewTurn(float&, float&, float&) { return false; }  // the driving levels turn by the stick: no in-between frames

int GunStyle() {
    if (kernel::R32(0x1E455C + 4) != 1 || MenuUp()) return -1;
    return int(kernel::R32(0x1E455C + 0xC) & 7);
}
}  // namespace nf
