// The PC settings in the modern menus (settings_page.h), laid out as NightFire's: the Settings page (field of view, mouse,
// controller setup, display) opens Graphics (its Style Options, Effects and Ray Tracing) and More Settings (performance,
// keyboard and mouse, language, Key Bindings, Controller Buttons, Steering Wheel, Achievements, Gameplay & More). Every
// setting the settings window (F1, off for this game) had is on one of them. A page's rows are made afresh from the
// settings each time they are read or pressed. Each row is a toggle or choice (left, right and A step it), a slider (left
// and right), a button, a page to open, or a binding: A waits for the key (app::BeginKeyCapture) or the wheel's control
// (app::WheelDetectStart). B goes back a page; a row that applies at the next start says so.
#include "common/settings_page.h"
#include "common/netplay_page.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/auf.h"
#include "core/achievements.h"
#include "core/input.h"
#include "core/log.h"

namespace auf::settings_page {
namespace {
using app::Settings;

enum Page {
    kRoot, kGraphics, kStyle, kEffects, kRayTracing, kMore, kKeys, kPad, kWheel, kWheelButtons, kWheelGears, kAchievements,
    kGameplay, kNetplay, kPages
};
const char* const kTitles[kPages] = {"Settings",     "Graphics",        "Style Options",  "Effects",        "Ray Tracing",
                                     "More Settings", "Key Bindings",   "Controller Buttons", "Steering Wheel", "Wheel Buttons",
                                     "Gear Shifter",  "Achievements",   "Gameplay & More", "Netplay"};

struct Item {
    std::string label, help, value;
    int kind = kModernButton;
    int lo = 0, hi = 0, now = 0;  // a slider
    bool restart = false;
    bool press = false;  // a binding or a button: only A (or a click) plays it, not left and right
    int go = -1;         // a page it opens
    std::function<void(int)> act;  // dir -1 / 1, 0 a press of A
};
using Items = std::vector<Item>;

void Save() { app::Save(); }

void Toggle(Items& out, const char* label, const char* help, bool cur, std::function<void(bool)> set) {
    Item i;
    i.label = label, i.help = help ? help : "", i.kind = kModernChoice, i.value = cur ? "On" : "Off";
    i.act = [cur, set](int) { set(!cur); };
    out.push_back(std::move(i));
}
void Toggle(Items& out, const char* label, const char* help, bool* p) {
    Toggle(out, label, help, *p, [p](bool v) {
        *p = v;
        Save();
    });
}
void Choice(Items& out, const char* label, const char* help, std::vector<std::string> names, int cur, std::function<void(int)> set) {
    if (names.empty()) return;
    Item i;
    i.label = label, i.help = help ? help : "", i.kind = kModernChoice;
    const int n = int(names.size());
    cur = std::clamp(cur, 0, n - 1);
    i.value = names[size_t(cur)];
    i.act = [cur, n, set](int d) { set((cur + (d ? d : 1) + n) % n); };
    out.push_back(std::move(i));
}
void Slider(Items& out, const char* label, const char* help, int lo, int hi, int step, const char* unit, int cur, std::function<void(int)> set) {
    Item i;
    i.label = label, i.help = help ? help : "", i.kind = kModernSlider;
    cur = std::clamp(cur, lo, hi);
    i.lo = lo, i.hi = hi, i.now = cur;
    i.value = std::to_string(cur) + (unit ? unit : "");
    i.act = [=](int d) {
        if (d) set(std::clamp(cur + d * step, lo, hi));
    };
    out.push_back(std::move(i));
}
void Slider(Items& out, const char* label, const char* help, int lo, int hi, int step, const char* unit, uint32_t* p) {
    Slider(out, label, help, lo, hi, step, unit, int(*p), [p](int v) {
        *p = uint32_t(v);
        Save();
    });
}
void Button(Items& out, const char* label, const char* help, std::function<void()> run, const char* value = nullptr) {
    Item i;
    i.label = label, i.help = help ? help : "", i.press = true;
    if (value) i.value = value, i.kind = kModernChoice;
    i.act = [run](int) { run(); };
    out.push_back(std::move(i));
}
void Go(Items& out, const char* label, const char* help, int page) {
    Item i;
    i.label = label, i.help = help ? help : "", i.go = page, i.press = true;
    out.push_back(std::move(i));
}
void Restart(Items& out) {
    if (!out.empty()) out.back().restart = true;
}

// the index of the number the setting has in a table of them, else def
template <size_t N>
int IndexOf(const uint32_t (&v)[N], uint32_t now, int def = 0) {
    for (size_t k = 0; k < N; k++)
        if (v[k] == now) return int(k);
    return def;
}

// one of AUF_FX_LIST's: shown while the setting it hangs on has the value it needs
void Fx(Items& out, int id) {
    Settings& s = app::Get();
    const app::FxDef& d = app::FxInfo(id);
    if (d.gate >= 0 && (d.gate_val < 0 ? s.fx[d.gate] == 0 : s.fx[d.gate] != d.gate_val)) return;
    auto set = [id](int v) {
        app::Get().fx[id] = int32_t(v);
        Save();
    };
    const int v = int(s.fx[id]);
    if (d.kind == 2) {
        Toggle(out, d.label, d.help, v != 0, [set](bool b) { set(b); });
    } else if (d.kind == 1) {
        std::vector<std::string> names;
        for (int k = 0; k <= d.hi; k++) names.push_back(d.names[k]);
        Choice(out, d.label, d.help, names, v, set);
    } else {
        Slider(out, d.label, d.help, d.lo, d.hi, d.step, d.unit, v, set);
    }
}

// the display modes of the monitor, widest first, kept until the monitor changes
const std::vector<app::Mode>& Modes() {
    static std::vector<app::Mode> list;
    static uint32_t monitor = ~0u;
    if (monitor != app::Get().monitor) list = app::ListModes(), monitor = app::Get().monitor;
    return list;
}

// the game's own settings as they start (constant-initialised before any of this runs): Gameplay & More's defaults
struct GameDefaults {
    bool tight_movement, ragdolls, aim_down_sights, weapon_sway, auto_aim, hit_flash, pickup_physics, audio_occlusion,
        skip_cutscenes, photo_mode, uncapped, modern_menus, weapon_keys;
};
const GameDefaults kGameDefaults{auf::tight_movement, auf::ragdolls,       auf::aim_down_sights, auf::weapon_sway, auf::auto_aim,
                                 auf::hit_flash,      auf::pickup_physics, auf::audio_occlusion, auf::skip_cutscenes,
                                 auf::photo_mode,     auf::uncapped,       auf::modern_menus,    auf::weapon_keys};

// ---- the state: the page open, the pages it was opened from, the row; a key or wheel binding waiting
struct Open {
    int page, cursor;
};
int g_page = kRoot;
std::vector<Open> g_back;  // the pages under this one, each with the row that opened the next
int g_cursor = 0;          // the row, -1 the pause's tab strip
int g_key = -1;            // the input::Control whose keys wait for a key (app::BeginKeyCapture), -1 none
const Settings::WheelBind* g_wheel = nullptr;  // the wheel binding waiting for its control (app::WheelDetectStart)
bool g_quit_armed = false;  // Quit Game pressed once: the next press quits
int g_quit_row = -1;
int g_push = 0;             // the wheel's force test: the side pushed last
std::chrono::steady_clock::time_point g_built;  // the last Build: the page shown

void EndWaits() {
    if (g_key >= 0) app::EndKeyCapture();
    g_key = -1;
    if (g_wheel) app::WheelDetectCancel();
    g_wheel = nullptr;
    g_quit_armed = false;
}

// ---- Key Bindings: each action on foot (the game's Setup 1, which the keyboard plays), its two keys
struct KeyRow {
    int control;
    const char* label;
    const char* help;
};
const KeyRow kKeyRows[] = {
    {input::kLSUp, "Move Forward", "Walk forward."},
    {input::kLSDown, "Move Back", "Walk backward."},
    {input::kRSLeft, "Strafe Left", "Step to the left."},
    {input::kRSRight, "Strafe Right", "Step to the right."},
    {input::kLSLeft, "Turn Left", "Turn to the left (the mouse turns too)."},
    {input::kLSRight, "Turn Right", "Turn to the right (the mouse turns too)."},
    {input::kRSUp, "Look Up", "Look up (the mouse looks too)."},
    {input::kRSDown, "Look Down", "Look down (the mouse looks too)."},
    {input::kRT, "Fire", "Fire the weapon."},
    {input::kLT, "Aim", "Aim down the sights, or the game's manual aim."},
    {input::kBlack, "Alt. Fire", "The weapon's other fire."},
    {input::kA, "Action / Reload", "Use what is in front of you, open doors, or reload."},
    {input::kX, "Crouch", "Crouch."},
    {input::kY, "Jump", "Jump."},
    {input::kB, "Use Gadget", "Use the gadget in hand."},
    {input::kWhite, "Cycle Weapon", "The next weapon carried (the mouse wheel too)."},
    {input::kUp, "Weapon Up", "The d-pad's up: the weapon before."},
    {input::kDown, "Weapon Down", "The d-pad's down: the weapon after."},
    {input::kLeft, "Gadget Left", "The d-pad's left: the gadget before."},
    {input::kRight, "Gadget Right", "The d-pad's right: the gadget after."},
    {input::kBack, "Center View", "Look straight ahead again."},
    {input::kStart, "Pause", "The pause screen."},
    {input::kLThumb, "Left Stick Click", "The controller's left stick pressed in."},
    {input::kRThumb, "Right Stick Click", "The controller's right stick pressed in."},
};
// a key as the row shows it: the settings file's name ("left_ctrl") in words ("Left Ctrl")
std::string KeyText(uint16_t vk) {
    std::string n = vk ? app::KeyToName(vk) : "";
    bool start = true;
    for (char& c : n) {
        if (c == '_') c = ' ';
        else if (start) c = char(std::toupper(static_cast<unsigned char>(c)));
        start = c == ' ';
    }
    return n;
}
std::string KeysText(const std::array<uint16_t, 2>& k) {
    std::string a = KeyText(k[0]), b = KeyText(k[1]);
    if (a.empty() && b.empty()) return "None";
    if (a.empty() || b.empty()) return a.empty() ? b : a;
    return a + ", " + b;
}

// ---- Steering Wheel
enum WheelKind { kWheelAxis, kWheelButton, kWheelGear };
struct WheelRow {
    WheelKind kind;
    int what;  // axis: 0 steering, 1 throttle, 2 brake, 3 clutch; button: input::Control; gear: 0 R, 1 to 7
    const char* label;
    const char* help;
};
constexpr const char* kNoWheel = "No wheel, pedals, shifter or button box found. One plugged in is found within two seconds.";
const WheelRow kWheelButtonRows[] = {
    {kWheelButton, input::kRT, "Gas", "A button that accelerates, for a wheel without pedals. A again: unbound."},
    {kWheelButton, input::kLT, "Brake / Reverse", "A button that brakes and reverses. A again: unbound."},
    {kWheelButton, input::kB, "Handbrake", "The handbrake. A again: unbound."},
    {kWheelButton, input::kA, "Fire", "Fire the car's weapon. A again: unbound."},
    {kWheelButton, input::kX, "Gadget", "Use the car's gadget. A again: unbound."},
    {kWheelButton, input::kY, "Camera", "Change the camera. A again: unbound."},
    {kWheelButton, input::kBlack, "Next Weapon", "The next weapon. A again: unbound."},
    {kWheelButton, input::kWhite, "Weapon Before", "The weapon before. A again: unbound."},
    {kWheelButton, input::kRThumb, "Look Back", "Look behind the car. A again: unbound."},
    {kWheelButton, input::kStart, "Pause", "The pause screen. A again: unbound."},
    {kWheelButton, input::kBack, "Back / View", "The console's Back button. A again: unbound."},
    {kWheelButton, input::kUp, "D-pad Up", "The steering device's hat is the d-pad too; a button here as well. A again: unbound."},
    {kWheelButton, input::kDown, "D-pad Down", "The steering device's hat is the d-pad too; a button here as well. A again: unbound."},
    {kWheelButton, input::kLeft, "D-pad Left", "The steering device's hat is the d-pad too; a button here as well. A again: unbound."},
    {kWheelButton, input::kRight, "D-pad Right", "The steering device's hat is the d-pad too; a button here as well. A again: unbound."},
};
Settings::WheelBind* WheelBindOf(Settings& s, const WheelRow& r) {
    if (r.kind == kWheelAxis) {
        Settings::WheelBind* axes[] = {&s.wheel_steer, &s.wheel_throttle, &s.wheel_brake, &s.wheel_clutch};
        return axes[std::clamp(r.what, 0, 3)];
    }
    if (r.kind == kWheelButton && r.what >= 0 && r.what < int(s.wheel_buttons.size())) return &s.wheel_buttons[size_t(r.what)];
    if (r.kind == kWheelGear && r.what >= 0 && r.what < int(s.wheel_gears.size())) return &s.wheel_gears[size_t(r.what)];
    return nullptr;
}
// a binding as the row shows it: "Wheel: X Axis", "Logitech G920: Button 3" (the device's name to a word that fits)
std::string WheelBindShort(const Settings::WheelBind& b) {
    if (b.axis < 0 && b.button < 0) return "None";
    std::string dev = b.device.empty() ? "Wheel" : b.device;
    if (dev.size() > 18) {
        const size_t cut = dev.rfind(' ', 18);
        dev.resize(cut == std::string::npos || cut < 6 ? 18 : cut);
    }
    static const char* const kAxes[8] = {"X", "Y", "Z", "Rx", "Ry", "Rz", "Slider 1", "Slider 2"};
    return dev + ": " + (b.axis >= 0 ? std::string(kAxes[std::clamp(b.axis, 0, 7)]) + " Axis" : "Button " + std::to_string(b.button));
}
void WheelBindRow(Items& out, const WheelRow& r) {
    Settings& s = app::Get();
    Settings::WheelBind* b = WheelBindOf(s, r);
    if (!b) return;
    const bool waiting = g_wheel == b;
    const bool none = input::Wheel().devices.empty();
    Item i;
    i.label = r.label, i.kind = kModernChoice, i.press = true;
    i.value = !waiting                 ? WheelBindShort(*b)
              : r.kind == kWheelGear   ? "Select the Gear..."
              : r.kind == kWheelButton ? "Press a Button..."
              : r.what == 0            ? "Turn Right..."
                                       : "Press Fully...";
    i.help = waiting                   ? (r.kind == kWheelGear     ? "Put the shifter in this gear. A again: unbound."
                                          : r.kind == kWheelButton ? "Press the button on the wheel or button box. A again: unbound."
                                          : r.what == 0            ? "Turn the wheel to the right. A again: unbound."
                                                                   : "Press the pedal all the way. A again: unbound.")
             : none && r.kind == kWheelAxis ? kNoWheel
                                            : r.help;
    const app::WheelDetect kind = r.kind == kWheelAxis ? (r.what == 0 ? app::WheelDetect::Steer : app::WheelDetect::Pedal) : app::WheelDetect::Button;
    i.act = [b, waiting, kind, none](int) {
        if (waiting) {  // A again: unbound
            app::WheelDetectCancel();
            g_wheel = nullptr;
            *b = {};
            Save();
            input::WheelSetup();
            return;
        }
        EndWaits();
        if (none) return;
        app::WheelDetectStart(kind, *b);
        g_wheel = b;
    };
    out.push_back(std::move(i));
}
void WheelSetupSaved() {
    Save();
    input::WheelSetup();
}

// ---- the pages
const char* const kLanguageNames[] = {"", "English", "Japanese", "Deutsch", "Fran\xC3\xA7" "ais", "Espa\xC3\xB1" "ol", "Italiano", "Korean", "Chinese", "Portugu\xC3\xAA" "s"};

Items MakeItems(int page) {
    Items out;
    Settings& s = app::Get();
    switch (page) {
        case kRoot: {
            Slider(out, "Field of View", "How wide the view is, on foot and driving; the scope zooms in proportion. The game's is 100%.", 50, 150, 5, "%",
                   &auf::fov_percent);
            if (app::kGame.mouse_look)
                Slider(out, "Mouse Sensitivity", "How far the view turns for the mouse's movement.", 10, 1000, 10, "%", &s.mouse_sensitivity);
            {
                std::vector<std::string> names;
                for (int k = 0; auf::kSchemeNames[k]; k++) names.push_back(auf::kSchemeNames[k]);
                Choice(out, "Controller Scheme",
                       "Which of the game's four controller setups the keyboard and mouse play (the keys are laid out for Setup 1); pads keep the setup chosen in the game's menu.",
                       names, int(auf::controller_scheme), [](int v) {
                           auf::controller_scheme = uint32_t(v);
                           Save();
                       });
            }
            if (app::kGame.mouse_look) Toggle(out, "Invert Mouse", "Moving the mouse forward looks down.", &s.mouse_invert_y);
            Toggle(out, "Skip Intro Movies", "Starts without the EA, MGM and logo movies.", &auf::skip_intro);
            if (app::kGame.sharpen_2d) Toggle(out, "Sharp Text", "Text and HUD pictures sharp at high resolutions.", &s.sharp_2d);
            Choice(out, "Aspect Ratio",
                   "Auto follows the screen; Fill widens the view to an ultrawide or 16:10 screen. 16:9 and Fill apply at once, to or from 4:3 at the next start.",
                   {"Auto", "4:3", "16:9", "Fill"}, int(s.aspect % 4), [](int v) {
                       app::Get().aspect = uint32_t(v);
                       Save();
                   });
            Restart(out);
            {
                static const app::DisplayMode kModes[] = {app::DisplayMode::Windowed, app::DisplayMode::Borderless, app::DisplayMode::Fullscreen};
                int cur = 0;
                for (int k = 0; k < 3; k++)
                    if (kModes[k] == s.display_mode) cur = k;
                Choice(out, "Display Mode", "A window, a borderless window over the screen, or the screen itself.", {"Window", "Borderless", "Fullscreen"}, cur,
                       [](int v) {
                           app::Get().display_mode = kModes[v];
                           Save();
                           app::ApplyDisplay();
                       });
            }
            static std::vector<std::string> monitors = app::Monitors();
            if (monitors.size() > 1) {
                std::vector<std::string> names{"Primary"};
                names.insert(names.end(), monitors.begin(), monitors.end());
                Choice(out, "Monitor", "The display the game uses.", names, int(s.monitor), [](int v) {
                    app::Get().monitor = uint32_t(v);
                    Save();
                    app::ApplyDisplay();
                });
            }
            {
                std::vector<std::string> names{"Desktop"};
                int cur = 0;
                std::vector<app::Mode> modes = Modes();
                if (s.width && s.height &&
                    std::none_of(modes.begin(), modes.end(), [&](const app::Mode& m) { return m.width == s.width && m.height == s.height; }))
                    modes.insert(modes.begin(), app::Mode{s.width, s.height});  // a size the settings file holds that the list lacks
                for (size_t k = 0; k < modes.size(); k++) {
                    names.push_back(std::to_string(modes[k].width) + " x " + std::to_string(modes[k].height));
                    if (modes[k].width == s.width && modes[k].height == s.height) cur = int(k) + 1;
                }
                Choice(out, "Resolution", "The window's or the screen's size; Desktop is the screen's own.", names, cur, [modes](int v) {
                    Settings& t = app::Get();
                    t.width = v ? modes[size_t(v - 1)].width : 0;
                    t.height = v ? modes[size_t(v - 1)].height : 0;
                    Save();
                    app::ApplyDisplay();
                });
            }
            Toggle(out, "Vertical Sync", "Frames shown with the screen's refresh: no tearing, a little more delay.", &s.vsync);
            static const uint32_t caps[] = {30, 60, 120, 144, 240, 0};
            Choice(out, "Frame Cap", "The most frames a second drawn; the game sets its own pace under it.", {"30", "60", "120", "144", "240", "Unlimited"},
                   IndexOf(caps, s.fps_cap, 1), [](int v) {
                       app::Get().fps_cap = caps[v];
                       Save();
                   });
            Go(out, "Graphics", "Picture quality, the shader style and its options, effects and ray tracing.", kGraphics);
            Go(out, "More Settings", "Performance, HUD, keyboard and mouse, language, key bindings, the steering wheel, achievements and more.", kMore);
            Go(out, "Netplay", "Network play: host a game, join one on this network or online (each player on their own PC).", kNetplay);
            break;
        }
        case kGraphics: {
            struct Preset {
                uint32_t aniso;
                int edge;
                bool ao;
                uint32_t bloom;
            };
            static const Preset presets[4] = {{0, 0, false, 0}, {4, 1, false, 20}, {8, 2, true, 30}, {16, 2, true, 40}};
            int q = 4;
            for (int i = 0; i < 4; i++)
                if (s.aniso == presets[i].aniso && (s.smaa ? 2 : s.fxaa ? 1 : 0) == presets[i].edge && s.ambient_occlusion == presets[i].ao &&
                    s.bloom == presets[i].bloom)
                    q = i;
            Choice(out, "Graphics Quality", "Low to Ultra set the filtering, edge smoothing, occlusion and bloom at once; changing one makes it Custom.",
                   {"Low", "Medium", "High", "Ultra", "Custom"}, q, [q](int v) {
                       if (q == 4 && v == 3) v = 0;  // from Custom, the presets only
                       if (v > 3) v = 0;
                       Settings& t = app::Get();
                       const Preset& p = presets[v];
                       t.aniso = p.aniso, t.fxaa = p.edge == 1, t.smaa = p.edge == 2, t.ambient_occlusion = p.ao, t.bloom = p.bloom;
                       Save();
                   });
            Fx(out, app::kFxStyle);
            out.back().label = "Shader Style";
            Go(out, "Style Options", "The chosen style's own settings, and the picture's exposure, contrast, colour, vignette and grain.", kStyle);
            Go(out, "Effects", "Ambient occlusion, bloom, light shafts, fog, depth of field, motion blur and ray tracing.", kEffects);
            static const uint32_t anisos[] = {0, 2, 4, 8, 16};
            Choice(out, "Texture Filtering", "How sharp textures stay at an angle: as the game draws them, or anisotropic.", {"Game's", "2x", "4x", "8x", "16x"},
                   IndexOf(anisos, s.aniso), [](int v) {
                       app::Get().aniso = anisos[v];
                       Save();
                   });
            Choice(out, "Anti-Aliasing", "On the finished frame, HUD included. SMAA keeps more detail than FXAA, and costs a little more.", {"Off", "FXAA", "SMAA"},
                   s.smaa ? 2 : s.fxaa ? 1 : 0, [](int v) {
                       app::Get().fxaa = v == 1;
                       app::Get().smaa = v == 2;
                       Save();
                   });
            Choice(out, "MSAA", "Smooths the edges of the 3D scene; applies at once.", {"Off", "2x", "4x", "8x"}, int(s.msaa % 4), [](int v) {
                app::Get().msaa = uint32_t(v);
                Save();
            });
            if (s.msaa) Toggle(out, "Smooth Alpha Edges", "With MSAA: softens fences, leaves and other cut-out textures, which MSAA alone misses.", &s.alpha_coverage);
            Toggle(out, "Temporal AA", "Jitters 3D draws and reprojects history. Fast-moving geometry and the weapon or HUD can ghost. Not used while an upscaler is on.",
                   &s.temporal_aa);
            Slider(out, "Sharpening", "Contrast adaptive sharpening of the finished picture.", 0, 100, 5, "%", &s.sharpen);
            Slider(out, "Render Scale", "The 3D picture's size against the screen's: lower is faster, higher smoother.", 25, 200, 5, "%", &s.render_percent);
            Choice(out, "Scaling Filter", "How the frame reaches the screen's size when the render scale is not 100%.", {"Bilinear", "Sharp"}, int(s.scale_filter % 2),
                   [](int v) {
                       app::Get().scale_filter = uint32_t(v);
                       Save();
                   });
            Toggle(out, "Texture Packs", "Pictures in mods/textures drawn in place of the game's textures. Texture packs are not included.", &s.texture_pack);
            Toggle(out, "Dump Textures", "Writes the game's own textures to mods/textures/dump as they appear, to make a pack from.", &s.dump_textures);
            Toggle(out, "Enhanced Textures", "Doubles the 3D scene's textures in the background as they appear. More video memory; can hitch as new scenery appears.",
                   &s.world_textures);
            Choice(out, "Upscaler", "FSR 2: the 3D scene drawn smaller and rebuilt over frames, the HUD sharp; in place of Temporal AA and SMAA while on.",
                   {"Off", "AMD FSR 2"}, int(s.upscaler % 2), [](int v) {
                       app::Get().upscaler = uint32_t(v);
                       Save();
                   });
            if (s.upscaler) {
                Choice(out, "Upscale Quality", "Quality 1.5x, Balanced 1.7x, Performance 2x, Ultra Performance 3x: how much smaller the scene is drawn.",
                       {"Quality", "Balanced", "Performance", "Ultra Performance"}, int(s.upscale_quality % 4), [](int v) {
                           app::Get().upscale_quality = uint32_t(v);
                           Save();
                       });
                Slider(out, "Upscale Sharpness", "FSR 2's own sharpening of the rebuilt scene.", 0, 100, 5, "%", &s.upscale_sharpness);
            }
            Toggle(out, "Upscale Movies", "Sharpens movie frames with edge-safe Lanczos scaling before they are drawn.", &s.movie_upscale);
            Toggle(out, "Movies at 4:3", "On a wide screen the movies stay at their 4:3 shape in the middle instead of stretching.", &s.movie_43);
            Toggle(out, "Low Latency", "Holds one finished frame instead of several: less input lag, but a slow frame costs more.", &s.low_latency);
            Restart(out);
            Button(out, "Restore Defaults", "Every row on this page, the style options and the effects as they first were.", [] {
                Settings& t = app::Get();
                const Settings d;
                t.render_percent = d.render_percent, t.aniso = d.aniso, t.msaa = d.msaa, t.fxaa = d.fxaa, t.smaa = d.smaa, t.temporal_aa = d.temporal_aa;
                t.upscaler = d.upscaler, t.upscale_quality = d.upscale_quality, t.upscale_sharpness = d.upscale_sharpness;
                t.auto_exposure = d.auto_exposure, t.scale_filter = d.scale_filter, t.alpha_coverage = d.alpha_coverage;
                t.world_textures = d.world_textures, t.texture_pack = d.texture_pack, t.dump_textures = d.dump_textures, t.low_latency = d.low_latency;
                t.sharpen = d.sharpen, t.color_style = d.color_style, t.bloom = d.bloom, t.light_shafts = d.light_shafts;
                t.atmospheric_fog = d.atmospheric_fog, t.ao_radius = d.ao_radius, t.ambient_occlusion = d.ambient_occlusion;
                t.depth_of_field = d.depth_of_field, t.focus_distance = d.focus_distance, t.dof_blur = d.dof_blur;
                t.motion_blur = d.motion_blur, t.motion_blur_strength = d.motion_blur_strength, t.motion_blur_limit = d.motion_blur_limit;
                t.vignette = d.vignette, t.film_grain = d.film_grain, t.movie_43 = d.movie_43, t.movie_upscale = d.movie_upscale;
                t.exposure = d.exposure, t.contrast = d.contrast, t.saturation = d.saturation, t.temperature = d.temperature;
                for (int i = 0; i < app::kFxCount; i++) t.fx[i] = app::FxInfo(i).def;
                Save();
                LOGR("auf: settings: graphics as they first were");
            });
            break;
        }
        case kStyle: {
            for (int i = app::kFxStyle + 1; i < app::kFxRayTracing; i++) Fx(out, i);  // the chosen shader style's own
            Slider(out, "Exposure", "How bright the scene is drawn (no HDR range is added).", 50, 150, 5, "%", &s.exposure);
            Slider(out, "Contrast", "The difference between the scene's darks and lights.", 50, 150, 5, "%", &s.contrast);
            Slider(out, "Colour", "How strong the scene's colours are.", 0, 200, 5, "%", &s.saturation);
            Slider(out, "Colour Temperature", "Cool to warm; 0 is neutral.", -100, 100, 5, "", s.temperature, [](int v) {
                app::Get().temperature = int32_t(v);
                Save();
            });
            Slider(out, "Vignette", "Darkens the picture's corners.", 0, 100, 5, "%", &s.vignette);
            Slider(out, "Film Grain", "A film's fine, moving grain over the picture.", 0, 100, 5, "%", &s.film_grain);
            Choice(out, "Colour Style", "Screen-space looks over the finished frame: Enhanced Colour adjusts contrast and saturation; Toon draws colour edges.",
                   {"Original", "Enhanced Colour", "Toon (Screen)", "Retro", "Noir"}, int(s.color_style), [](int v) {
                       app::Get().color_style = uint32_t(v);
                       Save();
                   });
            break;
        }
        case kEffects: {
            Toggle(out, "Ambient Occlusion", "Soft shade in corners and creases: ray queries where the graphics card has them, else from the scene's depth.",
                   &s.ambient_occlusion);
            if (s.ambient_occlusion)
                Slider(out, "Occlusion Radius", "How far each surface looks for what darkens it: wider reaches round larger shapes.", 25, 300, 25, "%", &s.ao_radius);
            Slider(out, "Bloom", "A glow around lights and bright surfaces.", 0, 100, 5, "%", &s.bloom);
            Slider(out, "Light Shafts", "Screen-space light beams from bright parts of the scene.", 0, 100, 5, "%", &s.light_shafts);
            Slider(out, "Atmospheric Fog", "Depth in the distance, tinted by the scene's sky. The game's own fog remains.", 0, 100, 5, "%", &s.atmospheric_fog);
            Toggle(out, "Depth of Field", "Distance: surfaces away from the focus soften. The weapon may blur too.", &s.depth_of_field);
            if (s.depth_of_field) {
                Slider(out, "Focus Distance", "Where the picture is sharpest.", 1, 100, 1, " m", &s.focus_distance);
                Slider(out, "Depth Blur", "The most blur, in pixels.", 0, 12, 1, " px", &s.dof_blur);
            }
            Toggle(out, "Motion Blur", "A little blur as you turn and move.", &s.motion_blur);
            if (s.motion_blur) {
                Slider(out, "Motion Blur Strength", "How long the blur's streaks are.", 0, 150, 5, "%", &s.motion_blur_strength);
                Slider(out, "Motion Blur Limit", "The longest streak, in pixels.", 1, 16, 1, " px", &s.motion_blur_limit);
            }
            Toggle(out, "Auto Exposure", "The exposure adapts to the scene's brightness over time, with a tone curve.", &s.auto_exposure);
            if (app::ray_query_ok)
                Go(out, "Ray Tracing", "Traced occlusion, reflections, shadows, a sun, bounced and volumetric light.", kRayTracing);
            break;
        }
        case kRayTracing:
            for (int i = app::kFxRayTracing; i < app::kFxCount; i++) Fx(out, i);
            break;
        case kMore: {
            Choice(out, "Performance", "FPS: frames a second in the corner. Full: a panel with the frame time's graph and the size the 3D scene is drawn at.",
                   {"Off", "FPS", "Full"}, s.perf_overlay ? 2 : s.show_fps ? 1 : 0, [](int v) {
                       app::Get().show_fps = v == 1;
                       app::Get().perf_overlay = v == 2;
                       Save();
                   });
            Toggle(out, "Pause in Background", "The game stands still and silent while its window is not in front.", &s.pause_unfocused);
            Toggle(out, "Hide HUD", "For screenshots: every 2D draw of the game (the HUD, its menus) left out until this is turned off again; not saved.",
                   &s.hide_2d);
            Toggle(out, "Keyboard and Mouse", "The keyboard and mouse play as controller 1.", &s.keyboard);
            if (app::kGame.mouse_look) Toggle(out, "Mouse Look", "The mouse turns and aims on foot.", &s.mouse_look);
            if (!app::kGame.languages.empty()) {
                std::vector<std::string> list;
                int cur = 0;
                for (size_t k = 0; k < app::kGame.languages.size(); k++) {
                    const uint32_t l = app::kGame.languages[k];
                    list.push_back(l < std::size(kLanguageNames) ? kLanguageNames[l] : "?");
                    if (l == s.language) cur = int(k);
                }
                Choice(out, "Language", "The game's language, at the next start.", list, cur, [](int v) {
                    app::Get().language = app::kGame.languages[size_t(v)];
                    Save();
                });
                Restart(out);
            }
            Go(out, "Key Bindings", "The keyboard and mouse keys for each action, and the screenshot, photo mode and restart keys.", kKeys);
            Go(out, "Controller Buttons", "The button on your controller that presses each of the console's.", kPad);
            Go(out, "Steering Wheel", "A wheel, pedals and shifter for the driving missions: bindings, rotation, force feedback.", kWheel);
            if (!app::kGame.achievements.empty())
                Go(out, "Achievements", "The achievements: which are unlocked, and when.", kAchievements);
            Go(out, "Gameplay & More", "Movement, aiming, ragdolls, hit feedback, sound, Discord, the menus and defaults.", kGameplay);
            Button(out, "Quit Game", "Leaves the game: progress since it last saved is lost.", [] {
                if (!g_quit_armed) {
                    g_quit_armed = true;
                    return;
                }
                LOGR("auf: quit from the settings page");
                app::Quit();
            }, g_quit_armed ? "Press A Again" : nullptr);
            if (g_quit_armed) out.back().help = "Press A again to quit; progress since the game last saved is lost.";
            g_quit_row = int(out.size()) - 1;
            break;
        }
        case kKeys: {
            if (s.keys.size() != size_t(input::kControls)) break;
            for (const KeyRow& r : kKeyRows) {
                const int c = r.control;
                Item i;
                i.label = r.label, i.kind = kModernChoice, i.press = true;
                const bool waiting = g_key == c;
                i.value = waiting ? "Press a Key..." : KeysText(s.keys[size_t(c)]);
                i.help = waiting ? "Press a key or mouse button for this action. Escape clears its keys."
                                 : std::string(r.help) + " A: a new key (the old one stays as the second).";
                i.act = [c](int) {
                    EndWaits();
                    g_key = c;
                    app::BeginKeyCapture();
                };
                out.push_back(std::move(i));
            }
            static const uint32_t shots[] = {0x7B, 0x7A, 0x79, 0x78, 0x2C, 0x13};
            Choice(out, "Screenshot", "A picture of the screen in the screenshots folder beside the game.", {"F12", "F11", "F10", "F9", "Print Screen", "Pause"},
                   IndexOf(shots, s.screenshot_key), [](int v) {
                       app::Get().screenshot_key = shots[v];
                       Save();
                   });
            for (const app::Option& o : app::kGame.options) {
                const std::string key = o.key;
                if (key == "photo_key" || key == "restart_key" || key == "previous_weapon_key") {
                    std::vector<std::string> names;
                    for (int k = 0; o.choices && o.choices[k]; k++) names.push_back(o.choices[k]);
                    uint32_t* p = o.value;
                    const char* label = key == "photo_key" ? "Photo Mode" : key == "restart_key" ? "Restart Mission" : "Previous Weapon";
                    Choice(out, label, o.help, names, int(*p), [p](int v) {
                        *p = uint32_t(v);
                        Save();
                    });
                }
            }
            Toggle(out, "Number Keys Pick Weapons", "1 to 9 select the weapon in that place of the inventory (the order the weapon button cycles in).",
                   &auf::weapon_keys);
            Button(out, "Default Keys", "Every action's keys as they first were.", [] {
                Settings& t = app::Get();
                for (int c = 0; c < input::kControls && c < int(t.keys.size()); c++) t.keys[size_t(c)] = app::DefaultKeys(c);
                Save();
                LOGR("auf: settings: the default keys");
            });
            break;
        }
        case kPad: {
            if (s.pad_map.size() != size_t(input::kControls)) break;
            std::vector<std::string> names;
            for (int c = 0; c < input::kControls; c++) names.push_back(input::ControlLabel(c));
            for (int c = 0; c < input::kControls; c++)
                Choice(out, input::ControlLabel(c), "The button or stick on your controller that presses this console input.", names, int(s.pad_map[size_t(c)]),
                       [c](int v) {
                           app::Get().pad_map[size_t(c)] = uint8_t(v);
                           Save();
                       });
            Button(out, "Default Controller", "Every console input back to the controller's own button.", [] {
                Settings& t = app::Get();
                for (int c = 0; c < input::kControls && c < int(t.pad_map.size()); c++) t.pad_map[size_t(c)] = uint8_t(c);
                Save();
            });
            break;
        }
        case kWheel: {
            WheelBindRow(out, {kWheelAxis, 0, "Steering", "A, then turn the wheel to the right. A again: unbound."});
            Toggle(out, "Invert Steering", "Turning right steers left (set by itself when the steering is bound).", s.wheel_steer.invert, [](bool v) {
                app::Get().wheel_steer.invert = v;
                WheelSetupSaved();
            });
            Slider(out, "Wheel Rotation", "The wheel's turn lock to lock, as its own software sets it (Logitech G HUB: 900).", 180, 2880, 90, " Degrees",
                   int(s.wheel_range), [](int v) {
                       app::Get().wheel_range = uint32_t(v);
                       WheelSetupSaved();
                   });
            Slider(out, "Full Steering At", "Degrees of wheel turn for the car's full steering: less turns it sooner (360 suits most).", 90, 1080, 30, " Degrees",
                   int(s.wheel_lock), [](int v) {
                       app::Get().wheel_lock = uint32_t(v);
                       WheelSetupSaved();
                   });
            Slider(out, "Dead Zone", "How much of the steering around the centre reads as straight.", 0, 20, 1, "%", int(s.wheel_deadzone), [](int v) {
                app::Get().wheel_deadzone = uint32_t(v);
                WheelSetupSaved();
            });
            WheelBindRow(out, {kWheelAxis, 1, "Throttle", "Let the pedal rest, A, then press it all the way. A again: unbound."});
            WheelBindRow(out, {kWheelAxis, 2, "Brake", "Let the pedal rest, A, then press it all the way. A again: unbound."});
            WheelBindRow(out, {kWheelAxis, 3, "Clutch", "Let the pedal rest, A, then press it all the way (the game has no clutch)."});
            Slider(out, "Force Feedback", "The wheel's forces overall: centring, the car's grip, kerbs and impacts. 0 off.", 0, 100, 5, "%", int(s.ffb_strength),
                   [](int v) {
                       app::Get().ffb_strength = uint32_t(v);
                       WheelSetupSaved();
                   });
            Slider(out, "Road and Impacts", "How much of the game's rumble (kerbs, gravel, impacts) shakes the wheel.", 0, 100, 5, "%", int(s.ffb_road), [](int v) {
                app::Get().ffb_road = uint32_t(v);
                WheelSetupSaved();
            });
            Button(out, "Test Force Feedback", "A push to the left, then to the right on the next press: the forces' direction and strength.", [] {
                g_push = g_push < 0 ? 1 : -1;
                input::WheelTestForce(0.6f * float(g_push));
            }, g_push < 0 ? "Left" : g_push > 0 ? "Right" : "Push");
            Go(out, "Buttons", "The wheel's buttons for gas, brake, handbrake, weapons, camera, pause and the d-pad.", kWheelButtons);
            Go(out, "Gear Shifter", "An H-pattern shifter's gears.", kWheelGears);
            break;
        }
        case kWheelButtons:
            for (const WheelRow& r : kWheelButtonRows) WheelBindRow(out, r);
            Button(out, "Default Buttons", "A Logitech G920 or G923's buttons. The wheel's hat is the d-pad.", [] {
                Settings& t = app::Get();
                for (int c = 0; c < input::kControls && c < int(t.wheel_buttons.size()); c++) t.wheel_buttons[size_t(c)] = {.button = app::DefaultWheelButton(c)};
                WheelSetupSaved();
            });
            break;
        case kWheelGears: {
            static const char* const kGearLabels[app::kGears] = {"Reverse", "Gear 1", "Gear 2", "Gear 3", "Gear 4", "Gear 5", "Gear 6", "Gear 7"};
            for (int g = 0; g < app::kGears; g++)
                WheelBindRow(out, {kWheelGear, g, kGearLabels[g], "A, then put the shifter in this gear. A again: unbound."});
            Button(out, "No Shifter", "Every gear unbound: the game's own shift buttons change gear.", [] {
                for (Settings::WheelBind& g : app::Get().wheel_gears) g = {};
                WheelSetupSaved();
            });
            break;
        }
        case kAchievements:
            for (const achievements::Status& a : achievements::List()) {
                Item i;
                i.label = a.def->title, i.kind = kModernChoice, i.value = a.unlocked ? "Unlocked" : "Locked";
                i.help = std::string(a.def->description) + " (" + std::to_string(a.def->points) + " points" +
                         (a.unlocked ? ", unlocked " + a.when : std::string()) + ")";
                out.push_back(std::move(i));
            }
            break;
        case kGameplay: {
            Toggle(out, "Tighter Movement", "With the keyboard Bond starts and stops at once instead of easing as with a stick.", &auf::tight_movement);
            Toggle(out, "Aim Down Sights",
                   "The left trigger (right mouse) raises the gun to the middle and zooms in a little, instead of the game's manual aim. Scopes stay as they are.",
                   &auf::aim_down_sights);
            Toggle(out, "Auto-aim", "The game's aim assist, on foot and on mounted guns.", &auf::auto_aim);
            Toggle(out, "Weapon Sway", "The gun trails your turns a little and settles back.", &auf::weapon_sway);
            Toggle(out, "Ragdolls", "Enemies go limp and fall when they die, in place of the death animations.", &auf::ragdolls);
            Toggle(out, "Pickup Physics", "Dropped weapons, ammo and pickups toss, bounce, slide and come to rest; explosions throw them. Off in network play.",
                   &auf::pickup_physics);
            Choice(out, "Hit Effects", "A mark at the crosshair when your shot hits: white a hit, red a kill; with a tick if you like.",
                   {"Off", "Marker", "Marker and Sound"}, int(s.hit_feedback % 3), [](int v) {
                       app::Get().hit_feedback = uint32_t(v);
                       Save();
                   });
            if (s.hit_feedback) Toggle(out, "Damage Numbers", "The damage a hit did, floating up from the hit mark.", &s.damage_numbers);
            Toggle(out, "Hit Flash", "The white flash on screen when Bond is hit; off for those it troubles.", &auf::hit_flash);
            Toggle(out, "3D Sound", "A sound with a wall or a closed door between it and Bond is quieter and muffled, traced through the level as it plays.",
                   &auf::audio_occlusion);
            Toggle(out, "Skip Cutscenes", "A mission's cutscenes are skipped as they start.", &auf::skip_cutscenes);
            Toggle(out, "Photo Mode",
                   "Its key (Key Bindings) stops the game and frees the camera: mouse to look, W A S D to move, Space and C up and down; F12 takes the picture.",
                   &auf::photo_mode);
            Toggle(out, "High Frame Rate", "Paced by the frame cap instead of the console's 30 on foot; driving at 60 steps a second instead of 50.",
                   &auf::uncapped);
            Restart(out);
            Toggle(out, "Discord", "Your Discord status shows the mission you play (with Discord running on this PC).", &s.discord);
            Toggle(out, "Modern Menus", "The menus and the pause drawn anew for the mouse; off for the game's own (its PC Settings' More Settings opens these pages).",
                   &auf::modern_menus);
            Button(out, "Reset to Defaults", "Every row on this page as it first was. Key bindings and graphics are kept.", [] {
                const GameDefaults& d = kGameDefaults;
                auf::tight_movement = d.tight_movement, auf::ragdolls = d.ragdolls, auf::aim_down_sights = d.aim_down_sights;
                auf::weapon_sway = d.weapon_sway, auf::auto_aim = d.auto_aim, auf::hit_flash = d.hit_flash, auf::pickup_physics = d.pickup_physics;
                auf::audio_occlusion = d.audio_occlusion, auf::skip_cutscenes = d.skip_cutscenes, auf::photo_mode = d.photo_mode;
                auf::uncapped = d.uncapped, auf::modern_menus = d.modern_menus;
                const Settings def;
                Settings& t = app::Get();
                t.hit_feedback = def.hit_feedback, t.damage_numbers = def.damage_numbers, t.discord = def.discord;
                Save();
                LOGR("auf: settings: gameplay as it first was");
            });
            break;
        }
        case kNetplay:  // LAN and online play (netplay_page.cpp)
            for (netplay_page::Row& r : netplay_page::Rows()) {
                Item i;
                i.label = r.label, i.value = r.value, i.help = r.help, i.kind = r.kind, i.act = r.act;
                i.press = r.kind == kModernButton;
                out.push_back(std::move(i));
            }
            break;
    }
    return out;
}

// the lines above a page's rows: the wheel's devices and what it reads now, the achievements' count
std::vector<std::string> Texts(int page) {
    std::vector<std::string> t;
    if (page == kWheel) {
        const input::WheelInfo w = input::Wheel();
        if (w.devices.empty()) {
            t.push_back(kNoWheel);
            return t;
        }
        std::string names;
        for (const input::WheelDevice& d : w.devices) names += (names.empty() ? "" : ", ") + d.name + (d.feedback ? " (force feedback)" : "");
        t.push_back(names);
        char b[160];
        std::snprintf(b, sizeof b, "Steering %d degrees    Throttle %d%%    Brake %d%%    Clutch %d%%",
                      int(w.steer * float(app::Get().wheel_lock) * 0.5f), int(w.throttle * 100), int(w.brake * 100), int(w.clutch * 100));
        t.push_back(b);
    } else if (page == kAchievements) {
        const std::vector<achievements::Status> list = achievements::List();
        uint32_t got = 0, points = 0, total = 0;
        for (const achievements::Status& a : list) {
            total += a.def->points;
            if (a.unlocked) got++, points += a.def->points;
        }
        t.push_back(std::to_string(got) + " of " + std::to_string(list.size()) + " unlocked, " + std::to_string(points) + " of " +
                    std::to_string(total) + " points");
    }
    return t;
}

int Rows() { return int(MakeItems(g_page).size()); }

std::string Shorten(const std::string& s) {
    if (s.size() <= 230) return s;
    const size_t at = s.rfind(' ', 226);
    return s.substr(0, at == std::string::npos ? 226 : at) + "...";
}

void OpenPage(int page) {
    EndWaits();
    g_back.push_back({g_page, g_cursor});
    g_page = page;
    g_cursor = 0;
}
}  // namespace

// each game frame (src/bond/pc_extras.cpp): a key or wheel wait whose page has not been shown for half a second (the
// pause left with Start, the game off the page) is let go; else app::KeyCaptureBusy stays on and the keyboard is dead
void Watch() {
    if ((g_key < 0 && !g_wheel) || std::chrono::steady_clock::now() - g_built < std::chrono::milliseconds(500)) return;
    LOGR("auf: settings: a binding's wait let go with its page closed");
    EndWaits();
}

void Reset() {
    EndWaits();
    g_page = kRoot;
    g_back.clear();
    g_cursor = 0;
}

uint32_t PageTag() { return uint32_t(g_page + 1); }

void Build(ModernPage& p, bool) {
    g_built = std::chrono::steady_clock::now();
    // a key or the wheel's control taken since the last frame
    if (uint16_t vk = 0; g_key >= 0 && app::TakeCapturedKey(vk)) {
        Settings& s = app::Get();
        if (g_key < int(s.keys.size())) {
            std::array<uint16_t, 2>& k = s.keys[size_t(g_key)];
            if (vk == 0x1B) k = {0, 0};  // Escape: no keys
            else if (k[0] != vk) k = {vk, k[0]};
            Save();
            LOGR("auf: settings: %s keys %s", input::ControlName(g_key), KeysText(k).c_str());
        }
        g_key = -1;
    }
    if (g_wheel && (app::WheelDetectPoll() || app::WheelDetecting() == app::WheelDetect::None)) g_wheel = nullptr;

    p.texts = Texts(g_page);
    p.agents.clear();
    p.rows.clear();
    p.help.clear();
    p.page += PageTag();
    p.prompt = "~A Select    ~B Back    ~H Change    ~V Move";
    p.title = kTitles[g_page];
    const Items items = MakeItems(g_page);
    for (size_t k = 0; k < items.size(); k++) {
        const Item& it = items[k];
        ModernRow r;
        r.id = uint32_t(k + 1);
        r.kind = it.kind;
        r.label = it.label + (it.restart ? "  (restart)" : "");
        r.value = it.value;
        if (it.kind == kModernSlider) r.slider_value = uint32_t(it.now - it.lo), r.slider_max = uint32_t(it.hi - it.lo);
        p.rows.push_back(r);
    }
    const int n = int(p.rows.size());
    g_cursor = std::min(g_cursor, n - 1);
    if (g_cursor >= 0) {
        const Item& it = items[size_t(g_cursor)];
        p.help = Shorten(it.help + (it.restart && it.help.find("next start") == std::string::npos ? " Applies at the next start." : ""));
    }
    p.focus = g_cursor;
    for (int k = 0; k < n; k++) p.rows[size_t(k)].focused = k == g_cursor;
}

Result Event(uint32_t row, int event, bool paused) {
    constexpr int kLeft = 0, kRight = 1, kUp = 2, kDown = 3, kA = 4, kB = 5;
    // a key binding waits for its key, or the key taken is still held: the presses are not the menu's (B cancels it)
    if (app::KeyCaptureBusy()) {
        if (event == kB && app::KeyCaptureWaiting()) {
            app::EndKeyCapture();
            g_key = -1;
        }
        return kStay;
    }
    const int was = g_cursor;
    if (row == 0 && event != kB) g_cursor = -1;  // the tab strip's own arrows
    else if (row >= 1 && row != kNoRow) g_cursor = int(row) - 1;
    if (g_cursor != was && g_cursor != g_quit_row) g_quit_armed = false;
    if (event == kB) {
        if (g_key >= 0 || g_wheel || g_quit_armed) {  // a wait let go first
            EndWaits();
            return kStay;
        }
        if (g_back.empty()) return kClose;
        g_page = g_back.back().page;
        g_cursor = g_back.back().cursor;
        g_back.pop_back();
        return kStay;
    }
    if (event < kLeft || event > kA) return kStay;
    const int n = std::max(Rows(), 1);
    if (g_cursor < 0) {  // the strip
        if (event == kLeft) return kClose;
        if (event == kDown || event == kA) g_cursor = 0;
        return kStay;
    }
    g_cursor = std::min(g_cursor, n - 1);
    if (event == kUp || event == kDown) {
        if (event == kUp && g_cursor == 0 && paused && g_page == kRoot) g_cursor = -1;
        else g_cursor = (g_cursor + (event == kUp ? n - 1 : 1)) % n;
        if (g_cursor != g_quit_row) g_quit_armed = false;
        return kStay;
    }
    const Items items = MakeItems(g_page);
    if (g_cursor >= int(items.size())) return kStay;
    const Item& it = items[size_t(g_cursor)];
    // left and right from the keys step a value; on a binding or button only a click (a row given) plays it, as A
    if (it.press && event != kA && row == kNoRow) return kStay;
    if (it.go >= 0) {
        if (event == kA || row != kNoRow) OpenPage(it.go);
        return kStay;
    }
    if (it.act) it.act(it.press || event == kA ? 0 : event == kLeft ? -1 : 1);
    return kStay;
}
}  // namespace auf::settings_page
