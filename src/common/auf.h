#pragma once
// Agent Under Fire's own settings and what its executables share (default.xbe: the logos and movies; bond_eng.xbe: the
// game; DRIVING.XBE: the driving levels)
#include <cstdint>
#include <string>
#include <vector>

#include "core/achievements.h"
#include "core/input.h"

struct X86Context;

namespace auf {
enum Image { kDefault, kBond, kDriving };
extern const bool kUsa;       // the USA disc: its programs, English only, 60 Hz (each image's hooks.cpp)
extern const int kImage;      // which exe this is (each image's hooks.cpp)
extern bool skip_intro;
extern bool uncapped;         // on foot paced by the frame rate cap, not the console's 30 (src/bond/framerate.cpp); driving at 60, not 50 (src/driving/hooks.cpp)
extern bool hit_flash;        // the white flash when Bond is hit, on by default (src/bond/hooks.cpp)
extern bool auto_aim;         // the game's auto-aim, off by default (src/bond/mouse.cpp, src/driving/mouse.cpp)
extern bool weapon_sway;       // the gun trails the view's turns (src/bond/aim.cpp)
extern bool aim_down_sights;  // L raises the gun to the middle and zooms a little, not manual aim (src/bond/aim.cpp)
extern uint32_t fov_percent;  // the 3D views' field of view, percent of the game's
float FovScale();             // fov_percent as a factor, 0.5 to 1.5
float FrameWidth();           // the frame's width over the game's 4:3, 1 on a 4:3 screen
bool MenuUp();                // a menu has the controller (each image's own test): the mouse plays A, B and the d-pad
bool OnGun();                 // DRIVING.XBE: a mounted gun's controls are in force, not the car's
bool Zoomed();                // bond_eng.xbe: through a scope
void ScopeZoom(int notches);  // bond_eng.xbe: the wheel's notches, up nearer
bool PlayerFree();            // bond_eng.xbe: Bond under the player's control, not a cutscene or a hint waiting for A
bool ButtonPictures(uint32_t header, uint32_t palette);  // GameInfo::texture_bound (src/common/buttons.cpp)
void RefreshButtons();        // each poll
void MenuMouse(int port, input::Pad& p);
// bond_eng.xbe's and each language's: the mission played, a run's medal, the best medals (0 in the other exes)
extern const uint32_t kAddrMission, kAddrRunMedal, kAddrBestMedals;
std::vector<achievements::Def> AchievementSet();  // src/common/achievement_set.cpp
// the menus' pointer, on foot (src/bond/pointer.cpp): where it is in the menus' 640x480, the buttons, and what a click does
enum { kClickNone, kClickWait, kClickGo, kClickLeft, kClickRight, kClickDown };  // Go plays A, the last three the d-pad
bool PointerAt(float& x, float& y);
uint32_t PointerButtons();
int MenuClick();
// the PC Settings page's rows, on foot and in the driving pause (src/common/pc_rows.cpp); lang 0 English, 1 French, 2 German
constexpr int kPcRows = 8;
int Language();                  // 0 English, 1 French, 2 German: the setting (src/common/pc_rows.cpp)
const char* PcRowName(int lang);
const char* PcTabName();         // on the pause's strip, where the name would not fit
int PcRows();                    // of the tab showing, its chooser (row 0) included
const char* PcLabel(int row, int lang);
std::string PcValue(int row, int lang);
int PcClickDir(float value_x);   // what a press of A does to a value: down with the mouse on its left arrow
void PcStep(int row, int dir);   // row 0 the tab; More Settings opens the settings window
bool PointerWanted();             // the arrow shows: each image's own test
void PcSettingsFrame(X86Context& ctx, uint8_t* base, uint32_t file);  // src/bond/pc_settings.cpp

}  // namespace auf
