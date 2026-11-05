// The number keys 1 to 9 pick a weapon, as in Counter-Strike: the Nth weapon Bond carries, in the game's own order
// (the one B / Q cycles through).
//
// The cycle (0xBA280: the object, gadgets or weapons, the step) walks the weapon table (0x18CFA0, 0x10C bytes an
// entry) from 1 to 0x47: an entry whose +4 is 1 is a weapon of its own (not a variant of another), carried when the
// actor's +0x15E + index * 12 is set; +0x160 there is the variant it is in (added to the index), and 0xB9EB0 (the
// actor, an index) whether that one can be drawn (its ammo). The one chosen goes to the weapon state's +0x53, which
// the gun's state machine then draws, as after a cycle.
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "app/app.h"
#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;

constexpr uint32_t kWeapons = 0x18CFA0, kWeaponSize = 0x10C, kLastWeapon = 0x47, kCanDraw = 0xB9EB0;
constexpr uint32_t kActor = 0xBC, kWeaponState = 0xB8, kCarried = 0x15E, kVariant = 0x160, kRequest = 0x53;

bool KeyHeld(int vk) {
#ifdef _WIN32
    return GetForegroundWindow() == static_cast<HWND>(app::Window()) && (GetAsyncKeyState(vk) & 0x8000);
#else
    (void)vk;
    return false;
#endif
}

// NF_SLOT_TEST=1 (diagnostic): the keys 1 to 9 pressed in turn, one every 150 frames
int TestSlot() {
    static const bool on = [] {
        const char* e = std::getenv("NF_SLOT_TEST");
        return e && *e == '1';
    }();
    if (!on) return 0;
    static uint32_t frames = 0;
    return ++frames % 150 == 0 ? int(frames / 150 - 1) % 9 + 1 : 0;
}
}  // namespace

namespace nf {

// once a frame for player 1's object (src/game/aim.cpp, after its gun was placed)
void WeaponKeys(X86Context& ctx, uint32_t obj) {
    static bool was[10];
    int slot = TestSlot();
    for (int k = 1; k <= 9; k++) {
        bool down = KeyHeld('0' + k);
        if (down && !was[k]) slot = k;
        was[k] = down;
    }
    if (!slot || MenuUp()) return;
    uint32_t actor = R32(obj + kActor), state = R32(obj + kWeaponState);
    if (!actor || !state) return;
    std::vector<uint32_t> carried;
    uint32_t eax = ctx.eax;
    for (uint32_t i = 1; i <= kLastWeapon; i++) {
        if (kernel::G<uint8_t>(kWeapons + i * kWeaponSize + 4) != 1 || !kernel::G<uint8_t>(actor + kCarried + i * 12))
            continue;
        uint32_t pick = i + uint32_t(kernel::G<int8_t>(actor + kVariant + i * 12));
        if (!(kernel::CallGuest(ctx, kCanDraw, {actor, pick}) & 0xFF)) {
            if (pick == i || !(kernel::CallGuest(ctx, kCanDraw, {actor, i}) & 0xFF)) continue;
            pick = i;
        }
        carried.push_back(pick);
    }
    ctx.eax = eax;
    if (std::getenv("NF_SLOT_TEST")) {
        std::string list;
        for (uint32_t w : carried) list += " " + std::to_string(w);
        LOGR("nightfire: weapon key %d: carried%s; holding %d", slot, list.c_str(), kernel::G<int8_t>(state + 0x52));
    }
    if (size_t(slot) <= carried.size()) kernel::G<int8_t>(state + kRequest) = int8_t(carried[slot - 1]);
}
}  // namespace nf
