// Draw distance (nf::draw_distance, percent of the game's) and the characters' shadows under ray traced lighting.
//
// On foot NightFire has no model LOD and no distance cull (.local/lod_notes.md): how far one sees is the level's fog,
// set by 0xDDB00 (enable, start, end, r, g, b; start and end per level, as immediates) through the setters 0xE3E60
// (start) and 0xE3F00 (end) (cdecl, the float at [esp+4], into vertex shader constant 0x66), and the camera's own
// room's portals cut at 200 units (the frustum's fifth plane, built by 0xDB5C0: its d at 0x29EAAC, the normal toward
// the camera, unit length). Both are scaled here: the fog's start and end (not the sky pass's own fog, 0xD9D60's calls,
// nor the levels whose dense fog is the scene: an end under 60), the far plane by as much. The character shadow
// (0xE04E0) is drawn within 35 units (dist² 1225 at 0x161DB8; the detailed one within 20, 400 at 0x161DB4): scaled
// too. The D3D far plane (3000) and the near plane stay.
//
// The blob shadow under each character (0xE04E0, cdecl, 5 arguments) is left out while ray traced lighting lights
// the level: the characters are in its scene and cast their shadows from the sun and lamps there.
#include <algorithm>
#include <cstring>

#include "common/nightfire.h"
#include "core/log.h"
#include "gpu/style.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;

constexpr uint32_t kLevel = 0x1F658C, kFarPlaneD = 0x29EAAC, kShadowFar = 0x161DB8, kShadowNear = 0x161DB4;
constexpr float kFarPlane = 200.0f;

float Scale() { return std::clamp(float(nf::draw_distance), 100.0f, 300.0f) / 100.0f; }
float GetF(uint32_t va) { return kernel::G<float>(va); }
void SetF(uint32_t va, float v) { kernel::G<float>(va) = v; }

bool g_sky_fog = false;    // 0xDDB00 called from the sky pass (its fog as authored)
float g_level_end = 1e9f;  // the level's fog end as set (unscaled)

// a fog setter's float argument scaled for the call
template <typename F>
void Scaled(X86Context& ctx, F&& call, bool end) {
    const uint32_t at = ctx.esp + 4;
    const float v = GetF(at), k = Scale();
    if (end && !g_sky_fog) g_level_end = v;
    const bool dense = g_level_end < 60.0f;  // the fog is the scene (a smoke-filled or blizzard level)
    if (k == 1.0f || g_sky_fog || dense) return call();
    SetF(at, v * k);
    call();
    SetF(at, v);
}

void ShadowRange() {
    static float done = 0;
    const float k = Scale();
    if (done == k) return;
    done = k;
    SetF(kShadowFar, 1225.0f * k * k);
    SetF(kShadowNear, 400.0f * k * k);
    LOGR("nightfire: draw distance %.0f%% (fog and the far plane scaled; shadows within %.0f units)", k * 100, 35 * k);
}
}  // namespace

// the far plane moved out with the fog (view.cpp's culling planes override calls this after the original)
void nf::FarPlane() {
    const float k = Scale();
    if (k != 1.0f) SetF(kFarPlaneD, GetF(kFarPlaneD) + (k - 1.0f) * kFarPlane);
}

X86_EXTERN_FUNC(__imp__sub_000DDB00);
X86_FUNC(sub_000DDB00) {  // the level's fog (enable, start, end, r, g, b)
    const uint32_t from = R32(ctx.esp);
    g_sky_fog = from >= 0xD9D60 && from < 0xDA200;  // the sky pass's own (0xD9D60)
    ShadowRange();
    __imp__sub_000DDB00(ctx, base);
    g_sky_fog = false;
}

X86_EXTERN_FUNC(__imp__sub_000E3E60);
X86_FUNC(sub_000E3E60) {  // the fog's start
    Scaled(ctx, [&] { __imp__sub_000E3E60(ctx, base); }, false);
}

X86_EXTERN_FUNC(__imp__sub_000E3F00);
X86_FUNC(sub_000E3F00) {  // the fog's end
    Scaled(ctx, [&] { __imp__sub_000E3F00(ctx, base); }, true);
}

X86_EXTERN_FUNC(__imp__sub_000E04E0);
X86_FUNC(sub_000E04E0) {  // a character's blob shadow (cdecl, 5 arguments)
    const gpu::GraphicsStyle& st = gpu::Style();
    if (st.ray_tracing && st.rt_lighting && gpu::RayTracingSupported()) {
        ctx.esp += 4;  // its return (the caller takes the arguments off)
        return;
    }
    __imp__sub_000E04E0(ctx, base);
}
