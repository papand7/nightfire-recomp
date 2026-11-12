// The PC settings in the modern menus (settings_page.h). A category's rows are made afresh from the settings each time they are
// read or pressed, so what the F1 window changes shows here at once. Each row is a toggle or choice (left, right and A step it),
// a slider (left and right) or a button; a row that applies at the next start says so.
#include "common/settings_page.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/auf.h"

namespace auf::settings_page {
namespace {
using app::Settings;

struct Item {
    std::string label, help, value;
    int kind = kModernButton;
    int lo = 0, hi = 0, now = 0;  // a slider
    bool restart = false;
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
void Button(Items& out, const char* label, const char* help, std::function<void()> run) {
    Item i;
    i.label = label, i.help = help ? help : "";
    i.act = [run](int d) {
        if (d >= 0) run();
    };
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

enum Category { kDisplay, kGraphics, kEffects, kRayTracing, kControls, kGameplay, kCategories };
const char* const kNames[kCategories] = {"Display", "Graphics", "Effects", "Ray Tracing", "Controls", "Gameplay"};
const char* const kBlurbs[kCategories] = {
    "Window or fullscreen, resolution, frame rate, vertical sync, aspect ratio and the frame counters.",
    "Render scale, filtering, edge smoothing, the FSR 2 upscaler, textures and movies.",
    "Colour grading, bloom, fog, ambient occlusion, depth of field, motion blur and the shader styles.",
    "Traced occlusion, reflections, shadows, bounced and volumetric light (needs a graphics card with ray queries).",
    "Mouse look, the keyboard, controller setup and the keys for photo mode, restarts and weapons.",
    "Language, aim and view options, hit feedback and the game's own extras."};

// which of the game's option table's rows are the controls' (the rest are gameplay's)
bool IsControlOption(const std::string& key) {
    static const char* const keys[] = {"photo_key", "restart_key", "weapon_keys", "previous_weapon_key", "controller_scheme", "tight_movement"};
    return std::any_of(std::begin(keys), std::end(keys), [&](const char* k) { return key == k; });
}

void GameOption(Items& out, const app::Option& o) {
    const char* name = o.label ? o.label : o.key;
    const std::string help = o.help ? o.help : "";
    const std::string key = o.key;
    if (o.flag) {
        Toggle(out, name, o.help, o.flag);
    } else if (o.value && o.choices) {
        std::vector<std::string> names;
        for (int k = 0; o.choices[k]; k++) names.push_back(o.choices[k]);
        uint32_t* p = o.value;
        Choice(out, name, o.help, names, int(*p), [p](int v) {
            *p = uint32_t(v);
            Save();
        });
    } else if (o.value && key == "fov_percent") {
        Slider(out, name, o.help, 50, 150, 5, "%", o.value);
    } else {
        return;
    }
    if (help.find("next start") != std::string::npos) Restart(out);
}

Items MakeItems(int cat) {
    Items out;
    Settings& s = app::Get();
    switch (cat) {
        case kDisplay: {
            static std::vector<std::string> monitors = app::Monitors();
            if (monitors.size() > 1) {
                std::vector<std::string> names{"Primary display"};
                names.insert(names.end(), monitors.begin(), monitors.end());
                Choice(out, "Monitor", "The display the game uses.", names, int(s.monitor), [](int v) {
                    app::Get().monitor = uint32_t(v);
                    Save();
                    app::ApplyDisplay();
                });
            }
            Choice(out, "Display mode", "Fullscreen, a borderless window the size of the screen, or a window.", {"Fullscreen", "Borderless window", "Window"},
                   int(s.display_mode), [](int v) {
                       app::Get().display_mode = app::DisplayMode(v);
                       Save();
                       app::ApplyDisplay();
                   });
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
                Choice(out, "Resolution", "The window's or screen's size; Desktop is the screen's own.", names, cur, [modes](int v) {
                    Settings& t = app::Get();
                    t.width = v ? modes[size_t(v - 1)].width : 0;
                    t.height = v ? modes[size_t(v - 1)].height : 0;
                    Save();
                    app::ApplyDisplay();
                });
            }
            Toggle(out, "Vertical sync", "Waits for the screen's refresh: no tearing, a little more delay.", &s.vsync);
            static const uint32_t caps[] = {30, 60, 120, 144, 240, 0};
            Choice(out, "Frame rate cap", "The game sets its own pace; the cap only limits it further.", {"30", "60", "120", "144", "240", "Unlimited"},
                   IndexOf(caps, s.fps_cap, 1), [](int v) {
                       app::Get().fps_cap = caps[v];
                       Save();
                   });
            Choice(out, "Aspect ratio", "16:9 and the two that fill a wide screen apply at once; to or from 4:3 at the next start (the game reads its TV setting when it boots).",
                   {"From the screen's shape", "4:3", "16:9 (widescreen)", "Fill the screen (ultrawide, 16:10)"}, int(s.aspect % 4), [](int v) {
                       app::Get().aspect = uint32_t(v);
                       Save();
                   });
            Restart(out);
            Toggle(out, "Show frames a second", "The frame counter in a corner.", &s.show_fps);
            Toggle(out, "Performance overlay", "A corner panel: frames a second, the frame time and a graph of the last two seconds, the size the 3D scene is drawn at.", &s.perf_overlay);
            static const uint32_t keys[] = {0x7B, 0x7A, 0x79, 0x78, 0x2C, 0x13};
            Choice(out, "Screenshot key", "Saves the frame as a PNG in the screenshots folder.", {"F12", "F11", "F10", "F9", "Print Screen", "Pause"},
                   IndexOf(keys, s.screenshot_key), [](int v) {
                       app::Get().screenshot_key = keys[v];
                       Save();
                   });
            Toggle(out, "Pause when the window is not in front", "The game and its sound stop while another window is in front, and carry on when you return.", &s.pause_unfocused);
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
            Choice(out, "Graphics quality", "Sets texture filtering, edge smoothing, ambient occlusion and bloom together. Changing one of them makes it Custom.",
                   {"Low", "Medium", "High", "Ultra", "Custom"}, q, [q](int v) {
                       if (q == 4 && v == 3) v = 0;  // from Custom, the presets only
                       if (v > 3) v = 0;
                       Settings& t = app::Get();
                       const Preset& p = presets[v];
                       t.aniso = p.aniso, t.fxaa = p.edge == 1, t.smaa = p.edge == 2, t.ambient_occlusion = p.ao, t.bloom = p.bloom;
                       Save();
                   });
            Slider(out, "Render scale", "Percent of the screen; applies at once. Above 100% also smooths edges.", 25, 200, 5, "%", &s.render_percent);
            static const uint32_t anisos[] = {0, 2, 4, 8, 16};
            Choice(out, "Anisotropic filtering", "Sharper textures at a slant.", {"Game's", "2x", "4x", "8x", "16x"}, IndexOf(anisos, s.aniso), [](int v) {
                app::Get().aniso = anisos[v];
                Save();
            });
            Choice(out, "MSAA", "Smooths the edges of the 3D scene; applies at once.", {"Off", "2x", "4x", "8x"}, int(s.msaa % 4), [](int v) {
                app::Get().msaa = uint32_t(v);
                Save();
            });
            Toggle(out, "Smooth alpha edges", "With MSAA on: softens fences, leaves and other cut-out textures, which MSAA alone misses.", &s.alpha_coverage);
            Choice(out, "Edge smoothing", "On the finished frame, HUD included. SMAA keeps more detail than FXAA, and costs a little more.", {"Off", "FXAA", "SMAA"},
                   s.smaa ? 2 : s.fxaa ? 1 : 0, [](int v) {
                       app::Get().fxaa = v == 1;
                       app::Get().smaa = v == 2;
                       Save();
                   });
            Toggle(out, "Temporal AA", "Jitters 3D draws and reprojects history. Fast-moving geometry and the weapon or HUD can ghost. Not used while an upscaler is on.", &s.temporal_aa);
            Choice(out, "Upscaler", "Draws the 3D scene smaller and rebuilds it over frames, the HUD kept sharp; replaces Temporal AA and SMAA while on.", {"Off", "AMD FSR 2"},
                   int(s.upscaler % 2), [](int v) {
                       app::Get().upscaler = uint32_t(v);
                       Save();
                   });
            if (s.upscaler) {
                Choice(out, "Upscale quality", "How much smaller the scene is drawn before FSR 2 rebuilds it.",
                       {"Quality (1.5x)", "Balanced (1.7x)", "Performance (2x)", "Ultra Performance (3x)"}, int(s.upscale_quality % 4), [](int v) {
                           app::Get().upscale_quality = uint32_t(v);
                           Save();
                       });
                Slider(out, "Upscale sharpness", "FSR 2's own sharpening (RCAS).", 0, 100, 5, "%", &s.upscale_sharpness);
            }
            Toggle(out, "Automatic exposure", "Meters scene brightness and eases exposure over time with an SDR tone curve.", &s.auto_exposure);
            Choice(out, "Scaling filter", "How the frame reaches the screen's size when render scale is not 100%.", {"Bilinear", "Sharp"}, int(s.scale_filter % 2), [](int v) {
                app::Get().scale_filter = uint32_t(v);
                Save();
            });
            Toggle(out, "Enhanced world textures", "Doubles the 3D scene's textures in the background as they appear. Uses more video memory; can hitch as new scenery appears.", &s.world_textures);
            Toggle(out, "HD texture pack", "Loads matching PNGs from mods/textures beside the game executable. Texture packs are not included.", &s.texture_pack);
            Toggle(out, "Upscale movies", "Sharpens movie frames with edge-safe Lanczos scaling before they are drawn.", &s.movie_upscale);
            Toggle(out, "Movies at 4:3", "On a wide screen the movies stay at their 4:3 shape in the middle instead of stretching.", &s.movie_43);
            if (app::kGame.sharpen_2d) Toggle(out, "Sharp text and HUD", "The HUD's, menus' and text's pictures enlarged with sharp edges.", &s.sharp_2d);
            Toggle(out, "Low latency", "Holds one finished frame instead of several: less input lag, but a slow frame costs more.", &s.low_latency);
            Restart(out);
            Toggle(out, "Hide the HUD and menus", "Photo mode's: every 2D draw of the game (the HUD, its menus) is left out until this is turned off again; not saved.", &s.hide_2d);
            break;
        }
        case kEffects: {
            Slider(out, "Exposure", "Adjusts screen brightness; it does not add HDR range.", 50, 150, 5, "%", &s.exposure);
            Slider(out, "Contrast", nullptr, 50, 150, 5, "%", &s.contrast);
            Slider(out, "Saturation", nullptr, 0, 200, 5, "%", &s.saturation);
            Slider(out, "Color temperature", "Cool to warm; 0 is neutral.", -100, 100, 5, "", s.temperature, [](int v) {
                app::Get().temperature = int32_t(v);
                Save();
            });
            Slider(out, "Bloom", "A soft glow around bright pixels in the finished frame.", 0, 100, 5, "%", &s.bloom);
            Slider(out, "Light shafts", "Screen-space rays from bright scene regions.", 0, 100, 5, "%", &s.light_shafts);
            Slider(out, "Atmospheric fog", "Distance fog tinted by the sampled scene sky. The game's own fog remains.", 0, 100, 5, "%", &s.atmospheric_fog);
            Toggle(out, "Ambient occlusion", "Darkens corners and nearby surfaces. Uses ray queries when supported, with screen-space depth AO as a fallback.", &s.ambient_occlusion);
            if (s.ambient_occlusion)
                Slider(out, "Occlusion radius", "How far each surface looks for what darkens it: wider reaches round larger shapes.", 25, 300, 25, "%", &s.ao_radius);
            Toggle(out, "Depth of field", "Blurs surfaces away from the focus distance using scene depth. The first-person weapon may blur too.", &s.depth_of_field);
            if (s.depth_of_field) {
                Slider(out, "Focus distance", nullptr, 1, 100, 1, " m", &s.focus_distance);
                Slider(out, "Depth blur", nullptr, 0, 12, 1, " px", &s.dof_blur);
            }
            Toggle(out, "Motion blur", "Blurs the scene along camera movement. First-person weapon blur is not separated yet.", &s.motion_blur);
            if (s.motion_blur) {
                Slider(out, "Motion blur strength", nullptr, 0, 150, 5, "%", &s.motion_blur_strength);
                Slider(out, "Motion blur limit", nullptr, 1, 16, 1, " px", &s.motion_blur_limit);
            }
            Slider(out, "Vignette", "Darkens the edges of the finished frame.", 0, 100, 5, "%", &s.vignette);
            Slider(out, "Film grain", "Animated fine noise on the finished frame.", 0, 100, 5, "%", &s.film_grain);
            Choice(out, "Color style", "Screen-space styles only. Enhanced Color adjusts contrast and saturation; Toon uses color edges, not game geometry.",
                   {"Original", "Enhanced Color", "Toon (Screen)", "Retro", "Noir"}, int(s.color_style), [](int v) {
                       app::Get().color_style = uint32_t(v);
                       Save();
                   });
            Slider(out, "Sharpening", "Contrast adaptive sharpening of the finished frame.", 0, 100, 5, "%", &s.sharpen);
            for (int i = 0; i < app::kFxRayTracing; i++) Fx(out, i);  // the shader styles
            break;
        }
        case kRayTracing:
            for (int i = app::kFxRayTracing; i < app::kFxCount; i++) Fx(out, i);
            break;
        case kControls: {
            Toggle(out, "Keyboard plays as controller 1", nullptr, &s.keyboard);
            if (app::kGame.mouse_look) {
                Toggle(out, "Mouse moves the right stick", "The mouse looks and aims.", &s.mouse_look);
                Toggle(out, "Invert mouse up and down", nullptr, &s.mouse_invert_y);
                Slider(out, "Mouse sensitivity", nullptr, 10, 1000, 10, "%", &s.mouse_sensitivity);
            }
            for (const app::Option& o : app::kGame.options)
                if (IsControlOption(o.key)) GameOption(out, o);
            Button(out, "Reset keys to default", "Every console input back to its default keys.", [] {
                Settings& t = app::Get();
                if (t.keys.size() == size_t(input::kControls))
                    for (int c = 0; c < input::kControls; c++) t.keys[size_t(c)] = app::DefaultKeys(c);
                Save();
            });
            Button(out, "Reset controller to default", "Every console input back to the pad button of its own.", [] {
                Settings& t = app::Get();
                if (t.pad_map.size() == size_t(input::kControls))
                    for (int c = 0; c < input::kControls; c++) t.pad_map[size_t(c)] = uint8_t(c);
                Save();
            });
            Button(out, "Key bindings and wheel (F1 window)", "Bind each key and controller button, and set up a steering wheel, in the settings window.", [] { app::ToggleOverlay(); });
            break;
        }
        case kGameplay: {
            if (!app::kGame.languages.empty()) {
                static const char* const names[] = {"", "English", "Japanese", "Deutsch", "Francais", "Espanol", "Italiano", "Korean", "Chinese", "Portugues"};
                std::vector<std::string> list;
                int cur = 0;
                for (size_t k = 0; k < app::kGame.languages.size(); k++) {
                    const uint32_t l = app::kGame.languages[k];
                    list.push_back(l < std::size(names) ? names[l] : "?");
                    if (l == s.language) cur = int(k);
                }
                Choice(out, "Language", "Applies at the next start.", list, cur, [](int v) {
                    app::Get().language = app::kGame.languages[size_t(v)];
                    Save();
                });
                Restart(out);
            }
            Choice(out, "Hit feedback", "A mark at the crosshair when your shot hits: white a hit, red a kill.", {"Off", "Marker", "Marker and sound"},
                   int(s.hit_feedback % 3), [](int v) {
                       app::Get().hit_feedback = uint32_t(v);
                       Save();
                   });
            if (s.hit_feedback) Toggle(out, "Damage numbers", "The damage of each hit, floating up from the mark.", &s.damage_numbers);
            for (const app::Option& o : app::kGame.options)
                if (!IsControlOption(o.key)) GameOption(out, o);
            Toggle(out, "Discord status", "Shows the mission you play on your Discord profile while Discord runs on this PC.", &s.discord);
            break;
        }
    }
    return out;
}

// the categories shown: ray tracing only where the graphics card can
std::vector<int> Categories() {
    std::vector<int> c;
    for (int k = 0; k < kCategories; k++)
        if (k != kRayTracing || app::ray_query_ok) c.push_back(k);
    return c;
}

int g_cat = -1;    // the category open, -1 the list of them
int g_cursor = 0;  // the row, -1 the pause's tab strip
const char* const kWindowName = "Settings window (F1)";

int Rows() { return g_cat < 0 ? int(Categories().size()) + 1 : int(MakeItems(g_cat).size()); }

std::string Shorten(const std::string& s) {
    if (s.size() <= 230) return s;
    const size_t at = s.rfind(' ', 226);
    return s.substr(0, at == std::string::npos ? 226 : at) + "...";
}
}  // namespace

void Reset() {
    g_cat = -1;
    g_cursor = 0;
}

uint32_t PageTag() { return uint32_t(g_cat + 2); }

void Build(ModernPage& p, bool) {
    p.texts.clear();
    p.agents.clear();
    p.rows.clear();
    p.help.clear();
    p.page += PageTag();
    p.prompt = "~A Select    ~B Back    ~H Change    ~V Move";
    int n = 0;
    if (g_cat < 0) {
        p.title = "Settings";
        const std::vector<int> cats = Categories();
        for (size_t k = 0; k < cats.size(); k++) {
            ModernRow r;
            r.id = uint32_t(k + 1);
            r.label = kNames[cats[k]];
            p.rows.push_back(r);
        }
        ModernRow r;
        r.id = uint32_t(cats.size() + 1);
        r.label = kWindowName;
        p.rows.push_back(r);
        n = int(p.rows.size());
        g_cursor = std::min(g_cursor, n - 1);
        if (g_cursor >= 0)
            p.help = g_cursor < int(cats.size()) ? kBlurbs[cats[size_t(g_cursor)]]
                                                 : "Every setting, the key bindings and a steering wheel's, in the settings window.";
    } else {
        p.title = kNames[g_cat];
        const Items items = MakeItems(g_cat);
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
        n = int(p.rows.size());
        g_cursor = std::min(g_cursor, n - 1);
        if (g_cursor >= 0) {
            const Item& it = items[size_t(g_cursor)];
            p.help = Shorten(it.help + (it.restart && it.help.find("next start") == std::string::npos ? " Applies at the next start." : ""));
        }
    }
    p.focus = g_cursor;
    for (int k = 0; k < n; k++) p.rows[size_t(k)].focused = k == g_cursor;
}

Result Event(uint32_t row, int event, bool paused) {
    constexpr int kLeft = 0, kRight = 1, kUp = 2, kDown = 3, kA = 4, kB = 5;
    if (row == 0 && event != kB) g_cursor = -1;  // the tab strip's own arrows
    else if (row >= 1 && row != kNoRow) g_cursor = int(row) - 1;
    if (event == kB) {
        if (g_cat < 0) return kClose;
        const std::vector<int> cats = Categories();
        const auto at = std::find(cats.begin(), cats.end(), g_cat);
        g_cursor = at == cats.end() ? 0 : int(at - cats.begin());
        g_cat = -1;
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
    if (event == kUp) {
        if (g_cursor == 0 && paused) g_cursor = -1;
        else g_cursor = (g_cursor + n - 1) % n;
        return kStay;
    }
    if (event == kDown) {
        g_cursor = (g_cursor + 1) % n;
        return kStay;
    }
    if (g_cat < 0) {
        if (event == kLeft) return kStay;
        const std::vector<int> cats = Categories();
        if (g_cursor >= int(cats.size())) {
            app::ToggleOverlay();
            return kStay;
        }
        g_cat = cats[size_t(g_cursor)];
        g_cursor = 0;
        return kStay;
    }
    const Items items = MakeItems(g_cat);
    if (g_cursor < int(items.size()) && items[size_t(g_cursor)].act) items[size_t(g_cursor)].act(event == kLeft ? -1 : event == kRight ? 1 : 0);
    return kStay;
}
}  // namespace auf::settings_page
