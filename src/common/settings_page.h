#pragma once
// The PC settings in the modern menus, laid out as NightFire's: Settings, then Graphics (Style Options, Effects, Ray Tracing) and
// More Settings (Key Bindings, Controller Buttons, Steering Wheel, Achievements, Gameplay & More), built here from app::Settings
// and the game's own settings, saved as they change. They hold every setting: this game has no F1 window. The pages are the host's, not the game's widgets: src/bond/pc_settings.cpp
// shows them in the place of the PC Settings page the game's menu holds open underneath, and plays the game's presses and the
// UI's clicks into Event.
#include <cstdint>

#include "common/auf.h"

namespace auf::settings_page {
enum Result { kStay, kClose };
constexpr uint32_t kNoRow = ~0u;  // Event: the press came from the game's input, not a click on a row

void Reset();                               // the page opens on its list of categories
void Build(ModernPage& page, bool paused);  // the rows, title, help and focus (the tab strip is the caller's)
void Watch();                               // each game frame: a binding's wait let go once its page is gone
uint32_t PageTag();                         // added to the game's page address as the UI's page id: a category scrolls from its top
// a press: event as the game's menu handler numbers them (0 left, 1 right, 2 up, 3 down, 4 A, 5 B); row from 1 is the row the mouse
// moved the focus to first, 0 the tab strip's own arrows, kNoRow none
Result Event(uint32_t row, int event, bool paused);
}  // namespace auf::settings_page

// the game's side (src/bond/pc_settings.cpp), for src/bond/modern_menu_frame.cpp: while the PC Settings page is open on the game's page,
// PcSettingsHost turns what was read of it into the host's settings page (true), and PcSettingsPlay plays the UI's clicks into it
namespace auf {
bool PcSettingsHost(uint32_t file, uint32_t page, ModernPage& p);
void PcSettingsPlay(X86Context& ctx, uint8_t* base, uint32_t file, uint32_t page);
}  // namespace auf
