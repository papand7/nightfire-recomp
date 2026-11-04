// PC Settings as a page of the game's own menus (default.xbe), from Edit Codename's list (Controller Setup, Driving
// Controller, Advanced Options, Multiplayer Options, AV Options, Save Codename) and the main menu's item.
//
// The page is the AV Options page (0x40000031) cloned into the menu file the page builder reads (src/game/
// pc_settings_item.cpp's copy), its page and row widgets given ids of ours: two sliders (the volumes' rows: field of
// view, mouse sensitivity) and four choices (invert mouse, skip intro movies, sharp text, aspect), the Restore Defaults
// button as "More Settings" (the settings window), the rest hidden as AV Options hides its own. The game's widget
// messages drive it as AV Options' handler 0x8D7A0 does (sub_72820(player, id, message, a, b)): a slider 0x27 its
// steps, 0x69, 0x2E its value, 0x40 read; a choice 0x17 cleared, 0x10 a choice added (its text, its value), 0x1E the
// choice of a value, 0x35 its value read; 0x2B 1
// hides; the page 0x4C entered, 0x4B A on an item (kept, and back with 0x5F). The help box (0x10000225) gets each row's
// line as it is focused.
//
// Edit Codename's list is the table 0x17CEC8 (7 entries of 24 bytes: picture, label, help, index, enabled, help when
// disabled). The list's handler 0x8C7F0 sets its range (message 0x27, 0 to 6) as it is made (0x51), on a focus move
// (0x49, 0x54) has 0x7F680 draw the rows' labels, help and picture from the table, and on A reads the index (0x40) and
// goes to that entry's page. It gets a copy with an eighth entry, ours, whose label and help are handles GetText
// (0x6D460) is made to answer; 0x8C7F0's direct call to 0x7F680 bypasses that function's hook, so the focus moves are
// done in 0x8C7F0's hook. Before the list is made its "index" is the widget's own pointer, which the game's
// arithmetic wraps into readable memory and the copy's would not: the copy is used only for a real index.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <mutex>
#include <vector>

#include "app/app.h"
#include "common/nightfire.h"
#include "game/menu_page.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nf {
extern uint32_t fov_percent;
}

namespace {
using kernel::R32;
using kernel::W32;

constexpr uint32_t kPage = 0x4000FC10;
constexpr uint32_t kFov = 0x1000FC11, kMouse = 0x1000FC12, kInvert = 0x1000FC13, kSkip = 0x1000FC14,
                   kSharp = 0x1000FC15, kAspect = 0x1000FC16, kMore = 0x1000FC17;
constexpr uint32_t kDisplay = 0x1000FC1B, kResolution = 0x1000FC1C, kVsync = 0x1000FC1D, kCap = 0x1000FC1E;
constexpr uint32_t kHelpBox = 0x10000225;
constexpr uint32_t kSetById = 0x72820, kGoTo = 0x92C40, kSetWidget = 0x72630;
constexpr uint32_t kMenuContext = 0x25F1D0, kContextSize = 0x1D8;
constexpr uint32_t kList = 0x10000100;  // Edit Codename's list widget
constexpr uint32_t kTable = 0x17CEC8, kEntry = 24, kEntries = 7;
constexpr uint32_t kLabelHandle = 0x0000FC01, kHelpHandle = 0x0100FC02;  // no text bank goes that far
constexpr uint32_t kPauseLabel = 0x0200FC03;  // the pause screen's choice (src/game/pc_settings_item.cpp)
constexpr uint32_t kAvPicture = 0x3000125;

// in memory past the title's, which the recompiled code reads as it is (AllocSystem's it may not: see ListCopy)
uint32_t GuestString(const char* s) {
    uint32_t n = uint32_t(std::strlen(s)) + 1, va = mem::AllocHostContiguous(n, 16);
    std::memcpy(kernel::HostPtr(va), s, n);
    return va;
}

using Row = nf::MenuRow;
constexpr nf::MenuRowKind kSliderRow = nf::kMenuSlider, kChoiceRow = nf::kMenuChoice, kButtonRow = nf::kMenuButton;
constexpr uint32_t kGraphics = 0x1000FC18;
const std::vector<Row> kRows = {
    {kFov, kSliderRow, "Field of View", "How wide the view is, on foot and driving. The game's is in the middle."},
    {kMouse, kSliderRow, "Mouse Sensitivity", "How far the view turns for the mouse's movement."},
    {kInvert, kChoiceRow, "Invert Mouse", "Moving the mouse forward looks down."},
    {kSkip, kChoiceRow, "Skip Intro Movies", "Starts without the EA and MGM logo movies."},
    {kSharp, kChoiceRow, "Sharp Text", "Draws the text and HUD pictures sharp at high resolutions."},
    {kAspect, kChoiceRow, "Aspect Ratio", "Auto follows the screen; Fill widens the view to an ultrawide screen."},
    {kDisplay, kChoiceRow, "Display Mode", "A window, a borderless window over the screen, or the screen itself."},
    {kResolution, kChoiceRow, "Resolution", "The window's or the screen's size; Desktop is the screen's own."},
    {kVsync, kChoiceRow, "Vertical Sync", "Frames shown with the screen's refresh: no tearing."},
    {kCap, kChoiceRow, "Frame Cap", "The most frames a second drawn."},
    {kGraphics, kButtonRow, "Graphics", "Picture quality, the shader style and its options."},
    {kMore, kButtonRow, "More Settings", "Sound, controls and the rest in the settings window."},
};
constexpr int16_t kFirstY = 78, kRowStep = 22;  // AV's rows are 28 apart from 86: twelve need it closer

// a string's guest copy, made once for each text (by its contents: a host pointer can be another string's later)
uint32_t Str(const char* s) {
    static std::vector<std::pair<std::string, uint32_t>> made;
    for (auto& [k, v] : made)
        if (k == s) return v;
    uint32_t va = GuestString(s);
    made.push_back({s, va});
    return va;
}

// a guest call's result is CallGuest's (the context is as it was after)
uint32_t SetById(X86Context& ctx, uint32_t player, uint32_t id, uint32_t msg, uint32_t a, uint32_t b = 0) {
    return kernel::CallGuest(ctx, kSetById, {player, id, msg, a, b});
}

// the settings as the page's controls hold them
uint32_t FovStep() { return uint32_t(std::clamp<int>((int(nf::fov_percent) - 50 + 2) / 5, 0, 20)); }  // 50-150 %
uint32_t MouseStep() {  // 25% to 400%, 100% the middle: 25 * 2^(step / 5)
    double s = std::max(1u, app::Get().mouse_sensitivity) / 25.0;
    return uint32_t(std::clamp<long>(std::lround(5 * std::log2(s)), 0, 20));
}

void Slider(X86Context& ctx, uint32_t player, uint32_t id, uint32_t value) {
    SetById(ctx, player, id, 0x27, 0, 20);
    SetById(ctx, player, id, 0x69, 0);
    SetById(ctx, player, id, 0x2E, value);
}
// a choice's items and their values (b), which 0x1E selects by and 0x35 gives back
void Choice(X86Context& ctx, uint32_t player, uint32_t id, const std::vector<std::pair<const char*, uint32_t>>& items,
            uint32_t value) {
    SetById(ctx, player, id, 0x17, 0);
    for (auto& [text, v] : items) SetById(ctx, player, id, 0x10, Str(text), v);
    SetById(ctx, player, id, 0x1E, value);
}
std::vector<std::pair<const char*, uint32_t>> Numbered(std::initializer_list<const char*> items) {
    std::vector<std::pair<const char*, uint32_t>> v;
    for (const char* s : items) v.push_back({s, uint32_t(v.size())});
    return v;
}

// the monitor's sizes, widest first (as the settings window lists them), Desktop first; value i+1 the ith
struct Size {
    uint32_t w, h;
};
std::vector<Size> Sizes() {
    std::vector<Size> sizes;
#ifdef _WIN32
    HMONITOR mon = static_cast<HMONITOR>(app::Monitor());
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        for (DWORD i = 0; EnumDisplaySettingsW(mi.szDevice, i, &dm); i++) {
            if (dm.dmBitsPerPel < 32 || dm.dmPelsHeight < 480) continue;
            Size z{dm.dmPelsWidth, dm.dmPelsHeight};
            if (std::none_of(sizes.begin(), sizes.end(), [&](const Size& o) { return o.w == z.w && o.h == z.h; }))
                sizes.push_back(z);
        }
    }
    std::sort(sizes.begin(), sizes.end(),
              [](const Size& a, const Size& b) { return a.w != b.w ? a.w > b.w : a.h > b.h; });
#endif
    if (sizes.size() > 24) sizes.resize(24);  // the choice's list (the largest)
    return sizes;
}
std::vector<Size>& ShownSizes() {
    static std::vector<Size> v;
    return v;
}
const char* SizeText(Size z) {
    static std::deque<std::string> kept;  // the texts stay where they are
    char b[32];
    std::snprintf(b, sizeof b, "%u x %u", z.w, z.h);
    for (const std::string& k : kept)
        if (k == b) return k.c_str();
    kept.push_back(b);
    return kept.back().c_str();
}
constexpr uint32_t kCaps[] = {30, 60, 120, 144, 240, 0};
constexpr uint32_t kAsIs = 0xFE;  // a choice's item for a value the list does not have: Keep leaves it as it is
uint32_t g_fov_step = 0, g_mouse_step = 0;  // the sliders as entered: unmoved, the exact values stay

void Enter(X86Context& ctx, uint32_t player) {
    const app::Settings& s = app::Get();
    g_fov_step = FovStep(), g_mouse_step = MouseStep();
    Slider(ctx, player, kFov, g_fov_step);
    Slider(ctx, player, kMouse, g_mouse_step);
    Choice(ctx, player, kInvert, Numbered({"Off", "On"}), s.mouse_invert_y ? 1 : 0);
    Choice(ctx, player, kSkip, Numbered({"Off", "On"}), nf::skip_intro ? 1 : 0);
    Choice(ctx, player, kSharp, Numbered({"Off", "On"}), s.sharp_2d ? 1 : 0);
    Choice(ctx, player, kAspect, Numbered({"Auto", "4:3", "16:9", "Fill"}), std::min<uint32_t>(s.aspect, 3));
    using app::DisplayMode;
    Choice(ctx, player, kDisplay,
           {{"Window", uint32_t(DisplayMode::Windowed)}, {"Borderless", uint32_t(DisplayMode::Borderless)},
            {"Fullscreen", uint32_t(DisplayMode::Fullscreen)}},
           uint32_t(s.display_mode));
    ShownSizes() = Sizes();
    std::vector<std::pair<const char*, uint32_t>> sizes = {{"Desktop", 0}};
    uint32_t size = 0;
    for (size_t i = 0; i < ShownSizes().size(); i++) {
        Size z = ShownSizes()[i];
        sizes.push_back({SizeText(z), uint32_t(i + 1)});
        if (z.w == s.width && z.h == s.height) size = uint32_t(i + 1);
    }
    if (!size && (s.width || s.height)) {  // a size not in the list (set by hand, another monitor's)
        sizes.push_back({SizeText({s.width, s.height}), kAsIs});
        size = kAsIs;
    }
    Choice(ctx, player, kResolution, sizes, size);
    Choice(ctx, player, kVsync, Numbered({"Off", "On"}), s.vsync ? 1 : 0);
    uint32_t cap = kAsIs;
    for (uint32_t i = 0; i < std::size(kCaps); i++)
        if (kCaps[i] == s.fps_cap) cap = i;
    auto caps = Numbered({"30", "60", "120", "144", "240", "Unlimited"});
    if (cap == kAsIs) caps.push_back({SizeText({s.fps_cap, 0}) , kAsIs});
    Choice(ctx, player, kCap, caps, cap);
    LOGR("nightfire: PC Settings page entered");
}

void Keep(X86Context& ctx, uint32_t player) {
    uint32_t fov = SetById(ctx, player, kFov, 0x40, 0) & 0xFF, mouse = SetById(ctx, player, kMouse, 0x40, 0) & 0xFF;
    uint32_t invert = SetById(ctx, player, kInvert, 0x35, 0) & 0xFF, skip = SetById(ctx, player, kSkip, 0x35, 0) & 0xFF;
    uint32_t sharp = SetById(ctx, player, kSharp, 0x35, 0) & 0xFF;
    uint32_t aspect = SetById(ctx, player, kAspect, 0x35, 0) & 0xFF;
    uint32_t display = SetById(ctx, player, kDisplay, 0x35, 0) & 0xFF;
    uint32_t size = SetById(ctx, player, kResolution, 0x35, 0) & 0xFF;
    uint32_t vsync = SetById(ctx, player, kVsync, 0x35, 0) & 0xFF, cap = SetById(ctx, player, kCap, 0x35, 0) & 0xFF;
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    app::Settings& s = app::Get();
    if (fov != g_fov_step) nf::fov_percent = 50 + std::min(fov, 20u) * 5;
    if (mouse != g_mouse_step)
        s.mouse_sensitivity = uint32_t(std::lround(25 * std::pow(2.0, std::min(mouse, 20u) / 5.0)));
    s.mouse_invert_y = invert == 1;
    nf::skip_intro = skip == 1;
    s.sharp_2d = sharp == 1;
    uint32_t w = 0, h = 0;
    if (size >= 1 && size <= ShownSizes().size()) w = ShownSizes()[size - 1].w, h = ShownSizes()[size - 1].h;
    if (size == kAsIs) w = s.width, h = s.height;
    app::DisplayMode mode = display <= 2 ? app::DisplayMode(display) : s.display_mode;
    bool apply = s.aspect != std::min(aspect, 3u) || s.display_mode != mode || s.width != w || s.height != h;
    s.aspect = std::min(aspect, 3u);
    s.display_mode = mode;
    s.width = w, s.height = h;
    s.vsync = vsync == 1;
    if (cap != kAsIs) s.fps_cap = kCaps[std::min<uint32_t>(cap, std::size(kCaps) - 1)];
    app::Save();
    if (apply) app::ApplyDisplay();
    LOGR("nightfire: PC Settings kept: field of view %u%%, mouse %u%%, invert %d, skip intro %d, sharp text %d, "
         "aspect %u, display %u, %ux%u, vsync %d, cap %u",
         nf::fov_percent, s.mouse_sensitivity, s.mouse_invert_y, nf::skip_intro, s.sharp_2d, s.aspect,
         uint32_t(s.display_mode), s.width, s.height, s.vsync, s.fps_cap);
}

// Edit Codename's list with our entry: the game's seven entries as they are now (it changes Save Codename's label as
// the page opens), then ours
// (and what follows the table, as the game has it: the list reads past its end before it is set up)
constexpr uint32_t kCopied = 64;
uint32_t ListCopy() {
    static uint32_t copy = 0;
    // past the title's memory, where the recompiled code reads it as it is (AllocSystem's pages it does not)
    if (!copy) copy = mem::AllocHostContiguous(kCopied * kEntry, 16);
    std::memcpy(kernel::HostPtr(copy), kernel::HostPtr(kTable), kCopied * kEntry);
    uint32_t e = copy + kEntries * kEntry;
    const uint32_t ours[6] = {kAvPicture, kLabelHandle, kHelpHandle, kEntries, 1, 0};
    for (int i = 0; i < 6; i++) W32(e + uint32_t(i) * 4, ours[i]);
    return copy;
}

bool Ours(uint32_t id) {
    for (const Row& r : kRows)
        if (r.id == id) return true;
    return false;
}

}  // namespace

namespace nf {
// the page's messages, from the dispatcher 0x8E320 (src/game/pc_settings_item.cpp): true when handled (al 1)
bool PcPageMessage(X86Context& ctx, uint32_t player, uint32_t widget, uint32_t msg, uint32_t arg) {
    uint32_t id = R32(widget + 0x18);
    if (id == kPage) {
        if (msg == 0x4C) Enter(ctx, player);
        if (msg == 0x4B) {
            if (arg && R32(arg + 0x18) == kMore) {
                std::lock_guard<std::recursive_mutex> lock(app::UiLock());
                if (!app::OverlayOpen()) app::ToggleOverlay();
                return true;
            }
            if (arg && R32(arg + 0x18) == kGraphics) {  // this page's values kept, then the Graphics page
                Keep(ctx, player);
                nf::OpenGraphicsPage(ctx, player);
                return true;
            }
            Keep(ctx, player);
            uint32_t eax = ctx.eax;
            kernel::CallGuest(ctx, kGoTo, {kMenuContext + (player & 0xFF) * kContextSize, 0x5F, 0, 0});
            ctx.eax = eax;
        }
        return true;
    }
    if (msg == 0x4E && Ours(id))  // focus gained: the row's help line
        for (const Row& r : kRows)
            if (r.id == id) SetById(ctx, player, kHelpBox, 0x18, Str(r.help));
    return false;
}

uint32_t MenuString(const char* s) { return Str(s); }
uint32_t MenuSet(X86Context& ctx, uint32_t player, uint32_t id, uint32_t msg, uint32_t a, uint32_t b) {
    return SetById(ctx, player, id, msg, a, b);
}
void MenuChoice(X86Context& ctx, uint32_t player, uint32_t id,
                const std::vector<std::pair<const char*, uint32_t>>& items, uint32_t value) {
    Choice(ctx, player, id, items, value);
}
uint32_t MenuChoiceValue(X86Context& ctx, uint32_t player, uint32_t id) { return SetById(ctx, player, id, 0x35, 0) & 0xFF; }
std::vector<std::pair<const char*, uint32_t>> MenuNumbered(std::initializer_list<const char*> items) {
    return Numbered(items);
}
void MenuHelp(X86Context& ctx, uint32_t player, const char* text) { SetById(ctx, player, kHelpBox, 0x18, Str(text)); }
void MenuGoTo(X86Context& ctx, uint32_t player, uint32_t page) {
    uint32_t eax = ctx.eax;
    kernel::CallGuest(ctx, kGoTo, {kMenuContext + (player & 0xFF) * kContextSize, 0x44, page, 0});
    ctx.eax = eax;
}
void MenuBack(X86Context& ctx, uint32_t player) {
    uint32_t eax = ctx.eax;
    kernel::CallGuest(ctx, kGoTo, {kMenuContext + (player & 0xFF) * kContextSize, 0x5F, 0, 0});
    ctx.eax = eax;
}

// the page from anywhere in the front end (the main menu's item)
void OpenPcPage(X86Context& ctx, uint32_t player) {
    uint32_t eax = ctx.eax;
    kernel::CallGuest(ctx, kGoTo, {kMenuContext + (player & 0xFF) * kContextSize, 0x44, kPage, 0});
    ctx.eax = eax;
}

// the PC Settings page made from AV Options' records (a page in the menu file, src/game/pc_settings_item.cpp): its
// page widget, title, prompt line, help box and pictures kept, its rows dropped, and ours made from its music slider
// (with its grey track), subtitles choice and Restore Defaults button rows (each a label and a value widget with one
// id), one under another; the focus moves between them by where they are
std::vector<uint8_t> PcPage(const std::vector<uint8_t>& av, uint32_t (*record_size)(uint32_t)) {
    return BuildPage(av, record_size, kPage, "PC Settings", kRows, kFirstY, kRowStep);
}

std::vector<uint8_t> BuildPage(const std::vector<uint8_t>& av, uint32_t (*record_size)(uint32_t), uint32_t page_id,
                               const char* title, const std::vector<MenuRow>& rows, int16_t first_y, int16_t step) {
    auto get = [](const std::vector<uint8_t>& b, size_t at) {
        uint32_t v;
        std::memcpy(&v, &b[at], 4);
        return v;
    };
    auto put = [](std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); };
    auto put16 = [](std::vector<uint8_t>& b, size_t at, int16_t v) { std::memcpy(&b[at], &v, 2); };
    constexpr uint32_t kOpWidget = 0xFFFFFFF9, kOpAttr = 0xFFFFFFF6, kOpFocus = 0xFFFFFFF4;
    // the page record, then groups: a widget record and the records after it up to the next
    const size_t head = record_size(get(av, 0));
    struct Group {
        uint32_t id, type;
        int16_t y;
        std::vector<uint8_t> bytes;
    };
    std::vector<Group> groups;
    for (size_t p = head; p + 4 <= av.size();) {
        uint32_t op = get(av, p), n = record_size(op);
        if (!n) break;
        if (op == kOpWidget) {
            int16_t y;
            std::memcpy(&y, &av[p + 0xC], 2);
            groups.push_back({get(av, p + 4), av[p + 8], y, {}});
        }
        if (!groups.empty()) groups.back().bytes.insert(groups.back().bytes.end(), av.begin() + p, av.begin() + p + n);
        p += n;
    }
    auto find = [&](uint32_t id, uint32_t type) -> const Group* {
        for (const Group& g : groups)
            if (g.id == id && g.type == type) return &g;
        return nullptr;
    };
    const Group *slider_label = find(0x10000135, 5), *slider = find(0x10000135, 10);
    const Group *choice_label = find(0x10000136, 5), *choice = find(0x10000136, 9);
    const Group *button = find(0x100001A3, 1), *page = find(0x40000031, 13);
    const Group* track = nullptr;  // a slider's grey track, behind its bar (AV's at 86 and 114)
    for (const Group& g : groups)
        if (g.id == 0x10000001 && g.y == 86) track = &g;
    if (!track || !slider_label || !slider || !choice_label || !choice || !button || !page) return {};
    // a group's texts (attribute 0x18) as ours, its id and row
    auto made = [&](const Group& g, uint32_t id, int16_t y, const char* text) {
        std::vector<uint8_t> b = g.bytes;
        put(b, 4, id);
        put16(b, 0xC, y);
        for (size_t p = 0; p + 4 <= b.size();) {
            uint32_t op = get(b, p), n = record_size(op);
            if (!n) break;
            if (op == kOpAttr && b[p + 4] == 0x18 && text) {
                put(b, p + 5, Str(text));
                put(b, p + 9, 0);
            }
            p += n;
        }
        return b;
    };
    std::vector<uint8_t> out(av.begin(), av.begin() + head);
    put(out, 4, page_id);
    // the page widget, its first focus our first row
    std::vector<uint8_t> pg = page->bytes;
    put(pg, 4, page_id);
    for (size_t p = 0; p + 4 <= pg.size();) {
        uint32_t op = get(pg, p), n = record_size(op);
        if (!n) break;
        if (op == kOpFocus) put(pg, p + 0xC, rows[0].id);
        p += n;
    }
    out.insert(out.end(), pg.begin(), pg.end());
    // what stays: the title (its text ours), the prompt line, the help box and the pictures
    for (const Group& g : groups) {
        bool is_title = g.id == 0x10000001 && g.y == 40, keep = (g.id == 0x10000001 && (g.y == 419 || g.y == 38)) ||
                                                         g.id == 0x10000225 || g.id == 0x10000224;
        if (is_title) {
            std::vector<uint8_t> t = made(g, g.id, g.y, title);
            out.insert(out.end(), t.begin(), t.end());
        } else if (keep) {
            out.insert(out.end(), g.bytes.begin(), g.bytes.end());
        }
    }
    int16_t y = first_y;
    for (const MenuRow& r : rows) {
        if (r.slot >= 0) y = int16_t(first_y + r.slot * step);
        std::vector<uint8_t> a, b;
        if (r.kind == kSliderRow) {
            std::vector<uint8_t> t = made(*track, track->id, y, nullptr);
            out.insert(out.end(), t.begin(), t.end());
            a = made(*slider_label, r.id, y, r.label), b = made(*slider, r.id, y, nullptr);
        }
        if (r.kind == kChoiceRow) a = made(*choice_label, r.id, y, r.label), b = made(*choice, r.id, y, nullptr);
        if (r.kind == kButtonRow) a = made(*button, r.id, y, r.label);
        out.insert(out.end(), a.begin(), a.end());
        out.insert(out.end(), b.begin(), b.end());
        y = int16_t(y + step);
    }
    put(out, 0x11, uint32_t(out.size() - head));  // the page's body size
    return out;
}
}  // namespace nf

// the list's table (cdecl: the list widget, the table, its count, ...)
X86_EXTERN_FUNC(__imp__sub_000759D0);
X86_FUNC(sub_000759D0) {
    if (R32(ctx.esp + 8) == kTable && R32(ctx.esp + 0xC) == kEntries) {
        W32(ctx.esp + 8, ListCopy());
        W32(ctx.esp + 0xC, kEntries + 1);
        static bool said = false;
        if (!said) said = true, LOGR("nightfire: PC Settings added to Edit Codename's list");
    }
    __imp__sub_000759D0(ctx, base);
}

// the list's focus (cdecl: player, widget, the table, ...): its help line and picture
X86_EXTERN_FUNC(__imp__sub_0007F680);
X86_FUNC(sub_0007F680) {
    // the rows' labels, help and picture from the copy; not before the list is made (its index then the widget's
    // own pointer, which the game's arithmetic wraps into memory the copy's address would not)
    uint32_t widget = R32(ctx.esp + 8);
    if (widget && R32(ctx.esp + 0xC) == kTable) {
        uint32_t index = kernel::CallGuest(ctx, kSetWidget, {widget, 0x40, 0, 0}) & 0xFF;
        if (index < kCopied) W32(ctx.esp + 0xC, ListCopy());
    }
    __imp__sub_0007F680(ctx, base);
}

// the list's items (cdecl: player, widget, id, message): A on ours opens the page
X86_EXTERN_FUNC(__imp__sub_0008C7F0);
X86_FUNC(sub_0008C7F0) {
    uint32_t player = R32(ctx.esp + 4), widget = R32(ctx.esp + 8), msg = R32(ctx.esp + 0x10);
    uint32_t index = widget ? kernel::CallGuest(ctx, kSetWidget, {widget, 0x40, 0, 0}) & 0xFF : ~0u;
    if ((msg == 0x49 || msg == 0x54) && index < kCopied) {
        // focus moved: the rows' labels, help and picture (the game's call, with the copy: its direct call to 0x7F680
        // does not come through the hook below); not before the list is made (its index the widget's pointer)
        uint32_t eax = ctx.eax;
        kernel::CallGuest(ctx, 0x7F680, {player, widget, ListCopy(), 0x100000FF, 0x100001A5, 0x100001EC, 0x10000109,
                                         msg == 0x49 ? 1u : 0u});
        ctx.eax = eax;
        ctx.eax = 1;
        ctx.esp += 4;
        return;
    }
    if (msg == 0x4B && widget) {
        if ((index & 0xFF) == kEntries) {
            nf::OpenPcPage(ctx, player);
            ctx.eax = 1;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_0008C7F0(ctx, base);
    if (msg == 0x51 && widget) {  // the list finished: its range the game's 0 to 6 and ours, 7
        uint32_t eax = ctx.eax;
        kernel::CallGuest(ctx, kSetWidget, {widget, 0x27, 0, kEntries});
        ctx.eax = eax;
    }
}

// GetText (cdecl: the handle, ...): our entry's label and help
X86_EXTERN_FUNC(__imp__sub_0006D460);
X86_FUNC(sub_0006D460) {
    uint32_t h = R32(ctx.esp + 4);
    if (h == kLabelHandle || h == kHelpHandle || h == kPauseLabel) {
        ctx.eax = Str(h == kLabelHandle ? "PC Settings" : h == kPauseLabel ? "PC SETTINGS" : "Picture, controls and more for this PC.");
        ctx.esp += 4;
        return;
    }
    __imp__sub_0006D460(ctx, base);
}
