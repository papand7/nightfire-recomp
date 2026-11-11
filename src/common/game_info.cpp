#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <toml++/toml.hpp>
#endif

#include "app/app.h"
#include "common/auf.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/state.h"

namespace auf {
bool skip_intro = false;
bool uncapped = true;
bool auto_aim = false;
bool hit_flash = true;
bool aim_down_sights = true;
bool weapon_sway = true;
bool ragdolls = false;
bool photo_mode = true;
uint32_t fov_percent = 100;

float FovScale() {
    uint32_t pct = std::clamp<uint32_t>(fov_percent, 50, 150);
    static uint32_t said = 100;
    if (said != pct) {
        said = pct;
        LOGR("auf: field of view %u%% of the game's", pct);
    }
    return float(pct) / 100;
}

float FrameWidth() {
    static const float forced = [] {  // AUF_WIDE=k headless (no screen)
        const char* e = std::getenv("AUF_WIDE");
        return e && *e ? float(std::atof(e)) : 0.0f;
    }();
    float k = forced > 0 ? forced : app::FrameAspect() / (4.0f / 3.0f);
    static float said = 0;
    if (k != said) {
        said = k;
        LOGR("auf: the 3D views are %.3f times the game's 4:3", k);
    }
    return k;
}
}  // namespace auf

namespace {
const bool kDrv = auf::kImage == auf::kDriving;

// Hit flash in the driving levels (auf::hit_flash): DRIVING.XBE adds a full screen quad to the frame when Bond is hit, through a
// vertex program with no texture and additive blending (ONE, ONE). Headless on mis02a's gun its frames were the paler ones
// and nothing else drew that way. With the setting off such a draw is left out (on foot it is the HUD's fill, src/bond/hooks.cpp).
void DrivingFlashFilter(const gpu::State& s, gpu::DrawAdjust& adjust) {
    if (auf::hit_flash || !(s.vs_handle & 1) || s.textures[0]) return;
    if (!s.simple[gpu::kMBlendEnable >> 2] || s.simple[gpu::kMSrcBlend >> 2] != 1 || s.simple[gpu::kMDestBlend >> 2] != 1) return;
    LOG_FIRST(1, "auf: the hit flash left out (hit_flash off)");
    adjust.skip = true;
}

// after the settings load: the harness's environment overrides them; each setting in effect is logged
void Init() {
    // every achievement's condition parses (a typo would leave it never met)
    for (const achievements::Def& d : app::kGame.achievements)
        if (std::string e = achievements::Check(d.condition); !e.empty()) LOGE("auf: achievement %s: %s", d.id, e.c_str());
    if (const char* e = std::getenv("AUF_SKIP_INTRO"); e && *e) auf::skip_intro = *e == '1';
    if (const char* e = std::getenv("AUF_AUTO_AIM"); e && *e) auf::auto_aim = *e == '1';
    if (const char* e = std::getenv("AUF_HIT_FLASH"); e && *e) auf::hit_flash = *e == '1';
    if (const char* e = std::getenv("AUF_ADS"); e && *e) auf::aim_down_sights = *e == '1';
    if (const char* e = std::getenv("AUF_FOV"); e && *e) auf::fov_percent = uint32_t(std::atoi(e));
    if (auf::skip_intro) LOGR("auf: the logo movies are skipped (skip_intro)");
    if (kDrv) gpu::g_draw_filter = DrivingFlashFilter;  // here, not a static initializer: those never ran in the Windows exe
}

// keyboard and mouse (Windows virtual key codes: this file builds for the headless harness too)
constexpr uint16_t kMouseLeft = 0x01, kMouseRight = 0x02, kMouseMiddle = 0x04, kTab = 0x09, kEnter = 0x0D,
                   kEscape = 0x1B, kSpace = 0x20, kArrowLeft = 0x25, kArrowUp = 0x26, kArrowRight = 0x27, kArrowDown = 0x28,
                   kLeftShift = 0xA0, kLeftCtrl = 0xA2, kBackspace = 0x08;
// on foot, as the game's Controller Setup page lists it: R fire, L manual aim, Y jump, White cycle weapon, Black alt.
// fire, B use gadget, X action, reload and open, A crouch, the d-pad gadget and weapon, Back center view; the mouse looks
std::array<uint16_t, 2> ShooterKeys(int c) {
    using namespace input;
    switch (c) {
        case kLSUp: return {'W', 0};
        case kLSDown: return {'S', 0};
        case kRSLeft: return {'A', 0};       // strafe: the game's Setup 1 has the left stick's x turn, the right stick's strafe,
        case kRSRight: return {'D', 0};      // and the mouse does the turning
        case kRT: return {kMouseLeft, 0};    // fire
        case kLT: return {kMouseRight, 0};   // manual aim
        case kA: return {'E', 'R'};          // action, reload, open (Enter selects in the menus: menu_mouse.cpp); not as NightFire's A
        case kX: return {'C', kLeftCtrl};    // crouch
        case kY: return {kSpace, 0};         // jump
        case kB: return {'G', kBackspace};   // use gadget; back in the menus
        case kWhite: return {'Q', 0};        // cycle weapon
        case kBlack: return {'F', kMouseMiddle};  // alt. fire
        case kUp: return {kArrowUp, '3'};    // the d-pad: up and down the weapon, left and right the gadget (tried in play: right
        case kDown: return {kArrowDown, '1'};  // Q-Laser, Q-Claw; down the gun); 1 the next weapon, 3 the one before, 2 the next
        case kLeft: return {kArrowLeft, 0};    // gadget, the wheel the weapon (src/common/menu_mouse.cpp)
        case kRight: return {kArrowRight, '2'};
        case kBack: return {kTab, 'V'};      // center view
        case kStart: return {kEscape, 'P'};
        default: return {0, 0};
    }
}
// driving, the layout of NightFire's later version of this engine: R gas, L brake, the left stick steers, A fire, B
// handbrake, Y camera, X gadget, Black and White the weapon (the wheel too), the right stick's click looks back. On a mounted
// gun the mouse plays the right stick, its buttons fire and zoom and R reloads (src/common/menu_mouse.cpp)
std::array<uint16_t, 2> DrivingKeys(int c) {
    using namespace input;
    switch (c) {
        case kRT: return {'W', 0};
        case kLT: return {'S', 0};
        case kLSLeft: return {'A', kArrowLeft};
        case kLSRight: return {'D', kArrowRight};
        case kLSUp: return {kArrowUp, kLeftShift};
        case kLSDown: return {kArrowDown, kLeftCtrl};
        case kA: return {kMouseLeft, kEnter};
        case kB: return {kSpace, kBackspace};
        case kX: return {kMouseRight, 'E'};
        case kY: return {'C', 0};
        case kBlack: return {'Q', 0};
        case kWhite: return {'Z', 0};
        case kRThumb: return {'V', kMouseMiddle};
        case kLeft: return {'1', 0};
        case kRight: return {'2', 0};
        case kUp: return {'3', 0};
        case kDown: return {'4', 0};
        case kStart: return {kEscape, 'P'};
        case kBack: return {kTab, 0};
        default: return {0, 0};
    }
}
std::array<uint16_t, 2> Keys(int c) { return kDrv ? DrivingKeys(c) : ShooterKeys(c); }

#ifdef _WIN32  // the settings file (the headless harness has none)
// One settings file for the exes, each with its own keys: the runtime reads and writes [keys] as this exe's (only
// what differs from its defaults); DRIVING.XBE's are kept as [driving_keys] instead, and each exe writes the other's
// table back as it read it (EoN's way)
constexpr const char* kOtherKeys[2] = {"driving_keys", "keys"};  // by kDriving: the table this exe keeps unread
toml::table g_other_keys;

void LoadKeys(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    const toml::table* other = t[kOtherKeys[kDrv]].as_table();
    g_other_keys = other ? *other : toml::table{};
    if (!kDrv) return;
    // the runtime took [keys], the game's, as these: the driving defaults and [driving_keys] instead
    app::Settings& s = app::Get();
    for (int c = 0; c < input::kControls && c < int(s.keys.size()); c++) s.keys[c] = DrivingKeys(c);
    if (const toml::table* mine = t["driving_keys"].as_table())
        for (auto& [name, v] : *mine) {
            int c = input::ControlByName(std::string(name.str()).c_str());
            const toml::array* a = v.as_array();
            if (c < 0 || c >= int(s.keys.size()) || !a) continue;
            s.keys[c] = {0, 0};
            for (size_t i = 0; i < 2 && i < a->size(); i++) s.keys[c][i] = app::KeyFromName((*a)[i].value_or(std::string()));
        }
}

void SaveKeys(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    if (kDrv) {
        // the runtime wrote this exe's as [keys]
        if (toml::table* mine = t["keys"].as_table()) {
            toml::table copy = *mine;
            t.erase("keys");
            t.insert("driving_keys", copy);
        }
    }
    if (!g_other_keys.empty()) t.insert_or_assign(kOtherKeys[kDrv], g_other_keys);
}
#else
constexpr void (*LoadKeys)(const void*) = nullptr;
constexpr void (*SaveKeys)(void*) = nullptr;
#endif
}  // namespace

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"007: Agent Under Fire",    // window and splash
    .short_title = L"Agent Under Fire",   // message boxes
    .name = L"AgentUnderFire",            // window classes, crash dumps, handover event
    .log = "auf",                         // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    // default.xbe launches the language's xbe; the runtime matches names lower case
    .images = {{"default.xbe", "AgentUnderFire"},
               {"bond_eng.xbe", "AgentUnderFireBond"},
               {"bond.xbe", "AgentUnderFireBond"},  // the USA disc's
               {"bond_fre.xbe", "AgentUnderFireBondFre"},
               {"bond_ger.xbe", "AgentUnderFireBondGer"},
               {"driving.xbe", "AgentUnderFireDriving"}},
    .options = {{"skip_intro", &auf::skip_intro, nullptr, "Skip intro movies", "Starts without the EA, MGM and logo movies"},
                {"auto_aim", &auf::auto_aim, nullptr, "Auto-aim", "The game's aim assist, on foot and on mounted guns. Off by default"},
                {"aim_down_sights", &auf::aim_down_sights, nullptr, "Aim down sights",
                 "The left trigger (right mouse) raises the gun to the middle and zooms in a little, instead of the game's manual aim. Scopes stay as they are"},
                {"photo_mode", &auf::photo_mode, nullptr, "Photo mode (F10)", "F10 stops the game and frees the camera: mouse to look, W A S D to move, Space and C up and down, Shift faster, Ctrl slower; F12 takes the picture, F10 again goes back"},
                {"ragdolls", &auf::ragdolls, nullptr, "Ragdolls", "Enemies go limp and fall when they die, in place of the death animations. Only their look; off by default"},
                {"weapon_sway", &auf::weapon_sway, nullptr, "Weapon sway", "The gun trails your turns a little and settles back"},
                {"hit_flash", &auf::hit_flash, nullptr, "Hit flash", "The white flash on screen when Bond is hit. On by default; off for those it troubles"},
                {"uncapped", &auf::uncapped, nullptr, "High frame rate",
                 "Paced by the frame rate cap instead of the console's 30 on foot; driving at 60 steps a second instead of 50 at the same speed, with frames in between above that. Vertical sync off gets the most. Takes effect at the next start"},
                {"fov_percent", nullptr, &auf::fov_percent, "Field of view (%)",
                 "50 to 150; 100 is the game's. On foot and driving; the scope zooms in proportion"}},
    .load = LoadKeys,                     // DRIVING.XBE's keys apart (DrivingKeys)
    .save = SaveKeys,
    .init = Init,                         // after the settings load, before the window and boot
    .loaded = nullptr,
    .window = nullptr,
    .message = nullptr,
    .ui = nullptr,
    .pad = auf::MenuMouse,                // changes a polled pad: the mouse in the menus
    .rumble = nullptr,
    .hud_aspect = 4.0f / 3.0f,            // the game lays out its 2D for 4:3: kept in the middle of a wider frame
    .squeeze_screen_programs = true,      // DRIVING.XBE draws its HUD and menus through vertex programs
    .sharpen_2d = true,                   // text and HUD pictures enlarged sharp (Settings::sharp_2d turns it off)
    .hit_feedback = true,                 // src/bond/hit_feedback.cpp
    .pal_console = !auf::kUsa,                  // a PAL disc (region 4)
    .mouse_look = true,                   // the mouse looks and aims
    .mouse_direct = true,                 // src/bond/mouse.cpp, src/driving/mouse.cpp
    .languages = auf::kUsa ? std::vector<uint32_t>{1} : std::vector<uint32_t>{1, 3, 4},               // English, German, French: default.xbe launches that language's game
    .achievements = auf::AchievementSet(), // conditions on bond_eng.xbe's memory
    .texture_bound = auf::ButtonPictures,  // the prompts' pictures as the keys (src/common/buttons.cpp)
    .step_interpolation = [] { return auf::kImage == auf::kDriving && auf::uncapped; },  // on foot draws and moves by the clock
    .step_ahead = [] { return auf::kImage == auf::kDriving && auf::OnGun() && !auf::MenuUp(); },
    .merge_draws = auf::kImage != auf::kDriving,
    .auto_fills_screen = true,            // the 3D views widen and the 2D stays 4:3: "from the screen's shape" can fill an ultrawide
    .fills_narrow_screens = true,         // and a 16:10 or 3:2 screen (the Steam Deck): its 4:3 mode widened, the 2D 4:3 in the middle
    .default_keys = Keys,                 // a shooter's keys on foot, a driving layout in DRIVING.XBE
};
