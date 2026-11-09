// Thrown weapons (the weapon table's 52-56: the grenades). They leave through the round spawner 0x211F0 like a shot
// (src/game/lan_play.cpp hooks it); the new object's data ([obj+0xBC]) has the speed at +0xD0 (the weapon's +0x20).
// With Tighter Movement (nf::TightMovement) the grenades fly 1.5x as fast and go off after 70% of the fuse.
// NF_THROW_TRACE=1 (diagnostic): logs each thrown object's data at the throw and its place every frame after.
#include <cstdlib>

#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;
uint32_t g_traced = 0, g_frames = 0;
bool Trace() {
    static const bool on = std::getenv("NF_THROW_TRACE") != nullptr;
    return on;
}
}  // namespace

namespace {
// the grenade's flight (class 5's think, 0x23670) integrates its velocity with the global gravity vector at 0x1F6648
// (y at 0x1F664C, the same one Tighter Movement scales for players); the thrown objects are kept here to scale it
constexpr uint32_t kGravityY = 0x1F664C;
constexpr float kGrenadeGravity = 1.6f;
constexpr int kKept = 8;
uint32_t g_thrown[kKept] = {};
int g_next = 0;
bool IsThrown(uint32_t obj) {
    for (uint32_t t : g_thrown)
        if (t && t == obj) return true;
    return false;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00023670);
X86_FUNC(sub_00023670) {  // a thrown grenade's think (cdecl: the object)
    const uint32_t obj = R32(ctx.esp + 4);
    const bool firmer = nf::TightMovement() && obj && IsThrown(obj);
    const float gravity = kernel::G<float>(kGravityY);
    if (firmer) kernel::G<float>(kGravityY) = gravity * kGrenadeGravity;
    __imp__sub_00023670(ctx, base);
    kernel::G<float>(kGravityY) = gravity;
}

namespace nf {
bool Thrown(uint32_t entry) {
    const uint16_t id = kernel::G<uint16_t>(entry);
    return id >= 52 && id <= 56;
}

void ThrowSpawned(uint32_t entry, uint32_t obj) {
    if (!obj || !Thrown(entry)) return;
    const uint32_t data = R32(obj + 0xBC);
    // Tighter Movement: a quicker throw and a shorter fuse (+0xD4 counts down a frame at a time, 210 = 3.5 s) for the
    // grenades 52-55; the remote mine 56 stays as it is
    if (data && kernel::G<uint16_t>(entry) <= 55 && TightMovement()) {
        kernel::G<float>(data + 0xD0) *= 1.5f;
        kernel::G<float>(data + 0xD4) *= 0.7f;
        g_thrown[g_next] = obj, g_next = (g_next + 1) % kKept;
    }
    if (!Trace()) return;
    LOGR("nightfire: throw: weapon %u obj %08X class %u data %08X", kernel::G<uint16_t>(entry), obj,
         kernel::G<uint8_t>(obj + 0xDB), data);
    for (uint32_t o = 0; o < 0xE0 && data; o += 0x10)
        LOGR("nightfire: throw: data +%02X %08X %08X %08X %08X", o, R32(data + o), R32(data + o + 4),
             R32(data + o + 8), R32(data + o + 12));
    g_traced = obj, g_frames = 0;
}

void ThrowFrame() {
    if (!Trace() || !g_traced) return;
    const uint32_t data = R32(g_traced + 0xBC);
    if (data < 0x10000 || kernel::G<uint8_t>(g_traced + 0xDB) != 5) {  // gone off: the object freed or reused
        g_traced = 0;
        return;
    }
    LOGR("nightfire: throw: frame %u pos %.3f %.3f %.3f d0 %.3f d4 %.3f state %u", g_frames,
         kernel::G<float>(g_traced + 0x24), kernel::G<float>(g_traced + 0x28), kernel::G<float>(g_traced + 0x2C),
         data ? kernel::G<float>(data + 0xD0) : 0.0f, data ? kernel::G<float>(data + 0xD4) : 0.0f,
         kernel::G<uint16_t>(g_traced + 0xD0));
    if (++g_frames > 300) g_traced = 0;
}
}  // namespace nf
