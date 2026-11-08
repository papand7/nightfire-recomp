// Quick restart and Discord's presence (default.xbe).
//
// Quick restart: the pause screen's RESTART asks "RESTART: ARE YOU SURE?" (its choices list turned into NO / YES);
// A on RESTART now answers it at once, by the presses a player would make (down to YES, A) once A is let go, so the
// game restarts the mission its own way. The restart key (Key Bindings, none by default) does it from play: Start,
// down to RESTART, A. Presses of ours are played on the polled pad (nf::QueuedPresses, from MenuMouse), each held
// 70 ms with 70 ms between, and only while the player holds nothing.
//
// Discord (app::SetPresence, SDK app/discord.cpp): "Playing <mission>" with the difficulty, "Multiplayer: <map>",
// or "In the menus", the elapsed time from when that began. A driving mission's name goes to Driving.xbe's exe in
// the environment (NF_PRESENCE), which shows it from its start.
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>

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
#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {
using kernel::R32;

constexpr uint32_t kMenuContext = 0x25F1D0, kPausePage = 0x4000004B;
constexpr uint32_t kLevel = 0x1F658C, kMpFlag = 0x260018, kMpMap = 0x260040, kMaps = 0x17C6A0, kDifficulty = 0x1F65A8;
constexpr uint32_t kPlayerObject = 0x1F6654, kGetText = 0x6D460;

std::mutex g_lock;
std::deque<int> g_presses;
int g_held = -1;
int64_t g_until = 0, g_rest = 0;

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void Queue(int control) {
    std::lock_guard lock(g_lock);
    if (g_presses.size() < 8) g_presses.push_back(control);
}

bool Paused() { return R32(kMenuContext + 0x1BC) != 0 && R32(kMenuContext + 0x1B8) == kPausePage; }

// the restart key: 0 none, 1 waiting for the pause screen
int g_restart = 0, g_restart_wait = 0;
}  // namespace

namespace nf {

// MenuMouse: our presses on controller 1's polled pad
void QueuedPresses(input::Pad& p) {
    std::lock_guard lock(g_lock);
    const int64_t now = NowMs();
    if (g_held >= 0 && now < g_until) {
        if (g_held < input::kA) p.buttons |= uint16_t(1u << g_held);
        else p.analog[g_held - input::kA] = 255;
        return;
    }
    if (g_held >= 0) g_held = -1, g_rest = now + 70;
    if (now < g_rest || g_presses.empty()) return;
    bool any = p.buttons != 0;  // the player's own presses let go first (A on RESTART still held)
    for (uint8_t a : p.analog) any |= a > 30;
    if (any) return;
    g_held = g_presses.front(), g_until = now + 70;
    g_presses.pop_front();
}

void QueuePress(int control) { Queue(control); }

// a list widget's entries (src/game/modern_menu_frame.cpp has the layout): the first the link at +0x270 less +0x27C
// dwords, each entry's +0 the next, column 0's text at +8, +0x2C8 an entry the cursor skips, +0x560 the one shown
std::string EntryText(uint32_t e) {
    std::string t;
    for (uint32_t i = 0; i < 40 && mem::IsCommitted(e + 8 + i); i++) {
        char c = kernel::G<char>(e + 8 + i);
        if (!c) break;
        if (c != ' ') t += char(std::toupper(static_cast<unsigned char>(c)));
    }
    return t;
}
uint32_t g_question = 0;      // the pause list whose question is awaited (RestartChosen), and since when
int64_t g_question_at = 0, g_answered = -100000;
// the question up (its entries the list's now): steps from the one shown to YES, then A
bool Answer(uint32_t w) {
    const uint32_t link = R32(w + 0x270), shown = R32(w + 0x560);
    int at = -1, yes = -1, k = 0;
    std::string all;
    for (uint32_t e = link ? link - R32(w + 0x27C) * 4 : 0; e && k < 16 && mem::IsCommitted(e + 0x2C8); e = R32(e)) {
        if (kernel::G<uint8_t>(e + 0x2C8)) continue;
        const std::string t = EntryText(e);
        all += (all.empty() ? "" : ", ") + t;
        if (e == shown) at = k;
        if (t == "YES" || t == "OUI" || t == "JA" || t == "SI" || t == "S\xCD") yes = k;
        k++;
    }
    if (at < 0 || yes < 0) return false;
    g_answered = NowMs(), g_question = 0;
    for (int i = 0; i < std::abs(yes - at) && i < 4; i++) Queue(yes > at ? input::kDown : input::kUp);
    Queue(input::kA);
    LOGR("nightfire: Restart: answered yes at once (%s; %d to %d)", all.c_str(), at, yes);
    return true;
}
bool RestartChosen(X86Context&, uint32_t w) {
    // the question's own A comes here again (its list is the same widget): the game's alone
    if (NowMs() - g_answered < 4000 || g_question) return false;
    g_question = w, g_question_at = NowMs();  // the game builds the question after this: answered from QuickFrame
    return true;
}

void QuickFrame(X86Context& ctx) {
    // the restart key
    static bool was = false;
    const bool key = restart_key && input::KeyHeld(int(restart_key)) && !app::KeyCaptureBusy();
    if (key && !was && !g_restart && !PhotoActive()) {
        if (Paused()) {
            g_restart = 1, g_restart_wait = 0;
        } else if (!MenuUp() && R32(kPlayerObject)) {
            Queue(input::kStart);
            g_restart = 1, g_restart_wait = 0;
        }
        if (g_restart) LOGR("nightfire: the restart key: the mission restarts");
    }
    was = key;
    if (g_restart == 1) {
        if (Paused()) {
            // the choices list from CONTINUE: down to RESTART, A (RestartChosen answers its question)
            Queue(input::kDown), Queue(input::kA);
            g_restart = 0;
        } else if (++g_restart_wait > 120) {
            g_restart = 0;
        }
    }

    if (g_question) {
        // the question is a list of its own beside the choices (same id, its own widget): the page's shown lists
        const uint32_t pw = R32(kMenuContext + 0x1BC);
        bool done = !Paused();
        uint32_t w = pw ? R32(pw + 0x94) : 0;
        if (w) w -= R32(pw + 0xA0) * 4;
        for (int n = 0; !done && w && n < 400 && mem::IsCommitted(w + 0x80); w = R32(w), n++)
            if (!(kernel::G<uint8_t>(w + 0x7A) & 1) && kernel::G<uint8_t>(w + 0x7B) == 6 && R32(w + 0x18) == R32(g_question + 0x18))
                done = Answer(w);
        if (done || NowMs() - g_question_at > 2000) {
            if (!done) LOGR("nightfire: Restart: the question did not come; asked as the game does");
            g_question = 0;
        }
    }
    // Discord's presence, twice a second
    static int frames = 0;
    if (++frames % 30) return;
    static std::string details, state, said;
    static int64_t start = 0;
    static uint32_t map_seen = ~0u;
    static std::string map_name;
    std::string d, st;
    const uint32_t level = R32(kLevel);
    if (R32(kPlayerObject)) {
        if (R32(kMpFlag)) {
            const uint32_t map = R32(kMpMap);
            if (map != map_seen) {
                map_seen = map, map_name.clear();
                for (uint32_t i = 0; i < 8; i++)
                    if (R32(kMaps + i * 0x18 + 0xC) == map) {
                        const uint32_t eax = ctx.eax, text = kernel::CallGuest(ctx, kGetText, {R32(kMaps + i * 0x18 + 4), 0});
                        ctx.eax = eax;
                        if (text && mem::IsCommitted(text)) map_name.assign(&kernel::G<char>(text), strnlen(&kernel::G<char>(text), 40));
                    }
            }
            d = map_name.empty() ? "Multiplayer" : "Multiplayer: " + map_name;
            st = NetOn() ? "Network game" : "Split screen";
        } else if (const char* m = MissionName(level)) {
            d = std::string("Playing ") + m;
            const uint32_t diff = R32(kDifficulty);
            st = diff == 1 ? "Operative" : diff == 2 ? "Agent" : diff == 3 ? "00 Agent" : "";
            if (coop) st = st.empty() ? "Story co-op" : st + ", story co-op";
        } else {
            d = "On a mission";
        }
    } else {
        d = "In the menus";
    }
#ifdef _WIN32
    // a driving mission's name for Driving.xbe's exe (its level id is set before the launch)
    if (const char* m = (level >> 24) == 0x09 ? MissionName(level) : nullptr; m && said != m) {
        said = m;
        SetEnvironmentVariableA("NF_PRESENCE", m);
    }
#endif
    if (d != details || st != state) {
        if (d != details) start = int64_t(std::time(nullptr));
        details = d, state = st;
#ifndef XBE_HEADLESS
        app::SetPresence(details, state, start);
#endif
    }
}

}  // namespace nf
