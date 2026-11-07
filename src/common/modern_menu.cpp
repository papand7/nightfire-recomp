// Modern menus (nf::modern_menus): the game's menu pages drawn anew over its animated background movie, in the blues
// of the PS2 cover: a dark panel down the left, the page's title, its rows as rounded bars (the focused one lit
// electric blue), a choice's or slider's value between arrows, the help line under them. What is shown is the
// game's own page as it is now (src/game/modern_menu_frame.cpp reads its widgets each frame on the game's thread:
// every row's text, value and the focus), so every page works, ours too. The mouse hovers a row (the game's focus
// moves onto it), clicks it (A, or left / right on a value by the arrow clicked), the right button and the Back pill
// go back (B), the wheel scrolls (the d-pad, menu_mouse.cpp); the keyboard and controllers drive the game as they
// always do, and the bars follow its focus. The game's own widgets are not drawn meanwhile (src/game/hud.cpp).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"

#ifdef _WIN32
#include <imgui.h>
#endif

namespace nf {

bool modern_menus = false;

namespace {
std::mutex g_lock;
ModernPage g_page;                 // the page as the game's thread last read it
std::deque<ModernAction> g_actions;  // the UI's clicks, for the game's thread
// presses for the polled pad (ModernMenuPad): a control and how long it is held, then a rest
struct Held {
    int control;
    int64_t until = 0;
};
std::deque<int> g_presses;
Held g_held{-1};
int64_t g_rest_until = 0;

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32
ImFont* g_body = nullptr;   // the rows (Bahnschrift, else Segoe UI), made at 40 px and scaled to the screen
ImFont* g_title = nullptr;  // the title, 84 px
ImFont* g_small = nullptr;  // help and hints, 28 px

// the PS2 cover's blues
constexpr ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
const ImU32 kNavy = Col(5, 14, 36), kDeep = Col(9, 30, 78), kElectric = Col(40, 120, 255), kCyan = Col(120, 210, 255),
            kIce = Col(205, 228, 255), kWhite = Col(255, 255, 255);

float Lerp(float a, float b, float t) { return a + (b - a) * t; }
#endif
}  // namespace

void ModernMenuPublish(const ModernPage& page) {
    std::lock_guard lock(g_lock);
    g_page = page;
}

bool ModernMenuAction(ModernAction& a) {
    std::lock_guard lock(g_lock);
    if (g_actions.empty()) return false;
    a = g_actions.front();
    g_actions.pop_front();
    return true;
}

void ModernMenuPress(int control) {
    std::lock_guard lock(g_lock);
    if (g_presses.size() < 8) g_presses.push_back(control);
}

bool ModernMenuActive() {
    std::lock_guard lock(g_lock);
    return modern_menus && g_page.up;
}

// the queued presses played on the polled pad: each held 70 ms, then 60 ms released
void ModernMenuPad(input::Pad& p) {
    std::lock_guard lock(g_lock);
    int64_t now = NowMs();
    if (g_held.control >= 0 && now < g_held.until) {
        int c = g_held.control;
        if (c < input::kA) p.buttons |= uint16_t(1u << c);
        else p.analog[c - input::kA] = 255;
        return;
    }
    if (g_held.control >= 0) g_held.control = -1, g_rest_until = now + 60;
    if (now < g_rest_until || g_presses.empty()) return;
    g_held = {g_presses.front(), now + 70};
    g_presses.pop_front();
}

// GameInfo::window: the fonts, before the renderer builds ImGui's atlas (the system's own: nothing shipped)
void ModernMenuFonts(void*) {
#ifdef _WIN32
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();  // the settings window's stays the first
    const char* files[] = {"C:\\Windows\\Fonts\\bahnschrift.ttf", "C:\\Windows\\Fonts\\segoeui.ttf"};
    for (const char* f : files) {
        if (FILE* t = std::fopen(f, "rb")) {
            std::fclose(t);
            g_body = io.Fonts->AddFontFromFileTTF(f, 40.0f);
            g_title = io.Fonts->AddFontFromFileTTF(f, 84.0f);
            g_small = io.Fonts->AddFontFromFileTTF(f, 28.0f);
            LOGR("nightfire: modern menus' font %s", f);
            break;
        }
    }
#endif
}

// GameInfo::ui: the page drawn, the mouse's clicks queued for the game's thread
void ModernMenuUi(double) {
#ifdef _WIN32
    ModernPage page;
    {
        std::lock_guard lock(g_lock);
        if (!modern_menus || !g_page.up) return;
        page = g_page;
    }
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 64 || H < 64) return;
    const float s = H / 1080.0f;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* body = g_body ? g_body : ImGui::GetFont();
    ImFont* title = g_title ? g_title : body;
    ImFont* small = g_small ? g_small : body;
    const float fb = 40 * s, ft = 84 * s, fs = 28 * s;

    // the panel: navy down the left, fading out over the movie; a thin electric line at its edge
    const float pw = std::max(W * 0.5f, 980 * s);
    dl->AddRectFilledMultiColor({0, 0}, {pw * 0.62f, H}, IM_COL32(5, 14, 36, 238), IM_COL32(5, 14, 36, 210),
                                IM_COL32(5, 14, 36, 210), IM_COL32(5, 14, 36, 238));
    dl->AddRectFilledMultiColor({pw * 0.62f, 0}, {pw, H}, IM_COL32(5, 14, 36, 210), IM_COL32(5, 14, 36, 0),
                                IM_COL32(5, 14, 36, 0), IM_COL32(5, 14, 36, 210));
    dl->AddRectFilledMultiColor({0, 0}, {W, 140 * s}, IM_COL32(9, 30, 78, 150), IM_COL32(9, 30, 78, 150),
                                IM_COL32(9, 30, 78, 0), IM_COL32(9, 30, 78, 0));
    dl->AddRectFilled({0, H - 96 * s}, {W, H}, IM_COL32(3, 9, 24, 200));
    dl->AddLine({0, H - 96 * s}, {W, H - 96 * s}, IM_COL32(40, 120, 255, 160), 2 * s);

    const float x0 = 96 * s, rw = pw - x0 - 90 * s;
    // the kicker and the title
    dl->AddText(small, fs, {x0, 64 * s}, kCyan, "007  NIGHTFIRE");
    dl->AddRectFilled({x0, 102 * s}, {x0 + 72 * s, 106 * s}, kElectric);
    std::string t = page.title.empty() ? std::string("MENU") : page.title;
    for (char& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
    dl->AddText(title, ft, {x0, 116 * s}, kWhite, t.c_str());

    // the rows: one under another, the focused one kept in view
    const float top = 250 * s, rh = 70 * s, gap = 12 * s, bottom = H - 190 * s;
    const int n = int(page.rows.size());
    static float scroll = 0, glow_y = -1;
    static uint32_t last_page = 0;
    if (page.page != last_page) scroll = 0, glow_y = -1, last_page = page.page;
    float want = scroll;
    if (page.focus >= 0) {
        float fy = page.focus * (rh + gap);
        if (fy - want < 0) want = fy;
        if (fy + rh - want > bottom - top) want = fy + rh - (bottom - top);
    }
    float most = std::max(0.0f, n * (rh + gap) - gap - (bottom - top));
    want = std::clamp(want, 0.0f, most);
    const float ease = 1 - std::exp(-io.DeltaTime * 14);
    scroll = Lerp(scroll, want, ease);

    ImVec2 m = io.MousePos;
    static int hovered = -1;
    int hover = -1;
    bool over_ui = ImGui::GetIO().WantCaptureMouse;  // the settings window (F1) open over it
    dl->PushClipRect({0, top - 4 * s}, {W, bottom + 4 * s}, true);
    for (int i = 0; i < n; i++) {
        const ModernRow& r = page.rows[i];
        float y = top + i * (rh + gap) - scroll;
        if (y + rh < top - rh || y > bottom + rh) continue;
        ImVec2 a{x0, y}, b{x0 + rw, y + rh};
        bool in = !over_ui && m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y && m.y >= top && m.y < bottom;
        if (in) hover = i;
        dl->AddRectFilled(a, b, in ? IM_COL32(30, 70, 150, 170) : IM_COL32(9, 24, 60, 150), 10 * s);
        if (r.focused) {
            if (glow_y < 0) glow_y = y;
            glow_y = Lerp(glow_y, y, ease);
        }
        ImU32 tc = r.disabled ? IM_COL32(140, 160, 190, 160) : kIce;
        float ty = y + (rh - fb) * 0.5f;
        dl->AddText(body, fb, {a.x + 28 * s, ty}, r.focused ? kWhite : tc, r.label.c_str());
        // a choice's, list's or slider's value: between arrows on the right
        if (r.kind == kModernChoice || r.kind == kModernSlider || r.kind == kModernList) {
            std::string v = r.value;
            ImVec2 vs = body->CalcTextSizeA(fb, 1e9f, 0, v.c_str());
            float vr = b.x - 64 * s, vl = vr - vs.x;
            if (r.kind == kModernSlider && r.slider_max > 0) {
                float bw = 220 * s, bx = vr - bw, by = y + rh * 0.5f;
                dl->AddRectFilled({bx, by - 3 * s}, {vr, by + 3 * s}, IM_COL32(255, 255, 255, 50), 3 * s);
                float f = std::clamp(float(r.slider_value) / float(r.slider_max), 0.0f, 1.0f);
                dl->AddRectFilled({bx, by - 3 * s}, {bx + bw * f, by + 3 * s}, kCyan, 3 * s);
                dl->AddCircleFilled({bx + bw * f, by}, 9 * s, kWhite);
                vl = bx;
            } else {
                dl->AddText(body, fb, {vl, ty}, r.focused ? kWhite : kCyan, v.c_str());
            }
            ImU32 ac = r.focused || in ? kCyan : IM_COL32(120, 160, 220, 120);
            float cy = y + rh * 0.5f, l = vl - 34 * s, rr = b.x - 34 * s;
            dl->AddTriangleFilled({l - 8 * s, cy}, {l + 4 * s, cy - 10 * s}, {l + 4 * s, cy + 10 * s}, ac);
            dl->AddTriangleFilled({rr + 8 * s, cy}, {rr - 4 * s, cy - 10 * s}, {rr - 4 * s, cy + 10 * s}, ac);
        } else if (r.focused) {
            float cy = y + rh * 0.5f, rr = b.x - 34 * s;
            dl->AddTriangleFilled({rr + 8 * s, cy}, {rr - 4 * s, cy - 10 * s}, {rr - 4 * s, cy + 10 * s}, kWhite);
        }
    }
    // the focus: a bright bar sliding to its row, an electric edge on the left
    if (glow_y >= 0 && page.focus >= 0) {
        ImVec2 a{x0, glow_y}, b{x0 + rw, glow_y + rh};
        dl->AddRectFilledMultiColor(a, b, IM_COL32(40, 120, 255, 120), IM_COL32(40, 120, 255, 20),
                                    IM_COL32(40, 120, 255, 20), IM_COL32(40, 120, 255, 120));
        dl->AddRect(a, b, IM_COL32(120, 210, 255, 220), 10 * s, 0, 2 * s);
        dl->AddRectFilled({a.x, a.y + 10 * s}, {a.x + 6 * s, b.y - 10 * s}, kCyan, 3 * s);
    }
    dl->PopClipRect();
    // more above or below: a fade
    if (scroll > 1) dl->AddRectFilledMultiColor({0, top - 4 * s}, {pw, top + 40 * s}, IM_COL32(5, 14, 36, 230),
                                                IM_COL32(5, 14, 36, 230), IM_COL32(5, 14, 36, 0), IM_COL32(5, 14, 36, 0));
    if (scroll < most - 1)
        dl->AddRectFilledMultiColor({0, bottom - 40 * s}, {pw, bottom + 4 * s}, IM_COL32(5, 14, 36, 0),
                                    IM_COL32(5, 14, 36, 0), IM_COL32(5, 14, 36, 230), IM_COL32(5, 14, 36, 230));

    // the help line
    if (!page.help.empty())
        dl->AddText(small, fs, {x0, bottom + 30 * s}, IM_COL32(170, 200, 240, 230), page.help.c_str(), nullptr, rw);

    // the footer: Back, and the keys
    ImVec2 ba{x0, H - 74 * s}, bb{x0 + 170 * s, H - 22 * s};
    bool on_back = !over_ui && m.x >= ba.x && m.x < bb.x && m.y >= ba.y && m.y < bb.y;
    dl->AddRectFilled(ba, bb, on_back ? IM_COL32(40, 120, 255, 200) : IM_COL32(20, 50, 120, 170), 26 * s);
    dl->AddText(body, fb * 0.8f, {ba.x + 30 * s, ba.y + 10 * s}, kWhite, "<  Back");
    // the page's own button line, its marks as keys (~A Enter, ~B Esc, ~V up / down, ~H left / right), else ours
    std::string prompt;
    for (size_t i = 0; i < page.prompt.size(); i++) {
        char c = page.prompt[i];
        if (c != '~' || i + 1 >= page.prompt.size()) {
            prompt += c;
            continue;
        }
        char k = page.prompt[++i];
        prompt += k == 'A' ? "[Enter]" : k == 'B' ? "[Esc]" : k == 'V' ? "[Up/Down]" : k == 'H' ? "[Left/Right]" : std::string("[") + k + "]";
    }
    const char* hint = !prompt.empty() ? prompt.c_str() : "[Enter] Select     [Esc] Back     [Left/Right] Change     [Wheel] Scroll";
    ImVec2 hs = small->CalcTextSizeA(fs, 1e9f, 0, hint);
    dl->AddText(small, fs, {W - hs.x - 64 * s, H - 62 * s}, IM_COL32(150, 180, 230, 200), hint);

    // the mouse: hover moves the game's focus, a click plays the row
    std::lock_guard lock(g_lock);
    // only a mouse moved takes the focus (one resting over a row as a page opens leaves the keys theirs)
    static ImVec2 last_m{-1, -1};
    if (last_m.x < 0) last_m = m;  // the first frame: where it rests is no move
    const bool moved = std::abs(m.x - last_m.x) + std::abs(m.y - last_m.y) > 3 * s;
    last_m = m;
    if (!moved) hovered = hover;
    if (moved && hover != hovered && hover >= 0 && !page.rows[hover].focused &&
        (!page.rows[hover].list || page.focus < 0 || page.rows[page.focus].id != page.rows[hover].id))
        g_actions.push_back({page.rows[hover].id, -1, page.page, false, 0});
    hovered = hover;
    // the buttons as the game's input reads them (a real click, or one a test plays): pressed since the last frame
    static uint32_t was_held = 0;
    const uint32_t held = input::MouseButtons(), pressed = held & ~was_held;
    was_held = held;
    const bool left = pressed & 1, right = pressed & 2;
    if (over_ui) return;
    if (right || (on_back && left)) {
        g_actions.push_back({0, input::kB, page.page, false, 0});
        return;
    }
    if (hover >= 0 && left) {
        const ModernRow& r = page.rows[hover];
        int c = input::kA;
        if (r.kind == kModernChoice || r.kind == kModernSlider) {
            float mid = x0 + rw * 0.5f;  // a value: left of the middle goes back, right of it on
            c = m.x < mid ? input::kLeft : input::kRight;
        }
        g_actions.push_back({r.id, c, page.page, r.list, r.list_step});
    }
#endif
}

}  // namespace nf
