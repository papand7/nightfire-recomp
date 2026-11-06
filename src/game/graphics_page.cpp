// Graphics, a page of the game's own menus (opened from Settings, in the front end and from the pause screen), and
// its three style pages (Enhanced, Toon and Retro options). They are built as Settings is, from AV Options'
// records (src/game/pc_settings_page.cpp, nf::BuildPage), so their text, choices, focus, sounds and prompts are the
// game's own. A keeps the page's values and goes back, B goes back without, as on AV Options.
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
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <cstring>

#include "app/app.h"
#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "game/menu_page.h"
#include "gpu/style.h"
#include "kernel/kernel.h"

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
                   kEffectsPage = 6, kKeysPage = 7, kKeys2Page = 8;  // the shared page's rows
// the Graphics page's rows
constexpr uint32_t kQuality = 0x1000FC21, kStyle = 0x1000FC22, kStyleOptions = 0x1000FC23, kFilter = 0x1000FC24,
                   kAo = 0x1000FC25, kBloom = 0x1000FC26, kAa = 0x1000FC27, kSharpen = 0x1000FC28,
                   kScale = 0x1000FC29, kPack = 0x1000FC2B, kDefaults = 0x1000FC2C,
                   kReflect = 0x1000FC2D, kParticles = 0x1000FC2E, kMaterials = 0x1000FC2F,
                   kLighting = 0x1000FC30, kEffects = 0x1000FC38, kRayTracing = 0x1000FC39,
                   kDof = 0x1000FC3A, kBlood = 0x1000FC3B, kHdr = 0x1000FC3C, kToneHdr = 0x1000FC3D,
                   kMotion2 = 0x1000FC3E, kSmoothTurn = 0x1000FC3F;
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
                   kKeys = 0x1000FC69, kMenuColors = 0x1000FC6A, kAds = 0x1000FC6B, kMotion = 0x1000FC6C;

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
    {kBlood, nf::kMenuChoice, "Blood", "Blood where your shots hit, and at your view's edges when you are hit. Every style.", 8},
    {kToneHdr, nf::kMenuChoice, "Tone Mapping", "AgX or ACES: bright light rolls off instead of clipping to white. Auto: the exposure adapts.", 9},
    {kMotion2, nf::kMenuChoice, "Motion Blur", "A little blur as you turn and move. The gun and HUD stay sharp.", 10},
    {kSmoothTurn, nf::kMenuChoice, "Smooth Turning", "A frame in between, turned by the mouse: 120 Hz turning on a 120 Hz+ screen (SDR).", 11},
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
    {kFps, nf::kMenuChoice, "Frame Counter", "Frames a second in the top corner.", 0},
    {kPauseBg, nf::kMenuChoice, "Pause in Background", "The game stands still and silent while its window is not in front.", 1},
    {kHideHud, nf::kMenuChoice, "Hide HUD", "For screenshots: no ammo, health or radar. The menus stay.", 2},
    {kButtonPics, nf::kMenuChoice, "Button Pictures", "The console's prompts, or today's pad's (LB, RB, Menu, View).", 3},
    {kStartMenus, nf::kMenuChoice, "Start in Menus", "START at the title opens the menus instead of Paris.", 4},
    {kKeyboard, nf::kMenuChoice, "Keyboard and Mouse", "Player 1 with the first controller, Player 1 alone, or off.", 5},
    {kMouseLook, nf::kMenuChoice, "Mouse Look", "The mouse turns and aims on foot.", 6},
    {kLanguage, nf::kMenuChoice, "Language", "The game's language, at the next start.", 7},
    {kKeys, nf::kMenuChoice, "Key Bindings", "The keyboard and mouse keys for each action. A steering wheel: F1.", 8},
    {kMenuColors, nf::kMenuChoice, "Menu Colours", "The menu backgrounds as the game has them, or blue as on the PC version. Text and Bond stay.", 9},
    {kAds, nf::kMenuChoice, "Aim Down Sights", "L (right mouse) raises the gun and zooms in, not the red crosshair. Blur: the background too.", 10},
    {kMotion, nf::kMenuChoice, "Weapon Sway", "A very light head bob and weapon sway as you move and turn.", 11},
};
// Key Bindings (More Settings' row), two sets of rows: each an action on foot, its two keys shown; A waits for the
// next key or mouse button (app::BeginKeyCapture), which becomes its first key and the old first its second; Escape
// clears them. Saved at once, as the settings window's Controls tab does (app::Settings::keys)
struct KeyRow {
    uint32_t id;
    int control;  // input::Control, -1 a button
    const char* label;
    const char* help;
    int slot;
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
    {kKeysMore, -1, "More Keys", "Weapons, gadgets, vision and pause.", 11},
    {0x1000FCB1, input::kB, "Next Weapon", "The next weapon carried.", 0},
    {0x1000FCB2, input::kBlack, "Next Gadget", "The next gadget carried.", 1},
    {0x1000FCB3, input::kBack, "Vision Mode", "The gadget's vision mode.", 2},
    {0x1000FCB4, input::kLeft, "Weapon Back", "The weapon before (the mouse wheel too).", 3},
    {0x1000FCB5, input::kRight, "Weapon On", "The weapon after (the mouse wheel too).", 4},
    {0x1000FCB6, input::kUp, "Gadget / Zoom In", "The gadget before, or zoom in through a scope.", 5},
    {0x1000FCB7, input::kDown, "Gadget / Zoom Out", "The gadget after, or zoom out through a scope.", 6},
    {0x1000FCB8, input::kStart, "Pause", "The pause screen.", 7},
    {kKeysDefaults, -1, "Default Keys", "Every action's keys as they first were.", 9},
    {kKeysBack, -1, "First Keys", "Movement, sprint, crouch, jump and fire.", 11},
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
// the shared page (Style Options, More Settings): twelve rows of its own, kShared0 + the line, each set's rows drawn
// on them by their slot; the page's widgets stay as few as the menu builder takes (a page of every set's rows, 28,
// crashed it as the front end loaded). A set's row id stands for the shared row on its line
constexpr uint32_t kShared0 = 0x1000FC71, kSharedRows = 12;
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
        }
        st.texture_pack = ReadOption(ctx, player, kPack, kOffOn) > 0;
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
    ShowOptions(ctx, player, kBlood, kOffOn, Current().blood ? 1.0f : 0.0f);  // drawn over the picture: any renderer
    if (!Vulkan()) {
        for (uint32_t id : {kAo, kBloom, kReflect, kParticles, kMaterials, kLighting, kRayTracing, kDof, kToneHdr, kMotion2, kSmoothTurn})
            Choice(ctx, player, id, {{"Needs Vulkan", 0xFF}}, 0xFF);
        return;
    }
    gpu::GraphicsStyle st = Current();
    ShowOptions(ctx, player, kAo, kAoLevels, float(st.quality_ao));
    ShowOptions(ctx, player, kBloom, kOffOn, st.quality_bloom ? 1.0f : 0.0f);
    ShowOptions(ctx, player, kReflect, kReflectLevels, float(st.reflections));
    Choice(ctx, player, kParticles, {{"Original", 0}, {"Soft", 1}, {"Soft + Lights", 2}},
           st.effect_lights ? 2 : st.soft_particles ? 1 : 0);
    // bit 0 the pack's maps, bit 1 relief, bit 2 ground detail, bit 3 relief High, bit 4 ground geometry
    const bool relief = st.materials && st.surface_depth > 0;
    Choice(ctx, player, kMaterials,
           {{"Original", 0}, {"Detail", 4}, {"Detail + Geometry", 20}, {"Detail + PBR", 5}, {"Detail + Relief", 7},
            {"Detail + Relief High", 15}, {"Everything", 31}, {"PBR", 1}, {"PBR + Relief", 3}, {"PBR + Relief High", 11}},
           (st.materials ? 1u : 0u) | (relief ? 2u : 0u) | (st.ground_detail ? 4u : 0u) | (relief && st.relief_high ? 8u : 0u) |
               (st.displacement ? 16u : 0u));
    Choice(ctx, player, kLighting,
           {{"Original", 0}, {"Fog", 1}, {"Bounce", 2}, {"Fog + Bounce", 3}, {"Fog + Snow", 5}, {"Fog + Bounce + Snow", 7}},
           (st.atmosphere ? 1u : 0u) | (st.bounce_light ? 2u : 0u) | (st.weather ? 4u : 0u));
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
}
void KeepEffects(X86Context& ctx, uint32_t player) {
    gpu::GraphicsStyle st = Current();
    st.blood = ReadOption(ctx, player, kBlood, kOffOn) > 0;
    if (!Vulkan()) {
        Request(st);
        return;
    }
    if (uint32_t lv = ChoiceValue(ctx, player, kLighting); lv <= 7) {
        st.atmosphere = lv & 1, st.bounce_light = lv & 2;
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
    if (mat <= 31) {
        st.materials = mat & 1;
        st.surface_depth = mat & 2 ? std::max(st.surface_depth, 0.6f) : 0.0f;
        st.ground_detail = mat & 4;
        st.relief_high = mat & 8;
        st.displacement = mat & 16;
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
    ShowOptions(ctx, player, kFps, kOffOn, s.show_fps ? 1.0f : 0.0f);
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
    Choice(ctx, player, kMenuColors, {{"Original", 0}, {"PC Blue", 1}}, s.menu_colors ? 1 : 0);
    Choice(ctx, player, kAds, {{"Off", 0}, {"On", 1}}, nf::aim_down_sights ? 1 : 0);
    ShowOptions(ctx, player, kMotion, kOffOn, nf::view_motion ? 1.0f : 0.0f);
}
void KeepMore(X86Context& ctx, uint32_t player) {
    uint32_t kb = ChoiceValue(ctx, player, kKeyboard), lang = ChoiceValue(ctx, player, kLanguage);
    uint32_t colors = ChoiceValue(ctx, player, kMenuColors);
    std::lock_guard<std::recursive_mutex> lock(app::UiLock());
    app::Settings& s = app::Get();
    s.show_fps = ReadOption(ctx, player, kFps, kOffOn) > 0;
    s.pause_unfocused = ReadOption(ctx, player, kPauseBg, kOffOn) > 0;
    nf::hide_hud = ReadOption(ctx, player, kHideHud, kOffOn) > 0;
    nf::modern_buttons = ChoiceValue(ctx, player, kButtonPics) == 1;
    nf::skip_paris = ReadOption(ctx, player, kStartMenus, kOffOn) > 0;
    if (kb <= 2) s.keyboard = kb != 2, s.keyboard_alone = kb == 1;
    s.mouse_look = ReadOption(ctx, player, kMouseLook, kOffOn) > 0;
    if (colors <= 1) s.menu_colors = colors;
    uint32_t ads = ChoiceValue(ctx, player, kAds);
    if (ads <= 1) nf::aim_down_sights = ads > 0;
    nf::view_motion = ReadOption(ctx, player, kMotion, kOffOn) > 0;
    for (uint32_t l : app::kGame.languages)
        if (l == lang) s.language = l;
    app::Save();
    LOGR("nightfire: More Settings kept: frame counter %d, pause in background %d, hide HUD %d, modern buttons %d, "
         "start in menus %d, keyboard %d alone %d, mouse look %d, language %u, menu colours %u, aim down sights %d",
         s.show_fps, s.pause_unfocused, nf::hide_hud, nf::modern_buttons, nf::skip_paris, s.keyboard,
         s.keyboard_alone, s.mouse_look, s.language, s.menu_colors, nf::aim_down_sights);
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
           : page == kNetPage  ? kNetRows
           : page == kEffectsPage ? kEffectsRows
           : page == kKeysPage ? kKeysRows
           : page == kKeys2Page ? kKeys2Rows
                               : kRetroRows;
}
constexpr uint32_t kSharedSets[] = {kEnhPage, kToonPage, kRetroPage, kMorePage, kNetPage, kEffectsPage, kKeysPage,
                                    kKeys2Page};
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
               kEffectsTitle[] = "Effects       ", kKeysTitle[] = "Key Bindings  ";
static_assert(sizeof(kStyleTitle) == sizeof(kMoreTitle) && sizeof(kStyleTitle) == sizeof(kNetTitle) &&
              sizeof(kStyleTitle) == sizeof(kEffectsTitle) && sizeof(kStyleTitle) == sizeof(kKeysTitle));
constexpr size_t kLabelBytes = 32;
const char* SharedLabel(uint32_t line) {
    static std::string labels[kSharedRows];
    if (labels[line].empty()) labels[line] = std::string(kLabelBytes - 2, ' ') + char('0' + line);
    return labels[line].c_str();
}
void SetSharedTexts(uint32_t set) {
    std::memcpy(kernel::HostPtr(nf::MenuString(kStyleTitle)),
                set == kMorePage      ? kMoreTitle
                : set == kNetPage     ? kNetTitle
                : set == kEffectsPage ? kEffectsTitle
                : set == kKeysPage || set == kKeys2Page ? kKeysTitle
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
    if (r.control >= 0) {
        // the guest strings are made once for each text (nf::MenuString): the texts kept as long as the game runs
        static std::vector<std::string>* texts = new std::vector<std::string>;
        std::string t = KeysText(r.control);
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

void OpenMorePage(X86Context& ctx, uint32_t player) {
    SetSharedTexts(kMorePage);
    MenuGoTo(ctx, player, kStylePage);
    LOGR("nightfire: More Settings opened");
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
        if (r.control < int(s.keys.size())) {
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
    LOGR("nightfire: Key Bindings: %s is %s", r.label, KeysText(r.control).c_str());
}

// each frame (nf::LanFrame): the games found shown as they come and go, outside the menu's update
void NetplayPageFrame(X86Context& ctx) {
    KeysPageFrame(ctx);
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
        if (msg == 0x4D && id == kStylePage) EndCapture();
        if (msg == 0x4C) {
            if (id == kGfxPage) {
                EnterGraphics(ctx, player);
            } else {
                ShowStyleRows(ctx, player);
                if (g_style_rows == kNetPage) EnterNet(ctx, player);
                else if (g_style_rows == kMorePage) EnterMore(ctx, player);
                else if (g_style_rows == kEffectsPage) EnterEffects(ctx, player);
                else if (g_style_rows == kKeysPage || g_style_rows == kKeys2Page) EnterKeys(ctx, player);
                else EnterStyle(ctx, player, g_style_rows);
            }
            uint32_t first = kGfxRows[0].id;  // the first row shown (back gives the row it kept after)
            for (uint32_t line = kSharedRows; id != kGfxPage && line-- > 0;)
                if (SharedRow(line)) first = kShared0 + line;
            MenuFocus(ctx, player, widget, first);
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
            if (id == kStylePage && g_style_rows == kMorePage && on == Wid(kKeys)) {
                // Key Bindings: More Settings kept, then the keys' rows on the same page (back returns to Settings)
                KeepMore(ctx, player);
                SetSharedTexts(kKeysPage);
                MenuGoTo(ctx, player, kStylePage);
                LOGR("nightfire: Key Bindings opened");
                return true;
            }
            if (id == kGfxPage) KeepGraphics(ctx, player);
            else if (g_style_rows == kMorePage) KeepMore(ctx, player);
            else if (g_style_rows == kEffectsPage) KeepEffects(ctx, player);
            else KeepStyle(ctx, player, g_style_rows);
            MenuBack(ctx, player);
        }
        return true;
    }
    if (msg == 0x4E) {  // focus gained: the row's help line
        if (id >= kShared0 && id < kShared0 + kSharedRows) {
            if (const MenuRow* r = SharedRow(id - kShared0)) MenuHelp(ctx, player, r->help);
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
