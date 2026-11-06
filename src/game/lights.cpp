// the game's lights for the renderer's ray traced level lighting (gpu::SetSceneLights). The level's light entities
// (spawner 0xA4BC0, entity 0x4E: radius, colour, flicker, strobe) and passing ones (muzzle flashes, effects) are made
// by 0x6D530 into 0x40 byte records, a list from [0x21570C] through +0x04; 0x6D660 eases their flicker and lifetime
// once a frame. A record: +0x08 the place (3 floats, the world's), +0x14 frames left (u16, 0 placed for good), +0x30
// the radius, +0x34 the intensity, +0x38 red, green, blue (bytes), +0x3B its kind (2 lights its owner alone), +0x3C
// on this frame. The game itself lights only characters and things that move with them (four each, 0x6D8F0); the
// level's own light is baked into its vertex colours.
#include <cstdint>

#include "common/nightfire.h"
#include "core/log.h"
#include "gpu/style.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kLightList = 0x21570C;
constexpr uint32_t kMaxLights = 256;
}  // namespace

X86_EXTERN_FUNC(__imp__sub_0006D660);
X86_FUNC(sub_0006D660) {  // the lights' flicker and lifetime, once a frame
    __imp__sub_0006D660(ctx, base);
    gpu::SceneLight out[kMaxLights];
    uint32_t n = 0, guard = 0;
    for (uint32_t p = kernel::G<uint32_t>(kLightList); p && n < kMaxLights && guard < 4096; guard++) {
        uint32_t next = kernel::G<uint32_t>(p + 0x04);
        uint8_t kind = kernel::G<uint8_t>(p + 0x3B), on = kernel::G<uint8_t>(p + 0x3C);
        float radius = kernel::G<float>(p + 0x30), power = kernel::G<float>(p + 0x34);
        if (on && kind != 2 && radius > 0 && power > 0) {
            gpu::SceneLight& l = out[n++];
            for (int i = 0; i < 3; i++) {
                l.pos[i] = kernel::G<float>(p + 0x08 + 4 * i);
                l.rgb[i] = kernel::G<uint8_t>(p + 0x38 + i) * power / 256.0f;
            }
            l.radius = radius;
            l.lifetime = float(kernel::G<uint16_t>(p + 0x14));
        }
        p = next;
    }
    gpu::SetSceneLights(out, n);
    static uint32_t said = 0;
    if (said < 3 && n) {
        said++;
        const gpu::SceneLight& l = out[0];
        LOGR("nightfire: %u lights; the first at %.1f %.1f %.1f, radius %.1f, colour %.2f %.2f %.2f, lifetime %.0f", n,
             l.pos[0], l.pos[1], l.pos[2], l.radius, l.rgb[0], l.rgb[1], l.rgb[2], l.lifetime);
    }
}
