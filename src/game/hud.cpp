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

namespace {
// a player looking through a scope (a weapon with the table's scoped flag, +0x68 bit 0x40, zoomed in: the camera's
// divisor at actor +0x860): the scope's picture is one of the HUD's sprites, so hide_hud leaves the HUD up then
bool AnyScoped() {
    for (uint32_t p = 0; p < 4; p++) {
        const uint32_t obj = kernel::R32(0x1F6654 + 4 * p);
        if (obj < 0x10000 || (obj & 0x7FFFFFFF) >= 0x08000000) continue;
        const uint32_t actor = kernel::R32(obj + 0xBC), state = kernel::R32(obj + 0xB8);
        if (actor < 0x10000 || state < 0x10000) continue;
        const int idx = kernel::G<int8_t>(state + 0x52);
        if (idx < 0 || idx >= 0x73) continue;
        const uint32_t weapon = 0x18CFA0 + uint32_t(idx) * 0x10C;
        if ((kernel::R32(weapon + 0x68) & 0x40) && kernel::G<float>(actor + 0x860) > 1.05f) return true;
    }
    return false;
}
}  // namespace

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
    // hide_hud: not while a scope is up (its picture is a HUD sprite: without it a sniper cannot aim)
    if ((nf::hide_hud && !nf::MenuUp() && !AnyScoped()) || nf::ModernHidesClassic() || nf::PhotoActive()) {
        ctx.esp += 4;  // cdecl: the return address only
        return;
    }
    __imp__sub_000D9410(ctx, base);
}
