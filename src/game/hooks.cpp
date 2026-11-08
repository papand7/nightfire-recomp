// Hooks for default.xbe (the on-foot levels). A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }

#include "common/nightfire.h"
#include "kernel/kernel.h"

namespace nf {
// which exe this is (default.xbe): src/common's key layouts and settings tables follow it
extern const bool kDriving = false;

// Eurocom's menu context for player 1 (0x25F1D0, 0x1D8 bytes a player): +0x1B8 the page shown (0x40000034 at the
// title, 0x400000xx in the front end and the pause menu), +0x1BC its widget, none in play
// (photo mode: none, though the game's pause screen stays up under it)
bool MenuUp() { return kernel::R32(0x25F1D0 + 0x1BC) != 0 && !PhotoActive(); }
bool MenuKeys() { return MenuUp() && kernel::R32(0x25F1D0 + 0x1B8) != 0x4000004B; }  // the pause page (0xAD520)
int GunStyle() { return -1; }  // the mounted guns are Driving.xbe's
}
