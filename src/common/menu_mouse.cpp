// The mouse in the menus: a pointer (the cursor shown and free there, GameInfo::menu_up), whose hover moves the
// game's own focus onto the row under it, a press of the d-pad at a time, so the highlight and the sound are the
// game's; a left click then plays what the controller would there (A on an item or button, left or right on a
// choice's or slider's value, by the side of its middle clicked), the right button goes back (B), the wheel scrolls
// (d-pad up and down), and Enter selects too (A's keys are crouch's in play). Where the menu up has no geometry
// (Driving.xbe's EA menus, a dialog) the left button is A on what is focused, as before. The rows are the menu
// file's widget records (nf::MenuGeometry), in the console's 640 x 480; the cursor is mapped there through the
// shown frame (app::FramePoint). A list (the pause screen's choices, a codename list) is one widget: hovering it
// focuses it, the wheel moves through it, a click plays A on its chosen entry. While a menu is up (nf::MenuUp, each exe's own test),
// the mouse's buttons stop playing what they are bound to in play (fire, aim) and play A and B instead; each notch of
// the wheel is a press of the d-pad (EoN's way). In play each notch cycles the weapon instead: on foot the d-pad's
// right a notch down and left a notch up (the NightFire style's Cycle Weapon), through a scope a zoom step
// (ScopeZoom), in the car Black and White, on a mounted gun its style's; there the mouse's buttons fire
// and zoom too (GunStyle).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "common/nightfire.h"
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

#include "app/app.h"
#endif

namespace nf {
namespace {
bool KeyHeld(int vk) {
#ifdef _WIN32
    return GetForegroundWindow() == static_cast<HWND>(app::Window()) && (GetAsyncKeyState(vk) & 0x8000) &&
           !app::KeyCaptureBusy();  // a key being bound in the menus
#else
    (void)vk;
    return false;
#endif
}

// a press of control c (a d-pad bit or an analog button)
void Press(input::Pad& p, int c) {
    if (c < input::kA) p.buttons |= uint16_t(1u << c);
    else p.analog[c - input::kA] = 255;
}
}  // namespace

// the pointer's picture: an arrow in the menus' selection orange, a dark outline, premultiplied BGRA
void MenuCursor(int size, std::vector<uint32_t>& bgra, int& hot_x, int& hot_y) {
    const float pts[][2] = {{0.06f, 0.04f}, {0.06f, 0.80f}, {0.25f, 0.62f}, {0.40f, 0.94f}, {0.53f, 0.88f},
                            {0.38f, 0.57f}, {0.64f, 0.57f}};
    constexpr int n = 7, ss = 4;
    auto inside = [&](float x, float y, float grow) {
        // inside the polygon (even-odd), or within grow of an edge
        bool in = false;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float xi = pts[i][0], yi = pts[i][1], xj = pts[j][0], yj = pts[j][1];
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
        }
        if (in || grow <= 0) return in;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float ax = pts[j][0], ay = pts[j][1], bx = pts[i][0], by = pts[i][1];
            float dx = bx - ax, dy = by - ay, t = std::clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
            float ex = ax + t * dx - x, ey = ay + t * dy - y;
            if (ex * ex + ey * ey < grow * grow) return true;
        }
        return false;
    };
    bgra.assign(size_t(size) * size, 0);
    float edge = 1.6f / size;  // the outline, in the picture's units
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float fill = 0, out = 0, light = 0;
            for (int sy = 0; sy < ss; sy++)
                for (int sx = 0; sx < ss; sx++) {
                    float fx = (x + (sx + 0.5f) / ss) / size, fy = (y + (sy + 0.5f) / ss) / size;
                    if (inside(fx, fy, 0)) fill += 1, light += 1 - fy;
                    else if (inside(fx, fy, edge)) out += 1;
                }
            float a = (fill + out) / (ss * ss);
            if (a <= 0) continue;
            float k = fill / (ss * ss), g = fill > 0 ? light / fill : 0;
            // orange (0xF8A020) brightening to the top, the outline dark brown (0x281408)
            float r = k * (0.97f * (0.75f + 0.25f * g)) + (a - k) * 0.16f;
            float gr = k * (0.63f * (0.70f + 0.30f * g)) + (a - k) * 0.08f;
            float b = k * (0.13f) + (a - k) * 0.03f;
            auto c = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); };
            bgra[size_t(y) * size + x] = c(a) << 24 | c(r) << 16 | c(gr) << 8 | c(b);
        }
    hot_x = int(0.06f * size);
    hot_y = int(0.04f * size);
}

namespace {
// a row the pointer can be on: a widget id's widgets together (a label and its value share one)
struct Row {
    uint32_t id;
    float x0, y0, x1, y1;
    bool value;          // a choice or slider: its value part below
    float vx0, vx1;      // the value widget's span across
    bool list;
};
bool Focusable(const MenuWidget& w) {
    // the page widget, titles and pictures (0x10000001), the help line and its box, and empty widgets are not
    // (a backdrop the size of the screen neither)
    return w.w > 0 && w.h > 0 && (w.id >> 28) == 1 && w.id != 0x10000001 && w.id != 0x10000224 &&
           w.id != 0x10000225 && int(w.w) * w.h < 640 * 480 / 3;
}
std::vector<Row> Rows(const std::vector<MenuWidget>& widgets) {
    std::vector<Row> rows;
    for (const MenuWidget& w : widgets) {
        if (!Focusable(w)) continue;
        auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.id == w.id; });
        if (it == rows.end()) {
            rows.push_back({w.id, float(w.x), float(w.y), float(w.x + w.w), float(w.y + w.h), false, 0, 0, false});
            it = rows.end() - 1;
        } else {
            it->x0 = std::min(it->x0, float(w.x)), it->y0 = std::min(it->y0, float(w.y));
            it->x1 = std::max(it->x1, float(w.x + w.w)), it->y1 = std::max(it->y1, float(w.y + w.h));
        }
        if (w.type == 9 || w.type == 10) it->value = true, it->vx0 = w.x, it->vx1 = w.x + w.w;
        if (w.type == 6) it->list = true;
    }
    // in the order the d-pad moves through them: down the page, across a line
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        float ay = (a.y0 + a.y1) / 2, by = (b.y0 + b.y1) / 2;
        return std::fabs(ay - by) > 6 ? ay < by : a.x0 < b.x0;
    });
    return rows;
}
}  // namespace

namespace {
// the pointer's presses: one control held kHold ms, then kRest ms before the next, so the game sees each as a press
void Pointer(input::Pad& p, uint32_t held, int64_t now, bool moved) {
    constexpr int64_t kHold = 50, kRest = 50;
    static uint32_t was_held = 0;
    static int act = -1;
    static int64_t act_end = 0, rest_end = 0;
    static float last_x = -1, last_y = -1;
    static bool hovering = false;  // the pointer leads the focus until the d-pad moves it
    static uint32_t target = 0, page_was = 0;
    static uint32_t given_up = 0;  // a row the d-pad did not reach: not tried again until the pointer leaves it
    static int steps = 0, last_gap = -1;  // the focus's distance to the target after the last step
    static bool click = false, click_value = false, click_left = false;
    if (now < 0) {
        was_held = 0, act = -1, target = 0, click = false, hovering = false;
        return;
    }
    uint32_t pressed = held & ~was_held;
    was_held = held;
    // the d-pad's own, the wheel's and the left stick's moves: the focus theirs until the pointer moves
    if ((p.buttons & 0xF) || moved || std::abs(p.lx) > 12000 || std::abs(p.ly) > 12000) hovering = false, target = 0;
    if (act >= 0 && now < act_end) {
        if (act < input::kA) p.buttons |= uint16_t(1u << act);
        else p.analog[act - input::kA] = 255;
        return;
    }
    act = -1;
    auto play = [&](int c) {
        act = c;
        act_end = now + kHold;
        rest_end = act_end + kRest;
        if (c < input::kA) p.buttons |= uint16_t(1u << c);
        else p.analog[c - input::kA] = 255;
    };
    if (pressed & 2) {  // back
        click = false, target = 0;
        play(input::kB);
        return;
    }
    uint32_t page = 0, focused = 0;
    std::vector<MenuWidget> widgets;
    std::vector<Row> rows;
    if (MenuGeometry(page, widgets, focused)) rows = Rows(widgets);
    if (page != page_was) {
        // a new page: the pointer leads again only once it moves (one left over a row pulled the new page's focus
        // to it, against the keyboard and the d-pad)
        target = 0, click = false, steps = 0, page_was = page, given_up = 0, hovering = false, last_gap = -1;
        // each page's rows once in the log: what the pointer finds there
        static std::vector<uint32_t> told;
        if (page && std::find(told.begin(), told.end(), page) == told.end() && told.size() < 64) {
            told.push_back(page);
            std::string list;
            for (const Row& r : rows) {
                char b[96];
                std::snprintf(b, sizeof b, " %08X %.0f,%.0f-%.0f,%.0f%s%s", r.id, r.x0, r.y0, r.x1, r.y1,
                              r.value ? " value" : "", r.list ? " list" : "");
                list += b;
            }
            LOGR("nightfire: menu page %08X, %zu pointer rows:%s", page, rows.size(), list.c_str());
        }
    }
    float u, v;
    bool in = app::FramePoint(u, v);
    float gx = u * 640, gy = v * 480;
    // the pointer leads once it really moves: a few pixels from where it rested when it last stopped leading (not a
    // hand resting on the mouse)
    if (in && !hovering) {
        if (last_x < 0) last_x = gx, last_y = gy;
        if (std::fabs(gx - last_x) > 8.0f || std::fabs(gy - last_y) > 8.0f) hovering = true;
    }
    if (!in || hovering) last_x = -1;
    const Row* hit = nullptr;
    if (in)  // the smallest row under the pointer (a panel behind rows is a row too)
        for (const Row& r : rows)
            if (gx >= r.x0 - 4 && gx < r.x1 + 4 && gy >= r.y0 && gy < r.y1 &&
                (!hit || (r.x1 - r.x0) * (r.y1 - r.y0) < (hit->x1 - hit->x0) * (hit->y1 - hit->y0)))
                hit = &r;
    if (pressed & 1) {
        if (rows.empty()) {  // no geometry: A on what is focused (a page with rows but nothing focused yet is
                             // woken below, its A would keep and leave it)
            play(input::kA);
            return;
        }
        if (hit) {
            target = hit->id, steps = 0, click = true, last_gap = -1;
            click_value = hit->value && gx >= hit->vx0 - 24 && gx < hit->vx1 + 24;
            click_left = gx < (hit->vx0 + hit->vx1) / 2;
        }
    } else if (hovering && hit && hit->id != target && hit->id != given_up) {
        target = hit->id, steps = 0, last_gap = -1;
    }
    if (!hit || hit->id != given_up) given_up = 0;
    if (now < rest_end || !target) return;
    auto at = [&](uint32_t id) {
        for (size_t i = 0; i < rows.size(); i++)
            if (rows[i].id == id) return int(i);
        return -1;
    };
    int f = at(focused), t = at(target);
    if (t < 0) {
        target = 0, click = false;
        return;
    }
    if (f != t) {
        // a press toward it: across when they share a line, else up or down; given up if it does not get there
        auto give_up = [&] { given_up = target, target = 0, click = false, last_gap = -1; };
        // a step that brought the focus no nearer: the row is not reached this way (the menu's order differs from
        // the rows' places): given up at once, the menu not walked through by itself
        const int gap = f < 0 ? 1000 : std::abs(t - f);
        if (steps > 0 && last_gap >= 0 && gap >= last_gap) return give_up();
        last_gap = gap;
        if (++steps > 16) return give_up();
        if (f < 0) {  // nothing focused yet (a page waits for the d-pad's first press): one wakes it
            if (steps > 2) return give_up();  // still on no row (a panel): not the d-pad's to move further
            play(input::kDown);
            return;
        }
        const Row &a = rows[size_t(f)], &b = rows[size_t(t)];
        bool across = std::fabs((a.y0 + a.y1) / 2 - (b.y0 + b.y1) / 2) <= 6;
        if (across && a.value) return give_up();  // left and right on a value change it, not the focus
        play(across ? (t > f ? input::kRight : input::kLeft) : (t > f ? input::kDown : input::kUp));
        return;
    }
    last_gap = -1;
    if (click) {
        click = false;
        const Row& r = rows[size_t(t)];
        if (r.value) {
            if (click_value) play(click_left ? input::kLeft : input::kRight);  // a choice or slider: its value
        } else {
            play(input::kA);  // an item, a button, a list's chosen entry
        }
        target = 0;
        return;
    }
    if (!hovering) target = 0;
}
}  // namespace

void MenuMouse(int port, input::Pad& p) {
    constexpr int64_t kPress = 70, kGap = 70;  // ms
    constexpr uint16_t kMouseBits[3] = {0x01, 0x02, 0x04};  // VK_LBUTTON VK_RBUTTON VK_MBUTTON
    if (port != 0) return;
    RefreshButtons();  // the button pictures for the keyboard or the controller, as it changes
    static int pending = 0;  // notches to play, up positive
    static int64_t pulse = 0;
    static bool was_menu = false;
    int notches = input::TakeWheel();  // taken in play too, so none wait for the next menu
    uint32_t held = input::MouseButtons();
    using namespace std::chrono;
    int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    bool menu = MenuUp();
    static bool was = false;
    if (menu != was) LOGR("nightfire: %s", menu ? "a menu is up: the mouse plays A, B and the d-pad" : "out of the menus");
    was = menu;
    // the wheel's two controls: the menus scroll, on foot the d-pad cycles the weapon (down the next), in the car Black
    // and White, on a mounted gun its style's (data\\control\\povcfgN.def: White/Black in 4, the d-pad's up and down
    // in 6 and 7, else its left and right)
    int gun = kDriving ? GunStyle() : -1;
    int next = input::kRight, prev = input::kLeft;
    if (menu) next = input::kDown, prev = input::kUp;
    else if (gun == 4) next = input::kBlack, prev = input::kWhite;
    else if (gun == 6 || gun == 7) next = input::kDown, prev = input::kUp;
    else if (kDriving && gun < 0) next = input::kBlack, prev = input::kWhite;
    // through a scope the wheel zooms (a step of the scope's angle a notch, on the game's thread: ScopeZoom)
    if (!kDriving && !menu && Zoomed() && notches) {
        ScopeZoom(notches);
        notches = 0;
    }
    // on foot where Bond is not the player's to move (a door's keypad, a missile, a cutscene) A is the console's
    // confirm, which the keyboard has as crouch (C, Ctrl): the left button and Enter press it too
    if (!kDriving && !menu && !PlayerFree() && input::UsingKeyboard() && ((held & 1) || KeyHeld(0x0D)))
        Press(p, input::kA);
    // on a mounted gun the car's buttons do not apply: the left button fires (RT), the right zooms (LT), R reloads (X
    // in style 0, else A)
    if (gun >= 0 && input::UsingKeyboard()) {
        for (int c : {input::kA, input::kX, input::kLT, input::kRT}) p.analog[c - input::kA] = 0;
        if (held & 1) Press(p, input::kRT);
        if (held & 2) Press(p, input::kLT);
        if (KeyHeld('R')) Press(p, gun == 0 ? input::kX : input::kA);
    }
    if (menu) {
        if (KeyHeld(0x0D)) p.analog[0] = 255;  // Enter: A
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
        Pointer(p, held, now, notches != 0 || pending != 0);
    } else {
        Pointer(p, 0, -1, false);  // out of the menus: nothing kept
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

}  // namespace nf
