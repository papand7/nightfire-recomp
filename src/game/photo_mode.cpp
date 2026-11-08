// Photo mode (default.xbe, on foot): the pause screen's PHOTO MODE (or its key, F10 by default, which pauses first)
// keeps the game paused, hides the HUD and the menus, and frees player 1's camera: the mouse or the right stick look,
// WASD or the left stick move, Q / E or the triggers go down and up, Shift or RB faster, Ctrl or LB slower. A panel
// sets the field of view, the exposure, depth of field and the look (a style preset); the screenshot key (F12) or Y
// takes the picture without the panel. Esc, B or Start leave it: the pause screen as it was (from the key, the game
// goes on).
//
// The game stays in its own pause: nothing of it changes. The free camera is player 1's (0x1F661C [0]) only while the
// frame's 3D and 2D pass runs (0xDAC20, src/game/hud.cpp): its world matrix (+0x108, rows right, up, forward and the
// position at +0x138) and its copy at +0x180 written for the pass, the field of view (+0x104, the zoom divisor +0xF4
// at 1), and the first person gun ([actor +0x778], +0xCC bit 0x10 hidden) left out; all put back after it. Every 2D
// list (0xD9410) is skipped meanwhile (hud.cpp), MenuUp says no menu (src/game/hooks.cpp: the mouse looks, the modern
// menus are not drawn) and the pad reaches the game empty (nf::PhotoPad from MenuMouse). The look's values are a
// style request (gpu::RequestStyle), the one before asked again on leaving.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#ifndef XBE_HEADLESS
#include <imgui.h>
#endif

#include "app/app.h"
#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/style.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {
using kernel::R32;

constexpr uint32_t kCameras = 0x1F661C, kWorld = 0x108, kWorld2 = 0x180, kFov = 0x104, kZoom = 0xF4;
constexpr uint32_t kActors = 0x2774B8, kGun = 0x778, kFlags = 0xCC, kHidden = 0x10;
constexpr uint32_t kMenuContext = 0x25F1D0, kPausePage = 0x4000004B;
constexpr float kPi = 3.14159265f;
constexpr float kReach = 40.0f;  // world units from where it started (the level's rooms are drawn from the player's)

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Norm(V3 a) {
    float l = std::sqrt(Dot(a, a));
    return l > 1e-6f ? a * (1 / l) : V3{0, 0, 1};
}
float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }
V3 Row(uint32_t at) { return {GetF(at), GetF(at + 4), GetF(at + 8)}; }

enum State { kOff, kPausing, kOn };
std::atomic<int> g_state{kOff};
bool g_by_key = false;      // entered by the key: leaving goes back to play
int g_wait = 0;             // frames waited for the pause screen (kPausing)
bool g_release = false;     // the pad kept from the game until let go (leaving)
bool g_resume = false;      // then Start: the pause screen's Continue

// the free camera (the game's thread)
struct Cam {
    bool set = false;       // taken from the game's camera
    V3 pos, fwd, start;
    float right_sign = 1, up_sign = 1;  // the game's rows against cross(up, forward) and cross(forward, right)
    int copy = 0;           // +0x180: 0 left alone, 1 the world matrix's copy, 2 its inverse
} g_cam;

// the look (the panel's rows)
enum Row { kRowFov, kRowExposure, kRowDof, kRowStyle, kRows };
struct Look {
    float fov_deg = 60;
    float exposure = 1.0f;
    int dof = 0;   // 0 off, 1-4 stronger
    int style = -1;  // -1 as it was, else gpu::StylePresets()'s
};
std::mutex g_lock;  // g_look, g_row, g_panel, g_style_name for the panel (the render thread)
Look g_look;
int g_row = 0;
bool g_panel = true;
std::string g_style_name = "As Before";
gpu::GraphicsStyle g_saved;  // the style before
bool g_saved_set = false;

// the pad as polled (PhotoPad), for its sticks and triggers
std::mutex g_pad_lock;
input::Pad g_pad;

float Stick(int16_t v) {
    float f = std::clamp(float(v) / 32767.0f, -1.0f, 1.0f);
    return std::fabs(f) < 0.15f ? 0.0f : (f - std::copysign(0.15f, f)) / 0.85f;
}

bool Paused() { return R32(kMenuContext + 0x1BC) != 0 && R32(kMenuContext + 0x1B8) == kPausePage; }

// the preset's look over the style before (quality, effects and the upscaler kept)
void LookOf(gpu::GraphicsStyle& st, const gpu::GraphicsStyle& p) {
    st.preset = p.preset, st.mode = p.mode, st.exposure = p.exposure, st.tonemap = p.tonemap, st.contrast = p.contrast;
    st.saturation = p.saturation, st.temperature = p.temperature, st.sharpen = p.sharpen, st.bloom = p.bloom;
    st.bloom_threshold = p.bloom_threshold, st.ao = p.ao, st.ao_radius = p.ao_radius, st.vignette = p.vignette;
    st.grain = p.grain, st.tex_filter = p.tex_filter, st.bands = p.bands, st.outline = p.outline;
    std::copy(std::begin(p.outline_color), std::end(p.outline_color), std::begin(st.outline_color));
    st.internal_edges = p.internal_edges, st.edge_threshold = p.edge_threshold, st.halftone = p.halftone;
    st.pixel = p.pixel, st.color_bits = p.color_bits, st.dither = p.dither, st.retro_tint = p.retro_tint;
    st.vertex_snap = p.vertex_snap;
}

void ApplyLook() {
    Look l;
    {
        std::lock_guard lock(g_lock);
        l = g_look;
    }
    gpu::GraphicsStyle st = g_saved;
    const auto& presets = gpu::StylePresets();
    if (l.style >= 0 && l.style < int(presets.size())) {
        gpu::GraphicsStyle p = presets[size_t(l.style)].values;
        p.preset = presets[size_t(l.style)].name;
        LookOf(st, p);
    }
    if (std::fabs(l.exposure - 1.0f) > 0.01f) {
        if (st.mode == gpu::kStyleOriginal) {  // Original has no exposure of its own: Enhanced with the game's grading
            gpu::GraphicsStyle neutral;
            LookOf(st, neutral);
            st.mode = gpu::kStyleEnhanced, st.tonemap = gpu::kToneOff, st.bloom = 0, st.ao = 0;
        }
        st.exposure *= l.exposure;
        st.preset.clear();
    }
    if (l.dof > 0) st.dof = gpu::kDofFocus, st.dof_strength = 0.25f * float(l.dof) * float(l.dof) / 2 + 0.25f;
    else if (l.dof == 0) st.dof = g_saved.dof, st.dof_strength = g_saved.dof_strength;
    gpu::RequestStyle(st);
}

void Enter(bool by_key) {
    g_saved = gpu::StyleSnapshot(), g_saved_set = true;
    {
        std::lock_guard lock(g_lock);
        g_look = Look{};
        g_row = 0, g_panel = true, g_style_name = "As Before";
        const uint32_t cam = R32(kCameras);
        if (cam) g_look.fov_deg = std::clamp(std::round(GetF(cam + kFov) * 180 / kPi), 20.0f, 110.0f);
    }
    g_cam = {};
    g_by_key = by_key;
    g_state = kOn;
    app::HideOverlays(true);
    LOGR("nightfire: photo mode on (%s)", by_key ? "its key" : "the pause screen");
}

void Leave() {
    if (g_state != kOn) return;
    g_state = kOff;
    g_release = true, g_resume = g_by_key;
    if (g_saved_set) gpu::RequestStyle(g_saved), g_saved_set = false;
    app::HideOverlays(false);
    LOGR("nightfire: photo mode off%s", g_by_key ? " (back to play)" : " (the pause screen)");
}

// one step of a row's value
void Step(int row, int d) {
    std::lock_guard lock(g_lock);
    Look& l = g_look;
    if (row == kRowFov) l.fov_deg = std::clamp(l.fov_deg + 5.0f * float(d), 20.0f, 110.0f);
    if (row == kRowExposure) l.exposure = std::clamp(std::round((l.exposure + 0.1f * float(d)) * 10) / 10, 0.3f, 3.0f);
    if (row == kRowDof) l.dof = std::clamp(l.dof + d, 0, 4);
    if (row == kRowStyle) {
        const int n = int(gpu::StylePresets().size());
        l.style = l.style + d < -1 ? n - 1 : l.style + d >= n ? -1 : l.style + d;
        g_style_name = l.style < 0 ? "As Before" : gpu::StylePresets()[size_t(l.style)].label;
    }
}

// the free camera's movement, from the game's thread each frame while on
void Fly(float dt) {
    if (!g_cam.set) return;
    input::Pad pad;
    {
        std::lock_guard lock(g_pad_lock);
        pad = g_pad;
    }
    auto key = [](int vk) { return input::KeyHeld(vk) ? 1.0f : 0.0f; };
    const uint32_t held = input::HeldControls();
    auto button = [&](int c) { return (held >> c & 1) != 0; };
    float fwd = key('W') - key('S'), side = key('D') - key('A'), rise = key('E') - key('Q');
    float sy = Stick(pad.ly), sx = Stick(pad.lx);
    if (std::fabs(sy) > std::fabs(fwd)) fwd = sy;
    if (std::fabs(sx) > std::fabs(side)) side = sx;
    rise += (pad.analog[7] - pad.analog[6]) / 255.0f * (input::UsingKeyboard() ? 0.0f : 1.0f);  // RT up, LT down
    float speed = 4.0f;  // world units a second
    if (key(0xA0) || key(0x10) || button(input::kBlack)) speed *= 4;  // Shift, RB
    if (key(0xA2) || key(0x11) || button(input::kWhite)) speed *= 0.25f;  // Ctrl, LB
    // the look: the mouse as mouse.cpp turns the view (0.0008 radians a count at 100%), the right stick 2 radians a second
    float mx = 0, my = 0;
    input::TakeMouse(mx, my);
    float fov = 60;
    {
        std::lock_guard lock(g_lock);
        fov = g_look.fov_deg;
    }
    const float zoom = std::min(fov / 60.0f, 1.0f);
    float yaw = mx * 2400 * dt * 0.0008f * zoom + Stick(pad.rx) * 2.0f * dt * zoom;
    float pitch = my * 2400 * dt * 0.0008f * zoom + Stick(pad.ry) * 1.5f * dt * zoom;
    const V3 up{0, 1, 0};
    V3 right = Norm(Cross(up, g_cam.fwd)) * g_cam.right_sign;
    V3 f = Norm(g_cam.fwd * std::cos(yaw) + right * std::sin(yaw));
    V3 cam_up = Norm(Cross(f, Norm(Cross(up, f))));
    V3 f2 = Norm(f * std::cos(pitch) + cam_up * std::sin(pitch));
    if (std::fabs(f2.y) < 0.97f) f = f2;
    g_cam.fwd = f;
    right = Norm(Cross(up, f)) * g_cam.right_sign;
    V3 p = g_cam.pos + (f * fwd + right * side + up * rise) * (speed * dt);
    V3 d = p - g_cam.start;
    if (Dot(d, d) > kReach * kReach) p = g_cam.start + Norm(d) * kReach;
    g_cam.pos = p;
}

// the panel's keys and buttons (edges), from the game's thread
void Controls() {
    static uint32_t was_held = 0;
    static bool was_keys[16] = {};
    const uint32_t held = input::HeldControls(), pressed = held & ~was_held;
    was_held = held;
    const int keys[] = {0x26, 0x28, 0x25, 0x27, 0x1B, 'H', 0x08};  // up, down, left, right, Escape, H, Backspace
    bool kp[std::size(keys)] = {};
    for (size_t i = 0; i < std::size(keys); i++) {
        bool h = input::KeyHeld(keys[i]);
        kp[i] = h && !was_keys[i];
        was_keys[i] = h;
    }
    auto bit = [&](int c) { return (pressed >> c & 1) != 0; };
    if (kp[4] || kp[6] || bit(input::kB) || bit(input::kStart)) {
        Leave();
        return;
    }
    if (kp[5] || bit(input::kX)) {
        std::lock_guard lock(g_lock);
        g_panel = !g_panel;
    }
    if (bit(input::kY)) nf::TakeScreenshot();
    int row;
    {
        std::lock_guard lock(g_lock);
        if (kp[0] || bit(input::kUp)) g_row = (g_row + kRows - 1) % kRows;
        if (kp[1] || bit(input::kDown)) g_row = (g_row + 1) % kRows;
        row = g_row;
    }
    int d = (kp[3] || bit(input::kRight) ? 1 : 0) - (kp[2] || bit(input::kLeft) ? 1 : 0);
    if (d) {
        Step(row, d);
        if (row != kRowFov) ApplyLook();
    }
}
}  // namespace

namespace nf {

bool PhotoActive() { return g_state == kOn; }

void PhotoEnter(X86Context&) {
    if (g_state == kOff && Paused()) Enter(false);
}

// the pad hook (MenuMouse): the pad kept from the game while photo mode is on, and after it until let go
bool PhotoPad(input::Pad& p) {
    if (g_state == kOn) {
        {
            std::lock_guard lock(g_pad_lock);
            g_pad = p;
        }
        const bool c = p.connected;
        p = input::Pad{};
        p.connected = c;
        return true;
    }
    if (g_release) {
        bool any = p.buttons || p.lx || p.ly || p.rx || p.ry;
        for (uint8_t a : p.analog) any |= a > 30;
        if (any) {
            const bool c = p.connected;
            p = input::Pad{};
            p.connected = c;
            return true;
        }
        g_release = false;
        if (g_resume) g_resume = false, QueuePress(input::kStart);  // the pause screen's Continue
    }
    return false;
}

// the game's thread, each frame (src/game/controls.cpp): the key, and the camera's movement while on
void PhotoFrame(X86Context& ctx) {
    (void)ctx;
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.1f);
    last = now;
    static bool key_was = false;
    const bool key = photo_key && input::KeyHeld(int(photo_key)) && !app::KeyCaptureBusy();
    const bool pressed = key && !key_was;
    key_was = key;
    if (g_state == kOn) {
        if (pressed) {
            Leave();
            return;
        }
        Controls();
        if (g_state == kOn) Fly(dt);
        return;
    }
    if (g_state == kPausing) {
        if (Paused()) Enter(true);
        else if (++g_wait > 90) g_state = kOff, LOGR("nightfire: photo mode: the pause screen did not open");
        return;
    }
    if (!pressed || g_release) return;
    if (Paused()) {
        Enter(false);
    } else if (!MenuUp() && PlayerFree()) {  // in play: paused first (Start), then on
        g_state = kPausing, g_wait = 0;
        QueuePress(input::kStart);
    } else {
        LOGR("nightfire: photo mode: not here (a menu or Bond not the player's to move)");
    }
}

// around the frame's 3D and 2D pass (src/game/hud.cpp): the free camera put in, then the game's put back
namespace {
uint8_t g_kept[0x40 * 2 + 8];
uint32_t g_kept_cam = 0, g_kept_gun = 0, g_gun_flags = 0;
}  // namespace
void PhotoBeforePass() {
    g_kept_cam = 0;
    if (g_state != kOn) return;
    const uint32_t cam = R32(kCameras);
    if (!cam || !mem::IsCommitted(cam + 0x1C0)) return;
    if (!g_cam.set) {
        // the game's camera as it is: the free one starts there
        V3 r0 = Row(cam + kWorld), r1 = Row(cam + kWorld + 0x10), r2 = Row(cam + kWorld + 0x20);
        g_cam.fwd = Norm(r2);
        g_cam.pos = g_cam.start = Row(cam + kWorld + 0x30);
        const V3 up{0, 1, 0};
        g_cam.right_sign = Dot(r0, Cross(up, g_cam.fwd)) < 0 ? -1.0f : 1.0f;
        V3 right = Norm(Cross(up, g_cam.fwd)) * g_cam.right_sign;
        g_cam.up_sign = Dot(r1, Cross(g_cam.fwd, right)) < 0 ? -1.0f : 1.0f;
        // +0x180: the same matrix, its inverse, or something else (left alone)
        float a[16], b[16];
        for (int i = 0; i < 16; i++) a[i] = GetF(cam + kWorld + 4 * i), b[i] = GetF(cam + kWorld2 + 4 * i);
        float same = 0, inv = 0;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) same += std::fabs(a[i * 4 + j] - b[i * 4 + j]), inv += std::fabs(a[i * 4 + j] - b[j * 4 + i]);
        for (int j = 0; j < 3; j++) same += std::fabs(a[12 + j] - b[12 + j]);
        g_cam.copy = same < 0.01f ? 1 : inv < 0.01f ? 2 : 0;
        g_cam.set = true;
        LOGR("nightfire: photo mode camera at %.2f %.2f %.2f, forward %.2f %.2f %.2f, right sign %.0f, up sign %.0f; "
             "+0x180 %s (%.3f %.3f %.3f / %.3f %.3f %.3f)",
             g_cam.pos.x, g_cam.pos.y, g_cam.pos.z, g_cam.fwd.x, g_cam.fwd.y, g_cam.fwd.z, g_cam.right_sign,
             g_cam.up_sign, g_cam.copy == 1 ? "the same" : g_cam.copy == 2 ? "its inverse" : "apart", b[0], b[1], b[2],
             b[12], b[13], b[14]);
    }
    g_kept_cam = cam;
    std::memcpy(g_kept, kernel::HostPtr(cam + kWorld), 0x40);
    std::memcpy(g_kept + 0x40, kernel::HostPtr(cam + kWorld2), 0x40);
    std::memcpy(g_kept + 0x80, kernel::HostPtr(cam + kZoom), 4);
    std::memcpy(g_kept + 0x84, kernel::HostPtr(cam + kFov), 4);
    const V3 up{0, 1, 0};
    V3 f = g_cam.fwd, r = Norm(Cross(up, f)) * g_cam.right_sign, u = Norm(Cross(f, Norm(Cross(up, f)))) * g_cam.up_sign;
    float m[16] = {r.x, r.y, r.z, 0, u.x, u.y, u.z, 0, f.x, f.y, f.z, 0, g_cam.pos.x, g_cam.pos.y, g_cam.pos.z, 1};
    // the game's fourth column as it was (its +0x114 ... may not be 0 0 0 1)
    for (int i = 0; i < 4; i++) m[i * 4 + 3] = GetF(cam + kWorld + 16 * i + 12);
    for (int i = 0; i < 16; i++) SetF(cam + kWorld + 4 * i, m[i]);
    if (g_cam.copy == 1) {
        for (int i = 0; i < 16; i++) SetF(cam + kWorld2 + 4 * i, m[i]);
    } else if (g_cam.copy == 2) {  // a rigid inverse: the rotation transposed, the position turned back
        float v[16] = {r.x, u.x, f.x, 0, r.y, u.y, f.y, 0, r.z, u.z, f.z, 0,
                       -Dot(r, g_cam.pos), -Dot(u, g_cam.pos), -Dot(f, g_cam.pos), 1};
        for (int i = 0; i < 4; i++) v[i * 4 + 3] = GetF(cam + kWorld2 + 16 * i + 12);
        for (int i = 0; i < 16; i++) SetF(cam + kWorld2 + 4 * i, v[i]);
    }
    float fov;
    {
        std::lock_guard lock(g_lock);
        fov = g_look.fov_deg;
    }
    SetF(cam + kFov, fov * kPi / 180), SetF(cam + kZoom, 1.0f);
    // the first person gun left out
    g_kept_gun = 0;
    const uint32_t actor = R32(kActors);
    const uint32_t gun = actor && mem::IsCommitted(actor + kGun) ? R32(actor + kGun) : 0;
    if (gun && mem::IsCommitted(gun + kFlags)) {
        g_kept_gun = gun, g_gun_flags = R32(gun + kFlags);
        kernel::W32(gun + kFlags, g_gun_flags | kHidden);
    }
}
void PhotoAfterPass() {
    if (!g_kept_cam) return;
    const uint32_t cam = g_kept_cam;
    g_kept_cam = 0;
    std::memcpy(kernel::HostPtr(cam + kWorld), g_kept, 0x40);
    std::memcpy(kernel::HostPtr(cam + kWorld2), g_kept + 0x40, 0x40);
    std::memcpy(kernel::HostPtr(cam + kZoom), g_kept + 0x80, 4);
    std::memcpy(kernel::HostPtr(cam + kFov), g_kept + 0x84, 4);
    if (g_kept_gun) kernel::W32(g_kept_gun + kFlags, g_gun_flags), g_kept_gun = 0;
}

// the panel (GameInfo::ui, src/common/pc_extras.cpp): bottom left, in the modern menus' colours; not in a picture
void PhotoUi() {
#ifndef XBE_HEADLESS
    if (g_state != kOn || gpu::ScreenshotPending()) return;
    Look l;
    int row;
    bool panel;
    std::string style;
    {
        std::lock_guard lock(g_lock);
        l = g_look, row = g_row, panel = g_panel, style = g_style_name;
    }
    ImGuiIO& io = ImGui::GetIO();
    const float s = io.DisplaySize.y / 1080.0f, fs = 26 * s, fh = 34 * s;
    ImFont* font = ImGui::GetFont();
    for (ImFont* f : io.Fonts->Fonts)
        if (f->FontSize == 28.0f) font = f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImU32 navy = IM_COL32(5, 14, 36, 225), electric = IM_COL32(40, 120, 255, 255), cyan = IM_COL32(120, 210, 255, 255),
                ice = IM_COL32(205, 228, 255, 255), white = IM_COL32(255, 255, 255, 255);
    const bool pad = !input::UsingKeyboard();
    const std::string shot = pad ? "Y" : KeyName(screenshot_key);
    if (!panel) {
        std::string hint = std::string(pad ? "X" : "H") + ": panel   " + shot + ": picture";
        dl->AddText(font, fs * 0.85f, {30 * s, io.DisplaySize.y - 50 * s}, IM_COL32(205, 228, 255, 150), hint.c_str());
        return;
    }
    char v[4][48];
    std::snprintf(v[0], 48, "%.0f\xC2\xB0", l.fov_deg);
    std::snprintf(v[1], 48, "%.1f", l.exposure);
    static const char* dofs[] = {"Off", "Light", "Medium", "Strong", "Strongest"};
    std::snprintf(v[2], 48, "%s", dofs[std::clamp(l.dof, 0, 4)]);
    std::snprintf(v[3], 48, "%s", style.c_str());
    const char* names[4] = {"Field of View", "Exposure", "Depth of Field", "Look"};
    const float x0 = 30 * s, w = std::min(860 * s, io.DisplaySize.x - 2 * x0);
    const std::string help1 = pad ? "Left stick: move   Right stick: look   LT / RT: down / up   LB / RB: slower / faster"
                                  : "Mouse: look   WASD: move   Q / E: down / up   Ctrl / Shift: slower / faster";
    const std::string help2 = pad ? "D-pad: choose and change   Y: picture   X: hide   B: back"
                                  : "Arrows: choose and change   " + shot + ": picture   H: hide   Esc: back";
    const float h = 20 * s + fh * 1.1f + 4 * (fh + 6 * s) + 2 * (fs * 0.8f + 6 * s) + 22 * s;
    const float y0 = io.DisplaySize.y - h - 30 * s;
    dl->AddRectFilled({x0, y0}, {x0 + w, y0 + h}, navy, 12 * s);
    dl->AddRectFilled({x0, y0}, {x0 + 6 * s, y0 + h}, electric, 12 * s, ImDrawFlags_RoundCornersLeft);
    float y = y0 + 16 * s;
    dl->AddText(font, fh * 1.1f, {x0 + 26 * s, y}, white, "PHOTO MODE");
    y += fh * 1.1f + 8 * s;
    for (int i = 0; i < 4; i++) {
        const bool cur = i == row;
        if (cur) dl->AddRectFilled({x0 + 16 * s, y - 3 * s}, {x0 + w - 16 * s, y + fh + 1 * s}, IM_COL32(40, 120, 255, 200), 8 * s);
        dl->AddText(font, fs, {x0 + 28 * s, y + (fh - fs) / 2}, cur ? white : ice, names[i]);
        std::string val = std::string("<  ") + v[i] + "  >";
        ImVec2 vs = font->CalcTextSizeA(fs, 1e9f, 0, val.c_str());
        dl->AddText(font, fs, {x0 + w - 30 * s - vs.x, y + (fh - fs) / 2}, cur ? white : cyan, val.c_str());
        y += fh + 6 * s;
    }
    y += 6 * s;
    dl->AddText(font, fs * 0.8f, {x0 + 26 * s, y}, IM_COL32(150, 180, 225, 230), help1.c_str());
    y += fs * 0.8f + 6 * s;
    dl->AddText(font, fs * 0.8f, {x0 + 26 * s, y}, IM_COL32(150, 180, 225, 230), help2.c_str());
#endif
}

}  // namespace nf
