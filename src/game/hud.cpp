// hide_hud on foot: the HUD is each player view's 2D sprite list (the cameras at 0x1F661C): the ammo and weapon, the
// health bar, the timer, scores, name tags and hints. The frame's 2D pass (0xDAC20) draws each view's list through
// 0xD9410 (cdecl: the view, the list, first and end index), sprites itself and text through 0x6A340. The menus and
// the pause screen are widgets drawn apart; while one is up (MenuUp) the lists are drawn as the game has them.
#include <cstdint>

#include "common/nightfire.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace nf {
void HitTest(X86Context& ctx);  // hit_feedback.cpp
}

X86_EXTERN_FUNC(__imp__sub_000DAC20);
X86_FUNC(sub_000DAC20) {  // the frame's 2D pass
    nf::HitTest(ctx);
    nf::PhotoBeforePass(ctx);  // photo mode's free camera for the pass (src/game/photo_mode.cpp)
    __imp__sub_000DAC20(ctx, base);
    nf::PhotoAfterPass();
}

X86_EXTERN_FUNC(__imp__sub_000D9410);
X86_FUNC(sub_000D9410) {
    // modern menus: the game's own widgets not drawn (theirs over the background movie)
    // photo mode: none of it (the HUD, the pause screen's widgets)
    if ((nf::hide_hud && !nf::MenuUp()) || nf::ModernHidesClassic() || nf::PhotoActive()) {
        ctx.esp += 4;  // cdecl: the return address only
        return;
    }
    __imp__sub_000D9410(ctx, base);
}
