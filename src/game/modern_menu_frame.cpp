// Modern menus, the game's side (src/common/modern_menu.cpp draws them): each frame in the menus (the pad reader's
// hook, controls.cpp) the page up is read from its widgets as they are now and handed to the UI, and the UI's clicks
// are played: the focus moved onto the row (MenuFocus), then A, B, left or right pressed on the polled pad.
// The page widget is the menu context's +0x1BC (0x25F1D0, player 1), its id +0x1B8; its widgets the list at +0x94 (the
// link +0xA0 dwords in), the focused one +0xAC. A widget: +0x18 id, +0x70 / +0x72 x, y (the page's 640 x 480), +0x7A bit
// 0 hidden, +0x7B type (1 button, 5 label, 6 list, 9 choice, 10 slider, 200 text box). Its text: a label's char* at
// +0xF4 (its string handle +0xF0, GetText 0x6D460), a button's its own label's (+0x94 in, so +0x188), a choice's shown
// item +0x304 (the item's text 8 in), a list's entries' columns (ListFirst), a text box's char* +0x1DC; a slider's
// value is message 0x40 (0x72630). A row is an id whose widgets include one to press (types 1, 6, 9, 10), its label
// the type 5 of the same id; the title is the topmost other text, the help the help box's (0x10000225), else the
// page's text box (a description); a page with no title and a text box is a message box ("Notice"). Several buttons
// of one id on a line are tabs (the pause screen's), the focused one the current. NF_MM_DUMP=1 logs each page's
// widgets as they change.
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
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
constexpr uint32_t kMenuContext = 0x25F1D0, kSetWidget = 0x72630, kGetText = 0x6D460, kHelpBox = 0x10000225;

uint8_t U8(uint32_t a) { return *static_cast<const uint8_t*>(kernel::HostPtr(a)); }
int16_t S16(uint32_t a) { return *static_cast<const int16_t*>(kernel::HostPtr(a)); }

// a guest string, printable characters only (none where the pointer is not one)
std::string Text(uint32_t p, int most = 600) {
    std::string s;
    if (p < 0x10000 || !mem::IsCommitted(p)) return s;
    for (int i = 0; i < most; i++) {
        if (((p + i) & 0xFFF) == 0 && !mem::IsCommitted(p + i)) break;
        uint8_t c = U8(p + i);
        if (!c) break;
        if (c >= 0x20 && c < 0x7F) s += char(c);
        else if (c == '\n') s += ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
// "C O N T I N U E" (the pause screen's spaced capitals) as "Continue"
std::string Unspace(std::string t) {
    if (t.size() < 3 || t[1] != ' ') return t;
    for (size_t i = 1; i < t.size(); i += 2)
        if (t[i] != ' ') return t;
    std::string o;
    for (size_t i = 0; i < t.size(); i += 2) o += o.empty() ? t[i] : char(std::tolower(static_cast<unsigned char>(t[i])));
    return o;
}
// "SETTINGS" as "Settings": a label all capitals (4 letters or more) in title case
std::string Titled(std::string t) {
    int letters = 0;
    for (char c : t) {
        if (std::islower(static_cast<unsigned char>(c))) return t;
        letters += std::isalpha(static_cast<unsigned char>(c)) != 0;
    }
    if (letters < 4) return t;
    bool start = true;
    for (char& c : t) {
        if (std::isalpha(static_cast<unsigned char>(c))) c = start ? c : char(std::tolower(static_cast<unsigned char>(c))), start = false;
        else start = true;
    }
    return t;
}
std::string TextAt(uint32_t field) { return mem::IsCommitted(field) ? Text(R32(field)) : std::string(); }
// a linked list (type 6, drawn by 0x90FC0): its first entry the link at +0x270 less +0x27C dwords, each entry's +0
// the next; +0x504 columns, column c's text inline at c * 0x8C + 8 (the objectives' first column is an empty one, the
// score's second its points); +0x2C8 an entry the cursor skips (a header or a gap); +0x560 the entry shown
uint32_t ListFirst(uint32_t w) {
    const uint32_t l = R32(w + 0x270);
    return l ? l - R32(w + 0x27C) * 4 : 0;
}
int ListColumns(uint32_t w) { return std::clamp(int(R32(w + 0x504)), 1, 5); }
std::string ListText(uint32_t e, int c) { return mem::IsCommitted(e + c * 0x8C + 0x88) ? Text(e + c * 0x8C + 8) : ""; }
// an entry's words: its columns' texts, empty ones left out
std::vector<std::string> ListTexts(uint32_t w, uint32_t e) {
    std::vector<std::string> o;
    for (int c = 0; c < ListColumns(w); c++) {
        std::string t = ListText(e, c);
        size_t k = 0;
        while (k < t.size() && t[k] == ' ') k++;
        if (k < t.size()) o.push_back(t);
    }
    return o;
}
// "Press ~A to continue or ~B to cancel." as "Continue": a message box's one entry as the row it is
std::string PressTo(std::string t) {
    if (t.compare(0, 9, "Press ~A ") != 0) return t;
    size_t to = t.find(" to ", 6);
    if (to == std::string::npos) return t;
    std::string r = t.substr(to + 4);
    size_t end = r.find(" or ");
    if (end != std::string::npos) r.resize(end);
    while (!r.empty() && (r.back() == '.' || r.back() == ' ' || r.back() == '!')) r.pop_back();
    if (r.empty()) return t;
    r[0] = char(std::toupper(static_cast<unsigned char>(r[0])));
    return r;
}

std::map<uint32_t, uint32_t> g_lists;  // a list widget's table (0x7F680's), 24 bytes an entry

// the pages the game draws itself with the modern menus on: the title screen ("Press START", its copyright) and the
// other pages to wait through
bool ClassicPage(uint32_t page) {
    return page == 0x40000009 || page == 0x4000004A || page == 0x40000043 || page == 0x40000032 || page == 0x40000034;
}
int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
std::set<uint32_t> g_modern_pages;  // the pages seen drawn modern (the game's thread alone)
nf::ModernPage g_last_up;              // the last page drawn modern, and when
int64_t g_last_up_ms = -100000;

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

// hud.cpp: the game's widgets left undrawn: a modern page up, or about to be (a page drawn modern before, the pause
// screen, or one just after a modern one: the frames a page opens or closes in, its widgets not yet or no longer read)
bool ModernHidesClassic() {
    if (!modern_menus || !MenuUp()) return false;
    if (ModernMenuActive()) return true;
    const uint32_t pw = R32(kMenuContext + 0x1BC);
    const uint32_t id = pw && mem::IsCommitted(pw + 0x18) ? R32(pw + 0x18) : 0;
    if (ClassicPage(id)) return false;
    return id == 0x4000004B || g_modern_pages.count(id) || NowMs() - g_last_up_ms < 500;
}

void ModernMenuFrame(X86Context& ctx) {
    // NF_MM_LATE=1 (tests): the modern menus on from the first frame in play (a scripted route through the classic ones)
    static const bool late = std::getenv("NF_MM_LATE") != nullptr;
    if (late && !modern_menus && !MenuUp()) modern_menus = true;
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
        else if (v.type == 6 && mem::IsCommitted(w + 0x560) && R32(w + 0x560)) {
            auto t = ListTexts(w, R32(w + 0x560));
            if (!t.empty()) v.text = t.front();
        } else if (v.type == 200 && mem::IsCommitted(w + 0x1DC))  // a text box (0x94CF0): its words, wrapped
            v.text = Text(R32(w + 0x1DC), 1000);
        seen.push_back(std::move(v));
    }
    static uint32_t dumped = 0;
    static const bool dump = std::getenv("NF_MM_DUMP") != nullptr;
    static size_t dumped_n = 0;
    size_t sig = seen.size();  // the widgets, and the lists' first entries (a tab's turn changes the list alone)
    for (const Seen& v : seen) sig = sig * 31 + v.id + v.type + (v.type == 6 ? ListFirst(v.w) : 0);
    if (dump && (dumped != page.page || dumped_n != sig)) {
        dumped = page.page, dumped_n = sig;
        LOGR("nightfire: modern menus: page %08X, %zu widgets, focus %08X", page.page, seen.size(), focused_id);
        for (const Seen& v : seen)
            LOGR("  %08X type %u at %d,%d \"%s\"%s", v.id, v.type, v.x, v.y, v.text.c_str(), v.w == focused ? " (focused)" : "");
        for (const Seen& v : seen) {  // a linked list's entries, their columns
            if (v.type != 6) continue;
            LOGR("    list %08X: %d columns, link %u, shown %08X", v.id, ListColumns(v.w), R32(v.w + 0x27C), R32(v.w + 0x560));
            for (uint32_t it = ListFirst(v.w), k = 0; it && k < 16 && mem::IsCommitted(it + 0x2C8); k++) {
                std::string cols;
                for (int c = 0; c < ListColumns(v.w); c++) cols += " | " + ListText(it, c);
                LOGR("    entry %u at %08X%s:%s", k, it, U8(it + 0x2C8) ? " (skipped)" : "", cols.c_str());
                it = R32(it);
            }
        }
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
    for (auto& [id, r] : rows) {
        if (!r.label.empty() || r.kind == kModernList) continue;
        const auto at = where[id];
        int best_dy = 1 << 30;
        for (const Seen& v : seen) {
            if (v.type != 5 || v.text.empty() || rows.count(v.id) || v.text.find('~') != std::string::npos) continue;
            int dy = at.first - v.y;
            if (dy > 0 && dy < 100 && std::abs(v.x - at.second) < 60 && dy < best_dy) best_dy = dy, r.label = v.text;
        }
    }
    std::vector<std::pair<std::pair<int, int>, ModernRow>> order;
    // tabs: three or more buttons of one id on one line (the pause screen's); that id's row is then its list
    {
        std::map<uint32_t, std::vector<const Seen*>> buttons;
        for (const Seen& v : seen)
            if (v.type == 1) buttons[v.id].push_back(&v);
        for (auto& [id, b] : buttons) {
            if (b.size() < 3 || b.front()->y != b.back()->y) continue;
            std::sort(b.begin(), b.end(), [](auto a, auto c) { return a->x < c->x; });
            for (const Seen* t : b) {  // the current tab: the focused widget (+0xAC) is its button
                if (t->w == focused) page.tab = int(page.tabs.size());
                page.tabs.push_back(Titled(Unspace(t->text)));
            }
            if (rows.count(id)) rows[id].label.clear(), rows[id].kind = kModernList;
        }
    }
    // a linked list (type 6, ListFirst): a row for each entry the cursor stops on (their steps from the shown one
    // count those alone); a table (columns of words, the score) where the page's button line has no ~A: its lines text
    bool select = false;
    for (const Seen& v : seen) select |= v.type == 5 && v.text.find("~A") != std::string::npos;
    std::vector<std::string> table;
    for (const Seen& v : seen) {
        if (v.type != 6 || !rows.count(v.id) || !rows[v.id].label.empty()) continue;
        const uint32_t shown = R32(v.w + 0x560);
        std::vector<std::vector<std::string>> entries;
        std::vector<uint32_t> ats;
        bool columns = false;
        for (uint32_t it = ListFirst(v.w); it && entries.size() < 40 && mem::IsCommitted(it + 0x2C8); it = R32(it)) {
            entries.push_back(ListTexts(v.w, it)), ats.push_back(it);
            columns |= entries.back().size() >= 2;
        }
        if (columns && !select) {  // each line its columns, a tab between them (the UI lines them up)
            for (size_t i = 0; i < entries.size(); i++) {
                if (entries[i].empty()) continue;
                std::string line;
                for (int c = 0; c < ListColumns(v.w); c++) line += (c ? "\t" : "") + Unspace(ListText(ats[i], c));
                table.push_back(line);
            }
            rows.erase(v.id);
            continue;
        }
        std::vector<std::pair<int, std::string>> picks;  // a step and its words
        int at = -1, k = 0;
        for (size_t i = 0; i < entries.size(); i++) {
            if (U8(ats[i] + 0x2C8)) continue;  // a header or a gap: the cursor passes it by
            if (ats[i] == shown) at = k;
            // its words: the first column that is not a lone mark (the objectives' "~I", a state's picture)
            for (const std::string& t : entries[i])
                if (!(t.size() == 2 && t[0] == '~')) {
                    picks.push_back({k, Titled(PressTo(Unspace(t)))});
                    break;
                }
            k++;
        }
        if (at < 0 || picks.empty()) continue;
        for (auto& [step, label] : picks) {
            ModernRow r;
            r.id = v.id, r.kind = kModernButton, r.label = label, r.list = true, r.list_step = step - at;
            r.focused = v.id == focused_id && step == at;
            order.push_back({{v.y, step}, r});
        }
        rows.erase(v.id);
    }
    // a list whose table 0x7F680 drew from (ModernListSeen: Select Difficulty's, Edit Codename's): a row for each of
    // its entries in the widget's range (+0x128 to +0x12C), 24 bytes an entry: +4 its label's string handle (GetText
    // 0x6D460), +0x10 enabled; the current one its value (message 0x40)
    for (const Seen& v : seen) {
        auto tl = g_lists.find(v.w);
        if (tl == g_lists.end() || (v.type != 6 && v.type != 10) || !rows.count(v.id) || !rows[v.id].label.empty()) continue;
        const int lo = int(R32(v.w + 0x128)), hi = int(R32(v.w + 0x12C));
        if (lo < 0 || hi < lo || hi - lo > 40 || !mem::IsCommitted(tl->second + uint32_t(hi + 1) * 24 - 1)) continue;
        const uint32_t eax = ctx.eax;
        const int cur = int(kernel::CallGuest(ctx, kSetWidget, {v.w, 0x40, 0, 0}) & 0xFF);
        std::vector<ModernRow> made;
        for (int k = lo; k <= hi; k++) {
            const uint32_t e = tl->second + uint32_t(k) * 24;
            ModernRow r;
            const uint32_t handle = R32(e + 4);
            if (handle && handle != ~0u) r.label = Titled(Text(kernel::CallGuest(ctx, kGetText, {handle, 0})));
            if (r.label.empty()) break;  // not the table it was (the widget's memory another's now)
            r.id = v.id, r.kind = kModernButton, r.list = true, r.list_step = k - cur, r.disabled = !U8(e + 0x10);
            r.focused = v.id == focused_id && k == cur;
            made.push_back(r);
        }
        ctx.eax = eax;
        if (int(made.size()) != hi - lo + 1 || cur < lo || cur > hi) continue;
        for (ModernRow& r : made) order.push_back({{v.y, r.list_step + 100}, r});
        rows.erase(v.id);
    }
    // else a list: one widget to press (type 6 or 10) with no label of its own, and a label drawn on several lines
    // (0x7F680's five: the entries two before the current one to two after it): each line a row, the middle the current
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
        r.label = Unspace(r.label), r.value = Unspace(r.value);
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
    // the split screen's agents (labels "AGENT 1" ...), and a page of text: the labels that are not the title, the
    // prompt, a row's or one taken as a row's label (Choose Character), top down
    std::vector<const Seen*> text_lines;
    // a message box: a text box's words (0x94CF0), its button line a list's entry (not the help box's, nor the split
    // screen's "Connect the controller" boxes)
    std::vector<const Seen*> boxes;
    for (const Seen& v : seen) {
        if (v.type == 200 && !v.text.empty()) {
            if (v.id == kHelpBox && page.help.empty()) page.help = v.text;
            else if (v.id != kHelpBox && !rows.count(v.id)) boxes.push_back(&v);
            continue;
        }
        if (v.type != 5 || v.text.empty() || v.id == kHelpBox || rows.count(v.id) || v.text == page.title ||
            v.text.find('~') != std::string::npos)
            continue;
        if (v.text.size() == 7 && v.text.compare(0, 6, "AGENT ") == 0) {
            page.agents.push_back(Titled(v.text));
            continue;
        }
        bool used = false;
        for (const ModernRow& r : page.rows) used |= r.label == v.text;
        if (!used) text_lines.push_back(&v);
    }
    std::sort(page.agents.begin(), page.agents.end());
    if (page.title.empty() && page.page == 0x40000002) page.title = "Main Menu";
    if (page.title.empty() && page.page == 0x4000004B) page.title = "Paused";
    // a page with no title of its own: its text boxes are its message (the box's button "1" no row); else the first
    // is the page's description, the help line's when it has none (Select Map's, Scenario Options')
    const bool message = !boxes.empty() && page.agents.size() < 2 && page.title.empty();
    if (message) {
        text_lines.insert(text_lines.end(), boxes.begin(), boxes.end());
        std::vector<ModernRow> kept;
        page.focus = -1;
        for (ModernRow& r : page.rows) {
            if (std::none_of(r.label.begin(), r.label.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); }))
                continue;
            if (r.focused) page.focus = int(kept.size());
            kept.push_back(std::move(r));
        }
        page.rows = std::move(kept);
    } else if (!boxes.empty() && page.help.empty() && page.agents.size() < 2) {
        page.help = boxes.front()->text;
    }
    std::sort(text_lines.begin(), text_lines.end(), [](auto a, auto b) { return a->y != b->y ? a->y < b->y : a->x < b->x; });
    page.texts = table;
    // labels side by side on one line (the controls' "Y-Axis" and "Normal") one line, a tab between them
    for (size_t k = 0; k < text_lines.size(); k++) {
        std::string line = Unspace(text_lines[k]->text);
        while (k + 1 < text_lines.size() && text_lines[k]->type == 5 && text_lines[k + 1]->type == 5 &&
               std::abs(text_lines[k + 1]->y - text_lines[k]->y) <= 4)
            line += "\t" + Unspace(text_lines[++k]->text);
        if (std::find(page.texts.begin(), page.texts.end(), line) == page.texts.end()) page.texts.push_back(line);
    }
    if (page.help.empty()) page.help = GraphicsMenuHelp(page.page, focused_id);
    if (page.title.empty() && message) page.title = "Notice";
    // up: rows to press, the split screen's agents, or a page of text with tabs, a button line or a message box's
    // words; the title screen ("Press START", its copyright lines) and the other pages to wait through keep the game's
    // own drawing
    page.up = !ClassicPage(page.page) && (!page.rows.empty() || page.agents.size() >= 2 || !page.tabs.empty() ||
                                          (!page.texts.empty() && (!page.prompt.empty() || message)));
    const int64_t now = NowMs();
    if (page.up) {
        g_modern_pages.insert(page.page), g_last_up = page, g_last_up_ms = now;
        ModernMenuPublish(page);
    } else if (!ClassicPage(page.page) && g_last_up.up && now - g_last_up_ms < 400) {
        ModernMenuPublish(g_last_up);  // a page opening or closing (its widgets hidden a moment): the last one kept
    } else {
        ModernMenuPublish(page);
    }

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
