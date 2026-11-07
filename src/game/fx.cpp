// GPU particles (gpu::EmitParticles, GraphicsStyle::particles): the game's effects handed to the renderer, which
// simulates and draws its own sparks, dust, chips, fire and smoke. While they are drawn (Ours) the game's sprites they
// stand for are made invisible as they are made (Hide: no size, no colour; the object lives on, as its callers keep it)
// - 0x690A0 (cdecl, 12 args): an explosion: the owner (an object: its +0x20 read), its place (a pointer, [esp+8]), a
//   direction (0x29D71C), two radii (floats, [esp+0x10], [esp+0x14], world units: metres), the effect (0x6000052), a
//   value kept at the explosion's data +0x20, 0xFF, 0xFF, 0x64, the shooter, 0 (the rocket's call at 0x21966)
// - 0x70430 (cdecl: the hit, the object, flags, a scale): a shot's mark on a surface. The hit: +0x10 the surface's
//   normal, +0x38 the place, +0x44 the material (& 0x3F; 0x0D and 0x0E water). Flags 0x20 a decal first, then one of:
//   1 0x6FFB0 (a sound if 0x40, up to three puffs from the material's table through 0xCD140, returning at 0x70151),
//   2 a tracer, 0x10 0x6FC60 (a sound if 0x40, a puff at 0x6FD49, sometimes a 3D chunk, on odd frames a glint
//   0x2000039 at 0x6FEA4), 0x80 0x6FC10. A hit with 1 or 0x10 makes sparks, dust and chips here (not on water)
// - 0xCD140 (cdecl, 11 args): a sprite particle (the room, place*, turn*, velocity*, sizes from and to (floats), the
//   texture, kind, colours from and to, frames to live); returns the object (+0xC4 its size, +0xDF-0xE2 its colour;
//   data at +0xBC: +0x0C the colour's steps, +0x1C the colour (floats), +0x30 the size's step, +0x34 its life). The
//   smoke (texture 0x200001F: the rockets' trails 0x21680, 0x217F0, 0x21870) and fireball (0x20008BC, 0x200026E:
//   0x36430, a character hit by a rocket or flame) ones add a puff or a flame of the renderer's now and then and are
//   hidden, as are the hits' puffs (0x6FFB0's but water's splash, 0x6FC60's); the glints (0x2000039), the water's
//   splash (0x2000129), a character's puffs (0x200021D, 0x68DB0) are the game's
// NF_FX_TEST=1 (diagnostic): every third hit of the gun an explosion (0x690A0) there, the shooter its owner; each
// sprite's caller, texture and sizes logged the first time
// The places are the world's, the lights' (lights.cpp). Nothing registers: these override the generated functions.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/log.h"
#include "gpu/style.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;
float F32(uint32_t a) { return kernel::G<float>(a); }

bool FxTest() {
    static const bool on = [] {
        const char* e = std::getenv("NF_FX_TEST");
        return e && *e == '1';
    }();
    return on;
}

// the renderer draws its particles in place of the game's sprites (as VulkanBackend::ParticlesWanted)
bool Ours() {
    const gpu::GraphicsStyle& st = gpu::Style();
    return st.particles && st.mode != gpu::kStyleRetro && gpu::ViewCount() < 2 && gpu::StyleRendererActive();
}

// a sprite made invisible and kept so: no size, no colour (alpha blended, added or multiplied alike)
void Hide(uint32_t obj) {
    if (obj < 0x10000) return;
    kernel::G<float>(obj + 0xC4) = 0;
    for (uint32_t i = 0; i < 4; i++) kernel::G<uint8_t>(obj + 0xDF + i) = 0;
    const uint32_t data = R32(obj + 0xBC);
    if (data < 0x10000) return;
    for (uint32_t i = 0; i < 4; i++) kernel::G<float>(data + 0x0C + 4 * i) = 0, kernel::G<float>(data + 0x1C + 4 * i) = 0;
    kernel::G<float>(data + 0x30) = 0;
}

int g_hit_material = -1;  // the hit 0x70430 is making (its material), else -1

bool Water(uint32_t m) { return m == 0x0D || m == 0x0E; }

bool Place(uint32_t p, float out[3]) {
    if (p < 0x10000) return false;
    for (int i = 0; i < 3; i++) {
        out[i] = F32(p + 4 * i);
        if (!std::isfinite(out[i]) || std::abs(out[i]) > 1e6f) return false;
    }
    return true;
}

// a surface's dust by its material (the game's material numbers, guessed from the levels: low numbers stone and
// concrete, then wood, metal, glass, earth): a grey-brown else
void MaterialDust(uint32_t m, float rgb[3], bool& metal) {
    metal = false;
    static const float kinds[][3] = {{0.55f, 0.53f, 0.50f}, {0.50f, 0.40f, 0.28f}, {0.45f, 0.45f, 0.47f}, {0.42f, 0.34f, 0.24f}};
    const float* c = kinds[m % 4];
    for (int i = 0; i < 3; i++) rgb[i] = c[i];
    metal = m % 4 == 2;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000690A0);
X86_FUNC(sub_000690A0) {  // an explosion
    float pos[3];
    const uint32_t at = R32(ctx.esp + 8);
    const float radius = F32(ctx.esp + 0x14);
    if (Place(at, pos) && std::isfinite(radius) && radius > 0) {
        const float up[3] = {0, 1, 0}, rgb[3] = {1.0f, 0.55f, 0.2f};
        gpu::EmitParticles(gpu::kParticleExplosion, pos, up, std::min(radius, 50.0f), rgb);
        static int said = 0;
        if (said++ < 4) LOGR("fx: an explosion at %.1f %.1f %.1f, radius %.1f", pos[0], pos[1], pos[2], radius);
    }
    __imp__sub_000690A0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00070430);
X86_FUNC(sub_00070430) {  // a shot's mark on a surface
    const uint32_t hit = R32(ctx.esp + 4), obj = R32(ctx.esp + 8), flags = R32(ctx.esp + 0xC);
    float pos[3], n[3];
    const uint32_t m = hit >= 0x10000 ? kernel::G<uint8_t>(hit + 0x44) & 0x3F : 0;
    const bool placed = hit >= 0x10000 && Place(hit + 0x38, pos);
    if ((flags & 0x11) && placed && !Water(m) && Place(hit + 0x10, n)) {
        float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-3f) {
            for (float& v : n) v /= len;
            float rgb[3];
            bool metal;
            MaterialDust(m, rgb, metal);
            gpu::EmitParticles(gpu::kParticleImpact, pos, n, metal ? 1.5f : 1.0f, rgb);
            static int said = 0;
            if (said++ < 6)
                LOGR("fx: a hit at %.1f %.1f %.1f, normal %.2f %.2f %.2f, material %u, flags 0x%X", pos[0], pos[1], pos[2],
                     n[0], n[1], n[2], m, flags);
        }
    }
    const int was = g_hit_material;
    g_hit_material = hit >= 0x10000 ? int(m) : -1;
    __imp__sub_00070430(ctx, base);
    g_hit_material = was;
    // NF_FX_TEST: an explosion where every third hit lands (the hit's object its owner), not within 6 m of the
    // object's place (+0x24) when it has one
    static uint32_t hits = 0;
    float at[3] = {};
    if (FxTest() && (flags & 0x11) && placed && obj >= 0x10000 && ++hits % 3 == 0) {
        const bool near_obj = Place(obj + 0x24, at) && (pos[0] - at[0]) * (pos[0] - at[0]) + (pos[1] - at[1]) *
                                                        (pos[1] - at[1]) + (pos[2] - at[2]) * (pos[2] - at[2]) < 36;
        if (!near_obj) {
            const uint32_t eax = ctx.eax, r = std::bit_cast<uint32_t>(3.0f);
            kernel::CallGuest(ctx, 0x690A0, {obj, hit + 0x38, 0x29D71C, r, r, 0x6000052, 0, 0xFF, 0xFF, 0x64, 0, 0});
            ctx.eax = eax;
        }
        LOGR("fx: test explosion at %.1f %.1f %.1f: %s (the object 0x%X, at %.1f %.1f %.1f)", pos[0], pos[1], pos[2],
             near_obj ? "too near" : "made", obj, F32(obj + 0x24), F32(obj + 0x28), F32(obj + 0x2C));
    }
}

X86_EXTERN_FUNC(__imp__sub_000CD140);
X86_FUNC(sub_000CD140) {  // a sprite particle
    const uint32_t from = R32(ctx.esp), tex = R32(ctx.esp + 0x1C);
    const bool smoke = tex == 0x200001F, fire = tex == 0x20008BC || tex == 0x200026E;
    // the hits' puffs ours stand for: 0x6FFB0's (not on water) and 0x6FC60's, but a splash or a glint
    const bool on_water = g_hit_material >= 0 && Water(uint32_t(g_hit_material));
    const bool puff = (from == 0x70151 && !on_water) || from == 0x6FD49;
    const bool hide = Ours() && (smoke || fire || (puff && tex != 0x2000129 && tex != 0x2000039));
    if (smoke || fire) {
        static uint32_t count = 0;
        float pos[3];
        const float size = F32(ctx.esp + 0x18);
        if (++count % (smoke ? 3 : 2) == 0 && Place(R32(ctx.esp + 8), pos) && std::isfinite(size)) {
            const float up[3] = {0, 1, 0}, grey[3] = {0.5f, 0.5f, 0.5f}, flame[3] = {1.0f, 0.6f, 0.25f};
            float s = std::fmin(std::fmax(std::abs(size), 0.2f), 6.0f);
            gpu::EmitParticles(smoke ? gpu::kParticleSmoke : gpu::kParticleFire, pos, up, s, smoke ? grey : flame);
            static int said = 0;
            if (said++ < 4) LOGR("fx: a %s sprite at %.1f %.1f %.1f, size %.2f (from 0x%X)%s", smoke ? "smoke" : "fire",
                                 pos[0], pos[1], pos[2], size, from, hide ? ", the game's hidden" : "");
        }
    }
    if (FxTest()) {  // each caller and texture once
        static std::vector<uint64_t> seen;
        const uint64_t key = uint64_t(from) << 32 | tex;
        if (seen.size() < 64 && std::find(seen.begin(), seen.end(), key) == seen.end()) {
            seen.push_back(key);
            LOGR("fx: sprite from 0x%X: texture 0x%X kind %u, size %.2f to %.2f, colour %08X to %08X, %u frames, "
                 "material %d%s", from, tex, R32(ctx.esp + 0x20), F32(ctx.esp + 0x14), F32(ctx.esp + 0x18),
                 R32(ctx.esp + 0x24), R32(ctx.esp + 0x28), R32(ctx.esp + 0x2C), g_hit_material, hide ? " (hidden)" : "");
        }
    }
    __imp__sub_000CD140(ctx, base);
    if (hide) Hide(ctx.eax);
}
