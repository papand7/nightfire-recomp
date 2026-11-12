// PC Settings in bond_eng.xbe's own menus: a row under the main menu's Options, a tab beside the pause's Briefing, and the page of
// rows those open.
//
// A menu file's page keeps its text at +0x24 (count) and +0x2C, its pictures at +0x3C and +0x44, its items at +0x54 and +0x5C,
// the current item at +0x60; a text is 0x2CC bytes (mode +0x8C, x +0x90, y +0x94, the string at +0x24), a picture 0x1F4, an
// item 0xDC (up +0x10 and +0x14, down +0x18 and +0x1C, an index and that item; what A does +0x38; its text +0xB0, picture
// +0xC4, index +0xC0). The new row is the page's arrays copied, with room for one more, into memory of ours. The settings page
// is a second set of arrays laid on the same page: the game moves, highlights and draws it as its own, and the presses it would
// act on are taken at 0x40A00 (file, event). Back puts the page's own arrays back.
#include "bond/addr.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/auf.h"
#include "common/settings_page.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(sub_00037AD0);  // an item loses the highlight
X86_EXTERN_FUNC(sub_00037A30);  // an item gets it

namespace {
constexpr uint32_t kTextCount = 0x24, kTexts = 0x2C, kPicCount = 0x3C, kPics = 0x44, kItemCount = 0x54, kItems = 0x5C, kCurrent = 0x60;
constexpr uint32_t kTextSize = 0x2CC, kPicSize = 0x1F4, kItemSize = 0xDC, kStateSize = 0x3F8;
constexpr uint32_t kItemText = 0xB0, kItemPic = 0xC4, kItemIndex = 0xC0, kAction = 0x38;
constexpr uint32_t kTextNext = 0x2C8, kPicNext = 0x1F0;
constexpr uint32_t kEventLeft = 0, kEventRight = 1, kEventUp = 2, kEventDown = 3, kEventA = 4, kEventB = 5;  // 0x40A00's, seen by pressing each
constexpr uint32_t kOptionsState = 9;
// the pause's tab strip: a page's plain texts at +0 (count) and +8, 0x278 each (x, its centre, +0x90, y +0x94, w +0x98, red +0xA4, 1 on the
// tab showing, the string +0x24), the tabs at y 113; its strip's bands at +0xC and +0x14, 0x1C4 each (y +0x1C, 111)
constexpr uint32_t kPlainCount = 0, kPlains = 8, kPlainSize = 0x278, kBandCount = 0xC, kBands = 0x14, kBandSize = 0x1C4;
constexpr float kTabY = 113, kPcTabX = 560;

// cdecl: the argument, the return address (the callee's ret takes it), the argument off
void Call1(X86Context& ctx, uint8_t* base, void (*f)(X86Context&, uint8_t*), uint32_t arg) {
    X86_PUSH32(arg);
    X86_PUSH32(0x40A00u);
    f(ctx, base);
    ctx.esp += 4;
}

uint32_t Page(uint32_t file) { return file ? kernel::R32(file + 0x44) : 0; }
uint32_t Frontend() { return kernel::R32(A(0x2A9724) + kernel::R32(A(0x2A9CA8)) * 4); }
uint32_t PauseFile() { return kernel::R32(A(0x2A9734)); }
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }
void SetText(uint32_t text, const char* s) {
    char* to = reinterpret_cast<char*>(&kernel::G<uint8_t>(text + 0x24));
    if (std::strncmp(to, s, 255)) std::snprintf(to, 256, "%s", s);
}
void Place(uint32_t text, float x, float y) {
    kernel::W32(text + 0x8C, 0);
    SetF(text + 0x90, x);
    SetF(text + 0x94, y);
}

// pointers into [from, from + bytes) moved to the copy, in the words given
void Remap(uint32_t at, uint32_t words, uint32_t from, uint32_t bytes, uint32_t to) {
    for (uint32_t i = 0; i < words; i++) {
        uint32_t v = kernel::R32(at + i * 4);
        if (v >= from && v < from + bytes) kernel::W32(at + i * 4, v - from + to);
    }
}

// a page's arrays copied with room for extra more
struct Copy {
    uint32_t items, texts, pics;
};
Copy Enlarge(uint32_t page, uint32_t extra_items, uint32_t extra_texts, uint32_t extra_pics) {
    const uint32_t ni = kernel::R32(page + kItemCount), nt = kernel::R32(page + kTextCount), np = kernel::R32(page + kPicCount);
    const uint32_t oi = kernel::R32(page + kItems), ot = kernel::R32(page + kTexts), op = kernel::R32(page + kPics);
    Copy c;
    c.items = mem::AllocHostContiguous((ni + extra_items) * kItemSize, 16);
    c.texts = mem::AllocHostContiguous((nt + extra_texts) * kTextSize, 16);
    c.pics = mem::AllocHostContiguous((np + extra_pics) * kPicSize, 16);
    std::memcpy(&kernel::G<uint8_t>(c.items), &kernel::G<uint8_t>(oi), ni * kItemSize);
    if (nt) std::memcpy(&kernel::G<uint8_t>(c.texts), &kernel::G<uint8_t>(ot), nt * kTextSize);
    if (np) std::memcpy(&kernel::G<uint8_t>(c.pics), &kernel::G<uint8_t>(op), np * kPicSize);
    auto all = [&](uint32_t from, uint32_t bytes, uint32_t to) {
        if (!bytes) return;
        Remap(page + 0x54, 8, from, bytes, to);
        Remap(c.items, ni * kItemSize / 4, from, bytes, to);
        Remap(c.texts, nt * kTextSize / 4, from, bytes, to);
        Remap(c.pics, np * kPicSize / 4, from, bytes, to);
    };
    all(oi, ni * kItemSize, c.items);
    all(ot, nt * kTextSize, c.texts);
    all(op, np * kPicSize, c.pics);
    kernel::W32(page + kItems, c.items);
    kernel::W32(page + kTexts, c.texts);
    kernel::W32(page + kPics, c.pics);
    return c;
}

void Link(uint32_t item, uint32_t up_index, uint32_t up, uint32_t down_index, uint32_t down) {
    kernel::W32(item + 0x10, up_index);
    kernel::W32(item + 0x14, up);
    kernel::W32(item + 0x18, down_index);
    kernel::W32(item + 0x1C, down);
}

// a text to copy (the Options page's rows on the main menu, which has none) and a picture (its chevron)
void Templates(uint32_t file, uint32_t page, uint32_t& text, uint32_t& pic) {
    // the pause's Briefing has no items: its Mission page's (state 1)
    if (!kernel::R32(page + kItemCount)) page = kernel::R32(file + 0x14) + kStateSize;
    uint32_t list = kernel::R32(page + kItems);
    text = kernel::R32(list + kItemText), pic = kernel::R32(list + kItemPic);
    if (text) return;
    const uint32_t options = kernel::R32(file + 0x14) + kOptionsState * kStateSize, row = kernel::R32(options + kItems) + kItemSize;
    text = kernel::R32(row + kItemText), pic = kernel::R32(row + kItemPic);
}

constexpr int kRows = auf::kPcRows;
#if defined(AUF_LANG_FRE)
constexpr int kLang = 1;
#elif defined(AUF_LANG_GER)
constexpr int kLang = 2;
#else
constexpr int kLang = 0;
#endif

// the row that opens the page: the page's last, with a copy of an item's text and (if it has them) its chevron
std::map<uint32_t, uint32_t> g_rows;  // page -> its row
void AddRow(X86Context& ctx, uint8_t* base, uint32_t file, uint32_t page, float x, float y) {
    const uint32_t n = kernel::R32(page + kItemCount), nt = kernel::R32(page + kTextCount), np = kernel::R32(page + kPicCount);
    auto known = g_rows.find(page);
    if (known != g_rows.end() && kernel::R32(page + kItems) + n * kItemSize > known->second && kernel::R32(page + kItems) <= known->second) return;
    uint32_t text = 0, pic = 0;
    Templates(file, page, text, pic);
    if (!text) return;
    const bool chevron = kernel::R32(kernel::R32(page + kItems) + kItemPic) != 0 && kernel::G<float>(pic + 0xA0) < 40;
    std::vector<uint8_t> text_copy(kTextSize), pic_copy(kPicSize);
    std::memcpy(text_copy.data(), &kernel::G<uint8_t>(text), kTextSize);
    if (pic) std::memcpy(pic_copy.data(), &kernel::G<uint8_t>(pic), kPicSize);
    Copy c = Enlarge(page, 1, 1, chevron ? 1 : 0);
    const uint32_t item = c.items + n * kItemSize, last = c.items + (n - 1) * kItemSize, first = c.items;
    const uint32_t new_text = c.texts + nt * kTextSize;
    std::memcpy(&kernel::G<uint8_t>(item), &kernel::G<uint8_t>(last), kItemSize);
    std::memcpy(&kernel::G<uint8_t>(new_text), text_copy.data(), kTextSize);
    kernel::W32(new_text + kTextNext, 0);
    SetText(new_text, auf::PcRowName(kLang));
    Place(new_text, x, y);
    kernel::W32(item + kItemText, new_text);
    kernel::W32(item + kItemPic, 0);
    if (chevron) {
        const uint32_t new_pic = c.pics + np * kPicSize;
        std::memcpy(&kernel::G<uint8_t>(new_pic), pic_copy.data(), kPicSize);
        kernel::W32(new_pic + kPicNext, 0);
        kernel::W32(new_pic + 0x90, 0);
        SetF(new_pic + 0x94, x - 6 - kernel::G<float>(new_pic + 0xA0));
        SetF(new_pic + 0x98, y);  // a chevron at its text's y, as the game's own
        kernel::W32(item + kItemPic, new_pic);
        kernel::W32(page + kPicCount, np + 1);
    }
    kernel::W32(item + kItemIndex, n);
    kernel::W32(item + kAction, 0);
    kernel::W32(item + 0x3C, 0);
    kernel::W32(item + 0x40, 0);
    Link(item, n - 1, last, 0, first);
    kernel::W32(last + 0x18, n);
    kernel::W32(last + 0x1C, item);
    kernel::W32(first + 0x10, n);
    kernel::W32(first + 0x14, item);
    kernel::W32(page + kTextCount, nt + 1);
    kernel::W32(page + kItemCount, n + 1);
    g_rows[page] = item;
    Call1(ctx, base, sub_00037AD0, item);  // the row copied may have been the highlighted one
    LOGR("auf: PC Settings is a row of the menu page %08X", page);
}

constexpr int kBandsKept = 16;
struct Open {
    uint32_t page = 0;
    uint32_t saved[9] = {};  // the page's text, picture and item arrays and current item
    uint32_t items = 0, values = 0;
    int rows = 0;
    uint32_t bands = 0;
    float band[kBandsKept] = {};
};
Open g_open;
const uint32_t kSavedAt[9] = {kTextCount, kTexts, kPicCount, kPics, kItemCount, kItems, kCurrent, kPlainCount, kPlains};

// the plain texts of a page that are its tabs; the one showing
std::vector<uint32_t> TabsOf(uint32_t page) {
    std::vector<uint32_t> tabs;
    const uint32_t list = kernel::R32(page + kPlains);
    for (uint32_t i = 0, n = kernel::R32(page + kPlainCount); i < n && n < 64; i++)
        if (kernel::G<float>(list + i * kPlainSize + 0x94) == kTabY) tabs.push_back(list + i * kPlainSize);
    return tabs;
}
std::map<uint32_t, uint32_t> g_pc_tabs;  // page -> the plain text that is PC Settings' tab

// the tab: one more plain text in the page's list, copied into memory of ours
void AddTab(uint32_t page) {
    const auto known = g_pc_tabs.find(page);
    if (known != g_pc_tabs.end() && kernel::R32(page + kPlains) <= known->second &&
        known->second < kernel::R32(page + kPlains) + kernel::R32(page + kPlainCount) * kPlainSize) {
        SetText(known->second, auf::PcTabName());
        return;
    }
    const std::vector<uint32_t> tabs = TabsOf(page);
    if (tabs.size() < 3) return;
    const uint32_t n = kernel::R32(page + kPlainCount), list = kernel::R32(page + kPlains);
    const uint32_t copy = mem::AllocHostContiguous((n + 1) * kPlainSize, 16);
    std::memcpy(&kernel::G<uint8_t>(copy), &kernel::G<uint8_t>(list), n * kPlainSize);
    const uint32_t tab = copy + n * kPlainSize;
    std::memcpy(&kernel::G<uint8_t>(tab), &kernel::G<uint8_t>(tabs.back()), kPlainSize);
    SetText(tab, auf::PcTabName());
    SetF(tab + 0x90, kPcTabX);
    SetF(tab + 0x98, 70);
    for (const uint32_t t : tabs)
        if (kernel::G<float>(t + 0xA4) <= 0.6f) std::memcpy(&kernel::G<uint8_t>(tab + 0xA4), &kernel::G<uint8_t>(t + 0xA4), 16);
    kernel::W32(page + kPlains, copy);
    kernel::W32(page + kPlainCount, n + 1);
    g_pc_tabs[page] = tab;
}

// the tab's rows: as many items, texts and chevrons as it has, linked in a ring
void ShowRows(uint32_t page, uint32_t items, bool chevrons) {
    const int n = auf::PcRows();
    for (int i = 0; i < n; i++) {
        const uint32_t up = uint32_t((i + n - 1) % n), down = uint32_t((i + 1) % n);
        Link(items + uint32_t(i) * kItemSize, up, items + up * kItemSize, down, items + down * kItemSize);
    }
    kernel::W32(page + kTextCount, uint32_t(n) * 2);
    kernel::W32(page + kPicCount, chevrons ? uint32_t(n) : 0);
    kernel::W32(page + kItemCount, uint32_t(n));
    g_open.rows = n;
}

void OpenPage(X86Context& ctx, uint8_t* base, uint32_t file, uint32_t page) {
    uint32_t text = 0, pic = 0;
    Templates(file, page, text, pic);
    if (!text) return;
    static std::map<uint32_t, Copy> built;  // by page
    Copy& c = built[page];
    if (!c.items) {
        c.items = mem::AllocHostContiguous(kRows * kItemSize, 16);
        c.texts = mem::AllocHostContiguous(kRows * 2 * kTextSize, 16);
        c.pics = mem::AllocHostContiguous(kRows * kPicSize, 16);
    }
    const uint32_t model = kernel::R32((kernel::R32(page + kItemCount) ? page : kernel::R32(file + 0x14) + kStateSize) + kItems);
    const bool paused = file == PauseFile();
    const float x = paused ? 110 : 100, value_x = paused ? 330 : 370, top = paused ? 158 : 150, pitch = paused ? 20 : 28;
    for (int i = 0; i < kRows; i++) {
        const uint32_t item = c.items + uint32_t(i) * kItemSize, label = c.texts + uint32_t(i) * 2 * kTextSize, value = label + kTextSize,
                       chevron = c.pics + uint32_t(i) * kPicSize;
        const float y = top + float(i) * pitch;
        std::memcpy(&kernel::G<uint8_t>(item), &kernel::G<uint8_t>(model), kItemSize);
        std::memcpy(&kernel::G<uint8_t>(label), &kernel::G<uint8_t>(text), kTextSize);
        std::memcpy(&kernel::G<uint8_t>(value), &kernel::G<uint8_t>(text), kTextSize);
        Place(label, x, y);
        Place(value, value_x, y);
        SetText(label, auf::PcLabel(i, kLang));
        SetText(value, auf::PcValue(i, kLang).c_str());
        kernel::W32(label + kTextNext, value);
        kernel::W32(value + kTextNext, 0);
        kernel::W32(item + kItemText, label);
        kernel::W32(item + kItemPic, 0);
        if (pic) {
            std::memcpy(&kernel::G<uint8_t>(chevron), &kernel::G<uint8_t>(pic), kPicSize);
            kernel::W32(chevron + kPicNext, 0);
            kernel::W32(chevron + 0x90, 0);
            SetF(chevron + 0x94, x - 16);
            SetF(chevron + 0x98, y);
            kernel::W32(item + kItemPic, chevron);
        }
        kernel::W32(item + kItemIndex, uint32_t(i));
        kernel::W32(item + kAction, 0);
    }
    g_open.page = page;
    for (int i = 0; i < 9; i++) g_open.saved[i] = kernel::R32(page + kSavedAt[i]);
    g_open.items = c.items;
    kernel::W32(page + kTexts, c.texts);
    kernel::W32(page + kPics, c.pics);
    ShowRows(page, c.items, pic != 0);
    if (paused) {
        // only the tabs and the footer of the page under it; PC Settings' tab bright, its band under the tab
        static uint32_t kept = 0;
        if (!kept) kept = mem::AllocHostContiguous(64 * kPlainSize, 16);
        const uint32_t list = g_open.saved[8], count = g_open.saved[7];
        uint32_t n = 0;
        // the page's own two reds
        uint32_t bright = 0, dim = 0;  // a tab of each kind: its four colours at +0xA4
        for (uint32_t i = 0; i < count; i++)
            if (kernel::G<float>(list + i * kPlainSize + 0x94) == kTabY && kernel::G<float>(list + i * kPlainSize + 0x90) != kPcTabX) {
                const uint32_t t = list + i * kPlainSize;
                if (kernel::G<float>(t + 0xA4) > 0.6f) bright = t;
                else dim = t;
            }
        for (uint32_t i = 0; i < count && n < 64; i++) {
            const uint32_t t = list + i * kPlainSize;
            const float ty = kernel::G<float>(t + 0x94);
            if (ty != kTabY && ty < 300) continue;
            const uint32_t k = kept + n++ * kPlainSize;
            std::memcpy(&kernel::G<uint8_t>(k), &kernel::G<uint8_t>(t), kPlainSize);
            const uint32_t like = kernel::G<float>(t + 0x90) == kPcTabX ? bright : dim;
            if (ty == kTabY && like) std::memcpy(&kernel::G<uint8_t>(k + 0xA4), &kernel::G<uint8_t>(like + 0xA4), 16);
        }
        kernel::W32(page + kPlains, kept);
        kernel::W32(page + kPlainCount, n);
        // the strip's lit box is under Briefing's label: all its bands away while this tab shows
        const uint32_t bands = kernel::R32(page + kBands);
        g_open.bands = bands;
        for (int i = 0; i < kBandsKept; i++) {
            g_open.band[i] = kernel::G<float>(bands + uint32_t(i) * kBandSize + 0x1C);
            if (g_open.band[i] == 111) SetF(bands + uint32_t(i) * kBandSize + 0x1C, -200);
        }
    }
    kernel::W32(page + kItems, c.items);
    for (int i = 0; i < kRows; i++) Call1(ctx, base, sub_00037AD0, c.items + uint32_t(i) * kItemSize);
    // on the pause it opens as its other tabs do, no row chosen (the current item 0): left and right go between tabs, down to the rows
    kernel::W32(page + kCurrent, paused ? 0 : c.items);
    if (!paused) Call1(ctx, base, sub_00037A30, c.items);
    auf::settings_page::Reset();
    LOGR("auf: PC Settings opened");
}

void RestoreBands() {
    if (!g_open.bands) return;
    for (int i = 0; i < kBandsKept; i++) SetF(g_open.bands + uint32_t(i) * kBandSize + 0x1C, g_open.band[i]);
    g_open.bands = 0;
}

void ClosePage() {
    if (!g_open.page) return;
    RestoreBands();
    for (int i = 0; i < 9; i++) kernel::W32(g_open.page + kSavedAt[i], g_open.saved[i]);
    g_open.page = 0;
    LOGR("auf: PC Settings closed");
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00040A00);

namespace {
int g_wanted = -1;  // the tab a click asked for, 3 PC Settings
// a press of the left button on the strip's tabs
void TabClick(const std::vector<uint32_t>& tabs) {
    static bool last_press = false;
    float x, y;
    const bool press = (auf::PointerButtons() & 1) != 0, edge = press && !last_press;
    last_press = press;
    if (!edge || tabs.size() < 3 || !auf::PointerAt(x, y) || y < 104 || y >= 132) return;
    for (int i = 0; i < 3; i++) {
        const float c = kernel::G<float>(tabs[size_t(i)] + 0x90);
        if (x >= c - 60 && x < c + 60) g_wanted = i;
    }
    if (x >= kPcTabX - 60) g_wanted = 3;
}
}  // namespace

namespace auf {
// the modern menus' page of the PC Settings page that is open: the host's own rows (src/common/settings_page.cpp) in place of the game's
bool PcSettingsHost(uint32_t file, uint32_t page, ModernPage& p) {
    if (!g_open.page || g_open.page != page) return false;
    settings_page::Build(p, file == PauseFile());
    return true;
}
// the UI's clicks on it, as the game's presses would be
void PcSettingsPlay(X86Context&, uint8_t*, uint32_t file, uint32_t page) {
    ModernAction a;
    while (ModernMenuAction(a)) {
        if (a.page != page + settings_page::PageTag()) continue;  // meant for a page since left
        const int c = a.control;
        const int event = c == input::kA ? int(kEventA) : c == input::kB ? int(kEventB) : c == input::kLeft ? int(kEventLeft)
                          : c == input::kRight ? int(kEventRight) : 6;  // 6: only the focus
        if (event == 6 && a.id == 0) continue;
        if (settings_page::Event(a.id, event, file == PauseFile()) == settings_page::kClose) {
            ClosePage();
            return;
        }
    }
}

// each frame a menu has the input (src/bond/pointer.cpp's hook of 0x56930)
void PcSettingsFrame(X86Context& ctx, uint8_t* base, uint32_t file) {
    const uint32_t page = Page(file);
    if (!page) return;
    if (g_open.page) {
        if (g_open.page != page) {  // the game left the page: it holds the arrays of ours still
            for (int i = 0; i < 9; i++) kernel::W32(g_open.page + kSavedAt[i], g_open.saved[i]);
            RestoreBands();
            g_open.page = 0;
            return;
        }
        // a click on another tab: back to the page under this one, then the steps there, below
        if (file == PauseFile()) {
            TabClick(TabsOf(page));
            if (g_wanted >= 0 && g_wanted != 3) {
                ClosePage();
                return;
            }
            g_wanted = -1;
        }
        if (auf::PcRows() != g_open.rows) ShowRows(page, g_open.items, kernel::R32(page + kPicCount) != 0);
        for (int i = 0; i < g_open.rows; i++) {
            const uint32_t label = kernel::R32(g_open.items + uint32_t(i) * kItemSize + kItemText);
            SetText(label, auf::PcLabel(i, kLang));
            SetText(label + kTextSize, auf::PcValue(i, kLang).c_str());
        }
        return;
    }
    // the game fills its texts again as a page comes up (seen on the pause: this row read Continue Mission)
    if (auto row = g_rows.find(page); row != g_rows.end() && kernel::R32(page + kItems) <= row->second &&
        row->second < kernel::R32(page + kItems) + kernel::R32(page + kItemCount) * kItemSize)
        SetText(kernel::R32(row->second + kItemText), auf::PcRowName(kLang));
    const uint32_t states = kernel::R32(file + 0x14);
    if (file == Frontend() && page == states + 3 * kStateSize && kernel::R32(page + kItemCount) >= 5) AddRow(ctx, base, file, page, 200, 402);
    if (file == PauseFile()) {
        AddTab(page);
        // the pointer on the tabs: steps through them with the game's left and right, a step a frame
        const std::vector<uint32_t> tabs = TabsOf(page);
        TabClick(tabs);
        if (g_wanted >= 0) {
            int showing = -1;
            for (size_t i = 0; i < tabs.size() && i < 3; i++)
                if (kernel::G<float>(tabs[i] + 0xA4) > 0.6f) showing = int(i);
            if (showing < 0) g_wanted = -1;
            else if (g_wanted == 3 && showing == 2) {
                OpenPage(ctx, base, file, page);
                g_wanted = -1;
            } else if (g_wanted == showing) {
                g_wanted = -1;
            } else {
                X86_PUSH32(g_wanted > showing ? kEventRight : kEventLeft);
                X86_PUSH32(file);
                X86_PUSH32(0x56930u);
                __imp__sub_00040A00(ctx, base);
                ctx.esp += 8;
            }
        }
    }
}
}  // namespace auf

// The presses: on the row, A opens the page; right from the pause's last tab does too; on the page, left and right and A change a
// row and Back leaves
X86_FUNC(sub_00040A00) {
    const uint32_t file = kernel::R32(ctx.esp + 4), event = kernel::R32(ctx.esp + 8), page = Page(file);
    static const bool said = std::getenv("AUF_EVENT_LOG") != nullptr;
    if (said) LOGR("auf: menu event %u", event);
    if (page && g_open.page == page && auf::modern_menus && event <= kEventB) {
        // the modern menus show the host's settings page over this one (PcSettingsHost): the presses go to it
        if (auf::settings_page::Event(auf::settings_page::kNoRow, int(event), file == PauseFile()) == auf::settings_page::kClose) ClosePage();
        ctx.esp += 4;
        return;
    }
    if (page && g_open.page == page) {
        const uint32_t current = kernel::R32(page + kCurrent), items = g_open.items;
        if (event == kEventB) {
            ClosePage();
            ctx.esp += 4;
            return;
        }
        if (!current) {  // on the strip
            if (event == kEventLeft) ClosePage();
            if (event == kEventDown) {
                kernel::W32(page + kCurrent, items);
                Call1(ctx, base, sub_00037A30, items);
            }
            ctx.esp += 4;
            return;
        }
        const int row = int(kernel::R32(current + kItemIndex));
        if (event == kEventUp && row == 0 && file == PauseFile()) {
            Call1(ctx, base, sub_00037AD0, current);
            kernel::W32(page + kCurrent, 0);
            ctx.esp += 4;
            return;
        }
        if (event == kEventLeft || event == kEventRight || event == kEventA) {
            if (event == kEventA) auf::PcStep(row, auf::PcClickDir(file == PauseFile() ? 330 : 370));
            else auf::PcStep(row, event == kEventLeft ? -1 : 1);
            ctx.esp += 4;
            return;
        }
    } else if (page && file == PauseFile() && event == kEventRight) {
        const std::vector<uint32_t> tabs = TabsOf(page);
        if (tabs.size() >= 3 && kernel::G<float>(tabs[2] + 0xA4) > 0.6f) {
            OpenPage(ctx, base, file, page);
            ctx.esp += 4;
            return;
        }
    } else if (page && event == kEventA) {
        auto row = g_rows.find(page);
        if (row != g_rows.end() && kernel::R32(page + kCurrent) == row->second) {
            OpenPage(ctx, base, file, page);
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_00040A00(ctx, base);
}
