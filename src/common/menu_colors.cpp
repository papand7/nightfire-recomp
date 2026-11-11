// Settings::menu_colors (blue and silver, after the PC version's menus): the front end's background movies recoloured
// as they are bound, the text, the widgets and the movies' animation left as the console's.
//
// The menus' backgrounds are XMV movies decoded into 640 x 480 linear YUY2 textures (format 0x24, two of them,
// written each frame). Their warm chroma (orange, red, gold: Cr above, Cb below the grey) is turned about the grey to
// a steel blue, pale golds to a blue silver; a pixel already cool is left, so a frame bound twice is not turned back.
// The main menu's movie (page 0x40000002) holds Bond: right of its menu he keeps his colours, only the strong
// orange of the background behind him (more chroma than any skin there) turns. A frame already done (a sparse sum of
// its bytes as it was left) is passed over.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "gpu/movie_pack.h"
#include "gpu/texture.h"
#include "common/nightfire.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {
constexpr uint32_t kYuy2 = 0x24, kWidth = 640, kHeight = 480;
constexpr uint32_t kMainMenu = 0x40000002;  // the front end's page (MenuGeometry)
constexpr int kBondX = 382, kBondRight = 578, kBondEdge = 12, kBondBottom = 414;  // movie pixels: Bond's columns, above y
constexpr float kTurn = 3.40f;                                              // radians about the grey: orange to blue
constexpr float kKeep = 0.9f;                                               // of the chroma kept

std::unordered_map<uint32_t, uint64_t> g_done;  // a movie texture's data: its sum after the last turn

uint64_t Sum(const uint8_t* bytes, uint32_t n) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 1; i < n; i += 509) h = (h ^ bytes[i]) * 1099511628211ull;
    return h;
}

float Smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// chroma about the grey (u, v: Cb and Cr less 128), `amount` of the way to blue; false when it is not warm
bool TurnUV(float& u, float& v, float amount) {
    if (v <= 0 || u >= v * 0.4f) return false;  // not warm: already blue or grey
    float c = std::cos(kTurn) * kKeep, s = std::sin(kTurn) * kKeep;
    float nu = u * c - v * s, nv = u * s + v * c;
    u += (nu - u) * amount, v += (nv - v) * amount;
    return true;
}

// a YUY2 pair's chroma (bytes 1 and 3), `amount` of the way to blue
void Turn(uint8_t* uv, float amount) {
    float u = uv[0] - 128.0f, v = uv[2] - 128.0f;
    if (!TurnUV(u, v, amount)) return;
    uv[0] = uint8_t(std::clamp(u + 128.5f, 0.0f, 255.0f));
    uv[2] = uint8_t(std::clamp(v + 128.5f, 0.0f, 255.0f));
}

// how far a pixel of the main menu's movie turns (movie pixels x, y; its chroma u, v): Bond keeps his colours, only
// the strong orange behind him turns
float BondAmount(float x, float y, float u, float v) {
    if (y >= kBondBottom) return 1;
    float in = Smooth(float(kBondX - kBondEdge), float(kBondX), x) * (1 - Smooth(float(kBondRight), float(kBondRight + kBondEdge), x));
    // the wooden column behind his head (right of his face, above his hand) turned all the same
    in *= 1 - Smooth(484, 494, x) * (1 - Smooth(562, 572, x)) * (1 - Smooth(218, 232, y));
    if (in <= 0) return 1;
    return 1 - in * (1 - Smooth(50, 62, std::sqrt(u * u + v * v)));
}

// the recolouring MenuMovie would do now: 0 none, 1 blue, 3 blue with Bond kept (the main menu)
uint32_t g_mode = 0;
}  // namespace

namespace nf {
const char* collect_movie = nullptr;

// GameInfo::texture_bound: true when the movie's texels were changed
bool MenuMovie(uint32_t header) {
    g_mode = 0;
    if (collect_movie) return false;  // the movie pack collects the game's own colours
    if ((!app::Get().menu_colors && !modern_menus) || !MenuUp()) return false;  // the modern menus are blue always
    uint32_t format = kernel::R32(header + 12), size = kernel::R32(header + 16);
    if ((format >> 8 & 0xFF) != kYuy2 || !size || (size & 0xFFF) + 1 != kWidth || (size >> 12 & 0xFFF) + 1 != kHeight)
        return false;
    uint32_t pitch = ((size >> 24) + 1) * 64, data = mem::VirtualFromPhysical(kernel::R32(header + 4));
    if (pitch < kWidth * 2 || !mem::IsCommitted(data) || !mem::IsCommitted(data + pitch * kHeight - 1)) return false;
    auto* bytes = static_cast<uint8_t*>(kernel::HostPtr(data));
    auto done = g_done.find(data);
    if (done != g_done.end() && done->second == Sum(bytes, pitch * kHeight)) {
        uint32_t page = 0, focused = 0;
        std::vector<MenuWidget> widgets;
        g_mode = !kDriving && MenuGeometry(page, widgets, focused) && page == kMainMenu ? 3 : 1;
        return false;
    }
    uint32_t page = 0, focused = 0;
    std::vector<MenuWidget> widgets;
    bool bond = !kDriving && MenuGeometry(page, widgets, focused) && page == kMainMenu;
    g_mode = bond ? 3 : 1;
    for (uint32_t y = 0; y < kHeight; y++) {
        uint8_t* row = bytes + y * pitch;
        for (uint32_t x = 0; x < kWidth; x += 2) {
            uint8_t* p = row + x * 2;
            const float amount = bond ? BondAmount(float(x), float(y), p[1] - 128.0f, p[3] - 128.0f) : 1.0f;
            if (amount > 0) Turn(p + 1, amount);
        }
    }
    g_done[data] = Sum(bytes, pitch * kHeight);
    return true;
}
uint32_t MenuMovieMode() { return g_mode; }

void CollectMovieFrame(uint32_t header) {
    if (collect_movie) gpu::MovieCollect(header, collect_movie);
}

// an upscaled frame of a movie pack (gpu::MovieShow) turned as MenuMovie turned the game's: each pixel to Cb and Cr
// (BT.601 studio range, as the renderers decode YUY2), turned, and back; Bond's place scaled from the 640 x 480 frame
void RecolorMovie(uint32_t* bgra, uint32_t w, uint32_t h, uint32_t mode) {
    if (!(mode & 1) || !w || !h) return;
    const bool bond = mode & 2;
    const float sx = float(kWidth) / float(w), sy = float(kHeight) / float(h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t& c = bgra[size_t(y) * w + x];
            const float b = float(c & 0xFF), g = float(c >> 8 & 0xFF), r = float(c >> 16 & 0xFF);
            const float Y = 16 + (65.738f * r + 129.057f * g + 25.064f * b) / 256;
            float u = (-37.945f * r - 74.494f * g + 112.439f * b) / 256, v = (112.439f * r - 94.154f * g - 18.285f * b) / 256;
            const float amount = bond ? BondAmount(float(x) * sx, float(y) * sy, u, v) : 1.0f;
            if (amount <= 0 || !TurnUV(u, v, amount)) continue;
            const float k = 1.164383f * (Y - 16);
            auto u8 = [](float f) { return uint32_t(std::clamp(f + 0.5f, 0.0f, 255.0f)); };
            c = (c & 0xFF000000u) | u8(k + 1.596027f * v) << 16 | u8(k - 0.391762f * u - 0.812968f * v) << 8 | u8(k + 2.017232f * u);
        }
}

// NF_TITLE_LOG=1: the big pictures bound while a menu is up, logged once each (to find the title screen's)
void TitleLog(uint32_t header) {
    static const bool on = std::getenv("NF_TITLE_LOG") != nullptr;
    if (!on || !MenuUp()) return;
    gpu::TextureDesc d;
    if (!gpu::Describe(header, d) || d.width < 256 || d.height < 128) return;
    static std::unordered_map<uint32_t, int> seen;
    if (seen[d.data]++) return;
    uint32_t page = 0, focused = 0;
    std::vector<MenuWidget> widgets;
    MenuGeometry(page, widgets, focused);
    LOGR("nightfire: menu picture %08X: format %02X %ux%u%s levels %u (page %08X)", d.data, d.format, d.width,
         d.height, d.linear ? " linear" : "", d.levels, page);
}
// modern menus: the title screen's picture (page 0x40000009: 512 x 512, Bond on its left) and the logos over it and
// the main menu (512 or 256 x 128), A8R8G8B8 swizzled, turned blue in place as the movies are; on the title Bond's
// skin is kept as on the main menu (his rectangle in texels, only the strong orange there turns)
bool TitlePicture(uint32_t header) {
    if (!modern_menus || !MenuUp()) return false;
    uint32_t page = 0, focused = 0;
    std::vector<MenuWidget> widgets;
    if (!MenuGeometry(page, widgets, focused) || (page != 0x40000009 && page != kMainMenu)) return false;
    gpu::TextureDesc d;
    if (!gpu::Describe(header, d) || d.format != 0x06 || d.linear || d.width < 256 || d.width > 512 ||
        (d.height != 128 && d.height != 512))
        return false;
    const uint32_t n = d.width * d.height;
    if (!mem::IsCommitted(d.data) || !mem::IsCommitted(d.data + n * 4 - 1)) return false;
    auto* px = static_cast<uint8_t*>(kernel::HostPtr(d.data));
    auto done = g_done.find(d.data);
    if (done != g_done.end() && done->second == Sum(px, n * 4)) return false;
    const bool bond = d.width == 512 && d.height == 512;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t* p = px + i * 4;  // B G R A
        float b = p[0], g = p[1], r = p[2];
        float y = 0.299f * r + 0.587f * g + 0.114f * b;
        uint8_t uv[3] = {uint8_t(std::clamp(128 + (b - y) * 0.564f, 0.0f, 255.0f)), 0,
                         uint8_t(std::clamp(128 + (r - y) * 0.713f, 0.0f, 255.0f))};
        float amount = 1;
        if (bond) {  // the texel's place: a square's swizzle interleaves x (even bits) and y (odd bits)
            uint32_t x = 0, yy = 0;
            for (int k = 0; k < 9; k++) x |= ((i >> (2 * k)) & 1) << k, yy |= ((i >> (2 * k + 1)) & 1) << k;
            // his face and hand (x 95 to 300, down to the logo) and his collar under it, right of the 007
            float fx = float(x), fy = float(yy);
            float in = std::max(Smooth(88, 100, fx) * (1 - Smooth(296, 308, fx)) * Smooth(14, 24, fy) * (1 - Smooth(222, 232, fy)),
                                Smooth(186, 196, fx) * (1 - Smooth(242, 252, fx)) * Smooth(222, 232, fy) * (1 - Smooth(330, 342, fy)));
            // his skin and shirt: bright and orange (green half the red); the red behind him dark or deeper red
            if (in > 0) amount = 1 - in * Smooth(45, 75, y) * Smooth(0.28f, 0.38f, g / std::max(r, 1.0f));
        }
        if (amount <= 0) continue;
        uint8_t was[3] = {uv[0], uv[1], uv[2]};
        Turn(uv, amount);
        if (uv[0] == was[0] && uv[2] == was[2]) continue;
        float cb = uv[0] - 128.0f, cr = uv[2] - 128.0f;
        p[2] = uint8_t(std::clamp(y + 1.403f * cr, 0.0f, 255.0f));
        p[1] = uint8_t(std::clamp(y - 0.344f * cb - 0.714f * cr, 0.0f, 255.0f));
        p[0] = uint8_t(std::clamp(y + 1.773f * cb, 0.0f, 255.0f));
    }
    if (bond && std::getenv("NF_TITLE_DUMP")) {  // the picture as turned, unswizzled, a TGA
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            std::vector<uint8_t> out(size_t(n) * 4);
            for (uint32_t i = 0; i < n; i++) {
                uint32_t x = 0, yy = 0;
                for (int k = 0; k < 9; k++) x |= ((i >> (2 * k)) & 1) << k, yy |= ((i >> (2 * k + 1)) & 1) << k;
                std::memcpy(&out[(size_t(yy) * 512 + x) * 4], px + i * 4, 4);
            }
            LOGR("nightfire: title picture dumped to %s", std::getenv("NF_TITLE_DUMP"));
            if (FILE* f = std::fopen(std::getenv("NF_TITLE_DUMP"), "wb")) {
                uint8_t head[18] = {0, 0, 2};
                head[12] = 0, head[13] = 2, head[14] = 0, head[15] = 2, head[16] = 32, head[17] = 0x28;
                std::fwrite(head, 1, 18, f);
                std::fwrite(out.data(), 1, out.size(), f);
                std::fclose(f);
            }
        }
    }
    g_done[d.data] = Sum(px, n * 4);
    static int said = 0;
    if (said++ < 4) LOGR("nightfire: modern menus: picture %08X (%ux%u) turned blue", d.data, d.width, d.height);
    return true;
}
}  // namespace nf
