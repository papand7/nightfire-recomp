// Clean text: the fonts drawn again eight times larger with smooth edges, for the renderers (gpu::SetReplacement).
//
// Both exes' fonts are pixel art with next to no soft edge (default.xbe's 512x64 A8R8G8B8, swizzled; Driving.xbe's
// 256x108 LIN_A8), so enlarged by interpolation (the runtime's sharp 2D) their curves and diagonals keep a stepped
// rim. Here a glyph's shape is rebuilt instead: the mask (alpha half or more) is enlarged four times by Scale2x twice,
// which keeps corners and straight strokes and turns 45 degree stairs into diagonals; a Gaussian half a source texel
// wide over that irons out the stairs of shallower slopes (Scale2x alone left bumps on them: Harry's screenshot)
// without closing a glyph's one-texel gaps; the field is taken twice larger again and cut at its middle a pixel
// wide, which gives the edge its antialiasing. default.xbe's is drawn from a real font instead where its slots are
// known (DrawFont). The colour is the nearest opaque texel's. The title's picture wins over the runtime's sharpened
// one; the guest bytes stay as they are (a font changed in memory drops it, and it is made again at the next bind).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/nightfire.h"
#include "core/log.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../third_party/stb/stb_truetype.h"

namespace nf {
// the fonts' letters drawn from Sour Gummy Black (a rounded font) instead of the game's own shapes (the setting,
// src/common/game_info.cpp)
bool rounded_font = false;
extern const unsigned char kFontTtf[];  // src/common/font_data.cpp
extern const size_t kFontTtfSize;
namespace {

constexpr int kScale = 8;      // Scale2x twice, then the field twice
constexpr float kSigma = 2.0f;  // the Gaussian's, in Scale2x's texels (half a source texel)
constexpr int kReach = 6;       // texels each side it takes

struct Font {
    uint32_t format, w, h;
};
// default.xbe's and Driving.xbe's (src/game, src/driving)
constexpr Font kFonts[] = {{0x06, 512, 64}, {0x1F, 256, 108}};

// Scale2x (EPX) on a mask: each texel four, a corner taking its two neighbours' value where they agree and the
// others differ
std::vector<uint8_t> Scale2x(const std::vector<uint8_t>& m, int w, int h) {
    auto at = [&](int x, int y) { return m[size_t(std::clamp(y, 0, h - 1)) * w + size_t(std::clamp(x, 0, w - 1))]; };
    std::vector<uint8_t> out(size_t(w) * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t p = at(x, y), a = at(x, y - 1), b = at(x + 1, y), c = at(x - 1, y), d = at(x, y + 1);
            uint8_t e0 = p, e1 = p, e2 = p, e3 = p;
            if (c == a && c != d && a != b) e0 = a;
            if (a == b && a != c && b != d) e1 = b;
            if (d == c && d != b && c != a) e2 = c;
            if (b == d && b != a && d != c) e3 = d;
            size_t o = size_t(y * 2) * (w * 2) + size_t(x * 2);
            out[o] = e0, out[o + 1] = e1, out[o + size_t(w * 2)] = e2, out[o + size_t(w * 2) + 1] = e3;
        }
    return out;
}

// a 0/1 mask blurred by the Gaussian (separable; outside the mask is clear)
std::vector<float> Blur(const std::vector<uint8_t>& m, int w, int h) {
    float k[2 * kReach + 1], ks = 0;
    for (int i = -kReach; i <= kReach; i++) ks += k[i + kReach] = std::exp(-float(i * i) / (2 * kSigma * kSigma));
    for (float& v : k) v /= ks;
    std::vector<float> rows(size_t(w) * h), out(size_t(w) * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float s = 0;
            for (int i = -kReach; i <= kReach; i++)
                if (x + i >= 0 && x + i < w) s += k[i + kReach] * m[size_t(y) * w + size_t(x + i)];
            rows[size_t(y) * w + x] = s;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float s = 0;
            for (int j = -kReach; j <= kReach; j++)
                if (y + j >= 0 && y + j < h) s += k[j + kReach] * rows[size_t(y + j) * w + x];
            out[size_t(y) * w + x] = s;
        }
    return out;
}

// a field `s` times larger (bilinear) cut at 0.5 a pixel wide: the edge's coverage
std::vector<uint8_t> Cut(const std::vector<float>& f, int w, int h, int s) {
    auto at = [&](int x, int y) { return f[size_t(std::clamp(y, 0, h - 1)) * w + size_t(std::clamp(x, 0, w - 1))]; };
    const int ow = w * s, oh = h * s;
    auto field = [&](int x, int y) {
        float u = (x + 0.5f) / s - 0.5f, v = (y + 0.5f) / s - 0.5f;
        int x0 = int(std::floor(u)), y0 = int(std::floor(v));
        float fx = u - x0, fy = v - y0;
        return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
               (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
    };
    std::vector<uint8_t> out(size_t(ow) * oh);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            float c = field(x, y), gx = (field(x + 1, y) - field(x - 1, y)) / 2;
            float gy = (field(x, y + 1) - field(x, y - 1)) / 2;
            float g = std::sqrt(gx * gx + gy * gy);
            float a = g > 1e-4f ? (c - 0.5f) / g + 0.5f : (c >= 0.5f ? 1.0f : 0.0f);
            out[size_t(y) * ow + x] = uint8_t(std::clamp(a, 0.0f, 1.0f) * 255 + 0.5f);
        }
    return out;
}

bool DrawFont(const std::vector<uint8_t>& mask, int w, int h, std::vector<uint8_t>& alpha);
bool DrawEaFont(uint32_t data, int w, int h, std::vector<uint8_t>& alpha);

// the font's picture kScale times larger (B8G8R8A8 rows)
std::vector<uint32_t> Rebuild(const std::vector<uint32_t>& bgra, int w, int h, uint32_t data) {
    std::vector<uint8_t> mask(size_t(w) * h);
    for (size_t i = 0; i < mask.size(); i++) mask[i] = (bgra[i] >> 24) >= 128;
    std::vector<uint8_t> m4 = Scale2x(Scale2x(mask, w, h), w * 2, h * 2);
    std::vector<uint8_t> alpha = Cut(Blur(m4, w * 4, h * 4), w * 4, h * 4, kScale / 4);
    if (!rounded_font) {
        static bool said = false;
        if (!said) said = true, LOGR("nightfire: the game's own font, its edges smoothed (rounded_font off)");
    } else if (w == 512 && h == 64 && DrawFont(mask, w, h, alpha)) {
        static bool said = false;
        if (!said) said = true, LOGR("nightfire: default.xbe's font drawn from Sour Gummy Black");
    }
    if (rounded_font && kDriving && DrawEaFont(data, w, h, alpha))
        LOGR("nightfire: Driving.xbe's font %08X (%dx%d) drawn from Sour Gummy Black", data, w, h);
    // each texel's colour: its own where it shows, else an opaque neighbour's (the glyph's), else white
    std::vector<uint32_t> colour(size_t(w) * h, 0xFFFFFF);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t best = 0, c = 0xFFFFFF;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    uint32_t p = bgra[size_t(ny) * w + nx], a = p >> 24;
                    if (a > best || (a == best && dx == 0 && dy == 0)) best = a, c = p & 0xFFFFFF;
                }
            colour[size_t(y) * w + x] = c;
        }
    const int ow = w * kScale, oh = h * kScale;
    std::vector<uint32_t> out(size_t(ow) * oh);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++)
            out[size_t(y) * ow + x] =
                colour[size_t(y / kScale) * w + x / kScale] | uint32_t(alpha[size_t(y) * ow + x]) << 24;
    return out;
}

// rounded_font: default.xbe's 512x64 font as a real font (Sour Gummy Black, Harry's pick: third_party/fonts): its glyphs lie in three
// rows, each a run of columns with a clear one between; the first row is ASCII from '!' to ']', the second '`', a to z
// and Latin-1 letters, the third more of them (the slots holding ligatures, quotes and a box keep the rebuilt mask).
// Each known slot is cleared and the character drawn in it from the font: one size for all (the game's H's height),
// on each row's baseline (the bottom of its H, x and a-grave), centred in the slot, narrowed to fit it and made
// smaller where it would leave its row's band (the game samples no further); one that would be much smaller (the
// accented capitals, c-cedilla) keeps the game's glyph, rebuilt.
constexpr int kRowGlyphs[3] = {63, 62, 25};
constexpr uint16_t kRow1[62] = {0,      '`',    'a',    'b',    'c',    'd',    'e',    'f',    'g',    'h',
                                'i',    'j',    'k',    'l',    'm',    'n',    'o',    'p',    'q',    'r',
                                's',    't',    'u',    'v',    'w',    'x',    'y',    'z',    '{',    0,
                                '}',    0,      0x20AC, 0,      0,      0,      0,      0,      0,      0x2013,
                                0x2014, 0x2122, 0xA1,   0x152,  0xA9,   0xAE,   0xBF,   0xC0,   0xC1,   0xC2,
                                0xC4,   0xC5,   0xC8,   0xC9,   0xCA,   0xCD,   0xCE,   0xD1,   0xD3,   0xD6,
                                0xD9,   0xDA};
constexpr uint16_t kRow2[25] = {0xDC, 0xDF, 0xE0, 0xE1, 0xE2, 0xE4, 0xE5, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC,
                                0xED, 0xEE, 0xEF, 0xF1, 0xF2, 0xF3, 0xF4, 0xF6, 0xF9, 0xFA, 0xFB, 0xFC};

struct Slot {
    int x0, x1;  // inclusive, texels
    int top, bottom;  // the glyph's own rows
};
struct Row {
    int y0, y1;  // the band, inclusive
    std::vector<Slot> slots;
};

// the rows and their slots, from the mask; empty when the font is not laid out as default.xbe's
std::vector<Row> Layout(const std::vector<uint8_t>& mask, int w, int h) {
    std::vector<Row> rows;
    for (int y = 0; y < h;) {
        auto lit = [&](int yy) {
            for (int x = 0; x < w; x++)
                if (mask[size_t(yy) * w + x]) return true;
            return false;
        };
        if (!lit(y)) {
            y++;
            continue;
        }
        Row r{y, y, {}};
        while (r.y1 + 1 < h && lit(r.y1 + 1)) r.y1++;
        y = r.y1 + 1;
        for (int x = 0; x < w;) {
            auto col = [&](int xx) {
                for (int yy = r.y0; yy <= r.y1; yy++)
                    if (mask[size_t(yy) * w + xx]) return true;
                return false;
            };
            if (!col(x)) {
                x++;
                continue;
            }
            Slot s{x, x, r.y1, r.y0};
            while (s.x1 + 1 < w && col(s.x1 + 1)) s.x1++;
            for (int yy = r.y0; yy <= r.y1; yy++)
                for (int xx = s.x0; xx <= s.x1; xx++)
                    if (mask[size_t(yy) * w + xx]) s.top = std::min(s.top, yy), s.bottom = std::max(s.bottom, yy);
            r.slots.push_back(s);
            x = s.x1 + 1;
        }
        rows.push_back(std::move(r));
    }
    if (rows.size() != 3) return {};
    for (int i = 0; i < 3; i++)
        if (int(rows[size_t(i)].slots.size()) != kRowGlyphs[i]) return {};
    return rows;
}

uint32_t Codepoint(int row, int i) {
    if (row == 0) return i <= ']' - '!' ? uint32_t('!' + i) : 0;
    return row == 1 ? kRow1[i] : kRow2[i];
}

// the slots known drawn from the font into `alpha` (w * kScale wide): true when the font could be read
bool DrawFont(const std::vector<uint8_t>& mask, int w, int h, std::vector<uint8_t>& alpha) {
    std::vector<Row> rows = Layout(mask, w, h);
    if (rows.empty()) return false;
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, kFontTtf, stbtt_GetFontOffsetForIndex(kFontTtf, 0))) return false;
    // one size: the game's H as tall as the font's
    const Slot& gh = rows[0].slots[size_t('H' - '!')];
    int hx0, hy0, hx1, hy1;
    if (!stbtt_GetCodepointBox(&font, 'H', &hx0, &hy0, &hx1, &hy1) || hy1 <= 0) return false;
    float scale = float(gh.bottom - gh.top + 1) * kScale / float(hy1);
    const int baseline_slot[3] = {'H' - '!', 25 /* x */, 2 /* a-grave */};
    const int ow = w * kScale;
    for (int r = 0; r < 3; r++) {
        const Row& row = rows[size_t(r)];
        float base = float(row.slots[size_t(baseline_slot[r])].bottom + 1) * kScale;  // the baseline, in output pixels
        for (int i = 0; i < int(row.slots.size()); i++) {
            uint32_t cp = Codepoint(r, i);
            if (!cp || !stbtt_FindGlyphIndex(&font, int(cp))) continue;
            const Slot& s = row.slots[size_t(i)];
            int bx0, by0, bx1, by1;
            float sy = scale, slot_w = float(s.x1 - s.x0 + 1) * kScale;
            // smaller where it would leave its band (the font's ascenders and accents are taller than the game's)
            stbtt_GetCodepointBitmapBox(&font, int(cp), scale, scale, &bx0, &by0, &bx1, &by1);
            float above = base - float(row.y0 * kScale), below = float((row.y1 + 1) * kScale) - base;
            if (by0 < 0 && -by0 > above) sy = std::min(sy, scale * above / float(-by0));
            if (by1 > 0 && by1 > below) sy = std::min(sy, scale * below / float(by1));
            if (sy < scale * 0.75f) continue;  // much smaller than its neighbours (an accented capital): the game's
            stbtt_GetCodepointBitmapBox(&font, int(cp), sy, sy, &bx0, &by0, &bx1, &by1);
            float sx = sy;
            if (bx1 - bx0 > slot_w) sx = sy * slot_w / float(bx1 - bx0);  // narrowed to fit its slot
            stbtt_GetCodepointBitmapBox(&font, int(cp), sx, sy, &bx0, &by0, &bx1, &by1);
            int gw = bx1 - bx0, gh2 = by1 - by0;
            if (gw <= 0 || gh2 <= 0) continue;
            std::vector<uint8_t> g(size_t(gw) * gh2);
            stbtt_MakeCodepointBitmap(&font, g.data(), gw, gh2, gw, sx, sy, int(cp));
            // the slot cleared (its band), the glyph placed centred on it and on the row's baseline
            int cx0 = s.x0 * kScale, cx1 = (s.x1 + 1) * kScale, cy0 = row.y0 * kScale, cy1 = (row.y1 + 1) * kScale;
            for (int y = cy0; y < cy1; y++)
                for (int x = cx0; x < cx1; x++) alpha[size_t(y) * ow + x] = 0;
            int left = cx0 + int(std::lround((slot_w - float(gw)) / 2)), top = int(std::lround(base)) + by0;
            for (int y = 0; y < gh2; y++)
                for (int x = 0; x < gw; x++) {
                    int ox = left + x, oy = top + y;
                    if (ox < cx0 || ox >= cx1 || oy < cy0 || oy >= cy1) continue;
                    uint8_t& o = alpha[size_t(oy) * ow + ox];
                    o = std::max(o, g[size_t(y) * gw + x]);
                }
        }
    }
    return true;
}

// Driving.xbe's font (EA's) keeps its glyph table in the same file, before its kerning pairs and the pixels: records
// of 12 bytes in code order (code u16, width, height, x u16, y u16 on the sheet, advance, x and y offsets from the
// pen and the line's top), the last the code 0xFF, then the kerning pairs' count and the pairs (4 bytes each), then a
// few bytes to the pixels. Each glyph's rectangle is cleared and its character drawn into it from the font: one size
// (the game's H's height), on the line's baseline (the bottom of its H), centred, fitted to the rectangle; one that
// would be much smaller keeps the game's glyph, rebuilt, as do the codes EA gave its ligatures (^ | ~ and the thorn).
struct EaGlyph {
    uint32_t code;
    int w, h, x, y, yoff;
};

std::vector<EaGlyph> EaTable(uint32_t data, int tw, int th) {
    std::vector<EaGlyph> out;
    for (uint32_t back = 16; back < 0x8000; back += 4) {
        uint32_t p = data - back;
        if (!mem::IsCommitted(p) || kernel::G<uint16_t>(p) != 0xFF) continue;
        uint32_t pairs = kernel::R32(p + 12), end = p + 16 + pairs * 4;
        if (pairs > 0x4000 || end > data || data - end > 256) continue;
        // the records back from 0xFF, codes falling, to the space
        uint32_t last = 0x100;
        for (uint32_t r = p; mem::IsCommitted(r) && r > data - 0x10000; r -= 12) {
            uint32_t code = kernel::G<uint16_t>(r);
            int w = kernel::G<uint8_t>(r + 2), h = kernel::G<uint8_t>(r + 3);
            int x = kernel::G<uint16_t>(r + 4), y = kernel::G<uint16_t>(r + 6), yoff = kernel::G<int8_t>(r + 10);
            if (code >= last || code < 0x20 || x + w > tw || y + h > th) break;
            out.push_back({code, w, h, x, y, yoff});
            last = code;
            if (code == 0x20) return out;
        }
        out.clear();
    }
    return out;
}

bool DrawEaFont(uint32_t data, int w, int h, std::vector<uint8_t>& alpha) {
    std::vector<EaGlyph> table = EaTable(data, w, h);
    const EaGlyph* big_h = nullptr;
    for (const EaGlyph& g : table)
        if (g.code == 'H') big_h = &g;
    if (!big_h || big_h->h <= 0) return false;
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, kFontTtf, stbtt_GetFontOffsetForIndex(kFontTtf, 0))) return false;
    int hx0, hy0, hx1, hy1;
    if (!stbtt_GetCodepointBox(&font, 'H', &hx0, &hy0, &hx1, &hy1) || hy1 <= 0) return false;
    float scale = float(big_h->h) * kScale / float(hy1);
    int line_base = big_h->yoff + big_h->h;  // texels from the line's top
    const int ow = w * kScale;
    for (const EaGlyph& g : table) {
        if (g.code <= 0x20 || g.w <= 0 || g.h <= 0 || !stbtt_FindGlyphIndex(&font, int(g.code))) continue;
        if (g.code == '^' || g.code == '|' || g.code == '~' || g.code == 0xDE) continue;  // EA's ffi, ffl, ff, fl
        float base = float(line_base - g.yoff) * kScale;  // the baseline from the rectangle's top, output pixels
        float above = base, below = float(g.h) * kScale - base, slot_w = float(g.w) * kScale;
        int bx0, by0, bx1, by1;
        stbtt_GetCodepointBitmapBox(&font, int(g.code), scale, scale, &bx0, &by0, &bx1, &by1);
        float sy = scale;
        if (by0 < 0 && -by0 > above) sy = std::min(sy, scale * above / float(-by0));
        if (by1 > 0 && by1 > below) sy = std::min(sy, scale * below / float(by1));
        if (sy < scale * 0.75f) continue;  // much smaller than its neighbours: the game's
        stbtt_GetCodepointBitmapBox(&font, int(g.code), sy, sy, &bx0, &by0, &bx1, &by1);
        float sx = sy;
        if (bx1 - bx0 > slot_w) sx = sy * slot_w / float(bx1 - bx0);
        stbtt_GetCodepointBitmapBox(&font, int(g.code), sx, sy, &bx0, &by0, &bx1, &by1);
        int gw = bx1 - bx0, gh = by1 - by0;
        if (gw <= 0 || gh <= 0) continue;
        std::vector<uint8_t> bm(size_t(gw) * gh);
        stbtt_MakeCodepointBitmap(&font, bm.data(), gw, gh, gw, sx, sy, int(g.code));
        int cx0 = g.x * kScale, cx1 = (g.x + g.w) * kScale, cy0 = g.y * kScale, cy1 = (g.y + g.h) * kScale;
        for (int y = cy0; y < cy1; y++)
            for (int x = cx0; x < cx1; x++) alpha[size_t(y) * ow + x] = 0;
        int left = cx0 + int(std::lround((slot_w - float(gw)) / 2)), top = cy0 + int(std::lround(base)) + by0;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                int ox = left + x, oy = top + y;
                if (ox < cx0 || ox >= cx1 || oy < cy0 || oy >= cy1) continue;
                uint8_t& o = alpha[size_t(oy) * ow + ox];
                o = std::max(o, bm[size_t(y) * gw + x]);
            }
    }
    (void)h;
    return true;
}

std::mutex g_lock;
std::unordered_map<uint32_t, uint64_t> g_made;  // texture data -> the guest bytes' FNV-1a it was made from

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

}  // namespace

// GameInfo::texture_bound (after the button pictures): a font bound without its picture gets one
void FontPicture(uint32_t header) {
    gpu::TextureDesc d;
    if (!gpu::Describe(header, d)) return;
    bool font = false;
    for (const Font& f : kFonts) font |= d.format == f.format && d.width == f.w && d.height == f.h;
    // Driving.xbe: any alpha texture (A8, LIN_A8) with EA's glyph table behind it (the HUD's objectives, timer, ammo)
    bool ea = kDriving && (d.format == 0x19 || d.format == 0x1F) && d.width <= 512 && d.height <= 512;
    if (!font && !ea) return;
    if (auto r = gpu::FindReplacement(d.data); r && !r->sharpened) return;  // made already, still holds
    std::lock_guard<std::mutex> hold(g_lock);
    uint32_t bytes = gpu::LevelSize(d, 0);
    if (!mem::IsCommitted(d.data) || !mem::IsCommitted(d.data + bytes - 1)) return;
    const auto* guest = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
    uint64_t hash = Fnv(guest, bytes);
    auto it = g_made.find(d.data);
    if (it != g_made.end() && it->second == hash) return;  // made from these bytes already (or no font)
    g_made[d.data] = hash;
    if (!font && EaTable(d.data, int(d.width), int(d.height)).empty()) return;  // an alpha texture, not a font
    std::vector<uint32_t> px;
    if (!gpu::DecodeBgra(d, 0, guest, px)) return;
    std::vector<uint32_t> big = Rebuild(px, int(d.width), int(d.height), d.data);
    // NF_FONT_DUMP=folder: each font as built (font_<texture>.tga), to look at
    if (const char* dir = std::getenv("NF_FONT_DUMP"))
        if (FILE* f = std::fopen((std::string(dir) + "/font_" + std::to_string(d.data) + ".tga").c_str(), "wb")) {
            uint32_t w = d.width * kScale, h = d.height * kScale;
            uint8_t head[18] = {0, 0, 2};
            head[12] = uint8_t(w), head[13] = uint8_t(w >> 8), head[14] = uint8_t(h), head[15] = uint8_t(h >> 8);
            head[16] = 32, head[17] = 0x28;
            std::fwrite(head, 1, sizeof head, f);
            std::fwrite(big.data(), 4, big.size(), f);
            std::fclose(f);
        }
    gpu::SetReplacement(d.data, bytes, d.width * kScale, d.height * kScale, std::move(big));
    LOGR("nightfire: the font %08X (%ux%u) drawn again %u times larger with smooth edges", d.data, d.width, d.height,
         kScale);
}
}  // namespace nf
