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

bool RestartChosen(X86Context&) {
    // the question's own A (on its YES, the list's second entry too) comes here again: the game's alone
    static int64_t answered = -100000;
    const int64_t now = NowMs();
    if (now - answered < 4000) return false;
    answered = now;
    Queue(input::kDown), Queue(input::kA);  // the question's YES
    LOGR("nightfire: Restart: answered yes at once");
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
