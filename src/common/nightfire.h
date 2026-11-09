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
extern uint32_t controller_scheme;  // original on-foot layout, 0 NightFire through 7 Classic Bond
extern bool rounded_font;  // the fonts drawn from Sour Gummy Black, not the game's letters (src/common/fonts.cpp)
// modern menus (src/common/modern_menu.cpp draws them, src/game/modern_menu_frame.cpp reads the game's page): the
// game's menu pages drawn anew over its background movie
extern bool modern_menus;
enum ModernKind { kModernButton, kModernChoice, kModernSlider, kModernList };
struct ModernRow {
    uint32_t id = 0;
    int kind = kModernButton;
    std::string label, value;
    uint32_t slider_value = 0, slider_max = 0;
    bool list = false;  // one of a list's shown entries: list_step from the list's current one
    int list_step = 0;
    bool focused = false, disabled = false;
};
struct ModernPage {
    bool up = false;
    uint32_t page = 0;  // the page's id
    std::string title, help, prompt;  // prompt: the page's button line ("~A Select  ~B Back")
    std::vector<std::string> tabs;     // a row of tabs (the pause screen's: Mission, Objectives ...), left / right moves
    int tab = -1;                      // the current one
    std::vector<std::string> texts;    // a page of text (objectives, a score): its lines, top down
    std::vector<std::string> agents;   // the split screen's Agent 1 to 4 panels (Join Game): their names
    std::vector<ModernRow> rows;
    int focus = -1;
};
struct ModernAction {
    uint32_t id;    // the row to focus, 0 none
    int control;    // then this pressed (input::kA, kB, kLeft, kRight), -1 none
    uint32_t page;  // the page it was meant for
    bool list = false;  // a list's entry: the list moved list_step first (down presses, up when negative)
    int list_step = 0;
};
void ModernMenuPublish(const ModernPage& page);  // the game's thread, each frame in the menus
bool ModernMenuAction(ModernAction& a);          // the game's thread: the UI's next click
void ModernMenuPress(int control);               // a press for the polled pad
bool ModernMenuActive();                         // modern menus on and a page up
bool ModernHidesClassic();                       // default.xbe: the game's own widgets not drawn (hud.cpp)
void ModernMenuPad(input::Pad& p);               // the presses played (MenuMouse)
bool TitlePicture(uint32_t header);              // modern menus: the title screen and logos blue
void TitleLog(uint32_t header);                  // NF_TITLE_LOG: the menus' big pictures (menu_colors.cpp)
void ModernMenuFonts(void* hwnd);                // GameInfo::window
void ModernMenuUi(double fps);                   // GameInfo::ui
void ModernMenuFrame(X86Context& ctx);           // default.xbe's game thread (Driving.xbe: none)
void ModernListSeen(uint32_t widget, uint32_t table);  // a table list drawn (0x7F680): its entries readable
extern bool skip_paris;  // a new game's START goes to the menus, not into Paris (src/game/start.cpp)
extern bool hide_hud;  // no HUD, for screenshots (src/game/hud.cpp, src/driving/hud.cpp)
extern bool tight_movement;  // optional firmer on-foot movement, original by default
bool TightMovement();       // effective host rule while networked
void ThrowSpawned(uint32_t entry, uint32_t obj);  // after the round spawner 0x211F0 (src/game/throw.cpp)
void ThrowFrame();                                // player 1's frame
bool CollectingTextures();                        // NF_COLLECT_TEXTURES=1 (src/game/texture_collect.cpp)
void CollectTextures(X86Context& ctx);            // the pads' frame: the level files' textures dumped
bool CanChangeMovement();
bool NetGuest();
bool NetMovement(bool& enabled);  // true when a LAN client follows the host
extern bool foot_ik;         // the characters' feet on the ground on slopes and steps (src/game/foot_ik.cpp)
extern bool coop;            // story co-op: a second player on controller 2, split screen (src/game/coop_test.cpp)
extern bool coop_side;      // story co-op: the views side by side (else one above the other)
extern bool coop_leash;     // story co-op: player 2 brought back beside player 1 when far
extern bool coop_respawn;   // story co-op: player 2 respawns beside player 1 (else dies as player 1 would)
extern bool coop_friendly_fire;  // story co-op: the players' shots hurt each other (src/game/hit_feedback.cpp)
int CoopSplit();             // 0 one view, 1 two one above the other, 2 side by side (in a level now)
void CoopHud(uint32_t actor, bool after);  // a player's HUD in its own view (src/game/coop.cpp; around 0xB6190)
void CoopFrame(X86Context& ctx);           // co-op's frame: the guns' views, player 2's respawn and leash
void CoopNpc(uint32_t obj);  // an NPC's think (NF_COOP_ANIMS=1: the animations its body plays, logged)
void CoopWeaponCheck(X86Context& ctx);
void InjectMpAnims(X86Context& ctx);  // a story level: the multiplayer's player animations put in (src/game/mp_anims.cpp)
bool CoopMpAnims();
void CoopHits(uint32_t obj);          // a player's hit list (after 0xAB450): the other co-op player's rounds                   // this level has them  // once a level: whether each player's body can show its weapon (logged)
extern bool view_motion;     // subtle weapon sway and head bob (src/game/sway.cpp)
void GunSway(int player, uint32_t gun, float keep);
void WeaponKeys(X86Context& ctx, uint32_t obj);  // 1-9 pick a weapon (src/game/weapon_keys.cpp)
extern bool aim_down_sights;  // L raises the gun and zooms in, not the game's manual aim (src/game/aim.cpp)
bool ViewTurn(float& yaw, float& pitch, float& tan_half_fov);  // GameInfo::view_turn (src/game/mouse.cpp)
extern uint32_t draw_distance;  // how far the fog and the far plane reach, percent of the game's (src/game/distance.cpp)
void FarPlane();                // the culling far plane moved out by it (after 0xDB5C0)
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
void BloodHit(X86Context& ctx, uint32_t victim, uint32_t hit);
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

// PC extras (src/common/pc_extras.cpp, both exes): the screenshot key and its note, the performance overlay; the keys'
// Windows virtual key codes (0 none), settings
extern bool perf_overlay;        // the corner panel: frames a second, frame time graph, GPU time, render size
extern uint32_t screenshot_key;  // F12: a PNG of the frame in Screenshots beside the exe
extern uint32_t photo_key;       // F10: photo mode (default.xbe)
extern uint32_t restart_key;     // the mission restarted at once (default.xbe), none by default
extern uint32_t ads_zoom;        // aim down sights' zoom, percent (130: the view's angle divided by 1.3; src/game/aim.cpp)
std::string KeyName(uint32_t vk);  // "F12", "Mouse Left"; "-" for none
void TakeScreenshot();             // the next frame, as shown
void GameUi(double fps);           // GameInfo::ui: the modern menus, then the above
// photo mode (src/game/photo_mode.cpp; Driving.xbe's do nothing)
bool PhotoActive();
bool PhotoPad(input::Pad& p);      // the pad hook: true when the pad was kept from the game
void PhotoEnter(X86Context& ctx);  // the pause screen's PHOTO MODE
void PhotoFrame(X86Context& ctx);  // the game's thread, each frame
void PhotoBeforePass(X86Context& ctx);            // around the frame's 3D and 2D pass (0xDAC20)
void PhotoAfterPass();
void PhotoUi();                    // its panel (GameInfo::ui)
// the quick restart key and Discord's presence (src/game/quick_keys.cpp), the game's thread each frame
void QuickFrame(X86Context& ctx);
void QueuePress(int control);           // a press of ours on controller 1 (Driving.xbe's do nothing)
void QueuedPresses(input::Pad& p);      // MenuMouse: them played
bool RestartChosen(X86Context& ctx, uint32_t list);  // A on the pause screen's RESTART: its question answered yes at once
const char* MissionName(uint32_t level);  // src/common/achievement_set.cpp: "The Exchange", null for none
// an environment variable of this process, which a relaunch (kernel::Relaunch) hands on; value null removes it
// (src/common/env.cpp)
void SetEnv(const char* name, const char* value);

}  // namespace nf
