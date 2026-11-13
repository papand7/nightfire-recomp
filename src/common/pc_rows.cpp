// The PC Settings page's rows: the same in bond_eng.xbe's menus and DRIVING.XBE's pause. Its first row chooses the tab; a row whose
// label ends in * applies at the next start. Each changes what the settings pages change (src/common/settings_page.cpp). The games' fonts are Latin-1.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/app.h"
#include "common/auf.h"
#include "common/settings_page.h"

namespace auf {
namespace {
enum Id {
    kRenderScale, kMsaa, kFxaa, kAlphaEdges, kFiltering, kSharpening, kScaleFilter, kSharpText, kWorldTextures, kFrameCounter,
    kDisplayMode, kVsync, kFrameCap, kAspect, kRenderer, kFps60, kFov,
    kMouseSpeed, kInvertMouse, kAutoAim,
    kLanguage, kSkipIntro, kPauseUnfocused, kHitFlash, kMore, kIds
};
const char* const kRowNames[3] = {"Settings", "Param\xE8tres PC", "PC-Einstellungen"};
const char* const kTabLabel[3] = {"Page", "Page", "Seite"};
const char* const kTabNames[3][5] = {{"Graphics", "Picture", "Display", "Controls", "Game"},
                                     {"Graphismes", "Image", "Affichage", "Commandes", "Jeu"},
                                     {"Grafik", "Bild", "Anzeige", "Steuerung", "Spiel"}};
const char* const kLabels[3][kIds] = {
    {"Render Scale", "MSAA", "Edge Smoothing", "Smooth Alpha Edges", "Texture Filtering", "Sharpening", "Scaling Filter", "Sharp Text and HUD", "Enhanced Textures", "Frame Counter",
     "Display Mode", "Vertical Sync", "Frame Rate Cap", "Aspect Ratio *", "Renderer *", "High Frame Rate *", "Field of View",
     "Mouse Sensitivity", "Invert Mouse", "Auto-aim",
     "Language *", "Skip Intro Movies", "Pause in Background", "Hit Flash", "More Settings"},
    {"\xC9" "chelle de rendu", "MSAA", "Lissage des bords", "Bords alpha lisses", "Filtrage des textures", "Nettet\xE9", "Filtre d'\xE9" "chelle", "Texte et ATH nets", "Textures am\xE9lior\xE9" "es", "Compteur d'images",
     "Mode d'affichage", "Synchro verticale", "Limite d'images", "Format d'image *", "Moteur de rendu *", "Images/s \xE9lev\xE9" "es *", "Champ de vision",
     "Sensibilit\xE9 souris", "Inverser la souris", "Vis\xE9" "e auto",
     "Langue *", "Passer les intros", "Pause en arri\xE8re-plan", "Flash si touch\xE9", "Plus d'options"},
    {"Renderskalierung", "MSAA", "Kantengl\xE4ttung", "Weiche Alpha-Kanten", "Texturfilterung", "Sch\xE4rfe", "Skalierungsfilter", "Scharfer Text und HUD", "Bessere Texturen", "Bildz\xE4hler",
     "Anzeigemodus", "V-Sync", "Bildratenlimit", "Seitenverh\xE4ltnis *", "Renderer *", "Hohe Bildrate *", "Sichtfeld",
     "Mausempfindlichkeit", "Maus invertieren", "Zielhilfe",
     "Sprache *", "Intros \xFC" "berspringen", "Pause im Hintergrund", "Treffer-Blitz", "Weitere Optionen"}};
const char* const kBilinear[3] = {"Bilinear", "Bilin\xE9" "aire", "Bilinear"};
const char* const kSharp[3] = {"Sharp", "Net", "Scharf"};
const char* const kOff[3] = {"Off", "Non", "Aus"};
const char* const kOn[3] = {"On", "Oui", "An"};
const char* const kGames[3] = {"Game's", "Du jeu", "Vom Spiel"};
const char* const kNoCap[3] = {"None", "Aucune", "Keins"};
const char* const kModes[3][3] = {{"Fullscreen", "Borderless", "Window"}, {"Plein \xE9" "cran", "Sans bordure", "Fen\xEA" "tre"}, {"Vollbild", "Rahmenlos", "Fenster"}};
const char* const kAspects[3][4] = {{"Screen's", "4:3", "16:9", "Fill"}, {"De l'\xE9" "cran", "4:3", "16:9", "Remplir"}, {"Vom Bildschirm", "4:3", "16:9", "F\xFCllen"}};

struct Tab {
    int count;
    int ids[kPcRows - 1];
};
const Tab kTabs[5] = {{7, {kRenderScale, kMsaa, kFxaa, kAlphaEdges, kFiltering, kSharpening, kScaleFilter}},
                      {4, {kSharpText, kWorldTextures, kFrameCounter, kMore}},
                      {7, {kDisplayMode, kVsync, kFrameCap, kAspect, kRenderer, kFps60, kFov}},
                      {4, {kMouseSpeed, kInvertMouse, kAutoAim, kMore}},
                      {5, {kLanguage, kSkipIntro, kPauseUnfocused, kHitFlash, kMore}}};
int g_tab = 0;

// the tab's rows; More Settings (the host's settings pages over the game's menus) only on foot, where they are drawn
int Count() {
    const Tab& t = kTabs[g_tab];
    return kImage == kBond || t.ids[t.count - 1] != kMore ? t.count : t.count - 1;
}
int IdOf(int row) { return kTabs[g_tab].ids[(row - 1) % Count()]; }
template <class T, size_t N>
T Next(const T (&order)[N], T now, int dir) {
    int at = 0;
    for (size_t i = 0; i < N; i++)
        if (order[i] == now) at = int(i);
    return order[(at + int(N) + dir) % int(N)];
}

std::string Plain(int id, int lang) {
    const app::Settings& s = app::Get();
    char b[32] = "";
    auto onoff = [&](bool v) { return std::string(v ? kOn[lang] : kOff[lang]); };
    switch (id) {
        case kRenderScale: std::snprintf(b, sizeof b, "%u%%", s.render_percent); break;
        case kMsaa: {
            const char* names[] = {kOff[lang], "2x", "4x", "8x"};
            return names[s.msaa % 4];
        }
        case kFxaa: return s.smaa ? "SMAA" : s.fxaa ? "FXAA" : kOff[lang];
        case kAlphaEdges: return onoff(s.alpha_coverage);
        case kScaleFilter: return s.scale_filter % 2 ? kSharp[lang] : kBilinear[lang];
        case kWorldTextures: return onoff(s.world_textures);
        case kSharpening:
            if (!s.sharpen) return kOff[lang];
            std::snprintf(b, sizeof b, "%u%%", s.sharpen);
            break;
        case kFiltering:
            if (!s.aniso) return kGames[lang];
            std::snprintf(b, sizeof b, "%ux", s.aniso);
            break;
        case kSharpText: return onoff(s.sharp_2d);
        case kFrameCounter: return onoff(s.show_fps);
        case kDisplayMode: return kModes[lang][int(s.display_mode) % 3];
        case kVsync: return onoff(s.vsync);
        case kFrameCap:
            if (!s.fps_cap) return kNoCap[lang];
            std::snprintf(b, sizeof b, "%u", s.fps_cap);
            break;
        case kAspect: return kAspects[lang][s.aspect % 4];
        case kRenderer: return s.renderer % 2 ? "Direct3D 11" : "Direct3D 12";
        case kFps60: return onoff(uncapped);
        case kFov: std::snprintf(b, sizeof b, "%u%%", fov_percent); break;
        case kMouseSpeed: std::snprintf(b, sizeof b, "%u%%", s.mouse_sensitivity); break;
        case kInvertMouse: return onoff(s.mouse_invert_y);
        case kAutoAim: return onoff(auto_aim);
        case kLanguage: return s.language == 3 ? "Deutsch" : s.language == 4 ? "Fran\xE7" "ais" : "English";
        case kSkipIntro: return onoff(skip_intro);
        case kHitFlash: return onoff(hit_flash);
        case kPauseUnfocused: return onoff(s.pause_unfocused);
        default: break;
    }
    return b;
}
}  // namespace

const char* PcTabName() {
    static const char* const kTab[3] = {"SETTINGS", "R\xC9GLAGES", "EINSTELLUNGEN"};  // on the pause's strip (Latin-1, as the game's text)
    return kTab[Language() % 3];
}
const char* PcRowName(int lang) { return kRowNames[lang % 3]; }
int PcRows() { return Count() + 1; }
const char* PcLabel(int row, int lang) { return row ? kLabels[lang % 3][IdOf(row)] : kTabLabel[lang % 3]; }

// the arrows are for the mouse: a click on the left one lowers (PcClickDir)
std::string PcValue(int row, int lang) {
    lang %= 3;
    const std::string v = row ? Plain(IdOf(row), lang) : kTabNames[lang][g_tab];
    return v.empty() ? v : "< " + v + " >";
}

int PcClickDir(float value_x) {
    float x, y;
    return (PointerButtons() & 1) && PointerAt(x, y) && x < value_x + 22 ? -1 : 1;
}

int Language() {
    const uint32_t l = app::Get().language;  // GameInfo::languages' numbers; XBE_LANGUAGE to test one
    const char* e = std::getenv("XBE_LANGUAGE");
    const uint32_t v = e && *e ? uint32_t(std::atoi(e)) : l;
    return v == 4 ? 1 : v == 3 ? 2 : 0;
}

void PcStep(int row, int dir) {
    if (!row) {
        g_tab = (g_tab + 5 + dir) % 5;
        return;
    }
    app::Settings& s = app::Get();
    switch (IdOf(row)) {
        case kRenderScale: s.render_percent = uint32_t(std::clamp(int(s.render_percent) + dir * 25, 25, 200)); break;
        case kMsaa: s.msaa = (s.msaa + 4 + uint32_t(dir)) % 4; break;
        case kFxaa: {  // off, FXAA, SMAA, as the settings window's Edge smoothing
            const int edge = ((s.smaa ? 2 : s.fxaa ? 1 : 0) + 3 + dir) % 3;
            s.fxaa = edge == 1;
            s.smaa = edge == 2;
            break;
        }
        case kAlphaEdges: s.alpha_coverage = !s.alpha_coverage; break;
        case kScaleFilter: s.scale_filter = (s.scale_filter + 1) % 2; break;
        case kWorldTextures: s.world_textures = !s.world_textures; break;
        case kSharpening: s.sharpen = uint32_t(std::clamp(int(s.sharpen) + dir * 10, 0, 100)); break;
        case kFiltering: {
            const uint32_t order[] = {0, 2, 4, 8, 16};
            s.aniso = Next(order, s.aniso, dir);
            break;
        }
        case kSharpText: s.sharp_2d = !s.sharp_2d; break;
        case kFrameCounter: s.show_fps = !s.show_fps; break;
        case kDisplayMode:
            s.display_mode = app::DisplayMode((int(s.display_mode) + 3 + dir) % 3);
            app::Save();
            app::ApplyDisplay();
            return;
        case kVsync: s.vsync = !s.vsync; break;
        case kFrameCap: {
            const uint32_t order[] = {30, 60, 120, 144, 240, 0};
            s.fps_cap = Next(order, s.fps_cap, dir);
            break;
        }
        case kAspect: s.aspect = (s.aspect + 4 + uint32_t(dir)) % 4; break;
        case kRenderer: s.renderer = (s.renderer + 1) % 2; break;
        case kFps60: uncapped = !uncapped; break;
        case kFov: fov_percent = uint32_t(std::clamp(int(fov_percent) + dir * 5, 50, 150)); break;
        case kMouseSpeed: s.mouse_sensitivity = uint32_t(std::clamp(int(s.mouse_sensitivity) + dir * 10, 10, 500)); break;
        case kInvertMouse: s.mouse_invert_y = !s.mouse_invert_y; break;
        case kAutoAim: auto_aim = !auto_aim; break;
        case kLanguage: {
            const uint32_t order[] = {1, 3, 4};
            s.language = Next(order, s.language, dir);
            break;
        }
        case kSkipIntro: skip_intro = !skip_intro; break;
        case kHitFlash: hit_flash = !hit_flash; break;
        case kPauseUnfocused: s.pause_unfocused = !s.pause_unfocused; break;
        case kMore:  // the host's settings pages over the game's own (src/bond/modern_menu_frame.cpp)
            if (kImage == kBond) {
                settings_page::Reset();
                settings_forced = true;
            }
            return;
        default: return;
    }
    app::Save();
}
}  // namespace auf
