// LAN play (work in progress): remote players as the match's bot characters, driven over the network instead of by
// their AI ("puppets").
//
// A multiplayer match (0xA25F0) makes the humans (up to 4, [0x260044]) and the bots ([0x260048], clamped to 6): bot
// i is score slot 4+i, its character object at 0x262814 + i*0x30 (the score table 0x262738, 0x30 a slot, +0x1C the
// object). Each frame 0x2DD00 calls a character's think by its class (object +0xDB): 3 a player's (0xAD520), 2 an
// NPC's or bot's (0x31530, cdecl: the object). A puppet's think runs (a bot's body is spawned and animated by its AI:
// skipped, none was drawn) with no target (0x37C10), so it never aims or fires; then its position (object +0x24,
// +0x28, +0x2C, metres, +0x28 up) and heading (+0x40, radians) are set from outside.
//
// NF_LAN_TEST=1 (diagnostic): matches get one bot (slot 4), a puppet standing in front of player 1, facing them.
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;
using kernel::W32;

constexpr uint32_t kHumans = 0x260044, kBots = 0x260048;
constexpr uint32_t kScores = 0x262738, kSlotSize = 0x30, kSlotObject = 0x1C;
constexpr uint32_t kPos = 0x24, kYaw = 0x40;

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

bool Test() {
    static const bool on = [] {
        const char* e = std::getenv("NF_LAN_TEST");
        return e && *e == '1';
    }();
    return on;
}

uint32_t SlotObject(int slot) { return R32(kScores + uint32_t(slot) * kSlotSize + kSlotObject); }

// the score slot whose character obj is, else -1
int SlotOf(uint32_t obj) {
    for (int s = 0; s < 10; s++)
        if (obj && SlotObject(s) == obj) return s;
    return -1;
}

}  // namespace
X86_EXTERN_FUNC(__imp__sub_00031530);
namespace {
bool Puppet(int slot) { return Test() && slot == 4; }

// a puppet's frame: where it stands and faces
void Drive(int slot, uint32_t obj) {
    (void)slot;
    uint32_t me = SlotObject(0);
    if (!me) return;
    float yaw = GetF(me + kYaw), d = 3.0f;
    SetF(obj + kPos + 0, GetF(me + kPos + 0) + std::sin(yaw) * d);
    SetF(obj + kPos + 4, GetF(me + kPos + 4));
    SetF(obj + kPos + 8, GetF(me + kPos + 8) + std::cos(yaw) * d);
    SetF(obj + kYaw, yaw + 3.14159265f);
    static int n = 0;
    if (++n % 120 == 1)
        LOGR("nightfire: LAN test: player %.1f %.1f %.1f yaw %.2f (+3C %.2f +44 %.2f), puppet %.1f %.1f %.1f",
             GetF(me + kPos), GetF(me + kPos + 4), GetF(me + kPos + 8), yaw, GetF(me + 0x3C), GetF(me + 0x44),
             GetF(obj + kPos), GetF(obj + kPos + 4), GetF(obj + kPos + 8));
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000A25F0);
X86_FUNC(sub_000A25F0) {  // a multiplayer match starts
    if (Test()) {
        W32(kBots, 1);
        LOGR("nightfire: LAN test: %u humans, 1 bot (a puppet)", R32(kHumans));
    }
    __imp__sub_000A25F0(ctx, base);
}

X86_FUNC(sub_00031530) {  // an NPC's or bot's think (cdecl: the object)
    uint32_t obj = R32(ctx.esp + 4);
    int slot = Test() ? SlotOf(obj) : -1;
    if (slot >= 0 && Puppet(slot)) {
        static bool said = false;
        if (!said) said = true, LOGR("nightfire: LAN: slot %d is a puppet (object %08X)", slot, obj);
        // the AI's own frame (it spawns the body and animates it; 0x37C10 below gives it no target), then moved
        __imp__sub_00031530(ctx, base);
        Drive(slot, obj);
        return;
    }
    __imp__sub_00031530(ctx, base);
}

// a bot's choice of target (0x37C10, cdecl: the actor; MP only): a puppet has none, as a dead bot's (0x37A10(actor,
// 0) drops the one it had): its AI's frame still runs (spawning, animation) but never aims or fires
X86_EXTERN_FUNC(__imp__sub_00037C10);
X86_FUNC(sub_00037C10) {
    uint32_t actor = R32(ctx.esp + 4);
    int slot = Test() && actor ? SlotOf(R32(actor + 0x0C)) : -1;
    if (slot >= 0 && Puppet(slot)) {
        if (R32(actor + 0x148)) kernel::CallGuest(ctx, 0x37A10, {actor, 0});
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    __imp__sub_00037C10(ctx, base);
}
