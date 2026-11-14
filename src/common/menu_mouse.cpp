// The mouse in the menus: the wheel scrolls (d-pad up and down), the left button selects (A), the right goes back (B),
// and Enter selects too. While a menu is up (auf::MenuUp, each image's own test) the mouse's buttons stop playing what
// they are bound to in play and play A and B instead; each notch of the wheel is a press of the d-pad. In play a notch
// cycles the weapon: on foot the d-pad's down a notch down and up a notch up; in the car Black and White. On a mounted
// gun (auf::OnGun) the left button fires and the right zooms. The menus show the Windows cursor, and the left button
// selects what it is over (src/bond/pointer.cpp, src/driving/pointer.cpp).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

#include "common/auf.h"
#include "core/input.h"
#include "core/log.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "app/app.h"

namespace auf {
namespace {
int g_test_key = 0;  // a test's key (AUF_PAD's esc, enter), held this poll
bool KeyHeld(int vk) {
    if (vk == g_test_key) return true;
    return input::GameFocused() && input::KeyDown(vk);
}

// AUF_POINTER=frame:x:y:buttons,... headless: the pointer from each frame of polls on, in 640x480
struct Forced {
    bool on = false;
    float x = 0, y = 0;
    uint32_t buttons = 0;
};
Forced g_forced;
void ForcedPoll() {
    static const char* e = std::getenv("AUF_POINTER");
    static int frame = 0;
    if (!e || !*e) return;
    frame++;
    g_forced.on = true;
    int at, used;
    float x, y;
    uint32_t b;
    while (std::sscanf(e, "%d:%f:%f:%u%n", &at, &x, &y, &b, &used) == 4 && at <= frame) {
        g_forced.x = x, g_forced.y = y, g_forced.buttons = b;
        e += used;
        if (*e == ',') e++;
    }
}

void Press(input::Pad& p, int c);

// AUF_PAD=secs:what,... a test's presses in bond_eng.xbe (Start pressed until the main menu is up, Enter with AUF_TITLE_ENTER), timed from then: what a control's settings name
// (start, a, dpad_down, left_stick_up ...) held 150 ms or the seconds after a '*' (left_stick_up*1.5), or a modern menu row's click: mN (A on row N), mNl / mNr (left / right on it),
// mNf (only focus it), mb (back), or a key: esc, enter
void ScriptedPoll(input::Pad& p) {
    static const char* e = std::getenv("AUF_PAD");
    if (!e || !*e || kImage != kBond) return;
    using namespace std::chrono;
    // until the main menu is up Start is pressed every 2 s (the title screen); the clock starts with it
    static steady_clock::time_point t0{};
    static const auto boot = steady_clock::now();
    if (!ModernMenuWasSeen()) {
        const double b = duration<double>(steady_clock::now() - boot).count();
        static const bool enter = std::getenv("AUF_TITLE_ENTER") != nullptr;  // Enter for it instead, as a player's key
        if (b > 4 && std::fmod(b, 2.0) < 0.3) {
            if (enter) g_test_key = 0x0D;
            else Press(p, input::kStart);
        }
        return;
    }
    if (t0 == steady_clock::time_point{}) t0 = steady_clock::now();
    const double t = duration<double>(steady_clock::now() - t0).count();
    static size_t done = 0;  // the steps already played (each click once)
    const char* s = e;
    size_t k = 0;
    double at;
    char what[32];
    int used;
    while (std::sscanf(s, "%lf:%31[^,]%n", &at, what, &used) == 2) {
        s += used;
        if (*s == ',') s++;
        if (t < at) break;
        if (what[0] == 'm' && (what[1] == 'b' || (what[1] >= '0' && what[1] <= '9'))) {
            if (k++ < done) continue;
            done = k;
            int row = what[1] == 'b' ? 0 : std::atoi(what + 1);
            const char last = what[std::strlen(what) - 1];
            const int c = what[1] == 'b' ? input::kB : last == 'l' ? input::kLeft : last == 'r' ? input::kRight
                                                       : last == 'f' ? -1 : input::kA;
            LOGR("auf: test click row %d control %d", row, c);
            ModernMenuTestClick(uint32_t(row), c);
            continue;
        }
        k++;
        double hold = 0.15;
        if (char* star = std::strchr(what, '*')) hold = std::atof(star + 1), *star = 0;
        if (t >= at + hold) continue;
        if (!std::strcmp(what, "esc") || !std::strcmp(what, "enter")) {
            g_test_key = what[0] == 'e' && what[1] == 's' ? 0x1B : 0x0D;
            continue;
        }
        const int c = input::ControlByName(what);
        if (c >= 0) Press(p, c);
    }
}

// AUF_PAD2=secs:what,... a second controller for a split screen test: plugged in, its presses (control names, held
// 150 ms or the seconds after a '*') timed as AUF_PAD's, from the main menu
void ScriptedPoll2(input::Pad& p) {
    static const char* e = std::getenv("AUF_PAD2");
    if (!e || !*e || kImage != kBond) return;
    p.connected = true;
    using namespace std::chrono;
    static steady_clock::time_point t0{};
    if (!ModernMenuWasSeen()) return;
    if (t0 == steady_clock::time_point{}) t0 = steady_clock::now();
    const double t = duration<double>(steady_clock::now() - t0).count();
    const char* s = e;
    double at;
    char what[32];
    int used;
    while (std::sscanf(s, "%lf:%31[^,]%n", &at, what, &used) == 2) {
        s += used;
        if (*s == ',') s++;
        if (t < at) break;
        double hold = 0.15;
        if (char* star = std::strchr(what, '*')) hold = std::atof(star + 1), *star = 0;
        if (t >= at + hold) continue;
        const int c = input::ControlByName(what);
        if (c >= 0) Press(p, c);
    }
}

#ifdef _WIN32
// the arrow while the pointer is up: the runtime hides the cursor while the mouse looks
WNDPROC g_proc = nullptr;
std::atomic<bool> g_arrow{false};
LRESULT CALLBACK PointerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT && g_arrow.load() && input::MouseLooking()) {
        SetCursor(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW)));
        return TRUE;
    }
    // nor does the runtime's timer take a resting arrow away (sdk main.cpp, kCursorTimer)
    if (msg == WM_TIMER && wp == 0x5243 && g_arrow.load() && input::MouseLooking()) return 0;
    return CallWindowProcW(g_proc, hwnd, msg, wp, lp);
}
void ShowArrow(bool show) {
    HWND hwnd = static_cast<HWND>(app::Window());
    if (!hwnd) return;
    if (!g_proc) {
        g_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PointerProc)));
        LOGR("auf: the menus' pointer is ready");
    }
    if (g_arrow.exchange(show) != show) PostMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}
#endif

// a press of control c (a d-pad bit or an analog button)
void Press(input::Pad& p, int c) {
    if (c < input::kA) p.buttons |= uint16_t(1u << c);
    else if (c < input::kLSLeft) p.analog[c - input::kA] = 255;
    else {  // a stick direction all the way (this wrote past analog[] before)
        static constexpr int16_t kFull[4] = {-32768, 32767, 32767, -32768};  // left, right, up, down
        const int d = (c - input::kLSLeft) % 4;
        int16_t& axis = c < input::kRSLeft ? (d < 2 ? p.lx : p.ly) : (d < 2 ? p.rx : p.ry);
        axis = kFull[d];
    }
}
}  // namespace

bool PointerAt(float& x, float& y) {
    if (g_forced.on) {
        x = g_forced.x, y = g_forced.y;
        return true;
    }
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(app::Window());
    POINT c;
    RECT r;
    if (!input::UsingKeyboard() || GetForegroundWindow() != hwnd || !GetCursorPos(&c) || !ScreenToClient(hwnd, &c) ||
        !GetClientRect(hwnd, &r) || r.right <= 0 || r.bottom <= 0)
        return false;
    // the menus' 4:3 stays in the frame's middle at any width
    x = 640 * (0.5f + (float(c.x) / r.right - 0.5f) * FrameWidth());
    y = 480 * float(c.y) / r.bottom;
    return true;
#else
    // the runtime's cursor in the 2D's 4:3 (the frame's letterbox and wide squeeze taken out), as above
    float u, v;
    if (!input::UsingKeyboard() || !input::GameFocused() || !app::FramePoint(u, v)) return false;
    x = 640 * u;
    y = 480 * v;
    return true;
#endif
}

uint32_t PointerButtons() { return g_forced.on ? g_forced.buttons : input::MouseButtons(); }

namespace {
std::atomic<bool> g_start_only{false}, g_pause_top{false}, g_resume{false};
}
void MenuKeysPage(bool start_only, bool pause_top) {
    g_start_only.store(start_only, std::memory_order_relaxed);
    g_pause_top.store(pause_top, std::memory_order_relaxed);
}
bool TakeResume() { return g_resume.exchange(false); }

void MenuMouse(int port, input::Pad& p) {
    constexpr int64_t kPress = 70, kGap = 70;  // ms
    constexpr uint16_t kMouseBits[3] = {0x01, 0x02, 0x04};  // VK_LBUTTON VK_RBUTTON VK_MBUTTON
    if (port == 1) ScriptedPoll2(p);
    if (port != 0) return;
    RefreshButtons();  // the button pictures for the keyboard or the controller, as it changes
    static int pending = 0;  // notches to play, up positive
    static int64_t pulse = 0;
    static bool was_menu = false;
    int notches = input::TakeWheel();  // taken in play too, so none wait for the next menu
    ForcedPoll();
    g_test_key = 0;
    ScriptedPoll(p);
    uint32_t held = PointerButtons();
    using namespace std::chrono;
    int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    bool menu = MenuUp();
    // the pointer free in the menus (the runtime holds it in the window in play only), and in a test's run (AUF_PAD,
    // AUF_POINTER), which must not trap the mouse of whoever sits at the desktop
    static const bool test = std::getenv("AUF_PAD") || std::getenv("AUF_POINTER");
    input::MouseFree(menu || test || kImage == kDefault);
#ifdef _WIN32
    if (kImage != kDefault) ShowArrow(PointerWanted() && input::UsingKeyboard());
#endif
    static bool was = false;
    if (menu != was) LOGR("auf: %s", menu ? "a menu is up: the mouse plays A, B and the d-pad" : "out of the menus");
    was = menu;
    if (kImage == kBond && !menu && Zoomed() && notches) {  // through a scope the wheel zooms
        ScopeZoom(notches);
        notches = 0;
    }
    // on foot Enter is the console's A everywhere, and where Bond is not the player's to move (a cutscene, a hint's "Press A") the left
    // button too
    if (kImage == kBond && !menu && input::UsingKeyboard() && (KeyHeld(0x0D) || (!PlayerFree() && (held & 1)))) Press(p, input::kA);
    int next = input::kRight, prev = input::kLeft;
    if (menu) next = input::kDown, prev = input::kUp;
    else if (kImage == kDriving) next = input::kBlack, prev = input::kWhite;
    else if (kImage == kBond) next = input::kDown, prev = input::kUp;  // the d-pad's up and down are the weapon on foot
    if (kImage == kDriving && !menu && OnGun() && input::UsingKeyboard()) {
        // the weapons as on foot: the d-pad's down and up, by 1 and 3, Q and the wheel
        next = input::kDown, prev = input::kUp;
        if (KeyHeld('1') || KeyHeld('Q')) Press(p, input::kDown);
        if (KeyHeld('3')) Press(p, input::kUp);
        for (int c : {input::kA, input::kX, input::kLT, input::kRT}) p.analog[c - input::kA] = 0;
        if (held & 1) Press(p, input::kRT);
        if (held & 2) Press(p, input::kLT);
        if (KeyHeld('R')) Press(p, input::kA);  // reload: povcfg0.def's GAMEACTION_POVRELOAD is +button0, A (X does nothing there)
    }
    // Escape, one of Start's keys: in bond_eng.xbe's menus back (B), and at the pause's top back to the game, not Start,
    // which the pause reads as A too (0x56A95) and the front end's pages as B or nothing. Only a press made in a menu: the
    // one that opened the pause is let go first
    static bool esc_was = false, esc_ours = false;
    const bool esc = KeyHeld(0x1B);
    if (esc && !esc_was) esc_ours = menu;
    if (!esc) esc_ours = false;
    const bool esc_press = esc && !esc_was;
    esc_was = esc;
    if (!menu) g_resume = false;
    if (menu) {
        // Enter: A; Start on a page with nothing to select (the title's "Press Start")
        const bool start_only = kImage == kBond && g_start_only.load(std::memory_order_relaxed);
        if (KeyHeld(0x0D)) {
            if (start_only) Press(p, input::kStart);
            else p.analog[0] = 255;
        }
        const auto start_keys = input::ControlKeys(input::kStart);
        if (kImage == kBond && esc && !start_only && input::UsingKeyboard() && (start_keys[0] == 0x1B || start_keys[1] == 0x1B)) {
            const uint16_t other = start_keys[0] == 0x1B ? start_keys[1] : start_keys[0];
            if (!other || !KeyHeld(other)) p.buttons = uint16_t(p.buttons & ~(1u << input::kStart));
            const bool top = g_pause_top.load(std::memory_order_relaxed);
            if (esc_ours && top && esc_press) {
                g_resume = true;
                LOGR("auf: Escape at the pause's top: back to the game");
            } else if (esc_ours && !top) {
                Press(p, input::kB);
            }
        }
        // the arrow keys are the d-pad in any menu (DRIVING.XBE's keys have them on the left stick, once)
        const int vks[4] = {0x26, 0x28, 0x25, 0x27}, pads[4] = {input::kUp, input::kDown, input::kLeft, input::kRight};
        for (int i = 0; i < 4; i++)
            if (KeyHeld(vks[i])) {
                Press(p, pads[i]);
                if (i < 2) p.ly = 0;
                else p.lx = 0;
            }
        // the buttons out of what they play in play
        for (int c = 0; c < input::kA + 8; c++) {
            auto keys = input::ControlKeys(c);
            bool mouse = false;
            for (int b = 0; b < 3; b++)
                if (held >> b & 1) mouse |= keys[0] == kMouseBits[b] || keys[1] == kMouseBits[b];
            if (!mouse) continue;
            if (c < input::kA) p.buttons = uint16_t(p.buttons & ~(1u << c));
            else p.analog[c - input::kA] = 0;
        }
        // A: once the pointer's item is the current one; a press off every item does nothing until it is let go
        // what it plays is read a poll after the press: the game's thread must have seen the pointer where it now is
        static bool was_held = false, arming = false;
        static int armed = kClickNone;
        int click = MenuClick();
        if (arming) {
            armed = click;
            LOGR("auf: menu click plays %d", armed);
        }
        const bool playing = (held & 1) && !arming && was_held;
        arming = (held & 1) && !was_held;
        was_held = held & 1;
        if (playing) {
            if (armed == kClickLeft) Press(p, input::kLeft);
            else if (armed == kClickRight) Press(p, input::kRight);
            else if (armed == kClickDown) Press(p, input::kDown);
            else if (armed != kClickNone && click == kClickGo) p.analog[0] = 255;
        }
    }
    // the wheel: a d-pad press a notch
    if (menu != was_menu) pending = 0;
    was_menu = menu;
    if (notches && !pending) pulse = now;
    pending = std::clamp(pending + notches, -8, 8);
    if (pending) {
        int64_t t = now - pulse;
        if (t < kPress) {
            Press(p, pending > 0 ? prev : next);
        } else if (t >= kPress + kGap) {
            pending += pending > 0 ? -1 : 1;
            pulse = now;
        }
    }
}

}  // namespace auf
