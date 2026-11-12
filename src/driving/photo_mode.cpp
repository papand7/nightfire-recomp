// Photo mode in the driving levels (auf::photo_mode): F10 stops the game and frees the camera: the mouse looks, W A S D
// move, Space and C go up and down, Shift faster, Ctrl slower, [ and ] the field of view, , . N M B R depth of field as on foot; the HUD is hidden, so F12 (the
// screenshot key) takes the picture. F10 again leaves it.
//
// The camera object ([view+4], view the ecx of both functions below) keeps its matrix at +0x10 (64 bytes, rows, the
// position the last row at +0x40; its row 2 is the one 0x6D1F0 negates for the D3D view, so rows 0, 1, 2 are taken as
// right, up and back as a right handed camera has them). 0x846B0 (thiscall) builds the 3D view from it each frame and
// 0x7B7F0 (thiscall) the culling frustum, both through a copy, so the free camera's matrix is written into the object at
// the top of both. The way round of the axes was not confirmed, so the camera turns about its own rows, from where the
// game's camera was: yaw about the up the seed had, pitch about its right. The pause is the level state [0x1BEB1C] 3
// (also movies and a mission's end), held here each frame and put back; the HUD pass 0xBC530 (thiscall, no
// arguments) is skipped. The field of view is auf::photo_fov, a factor FovScale takes.
// AUF_PHOTO_TRACE=1 logs the camera's matrix as photo mode starts.
#include "driving/addr.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
constexpr int VK_F10 = 0x79, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_SPACE = 0x20;
#endif

#include "app/app.h"
#include "common/auf.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kState = A(0x1BEB1C);
constexpr float kSpeed = 14.0f, kTurn = 160.0f;  // units, degrees a second

struct V {
    float x = 0, y = 0, z = 0;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator*(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V Norm(V a) {
    float l = std::sqrt(Dot(a, a));
    return l > 1e-6f ? a * (1 / l) : V{0, 1, 0};
}
// v turned about the unit axis by a radians
V Turn(V v, V axis, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (1 - c));
}

struct Photo {
    bool on = false, seeded = false;
    uint32_t state_before = 0;
    bool had_dof = false;
    uint32_t had_focus = 10, had_blur = 6;
    V r, u, b, p, up;  // right, up and back rows, the position, the fixed world up
    float rows[16] = {};  // the matrix written this frame
    std::chrono::steady_clock::time_point last{};
} g;

bool Key(int vk) {
#ifdef _WIN32
    return (GetAsyncKeyState(vk) & 0x8000) != 0 && GetForegroundWindow() == static_cast<HWND>(app::Window());
#else
    (void)vk;
    return false;
#endif
}

void Leave() {
    kernel::G<uint32_t>(kState) = g.state_before;
    g.on = false;
    app::Get().hide_2d = false;
    app::Settings& st = app::Get();
    st.depth_of_field = g.had_dof, st.focus_distance = g.had_focus, st.dof_blur = g.had_blur;
    auf::photo_fov = 1.0f;
    float x, y;
    input::TakeMouse(x, y);
    LOGR("auf: driving photo mode left");
}

void Toggle() {
    static bool was = false;
    const bool down = Key(VK_F10);
    if (down && !was) {
        if (g.on) Leave();
        else if (auf::photo_mode && !app::OverlayOpen() && !auf::MenuUp()) {
            g.state_before = kernel::R32(kState);
            g.on = true, g.seeded = false;
            app::Get().hide_2d = true;
            const app::Settings& st = app::Get();
            g.had_dof = st.depth_of_field, g.had_focus = st.focus_distance, g.had_blur = st.dof_blur;
            auf::photo_fov = 1.0f;
            g.last = std::chrono::steady_clock::now();
            LOGR("auf: driving photo mode");
        }
    }
    was = down;
}

void Fly(uint32_t camera) {
    float* f = reinterpret_cast<float*>(&kernel::G<uint32_t>(camera + 0x10));
    if (!g.seeded) {
        g.r = {f[0], f[1], f[2]}, g.u = {f[4], f[5], f[6]}, g.b = {f[8], f[9], f[10]}, g.p = {f[12], f[13], f[14]};
        g.up = Norm(g.u);
        g.seeded = true;
        if (std::getenv("AUF_PHOTO_TRACE"))
            LOGR("auf: camera rows right %.2f %.2f %.2f up %.2f %.2f %.2f back %.2f %.2f %.2f at %.1f %.1f %.1f", g.r.x, g.r.y,
                 g.r.z, g.u.x, g.u.y, g.u.z, g.b.x, g.b.y, g.b.z, g.p.x, g.p.y, g.p.z);
    }
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - g.last).count(), 0.0f, 0.1f);
    g.last = now;
    const float fov_rate = 0.6f * dt;
    if (Key(0xDB)) auf::photo_fov = std::clamp(auf::photo_fov - fov_rate, 0.3f, 2.0f);
    if (Key(0xDD)) auf::photo_fov = std::clamp(auf::photo_fov + fov_rate, 0.3f, 2.0f);
    // depth of field as on foot: , . the focus distance, N M the blur, B on and off, R all as the game had them
    app::Settings& st = app::Get();
    if (Key(0xBC) || Key(0xBE))
        st.focus_distance = uint32_t(std::clamp(float(st.focus_distance) + (Key(0xBE) - Key(0xBC)) * 30.0f * dt, 1.0f, 100.0f) + 0.5f);
    if (Key('N') || Key('M'))
        st.dof_blur = uint32_t(std::clamp(float(st.dof_blur) + (Key('M') - Key('N')) * 6.0f * dt, 0.0f, 12.0f) + 0.5f);
    static bool was_b = false, was_r = false;
    const bool kb = Key('B'), kr = Key('R');
    if (kb && !was_b) st.depth_of_field = !st.depth_of_field;
    if (kr && !was_r) {
        auf::photo_fov = 1.0f;
        st.depth_of_field = g.had_dof, st.focus_distance = g.had_focus, st.dof_blur = g.had_blur;
    }
    was_b = kb, was_r = kr;
    float mx = 0, my = 0;
    if (input::TakeMouse(mx, my)) {
        const float yaw = -mx * kTurn * dt * 3.14159265f / 180, pitch = my * kTurn * dt * 3.14159265f / 180;
        g.r = Norm(Turn(g.r, g.up, yaw)), g.u = Norm(Turn(g.u, g.up, yaw)), g.b = Norm(Turn(g.b, g.up, yaw));
        g.u = Norm(Turn(g.u, g.r, pitch)), g.b = Norm(Turn(g.b, g.r, pitch));
    }
    const float speed = kSpeed * dt * (Key(VK_SHIFT) ? 3.0f : 1.0f) * (Key(VK_CONTROL) ? 0.25f : 1.0f);
    const float mf = float(Key('W')) - float(Key('S')), mr = float(Key('D')) - float(Key('A')),
                mu = float(Key(VK_SPACE)) - float(Key('C'));
    g.p = g.p + (g.b * -mf + g.r * mr + g.up * mu) * speed;
    const float rows[16] = {g.r.x, g.r.y, g.r.z, f[3], g.u.x, g.u.y, g.u.z, f[7], g.b.x, g.b.y, g.b.z, f[11], g.p.x, g.p.y, g.p.z, f[15]};
    std::memcpy(g.rows, rows, sizeof rows);
}

void Apply(uint32_t camera) { std::memcpy(&kernel::G<uint8_t>(camera + 0x10), g.rows, sizeof g.rows); }

// both functions: the 3D view (not the 2D identity view [view+8] 0 or 1 pick another function for)
void Frame(uint32_t view) {
    const uint32_t camera = kernel::R32(view + 4);
    if (!camera) return;
    Toggle();
    if (!g.on) return;
    if (!auf::photo_mode) return Leave();
    kernel::G<uint32_t>(kState) = 3;  // held: the level may put it back
    Fly(camera);
    Apply(camera);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000846B0);
X86_FUNC(sub_000846B0) {
    Frame(ctx.ecx);
    __imp__sub_000846B0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_0007B7F0);
X86_FUNC(sub_0007B7F0) {
    if (g.on && g.seeded)
        if (const uint32_t camera = kernel::R32(ctx.ecx + 4)) Apply(camera);  // the matrix the view was just built from
    __imp__sub_0007B7F0(ctx, base);
}

// the HUD pass
X86_EXTERN_FUNC(__imp__sub_000BC530);
X86_FUNC(sub_000BC530) {
    if (g.on) {
        ctx.esp += 4;  // thiscall without arguments: the return address only
        return;
    }
    __imp__sub_000BC530(ctx, base);
}
