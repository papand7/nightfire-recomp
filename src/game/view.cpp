// Ultrawide and the field of view on foot (Eurocom's cameras).
//
// A camera's projection is set by 0x25190 (cdecl: camera index into 0x1F661C, vertical field of view in radians):
// the angle at +0x104, the aspect at +0x100 (16:9 with the widescreen flag 0x1F6610, else 4:3; halved or doubled for
// a split screen's viewports) and pixel scales at +0xEC (x) and +0xF0 (y) from the angle. The D3D projection (0xDD950)
// and the culling planes (0xDB5C0, the globals 0x29EA60.. and the camera's +0x6C) are both built from +0x100 and
// +0x104, so both get the widened aspect and the scaled angle for the call alone: what is drawn and what is culled
// agree, and the fields stay the game's (the scope's zoom adds to +0x104 and sets it again, a cutscene camera reads
// +0x100). Ultrawide leaves the pixel scales alone: the HUD markers (0xDB230) and the aim ray (0xB85A0) are laid out
// in the game's 16:9 space, which the runtime squeezes into the middle of the frame as the 3D widens around it. The
// field of view changes the angle itself, so they follow it (r = tan(fov/2) over tan(scaled/2)).
// (.local/fov_re.md has the addresses each read was found at.)
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;

constexpr uint32_t kCameras = 0x1F661C;  // the cameras by index (0-3 the players, 4 the scripted one)
constexpr uint32_t kWidescreen = 0x1F6610;
constexpr uint32_t kAspect = 0x100, kFov = 0x104, kScaleX = 0xEC, kScaleY = 0xF0;

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

float Scaled(float fov) { return std::min(fov * nf::FovScale(), 2.9f); }  // short of 180 degrees

// the camera at [esp+4] widened and its angle scaled while call runs (a split view's own shape is the game's: 0x25190
// doubles or halves the aspect while two players' cameras exist)
template <class Fn>
void Widened(uint32_t cam, Fn call) {
    float k = R32(kWidescreen) ? nf::Ultrawide() : 1.0f, s = nf::FovScale();
    if (!cam || (std::abs(k - 1.0f) <= 1e-3f && s == 1.0f)) return call();
    float aspect = GetF(cam + kAspect), fov = GetF(cam + kFov);
    SetF(cam + kAspect, aspect * k);
    SetF(cam + kFov, Scaled(fov));
    call();
    SetF(cam + kAspect, aspect);
    SetF(cam + kFov, fov);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00025190);
X86_FUNC(sub_00025190) {
    uint32_t i = R32(ctx.esp + 4) & 0xFFFF;
    float fov = GetF(ctx.esp + 8);
    __imp__sub_00025190(ctx, base);
    uint32_t cam = i < 9 ? R32(kCameras + 4 * i) : 0;
    // story co-op: the cutscene camera (4, full screen) given a split view's aspect while two players' cameras exist
    if (int split = nf::CoopSplit(); split && i >= 4 && cam) {
        float f = split == 1 ? 2.0f : 0.5f;
        SetF(cam + kAspect, GetF(cam + kAspect) / f);
        SetF(cam + kScaleX, GetF(cam + kScaleX) * f);
    }
    if (!cam || GetF(cam + kFov) != fov || nf::FovScale() == 1.0f) return;  // not set (no camera, no size)
    float r = std::tan(fov / 2) / std::tan(Scaled(fov) / 2);
    SetF(cam + kScaleX, GetF(cam + kScaleX) * r);
    SetF(cam + kScaleY, GetF(cam + kScaleY) * r);
}

X86_EXTERN_FUNC(__imp__sub_000DD950);
X86_FUNC(sub_000DD950) {  // the D3D projection
    Widened(R32(ctx.esp + 4), [&] { __imp__sub_000DD950(ctx, base); });
}

X86_EXTERN_FUNC(__imp__sub_000DB5C0);
X86_FUNC(sub_000DB5C0) {  // the culling planes
    Widened(R32(ctx.esp + 4), [&] { __imp__sub_000DB5C0(ctx, base); });
    nf::FarPlane();
}
