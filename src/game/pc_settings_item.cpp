// "Settings" and "Quit Game" in the game's own main menu (default.xbe): a fourth and fifth item under NightFire /
// Multiplayer / Codenames, the first opening the Settings page, the second leaving the game (app::Quit).
//
// Eurocom's menus are data: one menu file sits in memory as a record stream ([0x25FB08]), and the builder 0x93960
// makes the widgets of a page from it. The main menu is page 0x40000002 (var 0x80000002) in the front end's file;
// its items are widget records (F9) each followed by attribute records (F6, message 0x18 the label). The builder is
// given a copy of the stream with the Codenames item's records cloned as the new item (its own id, a label string
// of ours) and the items moved up to make room. A press of A reaches the per-id dispatcher 0x8E320, where the new
// id opens the overlay; the help box under the menu gets its line from 0x959E0, which knows only the game's items.
// A level's menu file (the pause screen, page 0x4000004B's) has no AV Options page: its choices list (item 0x10000028
// index 6: CONTINUE, RESTART, QUIT as text records, attribute 0x11) gets a fourth, SETTINGS, and the page is made
// there from the front end's AV Options records, kept from its file. A on that choice opens it, as the main menu's
// item does; back returns to the pause screen.
// (.local/menu_re.md has the record formats and how they were found.)
#include <cstring>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"
#include "game/menu_page.h"
#include "common/nightfire.h"

namespace nf {
bool PcPageMessage(X86Context& ctx, uint32_t player, uint32_t widget, uint32_t msg, uint32_t arg);  // pc_settings_page
void OpenPcPage(X86Context& ctx, uint32_t player);
std::vector<uint8_t> PcPage(const std::vector<uint8_t>& av, uint32_t (*record_size)(uint32_t));
}  // namespace nf

namespace {
using kernel::R32;
using kernel::W32;

constexpr uint32_t kStreamPtr = 0x25FB08;  // the resident menu file the builder reads
constexpr uint32_t kHelpBox = 0x25FC18;    // the main menu's help box widget
constexpr uint32_t kSetWidget = 0x72630;   // cdecl (widget, message, a, b)
constexpr uint32_t kMainMenu = 0x40000002, kFrontVar = 0x80000002;
constexpr uint32_t kNightFire = 0x10000002, kMultiplayer = 0x10000003, kCodenames = 0x10000004;
constexpr uint32_t kPcItem = 0x1000FC01;   // ours: no record or code of the game uses it
constexpr uint32_t kQuitItem = 0x1000FC05;
constexpr uint32_t kAvPage = 0x40000031;  // AV Options: the Settings page is made from it
constexpr uint32_t kOpPage = 0xFFFFFFF0, kOpWidget = 0xFFFFFFF9, kOpAttr = 0xFFFFFFF6, kOpEnd = 0xFFFFFFF2;
constexpr uint8_t kMsgText = 0x18;
constexpr uint32_t kActivate = 0x4B;       // a widget message: A on it
constexpr int16_t kRowY[5] = {222, 248, 274, 300, 326};  // five rows 26 apart (the three were 32) to the same last row, clear of the logo
constexpr uint32_t kPausePage = 0x4000004B, kPauseItem = 0x10000028, kPauseChoices = 6;  // the choices list's index
constexpr uint8_t kMsgAddText = 0x11;      // a list's entry: a text handle
constexpr uint32_t kMsgRead = 0x34;        // the list's choice
constexpr uint32_t kPauseLabel = 0x0200FC03;  // GetText answers it (src/game/pc_settings_page.cpp)
std::vector<uint8_t> g_av;                 // the front end's AV Options page records, for a level's file

// bytes of each record by its opcode (the dword before the fields included); 0 ends the stream
uint32_t RecordSize(uint32_t op) {
    switch (op) {
        case 0xFFFFFFF0: return 0x15;  // page: id, var, ?, platform byte, body size
        case 0xFFFFFFF9: return 0x22;  // widget: id, type, platform, x, y, w, h, ...
        case 0xFFFFFFF8: return 0x24;  // name
        case 0xFFFFFFF7: return 0x0C;  // child
        case 0xFFFFFFF6: return 0x0D;  // attribute: message byte, a, b
        case 0xFFFFFFF5: return 0x12;
        case 0xFFFFFFF4: return 0x11;
        case 0xFFFFFFF3: return 0x0D;
        case 0xFFFFFFFC: return 0x12;
        case 0xFFFFFFFB: return 0x24;
        case 0xFFFFFFFD: return 0x06;
        case 0xFFFFFFFE: return 0x08;
        default: return 0;
    }
}

uint32_t Get32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v;
    std::memcpy(&v, &b[at], 4);
    return v;
}
void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
void Put16(std::vector<uint8_t>& b, size_t at, int16_t v) { std::memcpy(&b[at], &v, 2); }

// the records of the pause screen's panel (its first widget after the page's, 0x10000001 at 45,107), empty if none
std::vector<uint8_t> PausePanel(const std::vector<uint8_t>& b) {
    size_t start = 0;
    for (size_t p = 4; p + 4 <= b.size();) {
        uint32_t op = Get32(b, p), n = RecordSize(op);
        if (!n) break;
        if (op == kOpPage && start) break;
        if (op == kOpPage && Get32(b, p + 4) == kPausePage) start = 1;
        if (op == kOpWidget && start == 1 && Get32(b, p + 4) == 0x10000001) start = p;
        else if (op == kOpWidget && start > 1) return {b.begin() + start, b.begin() + p};
        p += n;
    }
    return {};
}

// a level's file: SETTINGS after the pause choices' texts; false when the file has no pause page
bool AddToPause(std::vector<uint8_t>& b) {
    size_t page = 0, after = 0;
    bool in_list = false;
    for (size_t p = 4; p + 4 <= b.size();) {
        uint32_t op = Get32(b, p), n = RecordSize(op);
        if (!n) break;
        if (op == kOpPage) {
            if (page) break;
            if (Get32(b, p + 4) == kPausePage) page = p;
        } else if (op == kOpWidget && page) {
            in_list = Get32(b, p + 4) == kPauseItem && b[p + 8] == 6 && Get32(b, p + 0x14) == kPauseChoices;
            if (in_list) {  // room for four rows (three in 120): from 176 to 336, above the hint line at 341
                Put16(b, p + 0xC, 176);
                Put16(b, p + 0x10, 160);
            }
        } else if (op == kOpAttr && in_list && b[p + 4] == kMsgAddText) {
            after = p + n;
        }
        p += n;
    }
    if (!page || !after) return false;
    std::vector<uint8_t> entry(13);
    Put32(entry, 0, kOpAttr);
    entry[4] = kMsgAddText;
    Put32(entry, 5, kPauseLabel);
    Put32(entry, 9, 0);
    b.insert(b.begin() + after, entry.begin(), entry.end());
    Put32(b, page + 0x11, Get32(b, page + 0x11) + uint32_t(entry.size()));
    return true;
}

uint32_t GuestString(const char* s) {
    uint32_t n = uint32_t(std::strlen(s)) + 1, va = mem::AllocHostContiguous(n, 16);  // readable by the game's code
    std::memcpy(kernel::HostPtr(va), s, n);
    return va;
}

// the menu file with our item, in guest memory; 0 when the stream has no main menu (another menu file)
uint32_t Patch(uint32_t stream, uint32_t at);
uint32_t Patched(uint32_t stream) {
    static uint32_t source = 0, made = 0, source_len = 0, source_hash = 0;
    static std::unordered_map<uint64_t, uint32_t> copies;  // length and hash to the copy (0: none for that file)
    // the stream's length: its records to the end opcode
    uint32_t at = 4;  // a header dword first
    for (;;) {
        uint32_t op = R32(stream + at);
        if (op == kOpEnd) {
            at += 4;
            break;
        }
        uint32_t n = RecordSize(op);
        if (!n || at > 0x400000) return 0;  // not a stream this walker knows
        at += n;
    }
    uint32_t hash = 2166136261u;  // FNV-1a
    const uint8_t* bytes = static_cast<const uint8_t*>(kernel::HostPtr(stream));
    for (uint32_t i = 0; i < at; i++) hash = (hash ^ bytes[i]) * 16777619u;
    if (stream == source && at == source_len && hash == source_hash) return made;
    source = stream, source_len = at, source_hash = hash;
    uint64_t key = uint64_t(at) << 32 | hash;
    if (auto c = copies.find(key); c != copies.end()) return made = c->second;
    made = Patch(stream, at);
    if (made || !g_av.empty()) copies[key] = made;  // a level's file before the front end's is tried again
    return made;
}

uint32_t Patch(uint32_t stream, uint32_t at) {
    uint32_t made = 0;
    std::vector<uint8_t> b(at);
    std::memcpy(b.data(), kernel::HostPtr(stream), at);
    // the main menu page, its three items' widget records, and where the Codenames item's records end
    size_t page = 0, item[3] = {}, codenames_end = 0;
    for (size_t p = 4; p + 4 <= b.size();) {
        uint32_t op = Get32(b, p), n = RecordSize(op);
        if (!n) break;
        if (op == kOpPage) {
            if (page && !codenames_end && item[2]) codenames_end = p;
            if (Get32(b, p + 4) == kMainMenu && Get32(b, p + 8) == kFrontVar) page = p;
        } else if (op == kOpWidget && page && !codenames_end) {
            uint32_t id = Get32(b, p + 4);
            if (item[2] && id != kCodenames) codenames_end = p;
            if (id == kNightFire) item[0] = p;
            if (id == kMultiplayer) item[1] = p;
            if (id == kCodenames) item[2] = p;
        }
        p += n;
    }
    if (!page || !item[0] || !item[1] || !item[2] || !codenames_end) {
        // a level's file: the pause screen's entry, and the page before the file's end
        if (g_av.empty() || !AddToPause(b)) return 0;
        std::vector<std::vector<uint8_t>> pages = {nf::PcPage(g_av, RecordSize)};
        if (pages[0].empty()) return 0;
        for (auto& g : nf::GraphicsPages(g_av, RecordSize)) pages.push_back(std::move(g));
        // behind each page's rows the pause screen's panel, made the page's size (the front end's backdrop is not
        // loaded)
        std::vector<uint8_t> panel = PausePanel(b);
        if (!panel.empty())
            Put16(panel, 0xA, 16), Put16(panel, 0xC, 24), Put16(panel, 0xE, 608), Put16(panel, 0x10, 432);
        for (std::vector<uint8_t>& page_copy : pages) {
            if (page_copy.empty()) continue;
            size_t second = 0;  // the page's second widget record: the panel goes before it
            for (size_t p = RecordSize(kOpPage), widgets = 0; p + 4 <= page_copy.size() && !second;) {
                uint32_t op = Get32(page_copy, p), n = RecordSize(op);
                if (!n) break;
                if (op == kOpWidget && ++widgets == 2) second = p;
                p += n;
            }
            if (!panel.empty() && second) {
                page_copy.insert(page_copy.begin() + second, panel.begin(), panel.end());
                Put32(page_copy, 0x11, Get32(page_copy, 0x11) + uint32_t(panel.size()));
            }
            b.insert(b.end() - 4, page_copy.begin(), page_copy.end());  // before the end record
        }
        made = mem::AllocHostContiguous(uint32_t(b.size()), 16);
        std::memcpy(kernel::HostPtr(made), b.data(), b.size());
        LOGR("nightfire: Settings added to the pause screen (menu file %08X, %u bytes)", stream, at);
        return made;
    }
    // our items: the Codenames item's records with our id, row and label
    auto clone = [&](uint32_t id, int16_t y, uint32_t label) {
        std::vector<uint8_t> it(b.begin() + item[2], b.begin() + codenames_end);
        Put32(it, 4, id);
        Put16(it, 0xC, y);
        for (size_t p = 0; p + 4 <= it.size();) {
            uint32_t op = Get32(it, p), n = RecordSize(op);
            if (!n) break;
            if (op == kOpAttr && it[p + 4] == kMsgText) {
                Put32(it, p + 5, label);  // a: a string used as it is
                Put32(it, p + 9, 0);
            }
            p += n;
        }
        return it;
    };
    static const uint32_t pc_label = GuestString("Settings"), quit_label = GuestString("Quit Game");
    std::vector<uint8_t> ours = clone(kPcItem, kRowY[3], pc_label), quit = clone(kQuitItem, kRowY[4], quit_label);
    ours.insert(ours.end(), quit.begin(), quit.end());
    for (int i = 0; i < 3; i++) Put16(b, item[i] + 0xC, kRowY[i]);
    Put32(b, page + 0x11, Get32(b, page + 0x11) + uint32_t(ours.size()));  // the page's body size
    b.insert(b.begin() + codenames_end, ours.begin(), ours.end());
    // the Settings page, made from AV Options' and put after it (src/game/pc_settings_page.cpp)
    for (size_t p = 4; p + 4 <= b.size();) {
        uint32_t op = Get32(b, p), n = RecordSize(op);
        if (!n) break;
        if (op == kOpPage && Get32(b, p + 4) == kAvPage && Get32(b, p + 8) == kFrontVar && b[p + 0x10] == 0) {
            if (p + n + Get32(b, p + 0x11) > b.size()) break;  // a body past the file: not laid out as expected
            std::vector<uint8_t> av(b.begin() + p, b.begin() + p + n + Get32(b, p + 0x11));
            g_av = av;
            std::vector<uint8_t> page_copy = nf::PcPage(av, RecordSize);
            if (page_copy.empty()) break;  // not laid out as expected: no page
            p += av.size();
            for (auto& g : nf::GraphicsPages(av, RecordSize)) page_copy.insert(page_copy.end(), g.begin(), g.end());
            b.insert(b.begin() + p, page_copy.begin(), page_copy.end());
            LOGR("nightfire: Settings and Graphics pages made from AV Options' (%zu bytes)", page_copy.size());
            break;
        }
        p += n;
    }
    made = mem::AllocHostContiguous(uint32_t(b.size()), 16);  // past the title's memory, as the page's strings
    std::memcpy(kernel::HostPtr(made), b.data(), b.size());
    LOGR("nightfire: Settings added to the main menu (menu file %08X, %u bytes)", stream, at);
    return made;
}
}  // namespace

// ---- the menus' geometry for the mouse: every widget record of every page of the files the builder has read
namespace {
std::mutex g_geo_lock;
std::unordered_map<uint32_t, std::vector<nf::MenuWidget>> g_geo;  // page id to its widgets
uint32_t g_geo_stream = 0, g_geo_len = 0;
std::atomic<uint32_t> g_page_widget{0}, g_page_id{0};  // the page last entered (message 0x4C)

void Learn(uint32_t stream) {
    uint32_t at = 4;
    for (;;) {
        uint32_t op = R32(stream + at);
        if (op == kOpEnd) break;
        uint32_t n = RecordSize(op);
        if (!n || at > 0x400000) return;
        at += n;
    }
    if (stream == g_geo_stream && at == g_geo_len) return;
    g_geo_stream = stream, g_geo_len = at;
    std::vector<uint8_t> b(at);
    std::memcpy(b.data(), kernel::HostPtr(stream), at);
    std::lock_guard lock(g_geo_lock);
    uint32_t page = 0;
    nf::MenuWidget* last = nullptr;
    for (size_t p = 4; p + 4 <= b.size();) {
        uint32_t op = Get32(b, p), n = RecordSize(op);
        if (!n) break;
        if (op == kOpPage) {
            page = Get32(b, p + 4);
            g_geo[page].clear();
            last = nullptr;
        } else if (op == kOpWidget && page) {
            int16_t v[4];
            std::memcpy(v, &b[p + 0xA], 8);
            g_geo[page].push_back({Get32(b, p + 4), b[p + 8], v[0], v[1], v[2], v[3], 0});
            last = &g_geo[page].back();
        } else if (op == kOpAttr && last && b[p + 4] == kMsgAddText) {
            last->entries++;
        }
        p += n;
    }
}
}  // namespace

namespace nf {
bool MenuGeometry(uint32_t& page, std::vector<MenuWidget>& widgets, uint32_t& focused) {
    // the page the game last entered (its 0x4C, in the dispatcher below); the menu context's +0x1B8 keeps the
    // pause screen's and level pages' ids, not the front end's
    page = g_page_id;
    uint32_t w = g_page_widget;
    if (!w || w >= 0x84000000u || R32(w + 0x18) != page) return false;  // gone with its menu file
    uint32_t f = R32(w + 0xAC);  // the focused widget (0x959E0 reads it there)
    focused = f && f < 0x84000000u ? R32(f + 0x18) : 0;
    std::lock_guard lock(g_geo_lock);
    auto it = g_geo.find(page);
    if (it == g_geo.end()) return false;
    widgets.clear();
    for (const MenuWidget& m : it->second)  // not the rows hidden (the style page's other styles')
        if (!MenuRowHidden(page, m.id)) widgets.push_back(m);
    return true;
}
}  // namespace nf

// the page builder (cdecl, 7 arguments): it reads [0x25FB08] only while it runs
X86_EXTERN_FUNC(__imp__sub_00093960);
X86_FUNC(sub_00093960) {
    uint32_t stream = R32(kStreamPtr), copy = stream ? Patched(stream) : 0;
    if (copy || stream) Learn(copy ? copy : stream);
    if (copy) W32(kStreamPtr, copy);
    __imp__sub_00093960(ctx, base);
    if (copy) W32(kStreamPtr, stream);
}

// the per-id message dispatcher (cdecl: player byte, widget, message, widget, argument; al 1 = handled)
X86_EXTERN_FUNC(__imp__sub_0008E320);
X86_FUNC(sub_0008E320) {
    uint32_t widget = R32(ctx.esp + 8), message = R32(ctx.esp + 0xC);
    if (widget && message == 0x4C && (R32(widget + 0x18) >> 28) == 4) {  // a page entered: the mouse's geometry
        g_page_widget = widget;
        g_page_id = R32(widget + 0x18);
    }
    if (widget && (nf::PcPageMessage(ctx, R32(ctx.esp + 4) & 0xFF, widget, message, R32(ctx.esp + 0x14)) ||
                   nf::GraphicsPageMessage(ctx, R32(ctx.esp + 4) & 0xFF, widget, message, R32(ctx.esp + 0x14)))) {
        ctx.eax = 1;
        ctx.esp += 4;
        return;
    }
    // A on the pause screen's choices with SETTINGS chosen (its fourth, 3)
    for (uint32_t w : {widget, R32(ctx.esp + 0x10)}) {
        if (!w || message != kActivate || R32(w + 0x18) != kPauseItem || R32(w + 0x20) != kPauseChoices) continue;
        uint32_t eax = ctx.eax, chosen = kernel::CallGuest(ctx, kSetWidget, {w, kMsgRead, 0, 0}) & 0xFF;
        ctx.eax = eax;
        if (chosen != 3) break;
        nf::OpenPcPage(ctx, R32(ctx.esp + 4) & 0xFF);
        LOGR("nightfire: Settings opened from the pause screen");
        ctx.eax = 1;
        ctx.esp += 4;
        return;
    }
    if (widget && message == kActivate && R32(widget + 0x18) == kQuitItem) {
        LOGR("nightfire: Quit Game from the main menu");
        app::Quit();
        ctx.eax = 1;
        ctx.esp += 4;
        return;
    }
    if (!widget || message != kActivate || R32(widget + 0x18) != kPcItem) {
        __imp__sub_0008E320(ctx, base);
        return;
    }
    nf::OpenPcPage(ctx, R32(ctx.esp + 4) & 0xFF);
    LOGR("nightfire: Settings opened from the main menu");
    ctx.eax = 1;
    ctx.esp += 4;  // cdecl: the return address only
}

// the help line of the focused item (cdecl, the page at [esp+4]): ours has no entry in the game's table
X86_EXTERN_FUNC(__imp__sub_000959E0);
X86_FUNC(sub_000959E0) {
    uint32_t page = R32(ctx.esp + 4);
    __imp__sub_000959E0(ctx, base);
    uint32_t focused = page ? R32(page + 0xAC) : 0, box = R32(kHelpBox), id = focused ? R32(focused + 0x18) : 0;
    if (!box || (id != kPcItem && id != kQuitItem)) return;
    static const uint32_t help = GuestString("Picture, controls and more for this PC."),
                          quit_help = GuestString("Leave the game.");
    uint32_t eax = ctx.eax;
    kernel::CallGuest(ctx, kSetWidget, {box, kMsgText, id == kQuitItem ? quit_help : help, 0});
    ctx.eax = eax;
}
