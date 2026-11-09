// Graphics, a page of the game's own menus (opened from Settings, in the front end and from the pause screen), and
// its three style pages (Enhanced, Toon and Retro options). They are built as Settings is, from AV Options'
// records (src/game/pc_settings_page.cpp, nf::BuildPage), so their text, choices, focus, sounds and prompts are the
// game's own. Changes apply live; A and B return. Actions which restart network play still require A.
//
// Every row drives a value the renderers use: the quality preset (gpu::ApplyQuality's set: anisotropic filtering,
// FXAA or SMAA, ambient occlusion, bloom), reflections, the shader style and its options (gpu/style.h, drawn by the Vulkan renderer),
// sharpening and render scale (app::Settings, saved in settings.toml); the style's values in
// graphics.toml. What only Vulkan draws reads "Needs Vulkan" under the Direct3D renderers. Changing a value the
// quality preset sets makes it Custom; choosing a preset sets them all; the style is apart from the preset.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <cstdlib>
#include <cstring>
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
#include "core/achievements.h"
#include "core/input.h"
#include "core/log.h"
#include "game/menu_page.h"
#include "gpu/style.h"
#include "core/netplay.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace nf {
extern bool modern_buttons;  // src/common/buttons.cpp
}

namespace {
using kernel::R32;
using nf::MenuRow;

// the front end's file takes three pages more than its own, not four (a fourth crashed the menu code on its first
// page change): Enhanced, Toon and Retro options and Settings' More Settings share one page, their rows on the
// same lines, the others hidden, its title rewritten in place for the set shown (SetSharedTitle)
constexpr uint32_t kGfxPage = 0x4000FC20, kStylePage = 0x4000FC30;
constexpr uint32_t kEnhPage = 1, kToonPage = 2, kRetroPage = 3, kMorePage = 4, kNetPage = 5,
                   kEffectsPage = 6, kKeysPage = 7, kKeys2Page = 8, kCoopPage = 9,
                   kWheelPage = 10, kWheel2Page = 11, kWheel3Page = 12, kMore2Page = 13, kAchPage = 14,
                   kAch2Page = 15;  // the shared page's rows
// the Graphics page's rows
constexpr uint32_t kQuality = 0x1000FC21, kStyle = 0x1000FC22, kStyleOptions = 0x1000FC23, kFilter = 0x1000FC24,
                   kAo = 0x1000FC25, kBloom = 0x1000FC26, kAa = 0x1000FC27, kSharpen = 0x1000FC28,
                   kScale = 0x1000FC29, kPack = 0x1000FC2B, kDefaults = 0x1000FC2C,
                   kReflect = 0x1000FC2D, kParticles = 0x1000FC2E, kMaterials = 0x1000FC2F,
                   kLighting = 0x1000FC30, kEffects = 0x1000FC38, kRayTracing = 0x1000FC39,
                   kDof = 0x1000FC3A, kBlood = 0x1000FC3B, kHdr = 0x1000FC3C, kToneHdr = 0x1000FC3D,
                   kMotion2 = 0x1000FC3E, kSmoothTurn = 0x1000FC3F, kContact = 0x1000FC40, kFloorRefl = 0x1000FC5F;
// the upscaler's rows (gpu/upscale.h: FSR 2; DLSS where it runs, else shown as not available)
constexpr uint32_t kUpscaler = 0x1000FC2A, kUpQuality = 0x1000FC47, kUpscaleSharp = 0x1000FC48;
// the style pages' rows
constexpr uint32_t kExposure = 0x1000FC31, kTonemap = 0x1000FC32, kContrast = 0x1000FC33, kSaturation = 0x1000FC34,
                   kVignette = 0x1000FC35, kGrain = 0x1000FC36, kSceneSharpen = 0x1000FC37;
constexpr uint32_t kBands = 0x1000FC41, kOutline = 0x1000FC42, kOutlineColor = 0x1000FC43, kEdges = 0x1000FC44,
                   kHalftone = 0x1000FC45, kToonSaturation = 0x1000FC46;
constexpr uint32_t kPixel = 0x1000FC51, kDepth = 0x1000FC52, kDither = 0x1000FC53, kRetroFilter = 0x1000FC54,
                   kSnap = 0x1000FC55, kTint = 0x1000FC56;
// More Settings' rows
constexpr uint32_t kFps = 0x1000FC61, kPauseBg = 0x1000FC62, kHideHud = 0x1000FC63, kButtonPics = 0x1000FC64,
                   kStartMenus = 0x1000FC65, kKeyboard = 0x1000FC66, kMouseLook = 0x1000FC67, kLanguage = 0x1000FC68,
                   kKeys = 0x1000FC69, kMenuColors = 0x1000FC6A, kAds = 0x1000FC6B, kMotion = 0x1000FC6C, kCoop = 0x1000FC6D,
                   kWheel = 0x1000FC6E;
// More Settings' second set: aiming, Discord, resetting
constexpr uint32_t kAchievements = 0x1000FC6F, kMoreNext = 0x1000FC70, kAdsZoom = 0x1000FD31, kDiscord = 0x1000FD32,
                   kReset = 0x1000FD33, kMoreBack = 0x1000FD34, kMovement = 0x1000FD35, kGameplayBlood = 0x1000FD36, kShafts = 0x1000FD37;

const std::vector<MenuRow> kGfxRows = {
    {kQuality, nf::kMenuChoice, "Graphics Quality", "Low to Ultra set the rows below at once; changing one makes it Custom."},
    {kStyle, nf::kMenuChoice, "Shader Style", "The picture's look: the console's own, Enhanced, Toon, Retro, Noir or a mod's."},
    {kStyleOptions, nf::kMenuButton, "Style Options", "The chosen style's own settings."},
    {kEffects, nf::kMenuButton, "Effects", "Ambient occlusion, bloom, reflections, particles, materials, lighting, ray tracing, focus."},
    {kFilter, nf::kMenuChoice, "Texture Filtering", "How sharp textures stay at an angle: as the game draws them, or anisotropic."},
    {kAa, nf::kMenuChoice, "Anti-Aliasing", "SMAA smooths the scene's edges, the HUD stays sharp; FXAA the whole picture."},
    {kSharpen, nf::kMenuChoice, "Sharpening", "Contrast adaptive sharpening of the finished picture."},
    {kScale, nf::kMenuChoice, "Render Scale", "The 3D picture's size against the screen's: lower is faster, higher smoother."},
    {kPack, nf::kMenuChoice, "Texture Packs", "Pictures in mods/textures drawn in place of the game's textures."},
    {kHdr, nf::kMenuChoice, "HDR", "Brighter highlights on an HDR screen (Windows' HDR on). HUD and menus stay at SDR white."},
    {kUpscaler, nf::kMenuChoice, "Upscaler", "FSR 2 or NVIDIA DLSS (RTX graphics cards): the 3D scene drawn smaller and rebuilt over frames, the HUD sharp."},
    {kUpQuality, nf::kMenuChoice, "Upscale Quality", "Native AA (DLAA with DLSS): full size, the smoothest edges. Quality 1.5x, Balanced 1.7x, Performance 2x, Ultra Performance 3x: faster."},
    {kUpscaleSharp, nf::kMenuChoice, "Upscale Sharpness", "FSR 2's own sharpening of the rebuilt scene (DLSS has none)."},
    {kDefaults, nf::kMenuButton, "Restore Defaults", "Every row on this page and the style options as they first were."},
};
// Effects (Graphics' Effects button): what only Vulkan draws, apart from the style
const std::vector<MenuRow> kEffectsRows = {
    {kAo, nf::kMenuChoice, "Ambient Occlusion", "Soft shade in corners and creases, from the scene's depth.", 0},
    {kBloom, nf::kMenuChoice, "Bloom", "A glow around lights and bright surfaces.", 1},
    {kReflect, nf::kMenuChoice, "Reflections", "Glass mirrors what is on screen (screen-space reflections).", 2},
    {kParticles, nf::kMenuChoice, "Particles", "Soft: effects fade into walls. Lights: flashes and fire light the room.", 3},
    {kMaterials, nf::kMenuChoice, "Materials", "Detail: grass, snow, rock up close. PBR: the HD pack's maps (next start).", 4},
    {kLighting, nf::kMenuChoice, "Lighting", "Fog: depth in the distance. Bounce: rooms take on their lights' and walls' colour.", 5},
    {kRayTracing, nf::kMenuChoice, "Ray Tracing", "Shade: in place of AO. Shadows: from effect lights. Full: the level lit by its lamps and a sun. + GI: light bounced off walls. RTX-class GPU.", 6},
    {kDof, nf::kMenuChoice, "Depth of Field", "Distance: far scenery soft. Focus: what you aim at stays sharp.", 7},
    {kShafts, nf::kMenuChoice, "Light Shafts", "Screen-space light beams: Off or strength 0.1 to 3. Works without fog and applies immediately. Blood is in Gameplay & More.", 8},
    {kToneHdr, nf::kMenuChoice, "Tone Mapping", "AgX or ACES: bright light rolls off instead of clipping to white. Auto: the exposure adapts.", 9},
    {kMotion2, nf::kMenuChoice, "Motion Blur", "A little blur as you turn and move. The gun and HUD stay sharp.", 10},
    {kSmoothTurn, nf::kMenuChoice, "Smooth Turning", "A frame in between, turned by the mouse: 120 Hz turning on a 120 Hz+ screen.", 11},
    {kContact, nf::kMenuChoice, "Contact Shadows", "Short, crisp shadows under props, feet and ledges (without ray traced lighting).", 12},
    {kFloorRefl, nf::kMenuChoice, "Water / Floors", "Water: ripples, reflections, depth tint and foam. Ice and polished floors need Reflections on.", 13},
};
const std::vector<MenuRow> kEnhRows = {
    {kExposure, nf::kMenuChoice, "Exposure", "How bright the scene is drawn.", 0},
    {kTonemap, nf::kMenuChoice, "Tone Mapping", "Filmic keeps highlights from clipping; Reinhard is softer.", 1},
    {kContrast, nf::kMenuChoice, "Contrast", "The difference between the scene's darks and lights.", 2},
    {kSaturation, nf::kMenuChoice, "Colour", "How strong the scene's colours are.", 3},
    {kVignette, nf::kMenuChoice, "Vignette", "Darkens the picture's corners.", 4},
    {kGrain, nf::kMenuChoice, "Film Grain", "A film's grain over the scene.", 5},
    {kSceneSharpen, nf::kMenuChoice, "Detail", "Sharpens the scene's textures and edges.", 6},
};
const std::vector<MenuRow> kToonRows = {
    {kBands, nf::kMenuChoice, "Lighting Bands", "How many flat steps of light and shade.", 0},
    {kOutline, nf::kMenuChoice, "Outlines", "Ink lines around characters, vehicles and the scenery.", 1},
    {kOutlineColor, nf::kMenuChoice, "Outline Colour", "The ink's colour.", 2},
    {kEdges, nf::kMenuChoice, "Inner Lines", "Lines inside shapes too, where their colour changes sharply.", 3},
    {kHalftone, nf::kMenuChoice, "Halftone", "Comic print dots in the shade.", 4},
    {kToonSaturation, nf::kMenuChoice, "Colour", "How strong the colours are.", 5},
};
const std::vector<MenuRow> kRetroRows = {
    {kPixel, nf::kMenuChoice, "Pixel Size", "The picture drawn in blocks of this many pixels.", 0},
    {kDepth, nf::kMenuChoice, "Colour Depth", "Bits of colour each channel keeps.", 1},
    {kDither, nf::kMenuChoice, "Dithering", "A fine pattern that hides the colour steps.", 2},
    {kRetroFilter, nf::kMenuChoice, "Texture Filtering", "Nearest shows each texel as a square block.", 3},
    {kSnap, nf::kMenuChoice, "Vertex Snapping", "Corners jump between screen pixels, as on early consoles.", 4},
    {kTint, nf::kMenuChoice, "Colour Grading", "A warm, faded old screen.", 5},
};
const std::vector<MenuRow> kMoreRows = {
    {kFps, nf::kMenuChoice, "Performance", "FPS: frames a second in the corner. Full: a panel with the frame time graph, GPU time and render size.", 0},
    {kPauseBg, nf::kMenuChoice, "Pause in Background", "The game stands still and silent while its window is not in front.", 1},
    {kHideHud, nf::kMenuChoice, "Hide HUD", "For screenshots: no ammo, health or radar. The menus stay.", 2},
    {kButtonPics, nf::kMenuChoice, "Button Pictures", "The console's prompts, or today's pad's (LB, RB, Menu, View).", 3},
    {kStartMenus, nf::kMenuChoice, "Start in Menus", "START at the title opens the menus instead of Paris.", 4},
    {kKeyboard, nf::kMenuChoice, "Keyboard and Mouse", "Player 1 with the first controller, Player 1 alone, or off.", 5},
    {kMouseLook, nf::kMenuChoice, "Mouse Look", "The mouse turns and aims on foot.", 6},
    {kLanguage, nf::kMenuChoice, "Language", "The game's language, at the next start.", 7},
    {kKeys, nf::kMenuChoice, "Key Bindings", "The keyboard and mouse keys for each action.", 8},
    {kWheel, nf::kMenuChoice, "Steering Wheel", "A wheel, pedals and shifter for the driving missions: bindings, rotation, force feedback.", 9},
    {kMenuColors, nf::kMenuChoice, "Menu Colours", "The menu backgrounds as the game has them, or blue as on the PC version. Text and Bond stay.", 10},
    {kCoop, nf::kMenuChoice, "Co-op", "A second player in the story missions: split screen, network, friendly fire.", 11},
    {kAchievements, nf::kMenuChoice, "Achievements", "The 19 achievements: which are unlocked, and how far along the medal ones are.", 12},
    {kMoreNext, nf::kMenuChoice, "Gameplay & More", "Tighter movement, blood, aiming, Discord and defaults.", 13},
};
const std::vector<MenuRow> kMore2Rows = {
    {kGameplayBlood, nf::kMenuChoice, "Blood", "Blood at character impacts and red at the view edges when you are hurt. Applies immediately.", 6},
    {kMovement, nf::kMenuChoice, "Tighter Movement", "Quicker starts and direction changes, less floaty jumps with original jump height. Single-player and multiplayer: the host decides for everyone. Network co-op: set before hosting.", 4},
    {kAds, nf::kMenuChoice, "Aim Down Sights", "L (right mouse) raises the gun and zooms in, not the red crosshair. Blur: the background too.", 0},
    {kAdsZoom, nf::kMenuChoice, "Aim Zoom", "How far aiming down the sights zooms in (scopes keep their own).", 1},
    {kMotion, nf::kMenuChoice, "Weapon Sway", "A very light head bob and weapon sway as you move and turn.", 2},
    {kDiscord, nf::kMenuChoice, "Discord", "Your Discord status shows the mission you play (with Discord running on this PC).", 3},
    {kReset, nf::kMenuChoice, "Reset to Defaults", "Graphics and game settings as they first were. Key bindings and codenames are kept.", 5},
    {kMoreBack, nf::kMenuChoice, "First Page", "Performance, HUD, keyboard and mouse, language and more.", 13},
};
// Achievements (More Settings' row): two sets, the missions' twelve and the other seven, each row an achievement, its
// state shown (Unlocked, Locked, or how far along); its help the description, the points and when it was unlocked
constexpr uint32_t kAch0 = 0x1000FD01, kAchMore = 0x1000FD20, kAchBack = 0x1000FD21;
const std::vector<MenuRow>& AchRows(bool second) {
    static std::vector<MenuRow> rows[2];
    if (rows[0].empty()) {
        const auto& defs = app::kGame.achievements;
        for (size_t i = 0; i < defs.size() && i < 24; i++)
            rows[i >= 12].push_back({kAch0 + uint32_t(i), nf::kMenuChoice, defs[i].title, defs[i].description, int(i % 12)});
        rows[0].push_back({kAchMore, nf::kMenuChoice, "Medals and More", "Medals, difficulty and Bond Moves.", 13});
        rows[1].push_back({kAchBack, nf::kMenuChoice, "Missions", "An achievement for each mission completed.", 13});
    }
    return rows[second];
}
bool AchSet(uint32_t set) { return set == kAchPage || set == kAch2Page; }
// Key Bindings (More Settings' row), two sets of rows: each an action on foot, its two keys shown; A waits for the
// next key or mouse button (app::BeginKeyCapture), which becomes its first key and the old first its second; Escape
// clears them. Saved at once, as the settings window's Controls tab does (app::Settings::keys)
struct KeyRow {
    uint32_t id;
    int control;  // input::Control, -1 a button, -2 a key of its own (extra)
    const char* label;
    const char* help;
    int slot;
    uint32_t* extra = nullptr;  // the screenshot, photo mode and restart keys (one key each, nf::KeyName)
};
constexpr uint32_t kKeysMore = 0x1000FCAC, kKeysBack = 0x1000FCBB, kKeysDefaults = 0x1000FCBA;
const KeyRow kKeyRows[] = {
    {0x1000FCA1, input::kLSUp, "Move Forward", "Walk forward.", 0},
    {0x1000FCA2, input::kLSDown, "Move Back", "Walk backward.", 1},
    {0x1000FCA3, input::kLSLeft, "Strafe Left", "Step to the left.", 2},
    {0x1000FCA4, input::kLSRight, "Strafe Right", "Step to the right.", 3},
    {0x1000FCA5, input::kLThumb, "Sprint", "Held while moving forward: run faster (a controller's left stick click).", 4},
    {0x1000FCA6, input::kA, "Crouch", "Held: crouch (a controller's A toggles it).", 5},
    {0x1000FCA7, input::kY, "Jump", "Jump.", 6},
    {0x1000FCA8, input::kRT, "Fire", "Fire the weapon.", 7},
    {0x1000FCA9, input::kLT, "Aim", "Aim down the sights, or the game's manual aim.", 8},
    {0x1000FCAA, input::kWhite, "Alt. Fire", "The weapon's other fire.", 9},
    {0x1000FCAB, input::kX, "Action / Reload", "Use what is in front of you, or reload.", 10},
    {kKeysMore, -1, "More Keys", "Weapons, gadgets, vision, pause, screenshot, photo mode and restart.", 11},
    {0x1000FCB1, input::kB, "Next Weapon", "The next weapon carried.", 0},
    {0x1000FCB2, input::kBlack, "Next Gadget", "The next gadget carried.", 1},
    {0x1000FCB3, input::kBack, "Vision Mode", "The gadget's vision mode.", 2},
    {0x1000FCB4, input::kLeft, "Weapon Back", "The weapon before (the mouse wheel too).", 3},
    {0x1000FCB5, input::kRight, "Weapon On", "The weapon after (the mouse wheel too).", 4},
    {0x1000FCB6, input::kUp, "Gadget / Zoom In", "The gadget before, or zoom in through a scope.", 5},
    {0x1000FCB7, input::kDown, "Gadget / Zoom Out", "The gadget after, or zoom out through a scope.", 6},
    {0x1000FCB8, input::kStart, "Pause", "The pause screen.", 7},
    {0x1000FCBC, -2, "Screenshot", "A picture of the screen in the Screenshots folder beside the game. Escape: no key.", 8, &nf::screenshot_key},
    {0x1000FCBD, -2, "Photo Mode", "Pause and fly the camera for a picture (the pause screen has it too). Escape: no key.", 9, &nf::photo_key},
    {0x1000FCBE, -2, "Restart Mission", "The mission started again at once, without a question. Escape: no key.", 10, &nf::restart_key},
    {kKeysDefaults, -1, "Default Keys", "Every action's keys as they first were.", 12},
    {kKeysBack, -1, "First Keys", "Movement, sprint, crouch, jump and fire.", 13},
};
std::vector<MenuRow> KeySetRows(bool second) {
    std::vector<MenuRow> v;
    bool past = false;
    for (const KeyRow& r : kKeyRows) {
        if (second == past) v.push_back({r.id, nf::kMenuChoice, r.label, r.help, r.slot});
        if (r.id == kKeysMore) past = true;
    }
    return v;
}
const std::vector<MenuRow> kKeysRows = KeySetRows(false), kKeys2Rows = KeySetRows(true);

// Steering Wheel (More Settings' row), three sets of rows: the steering and pedals and their feel, the buttons of the
// driving controls, an H-pattern shifter's gears. A on a binding waits for the control to be turned, pressed or moved
// on any wheel, pedals, shifter or button box (app::WheelDetectStart, as the settings window's Wheel tab did); A again
// unbinds it. The choices are saved as they change (WheelPageFrame), each in app::Settings' wheel_ and ffb_ values
enum WheelKind { kWheelAxis, kWheelButton, kWheelGear, kWheelValue, kWheelGo, kWheelDo };
struct WheelRow {
    uint32_t id;
    uint32_t set;  // kWheelPage, kWheel2Page or kWheel3Page
    WheelKind kind;
    int what;      // axis: 0 steering, 1 throttle, 2 brake, 3 clutch; button: input::Control; gear: 0 R, 1 to 7;
                   // value: kWheelValues' index; go: the set opened; do: 0 the default buttons, 1 no gears
    const char* label;
    const char* help;
    int slot;
};
struct WheelValue {
    const char* text;  // "Off" for 0, else the number and this
    std::vector<uint32_t> steps;
};
// Invert Steering, Wheel Rotation, Full Steering At, Dead Zone, Force Feedback, Road and Impacts
const WheelValue kWheelValues[] = {
    {"", {0, 1}},
    {" Degrees", {180, 240, 270, 360, 450, 540, 720, 900, 1080, 1440, 1800, 2520, 2880}},
    {" Degrees", {90, 120, 180, 240, 270, 300, 360, 450, 540, 720, 900, 1080}},
    {"%", {0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20}},
    {"%", {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100}},
    {"%", {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100}},
};
constexpr const char* kNoWheel = "No wheel, pedals, shifter or button box found. One plugged in is found within two seconds.";
const WheelRow kWheelRows[] = {
    {0x1000FCD1, kWheelPage, kWheelAxis, 0, "Steering", "A, then turn the wheel to the right. A again: unbound.", 0},
    {0x1000FCD2, kWheelPage, kWheelValue, 0, "Invert Steering", "Turning right steers left (set by itself when the steering is bound).", 1},
    {0x1000FCD3, kWheelPage, kWheelValue, 1, "Wheel Rotation", "The wheel's turn lock to lock, as its own software sets it (Logitech G HUB: 900).", 2},
    {0x1000FCD4, kWheelPage, kWheelValue, 2, "Full Steering At", "Degrees of wheel turn for the car's full steering: less turns it sooner (360 suits most).", 3},
    {0x1000FCD5, kWheelPage, kWheelValue, 3, "Dead Zone", "How much of the steering around the centre reads as straight.", 4},
    {0x1000FCD6, kWheelPage, kWheelAxis, 1, "Throttle", "Let the pedal rest, A, then press it all the way. A again: unbound.", 5},
    {0x1000FCD7, kWheelPage, kWheelAxis, 2, "Brake", "Let the pedal rest, A, then press it all the way. A again: unbound.", 6},
    {0x1000FCD8, kWheelPage, kWheelAxis, 3, "Clutch", "Let the pedal rest, A, then press it all the way (the game has no clutch).", 7},
    {0x1000FCD9, kWheelPage, kWheelValue, 4, "Force Feedback", "The wheel's forces overall: centring, the car's grip, kerbs and impacts.", 8},
    {0x1000FCDA, kWheelPage, kWheelValue, 5, "Road and Impacts", "How much of the game's rumble (kerbs, gravel, impacts) shakes the wheel.", 9},
    {0x1000FCDB, kWheelPage, kWheelGo, int(kWheel2Page), "Buttons", "The wheel's buttons for gas, brake, handbrake, weapons, camera and pause.", 10},
    {0x1000FCDC, kWheelPage, kWheelGo, int(kWheel3Page), "Gear Shifter", "An H-pattern shifter's gears.", 11},

    {0x1000FCE1, kWheel2Page, kWheelButton, input::kRT, "Gas", "A button that accelerates, for a wheel without pedals. A again: unbound.", 0},
    {0x1000FCE2, kWheel2Page, kWheelButton, input::kLT, "Brake / Reverse", "A button that brakes and reverses. A again: unbound.", 1},
    {0x1000FCE3, kWheel2Page, kWheelButton, input::kB, "Handbrake", "The handbrake. A again: unbound.", 2},
    {0x1000FCE4, kWheel2Page, kWheelButton, input::kA, "Fire", "Fire the car's weapon. A again: unbound.", 3},
    {0x1000FCE5, kWheel2Page, kWheelButton, input::kX, "Gadget", "Use the car's gadget. A again: unbound.", 4},
    {0x1000FCE6, kWheel2Page, kWheelButton, input::kY, "Camera", "Change the camera. A again: unbound.", 5},
    {0x1000FCE7, kWheel2Page, kWheelButton, input::kBlack, "Next Weapon", "The next weapon. A again: unbound.", 6},
    {0x1000FCE8, kWheel2Page, kWheelButton, input::kWhite, "Weapon Before", "The weapon before. A again: unbound.", 7},
    {0x1000FCE9, kWheel2Page, kWheelButton, input::kRThumb, "Look Back", "Look behind the car. A again: unbound.", 8},
    {0x1000FCEA, kWheel2Page, kWheelButton, input::kStart, "Pause", "The pause screen. A again: unbound.", 9},
    {0x1000FCEB, kWheel2Page, kWheelButton, input::kBack, "Back / View", "The console's Back button. A again: unbound.", 10},
    {0x1000FCEC, kWheel2Page, kWheelDo, 0, "Default Buttons", "A Logitech G920 or G923's buttons. The wheel's hat is the d-pad.", 11},
    {0x1000FCED, kWheel2Page, kWheelGo, int(kWheel3Page), "Gear Shifter", "An H-pattern shifter's gears.", 12},
    {0x1000FCEE, kWheel2Page, kWheelGo, int(kWheelPage), "Steering and Pedals", "Steering, pedals and force feedback.", 13},

    {0x1000FCF1, kWheel3Page, kWheelGear, 0, "Reverse", "A, then put the shifter in reverse. A again: unbound.", 0},
    {0x1000FCF2, kWheel3Page, kWheelGear, 1, "Gear 1", "A, then put the shifter in first. A again: unbound.", 1},
    {0x1000FCF3, kWheel3Page, kWheelGear, 2, "Gear 2", "A, then put the shifter in second. A again: unbound.", 2},
    {0x1000FCF4, kWheel3Page, kWheelGear, 3, "Gear 3", "A, then put the shifter in third. A again: unbound.", 3},
    {0x1000FCF5, kWheel3Page, kWheelGear, 4, "Gear 4", "A, then put the shifter in fourth. A again: unbound.", 4},
    {0x1000FCF6, kWheel3Page, kWheelGear, 5, "Gear 5", "A, then put the shifter in fifth. A again: unbound.", 5},
    {0x1000FCF7, kWheel3Page, kWheelGear, 6, "Gear 6", "A, then put the shifter in sixth. A again: unbound.", 6},
    {0x1000FCF8, kWheel3Page, kWheelGear, 7, "Gear 7", "A, then put the shifter in seventh. A again: unbound.", 7},
    {0x1000FCF9, kWheel3Page, kWheelDo, 1, "No Shifter", "Every gear unbound: the game's own shift buttons change gear.", 8},
    {0x1000FCFA, kWheel3Page, kWheelGo, int(kWheel2Page), "Buttons", "The wheel's buttons for gas, brake, handbrake, weapons, camera and pause.", 9},
    {0x1000FCFB, kWheel3Page, kWheelGo, int(kWheelPage), "Steering and Pedals", "Steering, pedals and force feedback.", 10},
};
std::vector<MenuRow> WheelSetRows(uint32_t set) {
    std::vector<MenuRow> v;
    for (const WheelRow& r : kWheelRows)
        if (r.set == set) v.push_back({r.id, nf::kMenuChoice, r.label, r.help, r.slot});
    return v;
}
const std::vector<MenuRow> kWheelRows1 = WheelSetRows(kWheelPage), kWheelRows2 = WheelSetRows(kWheel2Page),
                           kWheelRows3 = WheelSetRows(kWheel3Page);
bool WheelSet(uint32_t set) { return set == kWheelPage || set == kWheel2Page || set == kWheel3Page; }

// Co-op's rows (single player's Co-op, More Settings' Co-op): how a second player joins the story missions
constexpr uint32_t kCoopMode = 0x1000FCC1, kCoopSplit = 0x1000FCC2, kCoopFF = 0x1000FCC3, kCoopNear = 0x1000FCC4,
                   kCoopRespawn = 0x1000FCC5, kCoopDone = 0x1000FCC6, kCoopAddr = 0x1000FCC7;
const std::vector<MenuRow> kCoopRows = {
    {kCoopMode, nf::kMenuChoice, "Co-op", "Split Screen: player 2 on controller 2. Network: the game restarts linked to another PC. Host: this PC waits (its address below). Join: the games found on this network, or an address.", 0},
    {kCoopAddr, nf::kMenuChoice, "Your Address", "", 1},  // its help made from this PC's addresses (CoopAddrHelp)
    {kCoopSplit, nf::kMenuChoice, "Screen Split", "The two views one above the other, or side by side.", 2},
    {kCoopFF, nf::kMenuChoice, "Friendly Fire", "Your shots hurt each other.", 3},
    {kCoopNear, nf::kMenuChoice, "Keep Together", "Player 2 is brought back beside player 1 when far away or lost.", 4},
    {kCoopRespawn, nf::kMenuChoice, "Player 2 Respawn", "Player 2 comes back beside player 1 after dying; player 1's death ends the mission.", 5},
    {kCoopDone, nf::kMenuButton, "Done", "Keep these and go back.", 6},
};
// Network: Join's rows (Co-op's Keep with Network: Join): the hosts waiting on this network (netplay::FindHosts, the
// list kept up to date each frame), then an address typed as four numbers for a host past this network
constexpr uint32_t kJoinNetPage = 20;  // a set of the shared page's rows
constexpr uint32_t kJoinHost0 = 0x1000FC94, kJoinHosts = 5, kJoinOctet0 = 0x1000FC99, kJoinAddress = 0x1000FC9D;
const std::vector<MenuRow> kJoinRows = {
    {kJoinHost0 + 0, nf::kMenuChoice, "Found Game 1", "A host waiting on this network: A joins it (the game restarts linked to it).", 0},
    {kJoinHost0 + 1, nf::kMenuChoice, "Found Game 2", "A host waiting on this network: A joins it (the game restarts linked to it).", 1},
    {kJoinHost0 + 2, nf::kMenuChoice, "Found Game 3", "A host waiting on this network: A joins it (the game restarts linked to it).", 2},
    {kJoinHost0 + 3, nf::kMenuChoice, "Found Game 4", "A host waiting on this network: A joins it (the game restarts linked to it).", 3},
    {kJoinHost0 + 4, nf::kMenuChoice, "Found Game 5", "A host waiting on this network: A joins it (the game restarts linked to it).", 4},
    {kJoinOctet0 + 0, nf::kMenuChoice, "Address (1st)", "A host's address, its first number (left and right). Over the internet: the host's public address, its port forwarded.", 6},
    {kJoinOctet0 + 1, nf::kMenuChoice, "Address (2nd)", "The address's second number (left and right).", 7},
    {kJoinOctet0 + 2, nf::kMenuChoice, "Address (3rd)", "The address's third number (left and right).", 8},
    {kJoinOctet0 + 3, nf::kMenuChoice, "Address (4th)", "The address's fourth number (left and right).", 9},
    {kJoinAddress, nf::kMenuChoice, "Join This Address", "Join the host at the address above (UDP port 27015 unless the launcher's Host address names another).", 10},
};
// Netplay's rows (the main menu's Netplay item): host, the games found on this network, join the one chosen
constexpr uint32_t kNetHost = 0x1000FC91, kNetServer = 0x1000FC92, kNetJoin = 0x1000FC93;
const std::vector<MenuRow> kNetRows = {
    {kNetHost, nf::kMenuChoice, "Host Game", "Start a game others on this network can join, then choose its options.", 0},
    {kNetServer, nf::kMenuChoice, "Game", "The games found on this network: PC, map, players.", 1},
    {kNetJoin, nf::kMenuChoice, "Join Game", "Join the game chosen above.", 2},
};
constexpr int16_t kFirstY = 88, kStep = 21;  // below the panel's title band (AV's first row is at 86), as Settings'
constexpr int16_t kStyleStep = 21;           // the shared page's twelve rows

// a choice of labelled values: shown at the one nearest the current value
struct Option {
    const char* text;
    float value;
};
using Options = std::vector<Option>;
uint32_t Nearest(const Options& o, float v) {
    uint32_t best = 0;
    for (uint32_t i = 1; i < o.size(); i++)
        if (std::fabs(o[i].value - v) < std::fabs(o[best].value - v)) best = i;
    return best;
}
// the shared page (Style Options, More Settings): fourteen rows of its own, kShared0 + the line, each set's rows drawn
// on them by their slot; the page's widgets stay as few as the menu builder takes (a page of every set's rows, 28,
// crashed it as the front end loaded). A set's row id stands for the shared row on its line
constexpr uint32_t kShared0 = 0x1000FC71, kSharedRows = 14;
uint32_t Wid(uint32_t id);
void Choice(X86Context& ctx, uint32_t player, uint32_t id, const std::vector<std::pair<const char*, uint32_t>>& items,
            uint32_t value) {
    nf::MenuChoice(ctx, player, Wid(id), items, value);
}
uint32_t ChoiceValue(X86Context& ctx, uint32_t player, uint32_t id) { return nf::MenuChoiceValue(ctx, player, Wid(id)); }

void ShowOptions(X86Context& ctx, uint32_t player, uint32_t id, const Options& o, float v) {
    std::vector<std::pair<const char*, uint32_t>> items;
    for (uint32_t i = 0; i < o.size(); i++) items.push_back({o[i].text, i});
    Choice(ctx, player, id, items, Nearest(o, v));
}
float ReadOption(X86Context& ctx, uint32_t player, uint32_t id, const Options& o) {
    uint32_t i = ChoiceValue(ctx, player, id);
    return o[std::min<size_t>(i, o.size() - 1)].value;
}

const Options kOffOn = {{"Off", 0}, {"On", 1}};
const Options kShaftLevels = {{"Off", 0}, {"0.1", 0.1f}, {"0.25", 0.25f}, {"0.5", 0.5f}, {"0.75", 0.75f},
                              {"1.0", 1}, {"1.5", 1.5f}, {"2.0", 2}, {"2.5", 2.5f}, {"3.0", 3}};
const Options kFilters = {{"As the Game", 0}, {"2x", 2}, {"4x", 4}, {"8x", 8}, {"16x", 16}};
const Options kAoLevels = {{"Off", 0}, {"Low", 1}, {"High", 2}};
const Options kReflectLevels = {{"Off", gpu::kReflectOff}, {"Low", gpu::kReflectLow}, {"High", gpu::kReflectHigh}};
const Options kSharpenLevels = {{"Off", 0}, {"Low", 25}, {"Medium", 50}, {"High", 75}};
const Options kScales = {{"50%", 50}, {"67%", 67}, {"75%", 75}, {"100%", 100}, {"125%", 125}, {"150%", 150}, {"200%", 200}};
const Options kExposures = {{"Darker", 0.9f}, {"Normal", 1.05f}, {"Brighter", 1.2f}, {"Bright", 1.35f}};
const Options kTonemaps = {{"Off", 0}, {"Filmic", 1}, {"Reinhard", 2}};
const Options kContrasts = {{"Low", 1.0f}, {"Normal", 1.08f}, {"High", 1.18f}};
const Options kSaturations = {{"Muted", 0.95f}, {"Normal", 1.1f}, {"Rich", 1.25f}, {"Vivid", 1.45f}};
const Options kLevels3 = {{"Off", 0}, {"Low", 0.2f}, {"High", 0.45f}};
const Options kGrains = {{"Off", 0}, {"Low", 0.15f}, {"High", 0.35f}};
const Options kDetails = {{"Off", 0}, {"Low", 0.35f}, {"High", 0.7f}};
const Options kBandCounts = {{"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}, {"6", 6}, {"8", 8}};
const Options kOutlines = {{"Off", 0}, {"Thin", 1}, {"Medium", 1.5f}, {"Thick", 2.5f}};
struct Ink {
    const char* text;
    float rgb[3];
};
const Ink kInks[] = {{"Black", {0.04f, 0.04f, 0.06f}}, {"Navy", {0.05f, 0.08f, 0.25f}},
                     {"Maroon", {0.3f, 0.04f, 0.04f}}, {"White", {0.95f, 0.95f, 0.95f}}};
const Options kHalftones = {{"Off", 0}, {"Low", 0.4f}, {"High", 0.8f}};
const Options kToonSaturations = {{"Muted", 0.9f}, {"Normal", 1.1f}, {"Rich", 1.3f}, {"Vivid", 1.6f}};
const Options kPixels = {{"1", 1}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}, {"6", 6}};
const Options kDepths = {{"3 bits", 3}, {"4 bits", 4}, {"5 bits", 5}, {"6 bits", 6}, {"8 bits", 8}};
const Options kRetroFilters = {{"Nearest", 1}, {"As the Game", 0}};
const Options kTints = {{"Off", 0}, {"Low", 0.4f}, {"High", 0.8f}};
const Options kUpscaleSharps = {{"Off", 0}, {"Low", 0.2f}, {"Medium", 0.4f}, {"High", 0.7f}};
std::vector<std::pair<const char*, uint32_t>> UpscalerItems() {
    return {{"Off", gpu::kUpscalerOff},
            {"FSR 2", gpu::kUpscalerFsr},
            {gpu::UpscalerAvailable(gpu::kUpscalerDlss) ? "DLSS" : "DLSS (Not Available)", gpu::kUpscalerDlss}};
}
const char* kQualityNames[] = {"Low", "Medium", "High", "Ultra", "Custom"};

bool Vulkan() { return gpu::StyleRendererActive(); }

// the anti-aliasing choice: 0 off, 1 FXAA, 2 to 4 MSAA 2x 4x 8x (the Direct3D renderers draw MSAA), 5 SMAA (Vulkan)
constexpr uint32_t kAaSmaa = 5;
std::vector<std::pair<const char*, uint32_t>> AaItems() {
    std::vector<std::pair<const char*, uint32_t>> v = {{"Off", 0}, {"FXAA", 1}};
    if (Vulkan()) v.push_back({"SMAA", kAaSmaa});
    if (!Vulkan()) v.insert(v.end(), {{"MSAA 2x", 2}, {"MSAA 4x", 3}, {"MSAA 8x", 4}});
    return v;
}

uint32_t g_entered_quality = gpu::kQualityCustom;  // the preset shown when the page opened

// the style choice's values: the presets' indexes, 0xFE "Custom" (the values as they are, edited)
std::vector<std::pair<const char*, uint32_t>> StyleItems(const gpu::GraphicsStyle& st, uint32_t& shown) {
    std::vector<std::pair<const char*, uint32_t>> v;
    shown = 0;
    const auto& presets = gpu::StylePresets();
    for (uint32_t i = 0; i < presets.size(); i++) {
        v.push_back({presets[i].label.c_str(), i});
        if (presets[i].name == st.preset) shown = i;
    }
    if (st.preset.empty()) {
        static const char* names[] = {"Original (Edited)", "Enhanced (Edited)", "Toon (Edited)", "Retro (Edited)",
                                      "Noir (Edited)"};
        v.push_back({names[std::clamp(st.mode, 0, int(std::size(names)) - 1)], 0xFE});
        shown = 0xFE;
    }
    return v;
}

// the style as the pages last asked for it: the render thread takes a request at its next frame, so a page entered
// at once (Style Options after Graphics' Keep) would show the one before; a second later the snapshot has it (and a
// later edit of graphics.toml wins)
std::mutex g_asked_lock;
std::optional<gpu::GraphicsStyle> g_asked;
std::chrono::steady_clock::time_point g_asked_at;
gpu::GraphicsStyle Current() {
    std::lock_guard lock(g_asked_lock);
    if (g_asked && std::chrono::steady_clock::now() - g_asked_at > std::chrono::seconds(1)) g_asked.reset();
    return g_asked ? *g_asked : gpu::StyleSnapshot();
}
void Request(const gpu::GraphicsStyle& st) {
    gpu::RequestStyle(st);
    std::lock_guard lock(g_asked_lock);
    g_asked = st, g_asked_at = std::chrono::steady_clock::now();
}

void EnterGraphics(X86Context& ctx, uint32_t player) {
    const app::Settings& s = app::Get();
    gpu::GraphicsStyle st = Current();
    g_entered_quality = uint32_t(gpu::MatchQuality(st, s.aniso, s.fxaa, s.smaa));
    Choice(ctx, player, kQuality,
                   {{"Low", 0}, {"Medium", 1}, {"High", 2}, {"Ultra", 3}, {"Custom", 4}}, g_entered_quality);
    if (Vulkan()) {
        uint32_t shown = 0;
        auto items = StyleItems(st, shown);
        Choice(ctx, player, kStyle, items, shown);
    } else {
        Choice(ctx, player, kStyle, {{"Original (Needs Vulkan)", 0xFF}}, 0xFF);
    }
    ShowOptions(ctx, player, kFilter, kFilters, float(s.aniso));
    uint32_t aa = s.msaa && !Vulkan() ? 1 + std::min<uint32_t>(s.msaa, 3) : s.smaa && Vulkan() ? kAaSmaa : s.fxaa ? 1 : 0;
    Choice(ctx, player, kAa, AaItems(), aa);
    ShowOptions(ctx, player, kSharpen, kSharpenLevels, float(s.sharpen));
    ShowOptions(ctx, player, kScale, kScales, float(s.render_percent));
    if (Vulkan()) ShowOptions(ctx, player, kPack, kOffOn, st.texture_pack ? 1.0f : 0.0f);
    else Choice(ctx, player, kPack, {{"Needs Vulkan", 0xFF}}, 0xFF);
    // HDR: off, on at the screen's peak (1), or a peak in nits
    if (Vulkan()) {
        std::vector<std::pair<const char*, uint32_t>> hdr = {{"Off", 0}, {"On", 1}, {"On, 400 nits", 400}, {"On, 600 nits", 600},
                                                             {"On, 1000 nits", 1000}, {"On, 1500 nits", 1500}, {"On, 2000 nits", 2000}};
        uint32_t shown = !s.hdr ? 0 : s.hdr_peak ? s.hdr_peak : 1;
        static std::string other;
        if (std::none_of(hdr.begin(), hdr.end(), [&](auto& i) { return i.second == shown; }))
            other = "On, " + std::to_string(shown) + " nits", hdr.push_back({other.c_str(), shown});
        Choice(ctx, player, kHdr, hdr, shown);
    } else {
        Choice(ctx, player, kHdr, {{"Needs Vulkan", 0xFF}}, 0xFF);
    }
    if (Vulkan()) {
        Choice(ctx, player, kUpscaler, UpscalerItems(), uint32_t(st.upscaler));
        Choice(ctx, player, kUpQuality,
               {{st.upscaler == gpu::kUpscalerDlss ? "DLAA" : "Native AA", 0}, {"Quality", 1}, {"Balanced", 2},
                {"Performance", 3}, {"Ultra Performance", 4}},
               uint32_t(st.upscale_quality));
        ShowOptions(ctx, player, kUpscaleSharp, kUpscaleSharps, st.upscale_sharpness);
    } else {
        for (uint32_t id : {kUpscaler, kUpQuality, kUpscaleSharp}) Choice(ctx, player, id, {{"Needs Vulkan", 0xFF}}, 0xFF);
    }
    LOGR("nightfire: Graphics page entered (quality %s, style %s)", kQualityNames[g_entered_quality],
         st.preset.empty() ? "edited" : st.preset.c_str());
}

// the page's values into the settings and the style; the style's options pages are opened with it kept
void KeepGraphics(X86Context& ctx, uint32_t player) {
    gpu::GraphicsStyle st = Current();
    uint32_t quality = ChoiceValue(ctx, player, kQuality);
    uint32_t aniso = uint32_t(ReadOption(ctx, player, kFilter, kFilters));
    uint32_t aa = ChoiceValue(ctx, player, kAa);
    uint32_t sharpen = uint32_t(ReadOption(ctx, player, kSharpen, kSharpenLevels));
    uint32_t scale = uint32_t(ReadOption(ctx, player, kScale, kScales));
    uint32_t hdr = ChoiceValue(ctx, player, kHdr);
    if (Vulkan()) {
        uint32_t style = ChoiceValue(ctx, player, kStyle);
        const auto& presets = gpu::StylePresets();
        if (style < presets.size() && presets[style].name != st.preset) {
            // a preset's look; quality and the texture settings are not part of it
            gpu::GraphicsStyle keep = st;
            st = presets[style].values;
            st.preset = presets[style].name;
            st.quality = keep.quality, st.quality_ao = keep.quality_ao, st.quality_bloom = keep.quality_bloom;
            st.texture_pack = keep.texture_pack, st.dump_textures = keep.dump_textures;
            st.reflections = keep.reflections;
            st.reflection_strength = keep.reflection_strength, st.reflections_auto = keep.reflections_auto;
            st.soft_particles = keep.soft_particles, st.particle_fade = keep.particle_fade;
            st.effect_lights = keep.effect_lights, st.light_strength = keep.light_strength;
            st.materials = keep.materials, st.material_strength = keep.material_strength;
            st.surface_depth = keep.surface_depth, st.relief_high = keep.relief_high;
            st.displacement = keep.displacement, st.displace_depth = keep.displace_depth;
            st.wall_displacement = keep.wall_displacement;
            st.ground_detail = keep.ground_detail, st.detail_strength = keep.detail_strength;
            st.detail_distance = keep.detail_distance;
            st.atmosphere = keep.atmosphere, st.fog_strength = keep.fog_strength, st.shaft_strength = keep.shaft_strength;
            st.bounce_light = keep.bounce_light, st.bounce_strength = keep.bounce_strength;
            st.ray_tracing = keep.ray_tracing, st.rt_ao = keep.rt_ao, st.rt_radius = keep.rt_radius;
            st.rt_rays = keep.rt_rays, st.rt_shadows = keep.rt_shadows, st.rt_lighting = keep.rt_lighting;
            st.rt_sun = keep.rt_sun, st.rt_gi = keep.rt_gi, st.rt_gi_strength = keep.rt_gi_strength, st.blood = keep.blood;
            st.rt_volumetric = keep.rt_volumetric, st.rt_vol_strength = keep.rt_vol_strength;
            st.weather = keep.weather, st.snow_amount = keep.snow_amount, st.footprints = keep.footprints;
            st.dof = keep.dof, st.dof_strength = keep.dof_strength;
            st.hdr_tonemap = keep.hdr_tonemap, st.auto_exposure = keep.auto_exposure;
            st.motion_blur = keep.motion_blur, st.motion_blur_strength = keep.motion_blur_strength;
            st.smooth_turn = keep.smooth_turn;
            st.contact_shadows = keep.contact_shadows, st.floor_reflections = keep.floor_reflections;
            st.upscaler = keep.upscaler, st.upscale_quality = keep.upscale_quality;
            st.upscale_sharpness = keep.upscale_sharpness;
        }
        st.texture_pack = ReadOption(ctx, player, kPack, kOffOn) > 0;
        // an upscaler this build or PC lacks (DLSS without an RTX GPU or its runtime) is not taken: the one before stays
        if (uint32_t up = ChoiceValue(ctx, player, kUpscaler); up < uint32_t(gpu::kUpscalers)) {
            if (gpu::UpscalerAvailable(int(up))) st.upscaler = int(up);
            else LOGR("nightfire: upscaler %u is not installed; left as it was", up);
        }
        if (uint32_t q = ChoiceValue(ctx, player, kUpQuality); q < uint32_t(gpu::kUpscaleQualities))
            st.upscale_quality = int(q);
        st.upscale_sharpness = ReadOption(ctx, player, kUpscaleSharp, kUpscaleSharps);
    }
    bool fxaa = aa == 1, smaa = Vulkan() ? aa == kAaSmaa : app::Get().smaa;
    uint32_t msaa = aa >= 2 && aa <= 4 ? aa - 1 : 0;
    // a preset chosen here sets its rows, whatever they show; else the rows make the preset (or Custom)
    if (quality != g_entered_quality && quality < uint32_t(gpu::kQualityCustom)) {
        int ao;
        bool bloom;
        gpu::QualityValues(int(quality), aniso, fxaa, smaa, ao, bloom);
        if (fxaa) msaa = 0;
        if (Vulkan()) st.quality_ao = ao, st.quality_bloom = bloom;
    }
    st.quality = gpu::MatchQuality(st, aniso, fxaa, smaa);
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings& s = app::Get();
        s.aniso = aniso;
        s.fxaa = fxaa;
        s.smaa = smaa;
        if (!Vulkan()) s.msaa = msaa;
        s.sharpen = sharpen;
        s.render_percent = scale;
        if (hdr != 0xFF) s.hdr = hdr != 0, s.hdr_peak = hdr > 1 ? hdr : 0;
        app::Save();
    }
    Request(st);
    LOGR("nightfire: Graphics kept: quality %s, style %s, filtering %u, AO %d, bloom %d, reflections %d, FXAA %d, "
         "SMAA %d, sharpen %u, scale %u%%, packs %d",
         kQualityNames[st.quality], st.preset.empty() ? "edited" : st.preset.c_str(), aniso, st.quality_ao,
         st.quality_bloom, st.reflections, fxaa, smaa, sharpen, scale, st.texture_pack);
}

void RestoreDefaults(X86Context& ctx, uint32_t player) {
    gpu::GraphicsStyle st;
    gpu::GraphicsStyle cur = Current();
    st.texture_pack = true;
    st.dump_textures = cur.dump_textures;
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings& s = app::Get();
        app::Settings d;
        s.aniso = d.aniso, s.fxaa = d.fxaa, s.smaa = d.smaa, s.msaa = d.msaa, s.sharpen = d.sharpen,
        s.render_percent = d.render_percent;
        app::Save();
        st.quality = gpu::MatchQuality(st, s.aniso, s.fxaa, s.smaa);
    }
    Request(st);
    // the rows show it (the request is taken at the next frame: the page is filled from the values set here)
    LOGR("nightfire: Graphics restored to defaults");
    (void)ctx, (void)player;
}

// Effects
void EnterEffects(X86Context& ctx, uint32_t player) {
    if (!Vulkan()) {
        for (uint32_t id : {kAo, kBloom, kReflect, kParticles, kMaterials, kLighting, kShafts, kRayTracing, kDof, kToneHdr, kMotion2, kSmoothTurn, kContact, kFloorRefl})
            Choice(ctx, player, id, {{"Needs Vulkan", 0xFF}}, 0xFF);
        return;
    }
    gpu::GraphicsStyle st = Current();
    ShowOptions(ctx, player, kShafts, kShaftLevels, st.atmosphere ? st.shaft_strength : 0.0f);
    ShowOptions(ctx, player, kAo, kAoLevels, float(st.quality_ao));
    ShowOptions(ctx, player, kBloom, kOffOn, st.quality_bloom ? 1.0f : 0.0f);
    ShowOptions(ctx, player, kReflect, kReflectLevels, float(st.reflections));
    Choice(ctx, player, kParticles, {{"Original", 0}, {"Soft", 1}, {"Soft + Lights", 2}},
           st.effect_lights ? 2 : st.soft_particles ? 1 : 0);
    // bit 0 the pack's maps, bit 1 relief, bit 2 ground detail, bit 3 relief High, bit 4 ground geometry, bit 5 wall
    // geometry
    const bool relief = st.materials && st.surface_depth > 0;
    Choice(ctx, player, kMaterials,
           {{"Original", 0}, {"Detail", 4}, {"Detail + Geometry", 20}, {"Detail + All Geometry", 52}, {"Detail + PBR", 5},
            {"Detail + Relief", 7}, {"Detail + Relief High", 15}, {"Everything but Walls", 31}, {"Everything", 63}, {"PBR", 1},
            {"PBR + Relief", 3}, {"PBR + Relief High", 11}},
           (st.materials ? 1u : 0u) | (relief ? 2u : 0u) | (st.ground_detail ? 4u : 0u) | (relief && st.relief_high ? 8u : 0u) |
               (st.displacement ? 16u : 0u) | (st.wall_displacement ? 32u : 0u));
    Choice(ctx, player, kLighting,
           {{"Original", 0}, {"Fog", 1}, {"Bounce", 2}, {"Fog + Bounce", 3}, {"Fog + Snow", 5}, {"Fog + Bounce + Snow", 7}},
           (st.atmosphere && st.fog_strength > 0 ? 1u : 0u) | (st.bounce_light ? 2u : 0u) | (st.weather ? 4u : 0u));
    if (gpu::RayTracingSupported())
        Choice(ctx, player, kRayTracing, {{"Off", 0}, {"Shade", 1}, {"Shade + Shadows", 2}, {"Full Lighting", 3},
                                 {"Full + GI", 4}, {"Full + Volumetric", 5}, {"Full + GI + Volumetric", 6}},
               st.ray_tracing ? (st.rt_lighting ? (st.rt_volumetric ? (st.rt_gi ? 6 : 5) : st.rt_gi ? 4 : 3)
                                                : st.rt_shadows ? 2 : 1)
                              : 0);
    else
        Choice(ctx, player, kRayTracing, {{"Not on this GPU", 0xFF}}, 0xFF);
    Choice(ctx, player, kDof, {{"Off", gpu::kDofOff}, {"Distance", gpu::kDofDistance}, {"Focus", gpu::kDofFocus}},
           uint32_t(st.dof));
    // the curve (bits 0 and 1), auto exposure (bit 2)
    Choice(ctx, player, kToneHdr,
           {{"Off", 0}, {"AgX", 1}, {"ACES", 2}, {"AgX + Auto Exposure", 5}, {"ACES + Auto Exposure", 6}},
           uint32_t(st.hdr_tonemap) | (st.hdr_tonemap && st.auto_exposure ? 4u : 0u));
    ShowOptions(ctx, player, kMotion2, kOffOn, st.motion_blur ? 1.0f : 0.0f);
    ShowOptions(ctx, player, kSmoothTurn, kOffOn, st.smooth_turn ? 1.0f : 0.0f);
    ShowOptions(ctx, player, kContact, kOffOn, st.contact_shadows ? 1.0f : 0.0f);
    Choice(ctx, player, kFloorRefl, {{"Off", 0}, {"Ice", 1}, {"Ice + Polished Floors", 2},
                                    {"Water", 3}, {"Water + Ice", 4}, {"Water + Floors", 5}},
           uint32_t(st.floor_reflections + (st.water ? 3 : 0)));
}
void KeepEffects(X86Context& ctx, uint32_t player) {
    gpu::GraphicsStyle st = Current();
    if (!Vulkan()) {
        Request(st);
        return;
    }
    if (uint32_t lv = ChoiceValue(ctx, player, kLighting); lv <= 7) {
        const bool fog = (lv & 1) != 0;
        st.shaft_strength = ReadOption(ctx, player, kShafts, kShaftLevels);
        st.atmosphere = fog || st.shaft_strength > 0;
        st.fog_strength = fog ? (st.fog_strength > 0 ? st.fog_strength : 0.5f) : 0.0f;
        st.bounce_light = lv & 2;
        st.weather = lv & 4 ? std::max(st.weather, 1) : 0;
    }
    st.quality_ao = int(ReadOption(ctx, player, kAo, kAoLevels));
    st.quality_bloom = ReadOption(ctx, player, kBloom, kOffOn) > 0;
    st.reflections = int(ReadOption(ctx, player, kReflect, kReflectLevels));
    if (uint32_t pv = ChoiceValue(ctx, player, kParticles); pv <= 2) {
        st.soft_particles = pv >= 1;
        st.effect_lights = pv == 2;
    }
    uint32_t mat = ChoiceValue(ctx, player, kMaterials);
    if (mat <= 63) {
        st.materials = mat & 1;
        st.surface_depth = mat & 2 ? std::max(st.surface_depth, 0.6f) : 0.0f;
        st.ground_detail = mat & 4;
        st.relief_high = mat & 8;
        st.displacement = mat & 16;
        st.wall_displacement = mat & 32;
    }
    if (uint32_t rt = ChoiceValue(ctx, player, kRayTracing); rt <= 6) {
        st.ray_tracing = rt >= 1;
        st.rt_shadows = rt >= 2;
        st.rt_lighting = rt >= 3;
        st.rt_gi = rt == 4 || rt == 6;
        st.rt_volumetric = rt >= 5;
    }
    if (uint32_t dof = ChoiceValue(ctx, player, kDof); dof < uint32_t(gpu::kDofModes)) st.dof = int(dof);
    if (uint32_t tm = ChoiceValue(ctx, player, kToneHdr); tm <= 6 && (tm & 3) < uint32_t(gpu::kHdrTones))
        st.hdr_tonemap = int(tm & 3), st.auto_exposure = (tm & 4) != 0;
    st.motion_blur = ReadOption(ctx, player, kMotion2, kOffOn) > 0;
    st.smooth_turn = ReadOption(ctx, player, kSmoothTurn, kOffOn) > 0;
    st.contact_shadows = ReadOption(ctx, player, kContact, kOffOn) > 0;
    if (uint32_t f = ChoiceValue(ctx, player, kFloorRefl); f <= 5) st.floor_reflections = int(f % 3), st.water = f >= 3;
    {
        // AO and bloom are the quality preset's: a change here can make it Custom
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        const app::Settings& s = app::Get();
        st.quality = gpu::MatchQuality(st, s.aniso, s.fxaa, s.smaa);
    }
    Request(st);
    LOGR("nightfire: Effects kept: AO %d, bloom %d, reflections %d, particles %d/%d, materials %d, ground detail %d, "
         "fog %d, bounce %d, ray tracing %d/%d/%d/%d, depth of field %d, blood %d",
         st.quality_ao, st.quality_bloom, st.reflections, st.soft_particles, st.effect_lights, st.materials,
         st.ground_detail, st.atmosphere, st.bounce_light, st.ray_tracing, st.rt_shadows, st.rt_lighting, st.rt_gi, st.dof,
         st.blood);
}

// the style pages
void EnterStyle(X86Context& ctx, uint32_t player, uint32_t page) {
    gpu::GraphicsStyle st = Current();
    if (page == kEnhPage) {
        ShowOptions(ctx, player, kExposure, kExposures, st.exposure);
        ShowOptions(ctx, player, kTonemap, kTonemaps, float(st.tonemap));
        ShowOptions(ctx, player, kContrast, kContrasts, st.contrast);
        ShowOptions(ctx, player, kSaturation, kSaturations, st.saturation);
        ShowOptions(ctx, player, kVignette, kLevels3, st.vignette);
        ShowOptions(ctx, player, kGrain, kGrains, st.grain);
        ShowOptions(ctx, player, kSceneSharpen, kDetails, st.sharpen);
    } else if (page == kToonPage) {
        ShowOptions(ctx, player, kBands, kBandCounts, float(st.bands));
        ShowOptions(ctx, player, kOutline, kOutlines, st.outline);
        uint32_t ink = 0;
        float best = 1e9f;
        std::vector<std::pair<const char*, uint32_t>> inks;
        for (uint32_t i = 0; i < std::size(kInks); i++) {
            inks.push_back({kInks[i].text, i});
            float d = 0;
            for (int c = 0; c < 3; c++) d += std::fabs(kInks[i].rgb[c] - st.outline_color[c]);
            if (d < best) best = d, ink = i;
        }
        Choice(ctx, player, kOutlineColor, inks, ink);
        ShowOptions(ctx, player, kEdges, kOffOn, st.internal_edges ? 1.0f : 0.0f);
        ShowOptions(ctx, player, kHalftone, kHalftones, st.halftone);
        ShowOptions(ctx, player, kToonSaturation, kToonSaturations, st.saturation);
    } else {
        ShowOptions(ctx, player, kPixel, kPixels, float(st.pixel));
        ShowOptions(ctx, player, kDepth, kDepths, float(st.color_bits));
        ShowOptions(ctx, player, kDither, kOffOn, st.dither ? 1.0f : 0.0f);
        ShowOptions(ctx, player, kRetroFilter, kRetroFilters, float(st.tex_filter == gpu::kFilterNearest ? 1 : 0));
        ShowOptions(ctx, player, kSnap, kOffOn, st.vertex_snap ? 1.0f : 0.0f);
        ShowOptions(ctx, player, kTint, kTints, st.retro_tint);
    }
}

// More Settings
const char* LanguageName(uint32_t l) {
    static const char* names[] = {"", "English", "Japanese", "German", "French", "Spanish", "Italian", "Korean",
                                  "Chinese", "Portuguese"};
    return l < std::size(names) ? names[l] : "Other";
}
void EnterMore(X86Context& ctx, uint32_t player) {
    const app::Settings& s = app::Get();
    Choice(ctx, player, kFps, {{"Off", 0}, {"FPS", 1}, {"Full", 2}}, nf::perf_overlay ? 2 : s.show_fps ? 1 : 0);
    ShowOptions(ctx, player, kPauseBg, kOffOn, s.pause_unfocused ? 1.0f : 0.0f);
    ShowOptions(ctx, player, kHideHud, kOffOn, nf::hide_hud ? 1.0f : 0.0f);
    Choice(ctx, player, kButtonPics, {{"Original", 0}, {"Modern", 1}}, nf::modern_buttons ? 1 : 0);
    ShowOptions(ctx, player, kStartMenus, kOffOn, nf::skip_paris ? 1.0f : 0.0f);
    Choice(ctx, player, kKeyboard, {{"Player 1", 0}, {"Player 1 Alone", 1}, {"Off", 2}},
                   !s.keyboard ? 2 : s.keyboard_alone ? 1 : 0);
    ShowOptions(ctx, player, kMouseLook, kOffOn, s.mouse_look ? 1.0f : 0.0f);
    std::vector<std::pair<const char*, uint32_t>> langs;
    for (uint32_t l : app::kGame.languages) langs.push_back({LanguageName(l), l});
    if (langs.empty()) langs.push_back({LanguageName(s.language), s.language});
    Choice(ctx, player, kLanguage, langs, s.language);
    Choice(ctx, player, kKeys, {{"Open", 0}}, 0);
    Choice(ctx, player, kWheel, {{"Open", 0}}, 0);
    Choice(ctx, player, kMenuColors, {{"Original", 0}, {"PC Blue", 1}}, s.menu_colors ? 1 : 0);
    Choice(ctx, player, kCoop, {{"Open", 0}}, 0);
    Choice(ctx, player, kAchievements, {{"Open", 0}}, 0);
    Choice(ctx, player, kMoreNext, {{"Open", 0}}, 0);
}
// Co-op
// this PC's addresses for a host to give (netplay::LocalAddresses), "192.168.1.20:27015"; the help line says the rest
std::vector<std::string> CoopAddresses() {
    std::vector<std::string> v;
    for (const std::string& a : netplay::LocalAddresses()) v.push_back(a + ":" + std::to_string(netplay::kPort));
    return v;
}
std::string CoopAddrHelp() {
    std::string ips;
    for (const std::string& a : netplay::LocalAddresses()) ips += (ips.empty() ? "" : " or ") + a;
    if (ips.empty()) return "This PC is on no network: network co-op needs one.";
    return "Your address: " + ips + " (port " + std::to_string(netplay::kPort) +
           "). Friends over the internet need your public address and the port forwarded (UDP).";
}
uint32_t g_coop_player = 0, g_coop_mode = 0xFF;  // the Co-op page open (its mode as last seen), else 0xFF
void EnterCoop(X86Context& ctx, uint32_t player) {
    const uint32_t mode = netplay::Active() ? (netplay::Me() == 0 ? 2u : 3u) : nf::coop ? 1u : 0u;
    Choice(ctx, player, kCoopMode, {{"Off", 0}, {"Split Screen", 1}, {"Network: Host", 2}, {"Network: Join", 3}}, mode);
    {
        static std::vector<std::string> shown;  // the guest's copies are made once for each text (nf::MenuString)
        shown = CoopAddresses();
        if (shown.empty()) shown.push_back("No Network");
        std::vector<std::pair<const char*, uint32_t>> items;
        for (size_t i = 0; i < shown.size(); i++) items.push_back({shown[i].c_str(), uint32_t(i)});
        Choice(ctx, player, kCoopAddr, items, 0);
    }
    g_coop_player = player, g_coop_mode = mode;
    Choice(ctx, player, kCoopSplit, {{"One Above the Other", 0}, {"Side by Side", 1}}, nf::coop_side ? 1 : 0);
    ShowOptions(ctx, player, kCoopFF, kOffOn, nf::coop_friendly_fire ? 1.0f : 0.0f);
    ShowOptions(ctx, player, kCoopNear, kOffOn, nf::coop_leash ? 1.0f : 0.0f);
    ShowOptions(ctx, player, kCoopRespawn, kOffOn, nf::coop_respawn ? 1.0f : 0.0f);
    Choice(ctx, player, kCoopDone, {{"Keep", 0}}, 0);
}
// Co-op kept; true when Network: Join's page is to open (the joining is done there)
bool KeepCoop(X86Context& ctx, uint32_t player, bool live = false) {
    uint32_t mode = ChoiceValue(ctx, player, kCoopMode), split = ChoiceValue(ctx, player, kCoopSplit);
    if (mode <= 3) nf::coop = mode >= 1;  // network co-op: split screen co-op over the SDK's lockstep netplay
    if (split <= 1) nf::coop_side = split == 1;
    nf::coop_friendly_fire = ReadOption(ctx, player, kCoopFF, kOffOn) > 0;
    nf::coop_leash = ReadOption(ctx, player, kCoopNear, kOffOn) > 0;
    nf::coop_respawn = ReadOption(ctx, player, kCoopRespawn, kOffOn) > 0;
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    app::Save();
    LOGR("nightfire: Co-op kept: %s, %s, friendly fire %d, keep together %d, respawn %d",
         mode == 2 ? "network host" : mode == 3 ? "network join" : nf::coop ? "split screen" : "off",
         nf::coop_side ? "side by side" : "one above the other", nf::coop_friendly_fire, nf::coop_leash, nf::coop_respawn);
    if (live) return false;  // changing a choice must never restart or join a network session
    // network: the game started again in lockstep (every copy runs the whole game, only the controllers travel:
    // core/netplay.h), from the title, the host's controller playing player 1, the joiner's player 2; leaving it the same
    const bool want = mode == 2 || mode == 3, now = netplay::Active();
    if (mode > 3 || (!want && !now) || (want && now && (mode == 2) == (netplay::Me() == 0))) return false;
    if (mode == 3) return true;  // the host chosen on Network: Join's page (JoinNetwork)
    std::string spec = mode == 2 ? "host" : "";
    nf::SetEnv("XBE_NETPLAY", spec.empty() ? nullptr : spec.c_str());
    LOGR("nightfire: Co-op: restarting %s", spec.empty() ? "without network play" : ("for network play: " + spec).c_str());
    kernel::Relaunch("Nightfire");
    return false;
}

// ---- Network: Join: the hosts found and an address of four numbers (prefilled from the launcher's Host address,
// else this PC's network with .1); A on a host, or on Join This Address, saves it as the Host address and restarts
// joining it
bool g_join_open = false;
uint32_t g_join_player = 0;
std::vector<netplay::FoundHost> g_join_shown;
uint16_t g_join_port = netplay::kPort;  // the Host address's port, kept for an address typed here

std::string HostLabel(const netplay::FoundHost& h) {
    std::string name = h.name.substr(0, 14);
    return name + " " + h.address + (h.port != netplay::kPort ? ":" + std::to_string(h.port) : "");
}
void ShowFoundHosts(X86Context& ctx, uint32_t player) {
    g_join_shown = netplay::FindHosts();
    static std::vector<std::string>* labels = new std::vector<std::string>;  // the guest's copies stay (MenuString)
    for (uint32_t i = 0; i < kJoinHosts; i++) {
        std::string t = i < g_join_shown.size() ? HostLabel(g_join_shown[i]) : i == 0 ? "Searching..." : "-";
        size_t k = std::find(labels->begin(), labels->end(), t) - labels->begin();
        if (k == labels->size()) labels->push_back(t);
        Choice(ctx, player, kJoinHost0 + i, {{(*labels)[k].c_str(), 0}}, 0);
    }
}
// an address's four numbers, false when it is not one ("a.b.c.d", a port after it kept apart)
bool ParseAddress(const std::string& text, uint32_t (&octets)[4], uint16_t& port) {
    unsigned a[4], p = netplay::kPort;
    char tail = 0;
    int n = std::sscanf(text.c_str(), "%u.%u.%u.%u:%u%c", &a[0], &a[1], &a[2], &a[3], &p, &tail);
    if (n < 4 || n == 6 || p == 0 || p > 65535) return false;
    for (int i = 0; i < 4; i++) {
        if (a[i] > 255) return false;
        octets[i] = a[i];
    }
    port = uint16_t(n == 5 ? p : netplay::kPort);
    return true;
}
void EnterJoin(X86Context& ctx, uint32_t player) {
    g_join_open = true, g_join_player = player;
    ShowFoundHosts(ctx, player);
    uint32_t oct[4] = {192, 168, 1, 1};
    uint16_t port = netplay::kPort;
    if (!ParseAddress(app::Get().join_ip, oct, port)) {
        port = netplay::kPort;
        std::vector<std::string> mine = netplay::LocalAddresses();
        uint16_t unused = 0;
        if (!mine.empty() && ParseAddress(mine[0], oct, unused)) oct[3] = 1;
    }
    g_join_port = port;
    static std::vector<std::string> numbers;
    static std::vector<std::pair<const char*, uint32_t>> items;
    if (items.empty()) {
        numbers.reserve(256);
        for (uint32_t i = 0; i < 256; i++) numbers.push_back(std::to_string(i));
        for (uint32_t i = 0; i < 256; i++) items.push_back({numbers[i].c_str(), i});
    }
    for (uint32_t i = 0; i < 4; i++) Choice(ctx, player, kJoinOctet0 + i, items, oct[i]);
    Choice(ctx, player, kJoinAddress, {{"Join", 0}}, 0);
    LOGR("nightfire: Network: Join opened: searching this network; address %u.%u.%u.%u:%u", oct[0], oct[1], oct[2], oct[3],
         port);
}
void JoinNetwork(const std::string& spec) {
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Get().join_ip = spec;
        app::Save();
    }
    netplay::StopFindingHosts();
    g_join_open = false;
    nf::SetEnv("XBE_NETPLAY", spec.c_str());
    LOGR("nightfire: Co-op: restarting for network play: %s", spec.c_str());
    kernel::Relaunch("Nightfire");
}
bool JoinActivate(X86Context& ctx, uint32_t player, uint32_t on) {
    for (uint32_t i = 0; i < kJoinHosts; i++) {
        if (on != Wid(kJoinHost0 + i)) continue;
        if (i >= g_join_shown.size()) {
            nf::MenuHelp(ctx, player, "No game found on this line yet. The host chooses Co-op, Network: Host and waits; "
                                  "or type its address below.");
            return true;
        }
        const netplay::FoundHost& h = g_join_shown[i];
        JoinNetwork(h.address + (h.port != netplay::kPort ? ":" + std::to_string(h.port) : ""));
        return true;
    }
    if (on == Wid(kJoinAddress)) {
        uint32_t o[4];
        for (uint32_t i = 0; i < 4; i++) o[i] = ChoiceValue(ctx, player, kJoinOctet0 + i);
        std::string spec = std::to_string(o[0]) + "." + std::to_string(o[1]) + "." + std::to_string(o[2]) + "." +
                           std::to_string(o[3]);
        if (g_join_port != netplay::kPort) spec += ":" + std::to_string(g_join_port);
        JoinNetwork(spec);
        return true;
    }
    nf::MenuHelp(ctx, player, "Left and right set this number; A on Join This Address joins.");
    return true;
}

void KeepMore(X86Context& ctx, uint32_t player) {
    uint32_t kb = ChoiceValue(ctx, player, kKeyboard), lang = ChoiceValue(ctx, player, kLanguage);
    uint32_t colors = ChoiceValue(ctx, player, kMenuColors);
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    app::Settings& s = app::Get();
    if (uint32_t perf = ChoiceValue(ctx, player, kFps); perf <= 2) s.show_fps = perf == 1, nf::perf_overlay = perf == 2;
    s.pause_unfocused = ReadOption(ctx, player, kPauseBg, kOffOn) > 0;
    nf::hide_hud = ReadOption(ctx, player, kHideHud, kOffOn) > 0;
    nf::modern_buttons = ChoiceValue(ctx, player, kButtonPics) == 1;
    nf::skip_paris = ReadOption(ctx, player, kStartMenus, kOffOn) > 0;
    if (kb <= 2) s.keyboard = kb != 2, s.keyboard_alone = kb == 1;
    s.mouse_look = ReadOption(ctx, player, kMouseLook, kOffOn) > 0;
    if (colors <= 1) s.menu_colors = colors;
    for (uint32_t l : app::kGame.languages)
        if (l == lang) s.language = l;
    app::Save();
    LOGR("nightfire: More Settings kept: frame counter %d, performance overlay %d, pause in background %d, hide HUD %d, "
         "modern buttons %d, start in menus %d, keyboard %d alone %d, mouse look %d, language %u, menu colours %u",
         s.show_fps, nf::perf_overlay, s.pause_unfocused, nf::hide_hud, nf::modern_buttons, nf::skip_paris, s.keyboard,
         s.keyboard_alone, s.mouse_look, s.language, s.menu_colors);
}
// the second set
const Options kAdsZooms = {{"Off", 100}, {"110%", 110}, {"120%", 120}, {"130% (Default)", 130}, {"150%", 150},
                           {"175%", 175}, {"200%", 200}};
bool g_reset_armed = false;  // Reset to Defaults asked once: A again resets
void EnterMore2(X86Context& ctx, uint32_t player) {
    ShowOptions(ctx, player, kGameplayBlood, kOffOn, Current().blood ? 1.0f : 0.0f);
    if (nf::CanChangeMovement())
        Choice(ctx, player, kMovement, {{"Original", 0}, {"Tighter", 1}}, nf::tight_movement ? 1 : 0);
    else
        Choice(ctx, player, kMovement, {{nf::TightMovement() ? "Tighter (Host)" : "Original (Host)", 0}}, 0);
    const app::Settings& s = app::Get();
    g_reset_armed = false;
    Choice(ctx, player, kAds, {{"Off", 0}, {"On", 1}}, nf::aim_down_sights ? 1 : 0);
    ShowOptions(ctx, player, kAdsZoom, kAdsZooms, float(nf::ads_zoom));
    ShowOptions(ctx, player, kMotion, kOffOn, nf::view_motion ? 1.0f : 0.0f);
    Choice(ctx, player, kDiscord, {{"Off", 0}, {s.discord_app_id.empty() ? "On (No App Id Set)" : "On", 1}}, s.discord ? 1 : 0);
    Choice(ctx, player, kReset, {{"Reset...", 0}}, 0);
    Choice(ctx, player, kMoreBack, {{"Open", 0}}, 0);
}
void KeepMore2(X86Context& ctx, uint32_t player) {
    auto blood_style = Current();
    blood_style.blood = ReadOption(ctx, player, kGameplayBlood, kOffOn) > 0;
    Request(blood_style);
    const uint32_t movement = ChoiceValue(ctx, player, kMovement);
    if (nf::CanChangeMovement() && movement <= 1) nf::tight_movement = movement != 0;
    uint32_t ads = ChoiceValue(ctx, player, kAds), discord = ChoiceValue(ctx, player, kDiscord);
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    if (ads <= 1) nf::aim_down_sights = ads > 0;
    nf::ads_zoom = uint32_t(ReadOption(ctx, player, kAdsZoom, kAdsZooms));
    nf::view_motion = ReadOption(ctx, player, kMotion, kOffOn) > 0;
    if (discord <= 1) app::Get().discord = discord == 1;
    app::Save();
    LOGR("nightfire: More Settings (page 2) kept: aim down sights %d, aim zoom %u%%, weapon sway %d, Discord %d",
         nf::aim_down_sights, nf::ads_zoom, nf::view_motion, app::Get().discord);
}
// Reset to Defaults: the settings file's and the game's settings as they first were, and the graphics style; the
// key bindings and the wheel's, the display (mode, size, monitor, renderer), the language, the host address and the
// Discord id kept; the codenames are the game's saves, untouched
void ResetAll() {
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings& s = app::Get();
        app::Settings d;
        d.display_mode = s.display_mode, d.width = s.width, d.height = s.height, d.monitor = s.monitor;
        d.renderer = s.renderer, d.language = s.language, d.join_ip = s.join_ip;
        d.keys = s.keys, d.pad_map = s.pad_map;
        d.wheel_steer = s.wheel_steer, d.wheel_throttle = s.wheel_throttle, d.wheel_brake = s.wheel_brake;
        d.wheel_clutch = s.wheel_clutch, d.wheel_buttons = s.wheel_buttons, d.wheel_gears = s.wheel_gears;
        d.wheel_range = s.wheel_range, d.wheel_lock = s.wheel_lock, d.wheel_deadzone = s.wheel_deadzone;
        d.ffb_strength = s.ffb_strength, d.ffb_road = s.ffb_road;
        d.discord_app_id = s.discord_app_id;
        s = d;
        nf::skip_intro = false, nf::skip_paris = true, nf::hide_hud = false, nf::aim_down_sights = true;
        nf::view_motion = true, nf::foot_ik = true, nf::coop = false, nf::coop_friendly_fire = false;
        nf::coop_side = false, nf::coop_leash = true, nf::coop_respawn = true, nf::fov_percent = 100;
        nf::draw_distance = 100, nf::modern_menus = true, nf::modern_buttons = true, nf::rounded_font = false;
        nf::perf_overlay = false, nf::ads_zoom = 130, nf::tight_movement = false;
        app::Save();
    }
    gpu::GraphicsStyle st, cur = Current();
    st.texture_pack = true;
    st.dump_textures = cur.dump_textures;
    st.quality = gpu::MatchQuality(st, app::Get().aniso, app::Get().fxaa, app::Get().smaa);
    Request(st);
    app::ApplyDisplay();
    LOGR("nightfire: Reset to Defaults: graphics and game settings as they first were (keys, display and language kept)");
}
// ---- Achievements: the rows' states from achievements::List(); the medal ones' progress from the profile
// (src/common/achievement_set.cpp: mission i's score record 0x3C bytes at 0x17F198 + 0x3C i, its best +0x30, the
// gold and platinum targets +0xC and +0x10)
std::string AchValue(const achievements::Status& a) {
    if (a.unlocked) return "Unlocked";
    const std::string id = a.def->id;
    if (id == "golden_service" || id == "platinum_service") {
        const uint32_t medal = id == "golden_service" ? 0xC : 0x10;
        int n = 0;
        for (uint32_t m = 0; m < 12; m++) {
            const uint32_t rec = 0x17F198 + 0x3C * m, target = R32(rec + medal);
            n += target && R32(rec + 0x30) >= target;
        }
        return std::to_string(n) + " of 12";
    }
    return "Locked";
}
std::string AchHelp(uint32_t id) {
    const auto list = achievements::List();
    if (id == kAchMore || id == kAchBack) {
        size_t n = 0;
        uint32_t points = 0, all = 0;
        for (const auto& a : list) n += a.unlocked, points += a.unlocked ? a.def->points : 0, all += a.def->points;
        return std::to_string(n) + " of " + std::to_string(list.size()) + " unlocked, " + std::to_string(points) + " of " +
               std::to_string(all) + " points. " +
               (id == kAchMore ? "A: medals, difficulty and Bond Moves." : "A: the missions.");
    }
    const size_t i = id - kAch0;
    if (i >= list.size()) return "";
    const auto& a = list[i];
    return std::string(a.def->description) + " (" + std::to_string(a.def->points) + " points" +
           (a.unlocked ? ", unlocked " + a.when : "") + ")";
}

void KeepStyle(X86Context& ctx, uint32_t player, uint32_t page) {
    gpu::GraphicsStyle st = Current(), before = st;
    if (page == kEnhPage) {
        st.mode = gpu::kStyleEnhanced;
        st.exposure = ReadOption(ctx, player, kExposure, kExposures);
        st.tonemap = int(ReadOption(ctx, player, kTonemap, kTonemaps));
        st.contrast = ReadOption(ctx, player, kContrast, kContrasts);
        st.saturation = ReadOption(ctx, player, kSaturation, kSaturations);
        st.vignette = ReadOption(ctx, player, kVignette, kLevels3);
        st.grain = ReadOption(ctx, player, kGrain, kGrains);
        st.sharpen = ReadOption(ctx, player, kSceneSharpen, kDetails);
    } else if (page == kToonPage) {
        if (st.mode != gpu::kStyleNoir) st.mode = gpu::kStyleToon;  // Noir: Toon's options in black, white and red
        st.bands = int(ReadOption(ctx, player, kBands, kBandCounts));
        st.outline = ReadOption(ctx, player, kOutline, kOutlines);
        uint32_t ink = std::min<uint32_t>(ChoiceValue(ctx, player, kOutlineColor), std::size(kInks) - 1);
        for (int c = 0; c < 3; c++) st.outline_color[c] = kInks[ink].rgb[c];
        st.internal_edges = ReadOption(ctx, player, kEdges, kOffOn) > 0;
        st.halftone = ReadOption(ctx, player, kHalftone, kHalftones);
        st.saturation = ReadOption(ctx, player, kToonSaturation, kToonSaturations);
    } else {
        st.mode = gpu::kStyleRetro;
        st.pixel = int(ReadOption(ctx, player, kPixel, kPixels));
        st.color_bits = int(ReadOption(ctx, player, kDepth, kDepths));
        st.dither = ReadOption(ctx, player, kDither, kOffOn) > 0;
        st.tex_filter = ReadOption(ctx, player, kRetroFilter, kRetroFilters) > 0 ? gpu::kFilterNearest : gpu::kFilterGame;
        st.vertex_snap = ReadOption(ctx, player, kSnap, kOffOn) > 0;
        st.retro_tint = ReadOption(ctx, player, kTint, kTints);
    }
    // the options as a preset left them keep its name; any change makes the look an edited one
    gpu::GraphicsStyle a = st, b = before;
    a.preset = b.preset = "";
    bool same = a.mode == b.mode && a.exposure == b.exposure && a.tonemap == b.tonemap && a.contrast == b.contrast &&
                a.saturation == b.saturation && a.vignette == b.vignette && a.grain == b.grain && a.sharpen == b.sharpen &&
                a.bands == b.bands && a.outline == b.outline && a.internal_edges == b.internal_edges &&
                a.halftone == b.halftone && a.pixel == b.pixel && a.color_bits == b.color_bits && a.dither == b.dither &&
                a.tex_filter == b.tex_filter && a.vertex_snap == b.vertex_snap && a.retro_tint == b.retro_tint &&
                std::equal(std::begin(a.outline_color), std::end(a.outline_color), std::begin(b.outline_color));
    if (!same) st.preset.clear();
    Request(st);
    LOGR("nightfire: %s options kept%s", page == kEnhPage ? "Enhanced" : page == kToonPage ? (st.mode == gpu::kStyleNoir ? "Noir" : "Toon") : "Retro",
         same ? " (unchanged)" : "");
}

const std::vector<MenuRow>& RowsOf(uint32_t page) {
    return page == kGfxPage    ? kGfxRows
           : page == kEnhPage  ? kEnhRows
           : page == kToonPage ? kToonRows
           : page == kMorePage ? kMoreRows
           : page == kMore2Page ? kMore2Rows
           : page == kAchPage ? AchRows(false)
           : page == kAch2Page ? AchRows(true)
           : page == kNetPage  ? kNetRows
           : page == kEffectsPage ? kEffectsRows
           : page == kKeysPage ? kKeysRows
           : page == kKeys2Page ? kKeys2Rows
           : page == kCoopPage ? kCoopRows
           : page == kJoinNetPage ? kJoinRows
           : page == kWheelPage ? kWheelRows1
           : page == kWheel2Page ? kWheelRows2
           : page == kWheel3Page ? kWheelRows3
                               : kRetroRows;
}
constexpr uint32_t kSharedSets[] = {kEnhPage, kToonPage, kRetroPage, kMorePage, kNetPage, kEffectsPage, kKeysPage,
                                    kKeys2Page, kCoopPage, kWheelPage, kWheel2Page, kWheel3Page, kJoinNetPage,
                                    kMore2Page, kAchPage, kAch2Page};
uint32_t g_style_rows = kEnhPage;  // the set the shared page was opened for
uint32_t Wid(uint32_t id) {
    for (uint32_t set : kSharedSets)
        for (const MenuRow& r : RowsOf(set))
            if (r.id == id) return kShared0 + uint32_t(r.slot);
    return id;
}
// the set's row on a shared line, else null
const MenuRow* SharedRow(uint32_t line) {
    for (const MenuRow& r : RowsOf(g_style_rows))
        if (uint32_t(r.slot) == line) return &r;
    return nullptr;
}
// the shared page's texts: strings of its own (the title's trailing space keeps it apart from the Graphics row's
// "Style Options"; the rows' are blanks as long as the longest label), rewritten for the set shown before the page
// opens
constexpr char kStyleTitle[] = "Style Options ", kMoreTitle[] = "More Settings ", kNetTitle[] = "Netplay       ",
               kEffectsTitle[] = "Effects       ", kKeysTitle[] = "Key Bindings  ", kCoopTitle[] = "Co-op         ",
               kWheelTitle[] = "Steering Wheel", kJoinTitle[] = "Network: Join ", kAchTitle[] = "Achievements  ";
static_assert(sizeof(kStyleTitle) == sizeof(kMoreTitle) && sizeof(kStyleTitle) == sizeof(kNetTitle) &&
              sizeof(kStyleTitle) == sizeof(kEffectsTitle) && sizeof(kStyleTitle) == sizeof(kKeysTitle) &&
              sizeof(kStyleTitle) == sizeof(kCoopTitle) && sizeof(kStyleTitle) == sizeof(kWheelTitle) &&
              sizeof(kStyleTitle) == sizeof(kJoinTitle) && sizeof(kStyleTitle) == sizeof(kAchTitle));
constexpr size_t kLabelBytes = 32;
const char* SharedLabel(uint32_t line) {
    static std::string labels[kSharedRows];
    if (labels[line].empty()) labels[line] = std::string(kLabelBytes - 2, ' ') + char('0' + line);
    return labels[line].c_str();
}
void SetSharedTexts(uint32_t set) {
    std::memcpy(kernel::HostPtr(nf::MenuString(kStyleTitle)),
                set == kMorePage || set == kMore2Page ? kMoreTitle
                : AchSet(set)         ? kAchTitle
                : set == kNetPage     ? kNetTitle
                : set == kEffectsPage ? kEffectsTitle
                : set == kKeysPage || set == kKeys2Page ? kKeysTitle
                : set == kCoopPage    ? kCoopTitle
                : set == kJoinNetPage ? kJoinTitle
                : WheelSet(set)       ? kWheelTitle
                                      : kStyleTitle,
                sizeof(kStyleTitle));
    g_style_rows = set;
    for (uint32_t line = 0; line < kSharedRows; line++) {
        const MenuRow* r = SharedRow(line);
        char text[kLabelBytes] = {};
        std::strncpy(text, r ? r->label : "", kLabelBytes - 1);
        std::memcpy(kernel::HostPtr(nf::MenuString(SharedLabel(line))), text, kLabelBytes);
    }
}
bool IsPage(uint32_t id) { return id == kGfxPage || id == kStylePage; }
std::vector<MenuRow> StylePageRows() {
    std::vector<MenuRow> v;
    for (uint32_t line = 0; line < kSharedRows; line++)
        v.push_back({kShared0 + line, nf::kMenuChoice, SharedLabel(line), "", int(line)});
    return v;
}
// the lines the set has no row on hidden (0x2B 1), the others shown (0x2B 0)
void ShowStyleRows(X86Context& ctx, uint32_t player) {
    for (uint32_t line = 0; line < kSharedRows; line++)
        nf::MenuSet(ctx, player, kShared0 + line, 0x2B, SharedRow(line) ? 0 : 1);
}

}  // namespace

namespace nf {

bool MenuRowHidden(uint32_t page, uint32_t id) {
    return page == kStylePage && id >= kShared0 && id < kShared0 + kSharedRows && !SharedRow(id - kShared0);
}

std::vector<std::vector<uint8_t>> GraphicsPages(const std::vector<uint8_t>& av, uint32_t (*record_size)(uint32_t)) {
    std::vector<std::vector<uint8_t>> pages = {BuildPage(av, record_size, kGfxPage, "Graphics", kGfxRows, kFirstY, kStep),
                                               BuildPage(av, record_size, kStylePage, kStyleTitle, StylePageRows(),
                                                         kFirstY, kStyleStep)};
    for (auto& p : pages)
        if (p.empty()) return {};
    return pages;
}

void OpenGraphicsPage(X86Context& ctx, uint32_t player) {
    MenuGoTo(ctx, player, kGfxPage);
    LOGR("nightfire: Graphics page opened");
}

// ---- Netplay: the session ends as the page opens (back from a lobby leaves the game), the search runs while it is
// open; Host Game hosts and Join Game joins the game chosen, each going on to the game's own join page (40000019),
// then the lobby (src/game/lan_play.cpp)
constexpr uint32_t kJoinPage = 0x40000019;
uint32_t g_net_player = 0;
bool g_net_open = false;
std::vector<std::string> g_net_labels;  // the Game row's items as shown
std::vector<nf::LanServer> g_net_shown;

void ShowServers(X86Context& ctx, uint32_t player) {
    g_net_shown = nf::NetServers();
    g_net_labels.clear();
    for (const auto& sv : g_net_shown) g_net_labels.push_back(sv.label);
    std::vector<std::pair<const char*, uint32_t>> items;
    for (size_t i = 0; i < g_net_labels.size(); i++) items.push_back({g_net_labels[i].c_str(), uint32_t(i)});
    if (items.empty()) items.push_back({"Searching...", 0});
    Choice(ctx, player, kNetServer, items, 0);
}
void EnterNet(X86Context& ctx, uint32_t player) {
    nf::NetStop();
    nf::NetSearch(true);
    g_net_open = true, g_net_player = player;
    Choice(ctx, player, kNetHost, {{"Start", 0}}, 0);
    ShowServers(ctx, player);
    Choice(ctx, player, kNetJoin, {{"Join", 0}}, 0);
    LOGR("nightfire: Netplay opened: searching this network");
}
bool NetActivate(X86Context& ctx, uint32_t player, uint32_t on) {
    if (on == Wid(kNetHost)) {
        nf::NetSearch(false), g_net_open = false;
        nf::NetHost();
        MenuGoTo(ctx, player, kJoinPage);
        LOGR("nightfire: Netplay: hosting");
        return true;
    }
    if (on == Wid(kNetJoin) || on == Wid(kNetServer)) {
        uint32_t i = ChoiceValue(ctx, player, kNetServer);
        if (i >= g_net_shown.size()) {
            MenuHelp(ctx, player, "No game found yet on this network.");
            return true;
        }
        nf::LanServer sv = g_net_shown[i];
        nf::NetSearch(false), g_net_open = false;
        nf::NetJoin(sv.ip, sv.port);
        MenuGoTo(ctx, player, kJoinPage);
        LOGR("nightfire: Netplay: joining %s", sv.label.c_str());
        return true;
    }
    return true;  // A elsewhere: nothing
}

// ---- Key Bindings
struct Capture {
    bool on = false;
    uint32_t player = 0;
    const KeyRow* row = nullptr;
} g_capture;

// a key's name as the menu shows it: "W", "Left Ctrl", "Mouse Left"
std::string KeyLabel(uint16_t vk) {
    std::string n = app::KeyToName(vk), out;
    bool start = true;
    for (char c : n) {
        if (c == '_') {
            out += ' ', start = true;
            continue;
        }
        out += start ? char(std::toupper(static_cast<unsigned char>(c))) : c;
        start = false;
    }
    return out;
}
std::string KeysText(int control) {
    std::array<uint16_t, 2> k = input::ControlKeys(control);
    std::string a = k[0] ? KeyLabel(k[0]) : "", b = k[1] ? KeyLabel(k[1]) : "";
    if (a.empty()) std::swap(a, b);
    return a.empty() ? "-" : b.empty() ? a : a + " / " + b;
}
void ShowKeyRow(X86Context& ctx, uint32_t player, const KeyRow& r) {
    if (r.control >= 0 || r.extra) {
        // the guest strings are made once for each text (nf::MenuString): the texts kept as long as the game runs
        static std::vector<std::string>* texts = new std::vector<std::string>;
        std::string t = r.extra ? nf::KeyName(*r.extra) : KeysText(r.control);
        size_t i = std::find(texts->begin(), texts->end(), t) - texts->begin();
        if (i == texts->size()) texts->push_back(t);
        Choice(ctx, player, r.id, {{(*texts)[i].c_str(), 0}}, 0);
    } else {
        Choice(ctx, player, r.id, {{r.id == kKeysDefaults ? "Restore" : "Open", 0}}, 0);
    }
}
void EnterKeys(X86Context& ctx, uint32_t player) {
    for (const KeyRow& r : kKeyRows)
        for (const MenuRow& m : RowsOf(g_style_rows))
            if (m.id == r.id) ShowKeyRow(ctx, player, r);
}
void EndCapture() {
    if (g_capture.on) app::EndKeyCapture();
    g_capture = {};
}
// the other set of keys on the same page: its texts, rows and values, the focus on its first row
void SwitchKeys(X86Context& ctx, uint32_t player, uint32_t page_widget, uint32_t set) {
    SetSharedTexts(set);
    ShowStyleRows(ctx, player);
    EnterKeys(ctx, player);
    nf::MenuFocus(ctx, player, page_widget, kShared0);
    if (const MenuRow* r = SharedRow(0)) nf::MenuHelp(ctx, player, r->help);
}
bool KeysActivate(X86Context& ctx, uint32_t player, uint32_t page_widget, uint32_t on) {
    if (g_capture.on) return true;  // A while a key is awaited (a controller's): nothing
    const KeyRow* row = nullptr;
    for (const KeyRow& r : kKeyRows)
        if (Wid(r.id) == on)
            for (const MenuRow& m : RowsOf(g_style_rows))
                if (m.id == r.id) row = &r;
    if (!row) return true;
    if (row->id == kKeysMore || row->id == kKeysBack) {
        SwitchKeys(ctx, player, page_widget, row->id == kKeysMore ? kKeys2Page : kKeysPage);
        return true;
    }
    if (row->id == kKeysDefaults) {
        {
            std::lock_guard<std::recursive_mutex> lock(app::UiLock());
            app::Settings& s = app::Get();
            for (int c = 0; c < input::kControls && c < int(s.keys.size()); c++) s.keys[c] = app::DefaultKeys(c);
            nf::screenshot_key = 0x7B, nf::photo_key = 0x79, nf::restart_key = 0;
            app::Save();
        }
        EnterKeys(ctx, player);
        nf::MenuHelp(ctx, player, "Every action's keys are as they first were.");
        LOGR("nightfire: Key Bindings: the default keys");
        return true;
    }
    g_capture = {true, player, row};
    app::BeginKeyCapture();
    Choice(ctx, player, row->id, {{"Press a Key...", 0}}, 0);
    nf::MenuHelp(ctx, player, "Press a key or mouse button for this action. Escape clears it.");
    return true;
}

// ---- Steering Wheel
struct WheelWait {
    bool open = false;              // the shared page shows a wheel set (entered, not yet left)
    uint32_t player = 0;
    uint32_t focus_page = 0;
    int focus_delay = 0;  // the guest finishes the activating row's focus after its handler returns
    const WheelRow* row = nullptr;  // the binding waiting for its control (app::WheelDetectStart)
} g_wheel;

app::Settings::WheelBind* WheelBindOf(app::Settings& s, const WheelRow& r) {
    if (r.kind == kWheelAxis) {
        app::Settings::WheelBind* axes[] = {&s.wheel_steer, &s.wheel_throttle, &s.wheel_brake, &s.wheel_clutch};
        return axes[std::clamp(r.what, 0, 3)];
    }
    if (r.kind == kWheelButton && r.what >= 0 && r.what < int(s.wheel_buttons.size())) return &s.wheel_buttons[size_t(r.what)];
    if (r.kind == kWheelGear && r.what >= 0 && r.what < int(s.wheel_gears.size())) return &s.wheel_gears[size_t(r.what)];
    return nullptr;
}
bool WheelBinding(const WheelRow& r) { return r.kind == kWheelAxis || r.kind == kWheelButton || r.kind == kWheelGear; }
// a value row's setting (0, the steering's invert, is a flag)
uint32_t* WheelValueOf(app::Settings& s, int what) {
    switch (what) {
        case 1: return &s.wheel_range;
        case 2: return &s.wheel_lock;
        case 3: return &s.wheel_deadzone;
        case 4: return &s.ffb_strength;
        case 5: return &s.ffb_road;
        default: return nullptr;
    }
}
uint32_t WheelValueNow(app::Settings& s, int what) {
    if (what == 0) return s.wheel_steer.invert ? 1 : 0;
    const uint32_t* v = WheelValueOf(s, what);
    return v ? *v : 0;
}
uint32_t WheelStep(int what, uint32_t v) {  // the step shown for a value: the nearest
    const std::vector<uint32_t>& steps = kWheelValues[what].steps;
    uint32_t best = 0;
    for (uint32_t i = 1; i < steps.size(); i++)
        if (std::llabs(int64_t(steps[i]) - int64_t(v)) < std::llabs(int64_t(steps[best]) - int64_t(v))) best = i;
    return best;
}
std::string WheelValueText(int what, uint32_t v) {
    if (what == 0) return v ? "On" : "Off";
    if (v == 0 && what >= 3) return "Off";
    return std::to_string(v) + kWheelValues[what].text;
}
// a binding as the row shows it: "Wheel: X Axis", "Logitech G920: Button 3" (the device's name to a word that fits)
std::string WheelBindShort(const app::Settings::WheelBind& b) {
    if (b.axis < 0 && b.button < 0) return "None";
    std::string dev = b.device.empty() ? "Wheel" : b.device;
    if (dev.size() > 18) {
        size_t cut = dev.rfind(' ', 18);
        dev.resize(cut == std::string::npos || cut < 6 ? 18 : cut);
    }
    return dev + ": " + (b.axis >= 0 ? std::string(app::WheelAxisName(b.axis)) + " Axis" : "Button " + std::to_string(b.button));
}
bool NoWheel() { return input::Wheel().devices.empty(); }
const char* WheelHelp(const WheelRow& r) { return WheelBinding(r) && NoWheel() ? kNoWheel : r.help; }

void ShowWheelRow(X86Context& ctx, uint32_t player, const WheelRow& r) {
    app::Settings s;
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        s = app::Get();
    }
    if (r.kind == kWheelValue) {
        const std::vector<uint32_t>& steps = kWheelValues[r.what].steps;
        std::vector<std::string> texts;
        for (uint32_t v : steps) texts.push_back(WheelValueText(r.what, v));
        std::vector<std::pair<const char*, uint32_t>> items;
        for (uint32_t i = 0; i < steps.size(); i++) items.push_back({texts[i].c_str(), i});
        Choice(ctx, player, r.id, items, WheelStep(r.what, WheelValueNow(s, r.what)));
    } else if (r.kind == kWheelGo) {
        Choice(ctx, player, r.id, {{"Open", 0}}, 0);
    } else if (r.kind == kWheelDo) {
        Choice(ctx, player, r.id, {{r.what == 0 ? "Restore" : "Clear", 0}}, 0);
    } else {
        const app::Settings::WheelBind* b = WheelBindOf(s, r);
        std::string t = g_wheel.row != &r            ? (b ? WheelBindShort(*b) : "None")
                        : r.kind == kWheelGear       ? "Select the Gear..."
                        : r.kind == kWheelButton     ? "Press a Button..."
                        : r.what == 0                ? "Turn Right..."
                                                     : "Press Fully...";
        Choice(ctx, player, r.id, {{t.c_str(), 0}}, 0);
    }
}
void EnterWheel(X86Context& ctx, uint32_t player) {
    g_wheel.open = true, g_wheel.player = player;
    for (const WheelRow& r : kWheelRows)
        if (r.set == g_style_rows) ShowWheelRow(ctx, player, r);
}
void EndWheelWait(X86Context& ctx, uint32_t player, bool show) {
    if (!g_wheel.row) return;
    const WheelRow* r = g_wheel.row;
    app::WheelDetectCancel();
    g_wheel.row = nullptr;
    if (show && r->set == g_style_rows) ShowWheelRow(ctx, player, *r);
}
// the other sets of the wheel's rows on the same page, the focus on its first row
void SwitchWheel(X86Context& ctx, uint32_t player, uint32_t page_widget, uint32_t set) {
    EndWheelWait(ctx, player, false);
    SetSharedTexts(set);
    ShowStyleRows(ctx, player);
    EnterWheel(ctx, player);
    g_wheel.focus_page = page_widget, g_wheel.focus_delay = 2;
    nf::MenuFocus(ctx, player, page_widget, kShared0);
    for (const WheelRow& r : kWheelRows)
        if (r.set == set && r.slot == 0) nf::MenuHelp(ctx, player, WheelHelp(r));
}
bool WheelActivate(X86Context& ctx, uint32_t player, uint32_t page_widget, uint32_t on) {
    const WheelRow* row = nullptr;
    for (const WheelRow& r : kWheelRows)
        if (r.set == g_style_rows && kShared0 + uint32_t(r.slot) == on) row = &r;
    if (!row || row->kind == kWheelValue) return true;
    if (row->kind == kWheelGo) {
        SwitchWheel(ctx, player, page_widget, uint32_t(row->what));
        return true;
    }
    if (row->kind == kWheelDo) {
        EndWheelWait(ctx, player, true);
        {
            std::lock_guard<std::recursive_mutex> lock(app::UiLock());
            app::Settings& s = app::Get();
            if (row->what == 0)
                for (int c = 0; c < input::kControls && c < int(s.wheel_buttons.size()); c++)
                    s.wheel_buttons[size_t(c)] = {.button = app::DefaultWheelButton(c)};
            else
                for (app::Settings::WheelBind& g : s.wheel_gears) g = {};
            app::Save();
        }
        input::WheelSetup();
        EnterWheel(ctx, player);
        nf::MenuHelp(ctx, player, row->what == 0 ? "The buttons are a Logitech G920 or G923's." : "No gear is bound.");
        LOGR("nightfire: Steering Wheel: %s", row->what == 0 ? "the default buttons" : "no shifter");
        return true;
    }
    if (g_wheel.row == row) {  // A again: unbound
        EndWheelWait(ctx, player, false);
        {
            std::lock_guard<std::recursive_mutex> lock(app::UiLock());
            if (app::Settings::WheelBind* b = WheelBindOf(app::Get(), *row)) *b = {};
            app::Save();
        }
        input::WheelSetup();
        ShowWheelRow(ctx, player, *row);
        nf::MenuHelp(ctx, player, "Unbound.");
        LOGR("nightfire: Steering Wheel: %s unbound", row->label);
        return true;
    }
    EndWheelWait(ctx, player, true);
    if (NoWheel()) {
        nf::MenuHelp(ctx, player, kNoWheel);
        return true;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings::WheelBind* b = WheelBindOf(app::Get(), *row);
        if (!b) return true;
        app::WheelDetectStart(row->kind == kWheelButton || row->kind == kWheelGear ? app::WheelDetect::Button
                              : row->what == 0                                     ? app::WheelDetect::Steer
                                                                                   : app::WheelDetect::Pedal,
                              *b);
    }
    g_wheel.row = row;
    ShowWheelRow(ctx, player, *row);
    nf::MenuHelp(ctx, player, row->kind == kWheelGear     ? "Put the shifter in this gear. A again: unbound."
                              : row->kind == kWheelButton ? "Press the button on the wheel or button box. A again: unbound."
                              : row->what == 0            ? "Turn the wheel to the right. A again: unbound."
                                                          : "Press the pedal all the way. A again: unbound.");
    return true;
}
const char* GraphicsMenuHelp(uint32_t page, uint32_t id) {
    if (page == kStylePage && id >= kShared0 && id < kShared0 + kSharedRows) {
        if (WheelSet(g_style_rows)) {
            for (const WheelRow& r : kWheelRows)
                if (r.set == g_style_rows && kShared0 + uint32_t(r.slot) == id)
                    return g_wheel.row == &r ? "Waiting for the control. A again: unbound." : WheelHelp(r);
        }
        if (const MenuRow* r = SharedRow(id - kShared0)) {
            if (AchSet(g_style_rows)) {
                static std::string help;
                help = AchHelp(r->id);
                return help.c_str();
            }
            return r->help;
        }
    }
    if (page == kGfxPage)
        for (const MenuRow& r : kGfxRows)
            if (r.id == id) return r.help;
    return "";
}

// each frame (nf::LanFrame) while a wheel set is shown: the control taken for the binding waiting, the choices
// saved as they change
void WheelPageFrame(X86Context& ctx) {
    if (!g_wheel.open || !WheelSet(g_style_rows)) return;
    const uint32_t eax = ctx.eax, player = g_wheel.player;
    if (g_wheel.focus_delay && --g_wheel.focus_delay == 0)
        nf::MenuFocus(ctx, player, g_wheel.focus_page, kShared0);
    if (g_wheel.row && app::WheelDetectPoll()) {
        const WheelRow& r = *g_wheel.row;
        g_wheel.row = nullptr;
        for (const WheelRow& o : kWheelRows)  // the steering's direction sets Invert Steering
            if (o.set == g_style_rows && (&o == &r || (r.kind == kWheelAxis && r.what == 0 && o.kind == kWheelValue && o.what == 0)))
                ShowWheelRow(ctx, player, o);
        nf::MenuHelp(ctx, player, r.help);
        app::Settings s;
        {
            std::lock_guard<std::recursive_mutex> lock(app::UiLock());
            s = app::Get();
        }
        if (const app::Settings::WheelBind* b = WheelBindOf(s, r))
            LOGR("nightfire: Steering Wheel: %s is %s", r.label, app::WheelBindText(*b).c_str());
    } else if (g_wheel.row && app::WheelDetecting() == app::WheelDetect::None) {
        EndWheelWait(ctx, player, true);  // ended elsewhere
    }
    bool changed = false;
    for (const WheelRow& r : kWheelRows) {
        if (r.set != g_style_rows || r.kind != kWheelValue) continue;
        uint32_t i = ChoiceValue(ctx, player, r.id);
        const std::vector<uint32_t>& steps = kWheelValues[r.what].steps;
        if (i >= steps.size()) continue;
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings& s = app::Get();
        if (WheelStep(r.what, WheelValueNow(s, r.what)) == i) continue;
        if (r.what == 0) s.wheel_steer.invert = steps[i] != 0;
        else if (uint32_t* v = WheelValueOf(s, r.what)) *v = steps[i];
        changed = true;
    }
    if (changed) {
        {
            std::lock_guard<std::recursive_mutex> lock(app::UiLock());
            app::Save();
            const app::Settings& s = app::Get();
            LOGR("nightfire: Steering Wheel: invert %d, rotation %u, full steering at %u, dead zone %u%%, force feedback "
                 "%u%%, road %u%%",
                 s.wheel_steer.invert, s.wheel_range, s.wheel_lock, s.wheel_deadzone, s.ffb_strength, s.ffb_road);
        }
        input::WheelSetup();
    }
    ctx.eax = eax;
}

void EnterAch(X86Context& ctx, uint32_t player) {
    static std::vector<std::string>* texts = new std::vector<std::string>;  // the guest's copies stay (MenuString)
    const auto list = achievements::List();
    for (const MenuRow& r : RowsOf(g_style_rows)) {
        const size_t i = r.id - kAch0;
        std::string t = r.id == kAchMore || r.id == kAchBack ? "Open" : i < list.size() ? AchValue(list[i]) : "-";
        size_t k = std::find(texts->begin(), texts->end(), t) - texts->begin();
        if (k == texts->size()) texts->push_back(t);
        Choice(ctx, player, r.id, {{(*texts)[k].c_str(), 0}}, 0);
    }
    size_t n = 0;
    for (const auto& a : list) n += a.unlocked;
    LOGR("nightfire: Achievements (%s): %zu of %zu unlocked", g_style_rows == kAchPage ? "missions" : "medals and more", n,
         list.size());
}
struct FocusLater {
    uint32_t page = 0, player = 0, row = 0;
    int frames = 0;
} g_focus_later;
// another set of rows on the shared page: its texts, rows and values, the focus on its first row
void SwitchSet(X86Context& ctx, uint32_t player, uint32_t page_widget, uint32_t set) {
    SetSharedTexts(set);
    ShowStyleRows(ctx, player);
    if (set == kMorePage) EnterMore(ctx, player);
    else if (set == kMore2Page) EnterMore2(ctx, player);
    else EnterAch(ctx, player);
    uint32_t first = kShared0;
    for (uint32_t line = kSharedRows; line-- > 0;)
        if (SharedRow(line)) first = kShared0 + line;
    // focused a little later, as the wheel's sets are: the guest finishes the activating row's focus after its handler
    g_focus_later = {page_widget, player, first, 2};
}

void OpenMorePage(X86Context& ctx, uint32_t player) {
    SetSharedTexts(kMorePage);
    MenuGoTo(ctx, player, kStylePage);
    LOGR("nightfire: More Settings opened");
}

void OpenCoopPage(X86Context& ctx, uint32_t player) {
    SetSharedTexts(kCoopPage);
    MenuGoTo(ctx, player, kStylePage);
    LOGR("nightfire: Co-op opened");
}

void OpenNetplayPage(X86Context& ctx, uint32_t player) {
    SetSharedTexts(kNetPage);
    MenuGoTo(ctx, player, kStylePage);
}

// each frame (nf::LanFrame): a key taken for the row waiting for one
void KeysPageFrame(X86Context& ctx) {
    uint16_t vk = 0;
    if (!g_capture.on || !app::TakeCapturedKey(vk)) return;
    const KeyRow& r = *g_capture.row;
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings& s = app::Get();
        if (r.extra) {
            *r.extra = vk == 0x1B ? 0 : vk;  // Escape: none
            app::Save();
        } else if (r.control < int(s.keys.size())) {
            std::array<uint16_t, 2>& k = s.keys[size_t(r.control)];
            if (vk == 0x1B) k = {0, 0};  // Escape: none
            else if (k[0] != vk) k = {vk, k[0] ? k[0] : k[1]};
            if (k[1] == k[0]) k[1] = 0;
            app::Save();
        }
    }
    uint32_t eax = ctx.eax, player = g_capture.player;
    g_capture = {};
    ShowKeyRow(ctx, player, r);
    nf::MenuHelp(ctx, player, r.help);
    ctx.eax = eax;
    LOGR("nightfire: Key Bindings: %s is %s", r.label, r.extra ? nf::KeyName(*r.extra).c_str() : KeysText(r.control).c_str());
}

// Poll selected values rather than focus/pointers: mouse, pad and classic menus all use the same widgets.
// Capture a baseline on entry, apply only edits, then absorb any dependent choices refreshed by a preset.
void SettingsLiveFrame(X86Context& ctx) {
    // CallGuest restores integer registers, but leaves flags/x87 state changed. The pad reader's
    // caller must receive the exact context it had before these extra widget queries.
    struct RestoreContext {
        X86Context& target;
        X86Context saved;
        ~RestoreContext() { target = saved; }
    } restore{ctx, ctx};
    static uint32_t last_page = 0, last_key = 0;
    static std::vector<std::pair<uint32_t, uint32_t>> last_values;
    const uint32_t pw = MenuUp() ? R32(0x25F1D0 + 0x1BC) : 0;
    const uint32_t page = pw && mem::IsCommitted(pw + 0xAC) ? R32(pw + 0x18) : 0;
    const bool shared = page == kStylePage &&
        (g_style_rows == kMorePage || g_style_rows == kMore2Page || g_style_rows == kEffectsPage ||
         g_style_rows == kCoopPage || g_style_rows == kEnhPage || g_style_rows == kToonPage || g_style_rows == kRetroPage);
    if (page != 0x4000FC10 && page != kGfxPage && !shared) {
        last_page = last_key = 0;
        last_values.clear();
        return;
    }
    const uint32_t key = shared ? g_style_rows : page;
    auto values = [&] {
        std::vector<std::pair<uint32_t, uint32_t>> out;
        uint32_t w = R32(pw + 0x94);
        if (w) w -= R32(pw + 0xA0) * 4;
        const uint32_t eax = ctx.eax;
        for (int n = 0; w && n < 200 && mem::IsCommitted(w + 0x7B); w = R32(w), n++) {
            if (kernel::G<uint8_t>(w + 0x7A) & 1) continue;
            const uint8_t type = kernel::G<uint8_t>(w + 0x7B);
            if (type != 9 && type != 10) continue;
            const uint32_t value = kernel::CallGuest(ctx, 0x72630, {w, type == 9 ? 0x35u : 0x40u, 0, 0});
            out.emplace_back(R32(w + 0x18), value);
        }
        ctx.eax = eax;
        return out;
    };
    auto now = values();
    if (pw != last_page || key != last_key) {
        last_page = pw, last_key = key, last_values = std::move(now);
        return;
    }
    if (now == last_values) return;
    if (page == 0x4000FC10) ApplyPcSettings(ctx, 0);
    else if (page == kGfxPage) {
        KeepGraphics(ctx, 0);
        EnterGraphics(ctx, 0);  // quality, AA and filtering must agree after a preset changes
    } else if (g_style_rows == kMorePage) KeepMore(ctx, 0);
    else if (g_style_rows == kMore2Page) KeepMore2(ctx, 0);
    else if (g_style_rows == kEffectsPage) KeepEffects(ctx, 0);
    else if (g_style_rows == kCoopPage) KeepCoop(ctx, 0, true);
    else KeepStyle(ctx, 0, g_style_rows);
    last_values = values();
}

// each frame (nf::LanFrame): the games found shown as they come and go, outside the menu's update
void NetplayPageFrame(X86Context& ctx) {
    SettingsLiveFrame(ctx);
    if (g_focus_later.frames && --g_focus_later.frames == 0) {
        const uint32_t eax = ctx.eax;
        nf::MenuFocus(ctx, g_focus_later.player, g_focus_later.page, g_focus_later.row);
        ctx.eax = eax;
    }
    KeysPageFrame(ctx);
    WheelPageFrame(ctx);
    if (!g_net_open) return;
    std::vector<nf::LanServer> now = nf::NetServers();
    bool same = now.size() == g_net_shown.size();
    for (size_t i = 0; same && i < now.size(); i++) same = now[i].label == g_net_shown[i].label;
    if (same) return;
    uint32_t eax = ctx.eax;
    ShowServers(ctx, g_net_player);
    ctx.eax = eax;
}

bool GraphicsPageMessage(X86Context& ctx, uint32_t player, uint32_t widget, uint32_t msg, uint32_t arg) {
    uint32_t id = R32(widget + 0x18);
    if (IsPage(id)) {
        if (msg == 0x4D && id == kStylePage && g_net_open) nf::NetSearch(false), g_net_open = false;
        if (msg == 0x4D && id == kStylePage) EndCapture(), g_focus_later.frames = 0;
        if (msg == 0x4D && id == kStylePage) EndWheelWait(ctx, player, false), g_wheel.open = false;
        if (msg == 0x4C) {
            if (id == kGfxPage) {
                EnterGraphics(ctx, player);
            } else {
                ShowStyleRows(ctx, player);
                if (g_style_rows == kNetPage) EnterNet(ctx, player);
                else if (g_style_rows == kMorePage) EnterMore(ctx, player);
                else if (g_style_rows == kMore2Page) EnterMore2(ctx, player);
                else if (AchSet(g_style_rows)) EnterAch(ctx, player);
                else if (g_style_rows == kEffectsPage) EnterEffects(ctx, player);
                else if (g_style_rows == kKeysPage || g_style_rows == kKeys2Page) EnterKeys(ctx, player);
                else if (g_style_rows == kCoopPage) EnterCoop(ctx, player);
                else if (WheelSet(g_style_rows)) EnterWheel(ctx, player);
                else EnterStyle(ctx, player, g_style_rows);
            }
            uint32_t first = kGfxRows[0].id;  // the first row shown (back gives the row it kept after)
            for (uint32_t line = kSharedRows; id != kGfxPage && line-- > 0;)
                if (SharedRow(line)) first = kShared0 + line;
            MenuFocus(ctx, player, widget, first);
            if (id == kStylePage && WheelSet(g_style_rows))
                g_wheel.focus_page = widget, g_wheel.focus_delay = 2;
            if (id == kStylePage && WheelSet(g_style_rows))  // with no wheel plugged in, the first row says so
                for (const WheelRow& r : kWheelRows)
                    if (r.set == g_style_rows && kShared0 + uint32_t(r.slot) == first) MenuHelp(ctx, player, WheelHelp(r));
        }
        if (msg == 0x4B) {
            uint32_t on = arg ? R32(arg + 0x18) : 0;
            if (id == kGfxPage && on == kStyleOptions) {
                // the style chosen here kept, then its page (Original has none)
                KeepGraphics(ctx, player);
                uint32_t chosen = ChoiceValue(ctx, player, kStyle);
                const auto& presets = gpu::StylePresets();
                int mode = chosen < presets.size() ? presets[chosen].values.mode : Current().mode;
                if (!Vulkan() || mode == gpu::kStyleOriginal) {
                    MenuHelp(ctx, player, Vulkan() ? "Original is the console's picture: it has no options."
                                                   : "The styles are drawn by the Vulkan renderer.");
                    return true;
                }
                SetSharedTexts(mode == gpu::kStyleEnhanced ? kEnhPage : mode == gpu::kStyleToon || mode == gpu::kStyleNoir ? kToonPage
                                                                                       : kRetroPage);
                MenuGoTo(ctx, player, kStylePage);
                return true;
            }
            if (id == kGfxPage && on == kEffects) {
                // the page's values kept (a quality preset chosen sets AO and bloom), then Effects
                KeepGraphics(ctx, player);
                SetSharedTexts(kEffectsPage);
                MenuGoTo(ctx, player, kStylePage);
                return true;
            }
            if (id == kGfxPage && on == kDefaults) {
                RestoreDefaults(ctx, player);
                MenuBack(ctx, player);
                return true;
            }
            if (id == kStylePage && g_style_rows == kNetPage) return NetActivate(ctx, player, on);
            if (id == kStylePage && (g_style_rows == kKeysPage || g_style_rows == kKeys2Page))
                return KeysActivate(ctx, player, widget, on);
            if (id == kStylePage && WheelSet(g_style_rows)) return WheelActivate(ctx, player, widget, on);
            if (id == kStylePage && g_style_rows == kMorePage && (on == Wid(kMoreNext) || on == Wid(kAchievements))) {
                KeepMore(ctx, player);
                SwitchSet(ctx, player, widget, on == Wid(kMoreNext) ? kMore2Page : kAchPage);
                return true;
            }
            if (id == kStylePage && g_style_rows == kMore2Page && on == Wid(kMoreBack)) {
                KeepMore2(ctx, player);
                SwitchSet(ctx, player, widget, kMorePage);
                return true;
            }
            if (id == kStylePage && g_style_rows == kMore2Page && on == Wid(kReset)) {
                if (!g_reset_armed) {
                    g_reset_armed = true;
                    Choice(ctx, player, kReset, {{"A Again to Reset", 0}}, 0);
                    MenuHelp(ctx, player, "A again: graphics and game settings as they first were (keys and codenames "
                                          "kept). Another row or B: nothing changes.");
                    return true;
                }
                ResetAll();
                SwitchSet(ctx, player, widget, kMore2Page);
                MenuHelp(ctx, player, "Done: graphics and game settings are as they first were.");
                return true;
            }
            if (id == kStylePage && AchSet(g_style_rows)) {
                if (on == Wid(kAchMore) || on == Wid(kAchBack))
                    SwitchSet(ctx, player, widget, on == Wid(kAchMore) ? kAch2Page : kAchPage);
                return true;  // A on an achievement: nothing
            }
            if (id == kStylePage && g_style_rows == kMorePage && on == Wid(kCoop)) {
                KeepMore(ctx, player);
                OpenCoopPage(ctx, player);
                return true;
            }
            if (id == kStylePage && g_style_rows == kMorePage && on == Wid(kKeys)) {
                // Key Bindings: More Settings kept, then the keys' rows on the same page (back returns to Settings)
                KeepMore(ctx, player);
                SetSharedTexts(kKeysPage);
                MenuGoTo(ctx, player, kStylePage);
                LOGR("nightfire: Key Bindings opened");
                return true;
            }
            if (id == kStylePage && g_style_rows == kMorePage && on == Wid(kWheel)) {
                // Steering Wheel: More Settings kept, then the wheel's rows on the same page, as Key Bindings
                KeepMore(ctx, player);
                SetSharedTexts(kWheelPage);
                MenuGoTo(ctx, player, kStylePage);
                LOGR("nightfire: Steering Wheel opened (%zu devices)", input::Wheel().devices.size());
                return true;
            }
            if (id == kGfxPage) KeepGraphics(ctx, player);
            else if (g_style_rows == kMorePage) KeepMore(ctx, player);
            else if (g_style_rows == kMore2Page) KeepMore2(ctx, player);
            else if (g_style_rows == kEffectsPage) KeepEffects(ctx, player);
            else if (g_style_rows == kCoopPage) KeepCoop(ctx, player);
            else KeepStyle(ctx, player, g_style_rows);
            MenuBack(ctx, player);
        }
        return true;
    }
    if (msg == 0x4E) {  // focus gained: the row's help line
        if (id >= kShared0 && id < kShared0 + kSharedRows) {
            if (WheelSet(g_style_rows)) {
                for (const WheelRow& r : kWheelRows)
                    if (r.set == g_style_rows && kShared0 + uint32_t(r.slot) == id)
                        MenuHelp(ctx, player, g_wheel.row == &r ? "Waiting for the control. A again: unbound." : WheelHelp(r));
                return false;
            }
            if (g_reset_armed && g_style_rows == kMore2Page && id != Wid(kReset)) {  // Reset's question let go
                g_reset_armed = false;
                Choice(ctx, player, kReset, {{"Reset...", 0}}, 0);
            }
            if (const MenuRow* r = SharedRow(id - kShared0))
                MenuHelp(ctx, player, AchSet(g_style_rows) ? AchHelp(r->id).c_str() : r->help);
            return false;
        }
        for (const MenuRow& r : kGfxRows)
            if (r.id == id) {
                MenuHelp(ctx, player, r.help);
                return false;
            }
    }
    return false;
}

}  // namespace nf
