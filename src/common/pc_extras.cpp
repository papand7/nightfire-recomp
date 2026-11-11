// What both exes draw over the game besides the modern menus (GameInfo::ui, nf::GameUi): the screenshot key (F12 by
// default) and its "saved" note, the performance overlay, and default.xbe's photo mode panel (src/game/photo_mode.cpp).
//
// A screenshot is the frame as shown (the HUD included; photo mode hides it), written by the Vulkan renderer at its
// next present (gpu::RequestScreenshot, the same path its XBE_PAD_CMD test shots take) as a PNG in Screenshots beside
// the exe, named by the local time. The performance overlay (More Settings' Performance: Full) is a corner panel in
// the modern menus' colours: frames a second, the frame time and a graph of the last two seconds of it, the GPU's time
// on a frame (the renderer's timestamps, gpu::LastFrameStats), and the 3D scene's size against the screen's with the
// upscaler rebuilding it.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>

#ifndef XBE_HEADLESS
#include <imgui.h>
#endif

#include "app/app.h"
#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/style.h"

namespace nf {
bool perf_overlay = false;
uint32_t screenshot_key = 0x7B;  // F12
uint32_t photo_key = 0x79;       // F10
uint32_t restart_key = 0;        // none
uint32_t ads_zoom = 130;         // aim down sights' zoom (src/game/aim.cpp)

std::string KeyName(uint32_t vk) {
    if (!vk) return "-";
    std::string n = app::KeyToName(uint16_t(vk)), out;
    bool start = true;
    for (char c : n) {
        if (c == '_') {
            out += ' ', start = true;
            continue;
        }
        out += start ? char(std::toupper(static_cast<unsigned char>(c))) : c;
        start = false;
    }
    return out.empty() ? "-" : out;
}

#ifndef XBE_HEADLESS
namespace {
constexpr ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
const ImU32 kNavy = Col(5, 14, 36, 225), kElectric = Col(40, 120, 255), kCyan = Col(120, 210, 255),
            kIce = Col(205, 228, 255), kWhite = Col(255, 255, 255);

// the modern menus' fonts by their size (src/common/modern_menu.cpp makes them at 40 and 28 px), else ImGui's
ImFont* FontOf(float size) {
    ImGuiIO& io = ImGui::GetIO();
    for (ImFont* f : io.Fonts->Fonts)
        if (f->FontSize == size) return f;
    return ImGui::GetFont();
}

std::filesystem::path ShotPath() {
    std::filesystem::path dir = app::ExeDir() / "Screenshots";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char name[64];
    std::strftime(name, sizeof name, "NightFire_%Y-%m-%d_%H-%M-%S", &tm);
    std::filesystem::path p = dir / (std::string(name) + ".png");
    for (int i = 2; std::filesystem::exists(p, ec) && i < 100; i++) p = dir / (std::string(name) + "_" + std::to_string(i) + ".png");
    return p;
}

// a key's press (held now, not last frame), not while the menus wait for a key to bind
bool Pressed(uint32_t vk, bool& was) {
    bool held = vk && input::KeyHeld(int(vk)) && !app::KeyCaptureBusy();
    bool p = held && !was;
    was = held;
    return p;
}

// the "screenshot saved" note: a panel at the bottom right for three seconds, fading out
void Toast() {
    static uint32_t seen = 0;
    static std::string text;
    static double shown_at = -100;
    std::wstring path;
    bool ok = false;
    const uint32_t n = gpu::ScreenshotResult(path, ok);
    const double now = ImGui::GetTime();
    if (n != seen) {
        seen = n;
        std::string name;
        for (wchar_t c : std::filesystem::path(path).filename().wstring()) name += c < 0x80 ? char(c) : '?';
        text = ok ? "Screenshot saved: Screenshots\\" + name : "The screenshot could not be saved";
        shown_at = now;
    }
    const double age = now - shown_at;
    if (age > 3.0 || gpu::ScreenshotPending()) return;  // never in the next picture
    const float a = float(std::clamp((3.0 - age) / 0.5, 0.0, 1.0));
    ImGuiIO& io = ImGui::GetIO();
    const float s = io.DisplaySize.y / 1080.0f, fs = 28 * s;
    ImFont* f = FontOf(28.0f);
    ImVec2 ts = f->CalcTextSizeA(fs, 1e9f, 0, text.c_str());
    const float pad = 18 * s, w = ts.x + pad * 2 + 8 * s, h = ts.y + pad * 1.4f;
    ImVec2 p0{io.DisplaySize.x - w - 40 * s, io.DisplaySize.y - h - 120 * s};
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto fade = [&](ImU32 c) { return (c & 0x00FFFFFF) | (ImU32(float(c >> 24) * a) << 24); };
    dl->AddRectFilled(p0, {p0.x + w, p0.y + h}, fade(kNavy), 10 * s);
    dl->AddRectFilled(p0, {p0.x + 6 * s, p0.y + h}, fade(kElectric), 10 * s, ImDrawFlags_RoundCornersLeft);
    dl->AddText(f, fs, {p0.x + pad + 8 * s, p0.y + pad * 0.7f}, fade(ok ? kIce : Col(255, 150, 120)), text.c_str());
}

// the performance overlay
void PerfOverlay(double fps) {
    if (!perf_overlay || app::OverlaysHidden()) return;
    ImGuiIO& io = ImGui::GetIO();
    constexpr int kN = 120;
    static float times[kN] = {};
    static int at = 0;
    times[at] = io.DeltaTime * 1000.0f;
    at = (at + 1) % kN;
    const gpu::FrameStats st = gpu::LastFrameStats();
    const float s = io.DisplaySize.y / 1080.0f, fb = 30 * s, fs = 22 * s;
    ImFont* big = FontOf(40.0f);
    ImFont* small = FontOf(28.0f);
    const float w = 360 * s, x0 = io.DisplaySize.x - w - 24 * s, y0 = 24 * s;
    const float gh = 70 * s;
    char line[128];
    std::string rows[3];
    std::snprintf(line, sizeof line, "Frame  %.1f ms", 1000.0 / std::max(fps, 1.0));
    rows[0] = line;
    if (st.gpu_ms > 0) std::snprintf(line, sizeof line, "GPU  %.1f ms", st.gpu_ms), rows[1] = line;
    else rows[1] = "GPU  -";
    if (st.scene_w && st.out_w) {
        const char* up = st.upscaler == gpu::kUpscalerFsr ? " FSR 2" : st.upscaler == gpu::kUpscalerDlss ? " DLSS" : "";
        if (*up) std::snprintf(line, sizeof line, "%ux%u > %ux%u%s", st.scene_w, st.scene_h, st.out_w, st.out_h, up);
        else std::snprintf(line, sizeof line, "3D %ux%u, screen %ux%u", st.scene_w, st.scene_h, st.out_w, st.out_h);
        rows[2] = line;
    }
    const float h = 16 * s + fb + 10 * s + gh + 12 * s + 3 * (fs + 6 * s) + 12 * s;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled({x0, y0}, {x0 + w, y0 + h}, kNavy, 12 * s);
    dl->AddRectFilled({x0, y0}, {x0 + 6 * s, y0 + h}, kElectric, 12 * s, ImDrawFlags_RoundCornersLeft);
    float y = y0 + 12 * s;
    std::snprintf(line, sizeof line, "%.0f", fps);
    dl->AddText(big, fb * 1.2f, {x0 + 24 * s, y - 4 * s}, kWhite, line);
    ImVec2 ns = big->CalcTextSizeA(fb * 1.2f, 1e9f, 0, line);
    dl->AddText(small, fs, {x0 + 32 * s + ns.x, y + fb * 1.2f - fs - 6 * s}, kCyan, "FPS");
    y += fb + 14 * s;
    // the frame time graph: 0 to 33 ms (bars over 16.7 ms brighter), a line at 16.7
    const float gx = x0 + 24 * s, gw = w - 48 * s;
    dl->AddRectFilled({gx, y}, {gx + gw, y + gh}, Col(9, 30, 78, 160), 6 * s);
    const float bw = gw / kN;
    for (int i = 0; i < kN; i++) {
        float ms = times[(at + i) % kN];
        float bh = std::min(ms / 33.3f, 1.0f) * gh;
        dl->AddRectFilled({gx + i * bw, y + gh - bh}, {gx + (i + 1) * bw, y + gh}, ms > 17.5f ? Col(255, 170, 80, 230) : Col(80, 160, 255, 220));
    }
    const float y60 = y + gh - gh * (16.7f / 33.3f);
    dl->AddLine({gx, y60}, {gx + gw, y60}, Col(205, 228, 255, 90), 1.5f * s);
    y += gh + 12 * s;
    for (const std::string& r : rows) {
        if (r.empty()) continue;
        dl->AddText(small, fs, {x0 + 24 * s, y}, kIce, r.c_str());
        y += fs + 6 * s;
    }
}
}  // namespace

void TakeScreenshot() {
    if (gpu::ScreenshotPending()) return;
    std::filesystem::path p = ShotPath();
    gpu::RequestScreenshot(p.wstring());
    LOGR("nightfire: screenshot asked for: %s", p.string().c_str());
}

void (*g_sight_tuner_ui)() = nullptr;
void GameUi(double fps) {
    ModernMenuUi(fps);
    if (kDriving) {  // Discord: the mission default.xbe launched (src/game/quick_keys.cpp)
        static bool told = false;
        if (!told) {
            told = true;
            const char* m = std::getenv("NF_PRESENCE");
            app::SetPresence(m && *m ? std::string("Playing ") + m : "Playing a driving mission", "Driving",
                             int64_t(std::time(nullptr)));
        }
    }
    static bool shot_was = false;
    if (Pressed(screenshot_key, shot_was)) TakeScreenshot();
    PhotoUi();
    if (g_sight_tuner_ui) g_sight_tuner_ui();
    PerfOverlay(fps);
    Toast();
}
#else
void TakeScreenshot() {}
void GameUi(double fps) { ModernMenuUi(fps); }
#endif
}  // namespace nf
