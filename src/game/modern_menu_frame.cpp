// Modern menus, the game's side (src/common/modern_menu.cpp draws them): each frame in the menus (the pad reader's
// hook, controls.cpp) the page up is read from its widgets as they are now and handed to the UI, and the UI's clicks
// are played: the focus moved onto the row (MenuFocus), then A, B, left or right pressed on the polled pad.
// The page widget is the menu context's +0x1BC (0x25F1D0, player 1), its id +0x1B8; its widgets the list at +0x94 (the
// link +0xA0 dwords in), the focused one +0xAC. A widget: +0x18 id, +0x70 / +0x72 x, y (the page's 640 x 480), +0x7A bit
// 0 hidden, +0x7B type (1 button, 5 label, 6 list, 9 choice, 10 slider). Its text: a label's char* at +0xF4, a
// button's its own label's (+0x94 in, so +0x188), a choice's shown item +0x304 (the item's text 8 in), a list's
// shown entry +0x560 (8 in); a slider's value is message 0x40 (0x72630). A row is an id whose widgets include one to
// press (types 1, 6, 9, 10), its label the type 5 of the same id; the title is the topmost other text, the help the
// help box's (0x10000225). NF_MM_DUMP=1 logs each page's widgets once.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "game/menu_page.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {
using kernel::R32;
constexpr uint32_t kMenuContext = 0x25F1D0, kSetWidget = 0x72630, kHelpBox = 0x10000225;

uint8_t U8(uint32_t a) { return *static_cast<const uint8_t*>(kernel::HostPtr(a)); }
int16_t S16(uint32_t a) { return *static_cast<const int16_t*>(kernel::HostPtr(a)); }

// a guest string, printable characters only (none where the pointer is not one)
std::string Text(uint32_t p) {
    std::string s;
    if (p < 0x10000 || !mem::IsCommitted(p)) return s;
    for (int i = 0; i < 160; i++) {
        if (((p + i) & 0xFFF) == 0 && !mem::IsCommitted(p + i)) break;
        uint8_t c = U8(p + i);
        if (!c) break;
        if (c >= 0x20 && c < 0x7F) s += char(c);
        else if (c == '\n') s += ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
std::string TextAt(uint32_t field) { return mem::IsCommitted(field) ? Text(R32(field)) : std::string(); }

std::map<uint32_t, uint32_t> g_lists;  // a list widget's table (0x7F680's), 24 bytes an entry

struct Seen {
    uint32_t id;
    uint8_t type;
    int x, y;
    std::string text;
    uint32_t w;
};
}  // namespace

namespace nf {

void ModernListSeen(uint32_t widget, uint32_t table) {
    if (g_lists.size() > 64) g_lists.clear();
    g_lists[widget] = table;
}

void ModernMenuFrame(X86Context& ctx) {
    ModernPage page;
    if (!modern_menus || !MenuUp()) {
        ModernMenuPublish(page);
        return;
    }
    const uint32_t pw = R32(kMenuContext + 0x1BC);
    if (!pw || !mem::IsCommitted(pw)) {
        ModernMenuPublish(page);
        return;
    }
    page.page = R32(pw + 0x18);
    const uint32_t focused = R32(pw + 0xAC);
    const uint32_t focused_id = focused && mem::IsCommitted(focused) ? R32(focused + 0x18) : 0;
    const int px = S16(pw + 0x70), py = S16(pw + 0x72);
    std::vector<Seen> seen;
    uint32_t w = R32(pw + 0x94);
    if (w) w -= R32(pw + 0xA0) * 4;
    for (int n = 0; w && n < 400 && mem::IsCommitted(w); w = R32(w), n++) {
        if (U8(w + 0x7A) & 1) continue;
        Seen v{R32(w + 0x18), U8(w + 0x7B), px + S16(w + 0x70), py + S16(w + 0x72), {}, w};
        if (v.type == 5) v.text = TextAt(w + 0xF4);
        else if (v.type == 1) v.text = TextAt(w + 0x188);
        else if (v.type == 9) v.text = mem::IsCommitted(w + 0x304) && R32(w + 0x304) ? Text(R32(w + 0x304) + 8) : "";
        else if (v.type == 6) v.text = mem::IsCommitted(w + 0x560) && R32(w + 0x560) ? Text(R32(w + 0x560) + 8) : "";
        seen.push_back(std::move(v));
    }
    static uint32_t dumped = 0;
    static const bool dump = std::getenv("NF_MM_DUMP") != nullptr;
    if (dump && dumped != page.page) {
        dumped = page.page;
        LOGR("nightfire: modern menus: page %08X, %zu widgets, focus %08X", page.page, seen.size(), focused_id);
        for (const Seen& v : seen)
            LOGR("  %08X type %u at %d,%d \"%s\"", v.id, v.type, v.x, v.y, v.text.c_str());
    }
    // the rows: ids with a widget to press, top down
    std::map<uint32_t, ModernRow> rows;
    std::map<uint32_t, std::pair<int, int>> where;
    for (const Seen& v : seen) {
        if (v.type != 1 && v.type != 6 && v.type != 9 && v.type != 10) continue;
        if ((v.id >> 28) != 1 || v.id == 0x10000001 || v.id == 0x10000224 || v.id == kHelpBox) continue;
        ModernRow& r = rows[v.id];
        r.id = v.id;
        auto [it, fresh] = where.emplace(v.id, std::make_pair(v.y, v.x));
        if (!fresh) it->second = std::min(it->second, std::make_pair(v.y, v.x));
        if (v.type == 1) r.kind = kModernButton, r.label = v.text;
        if (v.type == 9) r.kind = kModernChoice, r.value = v.text;
        if (v.type == 6) r.kind = kModernList, r.value = v.text;
        if (v.type == 10) {
            r.kind = kModernSlider;
            r.slider_value = kernel::CallGuest(ctx, kSetWidget, {v.w, 0x40, 0, 0});
            r.slider_max = 20;  // ours and AV Options' volumes: 0 to 20 steps
            r.value = std::to_string(r.slider_value);
        }
    }
    for (const Seen& v : seen) {
        auto it = rows.find(v.id);
        if (v.type == 5 && it != rows.end() && it->second.label.empty()) it->second.label = v.text;
    }
    // a list: one widget to press (type 6 or 10) with no label of its own, and a label drawn on several lines (0x7F680's
    // five: the entries two before the current one to two after it): each line a row, the middle one the current
    std::vector<std::pair<std::pair<int, int>, ModernRow>> order;
    std::map<uint32_t, std::vector<const Seen*>> lines;
    for (const Seen& v : seen)
        if (v.type == 5 && !rows.count(v.id) && v.id != kHelpBox) lines[v.id].push_back(&v);
    const std::vector<const Seen*>* most = nullptr;
    for (auto& [id, l] : lines)
        if (l.size() >= 3 && (!most || l.size() > most->size())) most = &l;
    for (auto it = rows.begin(); most && it != rows.end(); ++it) {
        ModernRow& lr = it->second;
        if (!lr.label.empty() || (lr.kind != kModernList && lr.kind != kModernSlider)) continue;
        std::vector<const Seen*> l = *most;
        std::sort(l.begin(), l.end(), [](auto a, auto b) { return a->y < b->y; });
        const int mid = int(l.size() - 1) / 2;
        for (int k = 0; k < int(l.size()); k++) {
            if (l[k]->text.empty()) continue;
            ModernRow r;
            r.id = lr.id, r.kind = kModernButton, r.label = l[k]->text, r.list = true, r.list_step = k - mid;
            r.focused = lr.id == focused_id && k == mid;
            order.push_back({{where[lr.id].first, k}, r});
        }
        rows.erase(it);
        break;
    }
    for (auto& [id, r] : rows) {
        if (r.label.empty() && r.value.empty()) continue;
        if (r.label.empty()) r.label = r.kind == kModernList ? "" : r.value;
        r.focused = id == focused_id;
        order.push_back({where[id], r});
    }
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [at, r] : order) {
        if (r.focused) page.focus = int(page.rows.size());
        page.rows.push_back(std::move(r));
    }
    // the title: the topmost text that is not a row's; the help box's line
    int best = 1 << 30;
    for (const Seen& v : seen) {
        if (v.type != 5 || v.text.empty()) continue;
        if (v.id == kHelpBox) {
            page.help = v.text;
            continue;
        }
        if (v.text.find('~') != std::string::npos) {
            page.prompt = v.text;
            continue;
        }
        if (rows.count(v.id)) continue;
        if (v.y < best && v.y < 120) best = v.y, page.title = v.text;
    }
    if (page.title.empty() && page.page == 0x40000002) page.title = "Main Menu";
    page.up = !page.rows.empty();
    ModernMenuPublish(page);

    // the UI's clicks, for this page
    ModernAction a;
    while (ModernMenuAction(a)) {
        if (a.page != page.page) continue;
        if (a.id && a.id != focused_id) MenuFocus(ctx, 0, pw, a.id);
        if (a.list && a.control >= 0)  // a list's entry: up or down to it first
            for (int k = 0; k < std::abs(a.list_step) && k < 8; k++) ModernMenuPress(a.list_step > 0 ? input::kDown : input::kUp);
        if (a.control >= 0) ModernMenuPress(a.control);
    }
}

}  // namespace nf
