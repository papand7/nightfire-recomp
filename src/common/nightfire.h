#pragma once
// NightFire's own settings and the hooks the two executables share (default.xbe: title, menus and the on-foot
// levels; Driving.xbe: EA's front end and the driving levels)
#include <cstdint>

#include <string>
#include <vector>

#include "core/achievements.h"
#include "core/input.h"

struct X86Context;

namespace nf {
extern const bool kDriving;  // Driving.xbe's exe (src/driving/hooks.cpp), default.xbe's false (src/game/hooks.cpp)
extern bool skip_intro;
extern bool rounded_font;  // the fonts drawn from Sour Gummy Black, not the game's letters (src/common/fonts.cpp)
extern bool skip_paris;  // a new game's START goes to the menus, not into Paris (src/game/start.cpp)
extern bool hide_hud;  // no HUD, for screenshots (src/game/hud.cpp, src/driving/hud.cpp)
extern bool view_motion;     // subtle weapon sway and head bob (src/game/sway.cpp)
void GunSway(int player, uint32_t gun, float keep);
void WeaponKeys(X86Context& ctx, uint32_t obj);  // 1-9 pick a weapon (src/game/weapon_keys.cpp)
extern bool aim_down_sights;  // L raises the gun and zooms in, not the game's manual aim (src/game/aim.cpp)
extern uint32_t fov_percent;  // the 3D views' field of view, percent of the game's (src/game/view.cpp, src/driving/view.cpp)
float Ultrawide();            // how much wider than 16:9 the frame is shown, 1 on a 16:9 or 4:3 screen
float FovScale();             // fov_percent as a factor, 0.5 to 1.5
// a menu has the controller (each exe's hooks.cpp): the mouse then plays A, B and the d-pad (MenuMouse)
bool MenuUp();
bool MenuMovie(uint32_t header);  // menu_colors: the background movies blue (src/common/menu_colors.cpp)
// the menu up's widgets as its menu file lays them out (640 x 480, the console's 2D) and the one focused, for the
// mouse (default.xbe: src/game/pc_settings_item.cpp; Driving.xbe's EA menus have none: false)
struct MenuWidget {
    uint32_t id, type;
    int16_t x, y, w, h;
    uint32_t entries;  // a list's text entries in the file (the pause screen's choices), 0 for others
};
bool MenuGeometry(uint32_t& page, std::vector<MenuWidget>& widgets, uint32_t& focused);
void MenuCursor(int size, std::vector<uint32_t>& bgra, int& hot_x, int& hot_y);  // GameInfo::menu_cursor
std::vector<achievements::Def> AchievementSet();  // src/common/achievement_set.cpp
void MenuMouse(int port, input::Pad& p);
void MouseAsStick();
float SprintFactor(int player);  // default.xbe: Shift held, a walk's speed times it (src/game/controls.cpp)
void AimDownSights(uint32_t count);  // default.xbe: after its pad reader (src/game/aim.cpp)
bool AimingDownSights();  // player 1, or the gun still on its way back
// LAN play from the menus (src/game/lan_play.cpp): hosts found on this network, hosting, joining, leaving
struct LanServer {
    std::string label;  // "PC - map, players"
    uint32_t ip;        // network order
    uint16_t port;
};
void NetSearch(bool on);
std::vector<LanServer> NetServers();
void NetHost();
void NetJoin(uint32_t ip, uint16_t port);
void NetStop();
bool NetOn();
void LanFrame(X86Context& ctx);  // default.xbe: each frame, this copy's players for LAN play (src/game/lan_play.cpp)
bool LanPuppet(uint32_t obj);
int LanDamage(uint32_t pair, uint32_t hit);  // before 0x3D900: 0 the game's, 1 refused
void LanHitMark(int player, bool head);      // src/game/hit_feedback.cpp  // a character another copy's player drives (src/game/lan_play.cpp)
// LAN play's check that the copies compute the same game (core/netplay.h): default.xbe's players (src/game/netplay.cpp)
uint64_t NetStateHash();
bool PlayerFree();  // default.xbe: Bond under the player's own control (not a keypad, a missile, a cutscene)
bool Zoomed();
void ScopeZoom(int notches);  // default.xbe: the wheel's notches through a scope, up nearer (src/game/mouse.cpp)  // default.xbe: player 1 looking through a scope or sight, zoomed in (src/game/mouse.cpp)
int GunStyle();
bool ButtonPictures(uint32_t header, uint32_t palette);  // GameInfo::texture_bound (src/common/buttons.cpp)
void RefreshButtons();  // each poll
void FontPicture(uint32_t header);  // GameInfo::texture_bound (src/common/fonts.cpp)
bool MenuKeys();  // the menu up shows Enter and Backspace for A and B (not a level's pause, whose controls page
                  // names the keys played)       // Driving.xbe on a mounted gun: its controller style (0-7); -1 in the car, on foot, in a menu  // default.xbe: after its pad reader (src/game/mouse.cpp)  // GameInfo::pad  // no EA and MGM logo movies at boot (HideFile)
// an NPC's or bot's think ran for this character object (lan_play.cpp's 0x31530 hook): it leaves footprints in the
// snow too (lights.cpp)
void NoteWalker(uint32_t obj);

}  // namespace nf
