#pragma once
// Agent Under Fire's own settings and what its executables share (default.xbe: the logos and movies; bond_eng.xbe: the
// game; DRIVING.XBE: the driving levels)
#include <cstdint>
#include <string>
#include <vector>

#include "core/achievements.h"
#include "core/input.h"

struct X86Context;
struct ImFont;

namespace auf {
enum Image { kDefault, kBond, kDriving };
extern const bool kUsa;       // the USA disc: its programs, English only, 60 Hz (each image's hooks.cpp)
extern const int kImage;      // which exe this is (each image's hooks.cpp)
extern bool skip_intro;
extern bool uncapped;         // on foot paced by the frame rate cap, not the console's 30 (src/bond/framerate.cpp); driving at 60, not 50 (src/driving/hooks.cpp)
extern bool hit_flash;        // the white flash when Bond is hit, on by default (src/bond/hooks.cpp)
extern bool auto_aim;         // the game's auto-aim, off by default (src/bond/mouse.cpp, src/driving/mouse.cpp)
extern uint32_t photo_key;       // photo mode's key, a choice of kKeyNames (F10)
extern uint32_t restart_key;     // the mission restarted at once, a choice of kKeyNames (none)
extern bool skip_cutscenes;      // a mission's cutscenes skipped as they start (src/bond/pc_extras.cpp)
extern bool tight_movement;      // keyboard movement without the stick's slow start (src/bond/pc_extras.cpp)
extern bool weapon_keys;         // the number keys 1 to 9 pick a weapon (src/bond/weapon_keys.cpp)
extern uint32_t previous_weapon_key;  // a choice of kLetterKeyNames: back to the last weapon (Z)
extern uint32_t controller_scheme;    // the game's controller setup the keyboard plays, 0 to 3 (src/bond/mouse.cpp)
extern const char* const kLetterKeyNames[];  // None, A .. Z
extern const char* const kSchemeNames[];
int LetterKeyOf(uint32_t choice);
extern const char* const kKeyNames[];  // None, F1 .. F12, Print Screen, Pause, Insert, Delete, Home, End
int KeyOf(uint32_t choice);      // the Windows virtual key of a kKeyNames choice, 0 none
void BondFrame(X86Context& ctx, uint8_t* base);  // bond_eng.xbe once a frame: presence, the restart key
void WatchLevelSun();  // bond_eng.xbe: the loading map's sun handed to the ray tracing (level_sun.cpp)
extern float photo_fov;         // photo mode's field of view, a factor on top of fov_percent (1 outside it)
extern bool photo_mode;        // F10 frees the camera (src/bond/photo_mode.cpp)
extern bool ragdolls;          // dead enemies go limp and fall (src/bond/ragdoll.cpp), off by default
extern bool pickup_physics;    // dropped weapons and pickups fly, bounce and settle (src/bond/pickup_physics.cpp), on by default
extern bool audio_occlusion;   // sounds behind walls quieter and dull (src/bond/audio_occlusion.cpp), on by default
extern bool weapon_sway;       // the gun trails the view's turns (src/bond/aim.cpp)
extern bool aim_down_sights;  // L raises the gun to the middle and zooms a little, not manual aim (src/bond/aim.cpp)
// the sight tuner (debug, off by default; src/bond/aim.cpp tunes, src/common/sight_tuner.cpp draws): what the game shows
extern bool sight_tuner;
struct SightTunerView {
    bool on = false;      // tuning now: player 1 in play with a gun
    bool scoped = false;  // the held gun has the game's scope: nothing to tune
    char weapon[32] = {};  // its class name, the file's key
    float x = 0, y = 0, z = 0;  // its correction, game units: +x left, +y up, +z farther
    int64_t saved_ms = 0;  // when Enter last saved (steady clock ms)
};
extern SightTunerView sight_view;
void SightTunerUi(double fps);                // GameInfo::ui after the modern menus: the panel and the centre dot
void SightTunerPad(int port, input::Pad& p);  // GameInfo::pad after MenuMouse: the arrows held back from the d-pad
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
void PcStep(int row, int dir);   // row 0 the tab; More Settings opens the host's settings pages (settings_forced)
bool PointerWanted();             // the arrow shows: each image's own test
void PcSettingsFrame(X86Context& ctx, uint8_t* base, uint32_t file);  // src/bond/pc_settings.cpp

// modern menus (src/common/modern_menu.cpp draws them, src/bond/modern_menu_frame.cpp reads the game's page): the
// game's menu pages drawn anew over its background, the game's own texts left out
extern bool modern_menus;
// the game's own menus (modern_menus off): their PC Settings' More Settings shows the host's settings pages over them
// (src/common/settings_page.cpp) until they are left
extern bool settings_forced;
enum ModernKind { kModernButton, kModernChoice, kModernSlider, kModernList };
struct ModernRow {
    uint32_t id = 0;  // the item's place on its page, from 1
    int kind = kModernButton;
    std::string label, value;
    uint32_t slider_value = 0, slider_max = 0;
    bool list = false;
    int list_step = 0;
    bool focused = false, disabled = false;
};
struct ModernPage {
    bool up = false;
    uint32_t page = 0;  // the page's address
    std::string title, help, prompt;
    std::vector<std::string> tabs;  // the pause's tab strip
    int tab = -1;
    std::vector<std::string> texts;
    std::vector<std::string> agents;
    std::vector<ModernRow> rows;
    int focus = -1;
};
struct ModernAction {
    uint32_t id;    // the row to focus, 0 none
    int control;    // then this pressed (input::kA, kB, kLeft, kRight), -1 none
    uint32_t page;  // the page it was meant for
    bool list = false;
    int list_step = 0;
};
void ModernMenuPublish(const ModernPage& page);  // the game's thread, each frame in the menus
bool ModernMenuAction(ModernAction& a);          // the game's thread: the UI's next click
bool ModernMenuActive();                         // modern menus on and a page up
void ModernMenuTestClick(uint32_t row, int control);  // a test's click (AUF_PAD): row from 1 (0 none), control or -1
void ModernMenuSeen();                           // a page of rows has been up (AUF_PAD's clock starts)
bool ModernMenuWasSeen();
void ModernMenuFonts(void* hwnd);                // GameInfo::window
::ImFont* GameMenuFont(const char* name);          // the game's font from the disc (game_font.cpp), null if unreadable
void ModernMenuUi(double fps);                   // GameInfo::ui
void ModernMenuFrame(X86Context& ctx, uint8_t* base, uint32_t file);  // bond_eng.xbe: the menu file read each frame
// LAN play (core/netplay.h): the host's options for every copy (src/common/netplay_rules.cpp, from GameInfo::init), and
// bond_eng.xbe's state hash the copies compare (src/bond/netplay_hash.cpp, each frame)
void NetplayInit();
void NetplayBond();
// story co-op (src/bond/coop.cpp): a second player in a story mission; its trace each frame (from NetplayBond)
extern bool coop;
void CoopFrame();

}  // namespace auf
