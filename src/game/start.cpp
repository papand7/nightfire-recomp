// skip_paris: a new game's START at the title launches Driving.xbe straight into Paris (level 0x9000001, the state
// machine 0x6ACA0's state 9 through 0xDFB50: cdecl, the launch data and its size, 0xA50). Started cold (not a
// relaunch from the other xbe: the game reads its launch data page at startup, so the page is no test by then), that
// first launch goes back to default.xbe instead, with the data Driving.xbe hands
// back when Paris is quit from its pause screen (as measured with XBE_LAUNCH_DUMP): the front end opens at the
// codename select and Paris stays the first mission, started from the menus as any other.
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <vector>

#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace {
constexpr uint32_t kLevel = 0x1F6588, kParis = 0x9000001;
constexpr uint32_t kLaunch = 0xE8FE0;      // cdecl: the xbe's name ("d:\" put before it), the data, its size
constexpr uint32_t kDefaultXbe = 0x162114;  // "default.xbe"

bool g_main_menu_seen = false;  // this run reached the main menu (page 0x40000002, as MenuGeometry names pages)

void Put(uint32_t data, uint32_t off, const char* s) {
    std::memcpy(kernel::HostPtr(data + off), s, std::strlen(s) + 1);
}
}  // namespace

namespace nf {
// the pads' frame (the menus too): the main menu seen, for skip_paris
void NoteMainMenu() {
    if (g_main_menu_seen) return;
    uint32_t page = 0, focused = 0;
    static std::vector<MenuWidget> widgets;
    if (MenuGeometry(page, widgets, focused) && page == 0x40000002) {
        g_main_menu_seen = true;
        LOGR("nightfire: the main menu is up: Paris from here starts as chosen");
    }
}
}  // namespace nf

X86_EXTERN_FUNC(__imp__sub_000DFB50);
X86_FUNC(sub_000DFB50) {
    static bool first = true;
#if defined(_WIN32)
    static const bool relaunched = std::wcsstr(GetCommandLineW(), L"--relaunched") != nullptr;  // app/main.cpp
#elif defined(XBE_HEADLESS)
    static const bool relaunched = true;  // the headless harness: as the console (smoke.sh drives Paris)
#else
    // app/main_sdl.cpp: --relaunched on the command line, or a relaunch's exec (kernel::Relaunch), sets it
    static const bool relaunched = [] {
        const char* e = std::getenv("XBE_RELAUNCHED");
        return e && *e == '1';
    }();
#endif
    // a cold start's Paris before the menus: the title's START. Once the main menu was up, a Paris launch is the
    // player choosing it (the title's START can open the menus without one), never sent back to them
    const bool cold = first && !relaunched && !g_main_menu_seen;
    first = false;
    uint32_t data = kernel::R32(ctx.esp + 4), size = kernel::R32(ctx.esp + 8);
    if (!nf::skip_paris || !cold || kernel::R32(kLevel) != kParis || !data || size < 0x980) {
        __imp__sub_000DFB50(ctx, base);
        return;
    }
    // Driving.xbe's fields of the data it hands back after a quit Paris
    Put(data, 0x4B4, "LOADER READY");
    Put(data, 0x4F4, "paris_mis01");
    kernel::W32(data + 0x53C, 3);
    kernel::G<uint8_t>(data + 0x5DD) = 1;
    kernel::G<uint8_t>(data + 0x615) = 2;
    kernel::G<uint8_t>(data + 0x619) = 1;
    kernel::G<uint8_t>(data + 0x625) = 1;
    kernel::G<uint8_t>(data + 0x979) = 2;
    LOGR("nightfire: Paris not started at the game's start (skip_paris), straight to the menus");
    kernel::CallGuest(ctx, kLaunch, {kDefaultXbe, data, size});
    ctx.esp += 4;  // cdecl: the return address only
}
