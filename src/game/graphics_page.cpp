// Graphics, a page of the game's own menus (opened from PC Settings, in the front end and from the pause screen), and
// its three style pages (Enhanced, Toon and Retro options). They are built as PC Settings is, from AV Options'
// records (src/game/pc_settings_page.cpp, nf::BuildPage), so their text, choices, focus, sounds and prompts are the
// game's own. A keeps the page's values and goes back, B goes back without, as on AV Options.
//
// Every row drives a value the renderers use: the quality preset (gpu::ApplyQuality's set: anisotropic filtering,
// FXAA, ambient occlusion, bloom), the shader style and its options (gpu/style.h, drawn by the Vulkan renderer),
// sharpening, render scale and the renderer itself (app::Settings, saved in settings.toml); the style's values in
// graphics.toml. What only Vulkan draws reads "Needs Vulkan" under the Direct3D renderers. Changing a value the
// quality preset sets makes it Custom; choosing a preset sets them all; the style is apart from the preset.
#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "game/menu_page.h"
#include "gpu/style.h"
#include "kernel/kernel.h"

namespace {
using kernel::R32;
using nf::MenuRow;

// the front end's file takes three pages more than its own, not four (a fourth crashed the menu code on its first
// page change): Enhanced, Toon and Retro options share one page, their rows on the same lines, the others hidden
constexpr uint32_t kGfxPage = 0x4000FC20, kStylePage = 0x4000FC30;
constexpr uint32_t kEnhPage = 1, kToonPage = 2, kRetroPage = 3;  // which rows the style page shows
// the Graphics page's rows
constexpr uint32_t kQuality = 0x1000FC21, kStyle = 0x1000FC22, kStyleOptions = 0x1000FC23, kFilter = 0x1000FC24,
                   kAo = 0x1000FC25, kBloom = 0x1000FC26, kAa = 0x1000FC27, kSharpen = 0x1000FC28,
                   kScale = 0x1000FC29, kRenderer = 0x1000FC2A, kPack = 0x1000FC2B, kDefaults = 0x1000FC2C;
// the style pages' rows
constexpr uint32_t kExposure = 0x1000FC31, kTonemap = 0x1000FC32, kContrast = 0x1000FC33, kSaturation = 0x1000FC34,
                   kVignette = 0x1000FC35, kGrain = 0x1000FC36, kSceneSharpen = 0x1000FC37;
constexpr uint32_t kBands = 0x1000FC41, kOutline = 0x1000FC42, kOutlineColor = 0x1000FC43, kEdges = 0x1000FC44,
                   kHalftone = 0x1000FC45, kToonSaturation = 0x1000FC46;
constexpr uint32_t kPixel = 0x1000FC51, kDepth = 0x1000FC52, kDither = 0x1000FC53, kRetroFilter = 0x1000FC54,
                   kSnap = 0x1000FC55, kTint = 0x1000FC56;

const std::vector<MenuRow> kGfxRows = {
    {kQuality, nf::kMenuChoice, "Graphics Quality", "Low to Ultra set the rows below at once; changing one makes it Custom."},
    {kStyle, nf::kMenuChoice, "Shader Style", "The picture's look: the console's own, Enhanced, Toon, Retro or a mod's."},
    {kStyleOptions, nf::kMenuButton, "Style Options", "The chosen style's own settings."},
    {kFilter, nf::kMenuChoice, "Texture Filtering", "How sharp textures stay at an angle: as the game draws them, or anisotropic."},
    {kAo, nf::kMenuChoice, "Ambient Occlusion", "Soft shade in corners and creases, from the scene's depth."},
    {kBloom, nf::kMenuChoice, "Bloom", "A glow around lights and bright surfaces."},
    {kAa, nf::kMenuChoice, "Anti-Aliasing", "Smooths jagged edges: FXAA on the finished picture, or MSAA on the scene."},
    {kSharpen, nf::kMenuChoice, "Sharpening", "Contrast adaptive sharpening of the finished picture."},
    {kScale, nf::kMenuChoice, "Render Scale", "The 3D picture's size against the screen's: lower is faster, higher smoother."},
    {kRenderer, nf::kMenuChoice, "Renderer", "Vulkan draws the styles and effects; changes at the next start."},
    {kPack, nf::kMenuChoice, "Texture Packs", "Pictures in mods/textures drawn in place of the game's textures."},
    {kDefaults, nf::kMenuButton, "Restore Defaults", "Every row on this page and the style options as they first were."},
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
constexpr int16_t kFirstY = 78, kStep = 22;

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
void ShowOptions(X86Context& ctx, uint32_t player, uint32_t id, const Options& o, float v) {
    std::vector<std::pair<const char*, uint32_t>> items;
    for (uint32_t i = 0; i < o.size(); i++) items.push_back({o[i].text, i});
    nf::MenuChoice(ctx, player, id, items, Nearest(o, v));
}
float ReadOption(X86Context& ctx, uint32_t player, uint32_t id, const Options& o) {
    uint32_t i = nf::MenuChoiceValue(ctx, player, id);
    return o[std::min<size_t>(i, o.size() - 1)].value;
}

const Options kOffOn = {{"Off", 0}, {"On", 1}};
const Options kFilters = {{"As the Game", 0}, {"2x", 2}, {"4x", 4}, {"8x", 8}, {"16x", 16}};
const Options kAoLevels = {{"Off", 0}, {"Low", 1}, {"High", 2}};
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

// the anti-aliasing choice: 0 off, 1 FXAA, 2 to 4 MSAA 2x 4x 8x (the Direct3D renderers draw MSAA)
std::vector<std::pair<const char*, uint32_t>> AaItems() {
    std::vector<std::pair<const char*, uint32_t>> v = {{"Off", 0}, {"FXAA", 1}};
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
        static const char* names[] = {"Original (Edited)", "Enhanced (Edited)", "Toon (Edited)", "Retro (Edited)"};
        v.push_back({names[std::clamp(st.mode, 0, 3)], 0xFE});
        shown = 0xFE;
    }
    return v;
}

void EnterGraphics(X86Context& ctx, uint32_t player) {
    const app::Settings& s = app::Get();
    gpu::GraphicsStyle st = gpu::StyleSnapshot();
    g_entered_quality = uint32_t(gpu::MatchQuality(st, s.aniso, s.fxaa));
    nf::MenuChoice(ctx, player, kQuality,
                   {{"Low", 0}, {"Medium", 1}, {"High", 2}, {"Ultra", 3}, {"Custom", 4}}, g_entered_quality);
    if (Vulkan()) {
        uint32_t shown = 0;
        auto items = StyleItems(st, shown);
        nf::MenuChoice(ctx, player, kStyle, items, shown);
        ShowOptions(ctx, player, kAo, kAoLevels, float(st.quality_ao));
        ShowOptions(ctx, player, kBloom, kOffOn, st.quality_bloom ? 1.0f : 0.0f);
    } else {
        nf::MenuChoice(ctx, player, kStyle, {{"Original (Needs Vulkan)", 0xFF}}, 0xFF);
        nf::MenuChoice(ctx, player, kAo, {{"Needs Vulkan", 0xFF}}, 0xFF);
        nf::MenuChoice(ctx, player, kBloom, {{"Needs Vulkan", 0xFF}}, 0xFF);
    }
    ShowOptions(ctx, player, kFilter, kFilters, float(s.aniso));
    uint32_t aa = s.msaa && !Vulkan() ? 1 + std::min<uint32_t>(s.msaa, 3) : s.fxaa ? 1 : 0;
    nf::MenuChoice(ctx, player, kAa, AaItems(), aa);
    ShowOptions(ctx, player, kSharpen, kSharpenLevels, float(s.sharpen));
    ShowOptions(ctx, player, kScale, kScales, float(s.render_percent));
    nf::MenuChoice(ctx, player, kRenderer, {{"Direct3D 12", 0}, {"Direct3D 11", 1}, {"Vulkan", 2}}, s.renderer % 3);
    if (Vulkan()) ShowOptions(ctx, player, kPack, kOffOn, st.texture_pack ? 1.0f : 0.0f);
    else nf::MenuChoice(ctx, player, kPack, {{"Needs Vulkan", 0xFF}}, 0xFF);
    LOGR("nightfire: Graphics page entered (quality %s, style %s)", kQualityNames[g_entered_quality],
         st.preset.empty() ? "edited" : st.preset.c_str());
}

// the page's values into the settings and the style; the style's options pages are opened with it kept
void KeepGraphics(X86Context& ctx, uint32_t player) {
    gpu::GraphicsStyle st = gpu::StyleSnapshot();
    uint32_t quality = nf::MenuChoiceValue(ctx, player, kQuality);
    uint32_t aniso = uint32_t(ReadOption(ctx, player, kFilter, kFilters));
    uint32_t aa = nf::MenuChoiceValue(ctx, player, kAa);
    uint32_t sharpen = uint32_t(ReadOption(ctx, player, kSharpen, kSharpenLevels));
    uint32_t scale = uint32_t(ReadOption(ctx, player, kScale, kScales));
    uint32_t renderer = nf::MenuChoiceValue(ctx, player, kRenderer);
    if (Vulkan()) {
        uint32_t style = nf::MenuChoiceValue(ctx, player, kStyle);
        const auto& presets = gpu::StylePresets();
        if (style < presets.size() && presets[style].name != st.preset) {
            // a preset's look; quality and the texture settings are not part of it
            gpu::GraphicsStyle keep = st;
            st = presets[style].values;
            st.preset = presets[style].name;
            st.quality = keep.quality, st.quality_ao = keep.quality_ao, st.quality_bloom = keep.quality_bloom;
            st.texture_pack = keep.texture_pack, st.dump_textures = keep.dump_textures;
        }
        st.quality_ao = int(ReadOption(ctx, player, kAo, kAoLevels));
        st.quality_bloom = ReadOption(ctx, player, kBloom, kOffOn) > 0;
        st.texture_pack = ReadOption(ctx, player, kPack, kOffOn) > 0;
    }
    bool fxaa = aa == 1;
    uint32_t msaa = aa >= 2 && aa <= 4 ? aa - 1 : 0;
    // a preset chosen here sets its rows, whatever they show; else the rows make the preset (or Custom)
    if (quality != g_entered_quality && quality < uint32_t(gpu::kQualityCustom)) {
        int ao;
        bool bloom;
        gpu::QualityValues(int(quality), aniso, fxaa, ao, bloom);
        if (fxaa) msaa = 0;
        if (Vulkan()) st.quality_ao = ao, st.quality_bloom = bloom;
    }
    st.quality = gpu::MatchQuality(st, aniso, fxaa);
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings& s = app::Get();
        s.aniso = aniso;
        s.fxaa = fxaa;
        if (!Vulkan()) s.msaa = msaa;
        s.sharpen = sharpen;
        s.render_percent = scale;
        if (renderer <= 2) s.renderer = renderer;
        app::Save();
    }
    gpu::RequestStyle(st);
    LOGR("nightfire: Graphics kept: quality %s, style %s, filtering %u, AO %d, bloom %d, FXAA %d, sharpen %u, scale "
         "%u%%, renderer %u, packs %d",
         kQualityNames[st.quality], st.preset.empty() ? "edited" : st.preset.c_str(), aniso, st.quality_ao,
         st.quality_bloom, fxaa, sharpen, scale, renderer, st.texture_pack);
}

void RestoreDefaults(X86Context& ctx, uint32_t player) {
    gpu::GraphicsStyle st;
    gpu::GraphicsStyle cur = gpu::StyleSnapshot();
    st.texture_pack = true;
    st.dump_textures = cur.dump_textures;
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings& s = app::Get();
        app::Settings d;
        s.aniso = d.aniso, s.fxaa = d.fxaa, s.msaa = d.msaa, s.sharpen = d.sharpen, s.render_percent = d.render_percent;
        app::Save();
        st.quality = gpu::MatchQuality(st, s.aniso, s.fxaa);
    }
    gpu::RequestStyle(st);
    // the rows show it (the request is taken at the next frame: the page is filled from the values set here)
    LOGR("nightfire: Graphics restored to defaults");
    (void)ctx, (void)player;
}

// the style pages
void EnterStyle(X86Context& ctx, uint32_t player, uint32_t page) {
    gpu::GraphicsStyle st = gpu::StyleSnapshot();
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
        nf::MenuChoice(ctx, player, kOutlineColor, inks, ink);
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

void KeepStyle(X86Context& ctx, uint32_t player, uint32_t page) {
    gpu::GraphicsStyle st = gpu::StyleSnapshot(), before = st;
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
        st.mode = gpu::kStyleToon;
        st.bands = int(ReadOption(ctx, player, kBands, kBandCounts));
        st.outline = ReadOption(ctx, player, kOutline, kOutlines);
        uint32_t ink = std::min<uint32_t>(nf::MenuChoiceValue(ctx, player, kOutlineColor), std::size(kInks) - 1);
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
    gpu::RequestStyle(st);
    LOGR("nightfire: %s options kept%s", page == kEnhPage ? "Enhanced" : page == kToonPage ? "Toon" : "Retro",
         same ? " (unchanged)" : "");
}

const std::vector<MenuRow>& RowsOf(uint32_t page) {
    return page == kGfxPage ? kGfxRows : page == kEnhPage ? kEnhRows : page == kToonPage ? kToonRows : kRetroRows;
}
bool IsPage(uint32_t id) { return id == kGfxPage || id == kStylePage; }
uint32_t g_style_rows = kEnhPage;  // the style the style page was opened for
// the style page: every style's rows, one after another (their slots put them on the same lines)
std::vector<MenuRow> StylePageRows() {
    std::vector<MenuRow> v = kEnhRows;
    v.insert(v.end(), kToonRows.begin(), kToonRows.end());
    v.insert(v.end(), kRetroRows.begin(), kRetroRows.end());
    return v;
}
// the rows of the styles not shown hidden (0x2B 1), the chosen style's shown (0x2B 0)
void ShowStyleRows(X86Context& ctx, uint32_t player, uint32_t which) {
    for (uint32_t set : {kEnhPage, kToonPage, kRetroPage})
        for (const MenuRow& r : RowsOf(set)) nf::MenuSet(ctx, player, r.id, 0x2B, set == which ? 0 : 1);
}

}  // namespace

namespace nf {

std::vector<std::vector<uint8_t>> GraphicsPages(const std::vector<uint8_t>& av, uint32_t (*record_size)(uint32_t)) {
    std::vector<std::vector<uint8_t>> pages = {BuildPage(av, record_size, kGfxPage, "Graphics", kGfxRows, kFirstY, kStep),
                                               BuildPage(av, record_size, kStylePage, "Style Options", StylePageRows(),
                                                         kFirstY, kStep)};
    for (auto& p : pages)
        if (p.empty()) return {};
    return pages;
}

void OpenGraphicsPage(X86Context& ctx, uint32_t player) {
    MenuGoTo(ctx, player, kGfxPage);
    LOGR("nightfire: Graphics page opened");
}

bool GraphicsPageMessage(X86Context& ctx, uint32_t player, uint32_t widget, uint32_t msg, uint32_t arg) {
    uint32_t id = R32(widget + 0x18);
    if (IsPage(id)) {
        if (msg == 0x4C) {
            if (id == kGfxPage) {
                EnterGraphics(ctx, player);
            } else {
                ShowStyleRows(ctx, player, g_style_rows);
                EnterStyle(ctx, player, g_style_rows);
            }
        }
        if (msg == 0x4B) {
            uint32_t on = arg ? R32(arg + 0x18) : 0;
            if (id == kGfxPage && on == kStyleOptions) {
                // the style chosen here kept, then its page (Original has none)
                KeepGraphics(ctx, player);
                uint32_t chosen = nf::MenuChoiceValue(ctx, player, kStyle);
                const auto& presets = gpu::StylePresets();
                int mode = chosen < presets.size() ? presets[chosen].values.mode : gpu::StyleSnapshot().mode;
                if (!Vulkan() || mode == gpu::kStyleOriginal) {
                    MenuHelp(ctx, player, Vulkan() ? "Original is the console's picture: it has no options."
                                                   : "The styles are drawn by the Vulkan renderer.");
                    return true;
                }
                g_style_rows = mode == gpu::kStyleEnhanced ? kEnhPage : mode == gpu::kStyleToon ? kToonPage : kRetroPage;
                MenuGoTo(ctx, player, kStylePage);
                return true;
            }
            if (id == kGfxPage && on == kDefaults) {
                RestoreDefaults(ctx, player);
                MenuBack(ctx, player);
                return true;
            }
            if (id == kGfxPage) KeepGraphics(ctx, player);
            else KeepStyle(ctx, player, g_style_rows);
            MenuBack(ctx, player);
        }
        return true;
    }
    if (msg == 0x4E)  // focus gained: the row's help line
        for (uint32_t page : {kGfxPage, kEnhPage, kToonPage, kRetroPage})
            for (const MenuRow& r : RowsOf(page))
                if (r.id == id) {
                    MenuHelp(ctx, player, r.help);
                    return false;
                }
    return false;
}

}  // namespace nf
