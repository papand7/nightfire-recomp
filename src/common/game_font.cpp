// The game's own menu fonts for the modern menus (src/common/modern_menu.cpp): read from the disc at startup, before
// the window's ImGui atlas is built, so they draw everywhere (the menus' font is loaded by the game only in the front end).
//
// The archive assets\1p_<lang>.viv is BIGF: "BIGF", the total size (LE), the count and the header's size (BE), then per file
// its offset and size (BE) and its name. maps\frontend.xbc is a chain of chunks: an 8-byte name, the size, a float version,
// 0, 0xCDCDCDCD and the padded size twice (32 bytes), then the data, the next chunk after the padded size. "restable" holds
// the resources: a count, then from +0x10 16-byte entries {name offset, data offset, size, hash} from the chunk's data;
// fonts/<name>.xbf is a font: "TNOF", the glyph count at +0xA, the glyphs' offset at +0xC, the line height less 2 at +0x18;
// a glyph is 0x14 bytes: the character (lo, hi), w, h, the advance less 2, x and y offsets (s16), u0 v0 u1 v1 in 1/16
// texel. "restxtrs" holds the textures: {1.0, count, 0x14, the headers' offset, the data's size}, count name hashes
// (h = h * 0x1003F + tolower(c) of "fonts/<name>"), 0x80-byte DDSURFACEDESC2 headers (+8 height, +0xC width, +0x10
// pitch, +0x7C the pixels' offset from the end of the headers): linear A8R8G8B8, white, 4-bit alpha.
// The glyphs become ImGui custom glyphs of a font of their own; the atlas builder is wrapped so they are painted into
// the atlas whenever it is built (the SDK adds its fonts after this, which builds it again).
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/auf.h"
#include "core/log.h"
#include "app/app.h"
#include "kernel/disc.h"
#include "kernel/kernel.h"

namespace {
// names compared without case (_stricmp, which is Windows' own)
bool SameName(const char* a, const char* b) {
    for (; *a && *b; a++, b++)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}
}  // namespace

namespace {
uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint32_t BE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t U16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
int16_t S16(const uint8_t* p) { int16_t v; std::memcpy(&v, p, 2); return v; }

uint32_t Hash(const std::string& s) {
    uint32_t h = 0;
    for (unsigned char c : s) h = h * 0x1003F + uint32_t(std::tolower(c));
    return h;
}

struct Glyph {
    int code, w, h, advance, x, y, u, v;
    int rect = -1;  // its custom rectangle in the atlas
};
struct GameFont {
    std::string name;
    ImFont* font = nullptr;
    int line = 0, atlas_w = 0, atlas_h = 0, pitch = 0;
    std::vector<uint8_t> pixels;  // A8R8G8B8
    std::vector<Glyph> glyphs;
};
std::vector<GameFont> g_fonts;

// a part of a game file before boot mounts D: (the window is up first): the disc image FindGame mounted, else the game's
// folder; the kernel's path once it is mounted
bool ReadRange(const std::string& rel, uint64_t offset, uint32_t len, std::vector<uint8_t>& out) {
    if (kernel::ReadGameFileRange("D:\\" + rel, offset, len, out)) return true;
    if (const kernel::Disc* disc = kernel::MountedDisc()) {
        const kernel::Disc::Entry* e = disc->Find(rel);
        if (!e || e->dir || offset >= e->size) return false;
        const uint32_t n = uint32_t(std::min<uint64_t>(len, e->size - offset));
        out.resize(n);
        return disc->Read(*e, offset, out.data(), n) == n;
    }
    std::string host = rel;
#ifndef _WIN32
    std::replace(host.begin(), host.end(), '\\', '/');  // the disc's separators, a folder's off Windows
#endif
    std::ifstream f(app::GameDir() / host, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const uint64_t size = uint64_t(f.tellg());
    if (offset >= size) return false;
    const uint32_t n = uint32_t(std::min<uint64_t>(len, size - offset));
    out.resize(n);
    f.seekg(std::streamoff(offset));
    return bool(f.read(reinterpret_cast<char*>(out.data()), n));
}

// a file inside a BIGF archive on the disc or in the game's folder (the archive's directory read first, then only the
// file's own bytes)
bool ReadFromViv(const std::string& viv, const std::string& file, std::vector<uint8_t>& out) {
    std::vector<uint8_t> head;
    if (!ReadRange(viv, 0, 16, head) || head.size() < 16 || std::memcmp(head.data(), "BIGF", 4)) return false;
    const uint32_t count = BE32(&head[8]), size = BE32(&head[12]);
    std::vector<uint8_t> dir;
    if (!ReadRange(viv, 16, size, dir)) return false;
    size_t at = 0;
    for (uint32_t i = 0; i < count && at + 8 < dir.size(); i++) {
        const uint32_t off = BE32(&dir[at]), len = BE32(&dir[at + 4]);
        at += 8;
        const char* name = reinterpret_cast<const char*>(&dir[at]);
        const size_t n = strnlen(name, dir.size() - at);
        std::string s(name, n);
        at += n + 1;
        std::replace(s.begin(), s.end(), '/', '\\');
        if (SameName(s.c_str(), file.c_str())) return ReadRange(viv, off, len, out);
    }
    return false;
}

bool LoadFont(const std::vector<uint8_t>& xbc, const std::string& name, GameFont& f) {
    const uint8_t *table = nullptr, *textures = nullptr;
    for (size_t at = 0; at + 32 <= xbc.size();) {
        const uint8_t* c = &xbc[at];
        if (U32(c + 20) != 0xCDCDCDCDu) break;
        const std::string chunk(reinterpret_cast<const char*>(c), strnlen(reinterpret_cast<const char*>(c), 8));
        if (chunk == "restable") table = c + 32;
        if (chunk == "restxtrs") textures = c + 32;
        at += 32 + U32(c + 24);
    }
    if (!table || !textures) return false;
    // the glyphs
    const std::string xbf = "fonts/" + name + ".xbf";
    const uint8_t* data = nullptr;
    uint32_t data_size = 0;
    for (uint32_t i = 0, n = U32(table); i < n; i++) {
        const uint8_t* e = table + 0x10 + i * 16;
        const char* rn = reinterpret_cast<const char*>(table + U32(e));
        if (SameName(rn, xbf.c_str())) data = table + U32(e + 4), data_size = U32(e + 8);
    }
    if (!data || data_size < 0x20 || std::memcmp(data, "TNOF", 4)) return false;
    f.line = int32_t(U32(data + 0x18)) + 2;
    const uint32_t glyphs = U16(data + 0xA), go = U32(data + 0xC);
    if (go + glyphs * 0x14 > data_size) return false;
    for (uint32_t i = 0; i < glyphs; i++) {
        const uint8_t* r = data + go + i * 0x14;
        Glyph g{r[0] | r[1] << 8, U16(r + 2), U16(r + 4), U16(r + 6) + 2, S16(r + 8), S16(r + 10), U16(r + 12) >> 4, U16(r + 14) >> 4};
        if (g.code >= 0x20 && g.code <= 0xFF && !(g.code >= 0x80 && g.code < 0xA0)) f.glyphs.push_back(g);
    }
    // the texture
    const uint32_t count = U32(textures + 4), headers = U32(textures + 12), want = Hash("fonts/" + name);
    for (uint32_t i = 0; i < count; i++) {
        if (U32(textures + 20 + i * 4) != want) continue;
        const uint8_t* d = textures + headers + i * 0x80;
        f.atlas_h = int(U32(d + 8)), f.atlas_w = int(U32(d + 0xC)), f.pitch = int(U32(d + 0x10));
        const uint8_t* px = textures + headers + count * 0x80 + U32(d + 0x7C);
        if (px + size_t(f.pitch) * f.atlas_h > xbc.data() + xbc.size()) return false;
        f.pixels.assign(px, px + size_t(f.pitch) * f.atlas_h);
        return f.atlas_w > 0 && f.atlas_h > 0;
    }
    return false;
}

// the atlas built by ImGui's own builder, then the game's glyphs painted into their rectangles (alpha, white)
const ImFontBuilderIO* g_inner = nullptr;
bool Build(ImFontAtlas* atlas) {
    if (!g_inner->FontBuilder_Build(atlas)) return false;
    if (!atlas->TexPixelsAlpha8) return true;
    for (GameFont& f : g_fonts)
        for (Glyph& g : f.glyphs) {
            if (g.rect < 0) continue;
            const ImFontAtlasCustomRect* r = atlas->GetCustomRectByIndex(g.rect);
            for (int y = 0; y < g.h; y++)
                for (int x = 0; x < g.w; x++) {
                    const int sx = g.u + x, sy = g.v + y;
                    if (sx >= f.atlas_w || sy >= f.atlas_h) continue;
                    atlas->TexPixelsAlpha8[(r->Y + y) * atlas->TexWidth + r->X + x] = f.pixels[size_t(sy) * f.pitch + sx * 4 + 3];
                }
        }
    return true;
}
}  // namespace

namespace auf {
// the game's font by name ("univers13bold_57"), from the disc; null when it cannot be read. Before the atlas is built.
::ImFont* GameMenuFont(const char* name) {
    for (GameFont& f : g_fonts)
        if (f.name == name) return f.font;
    static std::vector<uint8_t> xbc;
    if (xbc.empty()) {
        static const char* const kLang[] = {"uk", "fre", "ger", "us"};
        for (const char* l : kLang)
            if (ReadFromViv(std::string("assets\\1p_") + l + ".viv", "maps\\frontend.xbc", xbc)) break;
        if (xbc.empty()) {
            LOGR("auf: the game's menu fonts were not found on the disc: the menus keep the system's");
            return nullptr;
        }
    }
    GameFont f;
    f.name = name;
    if (!LoadFont(xbc, name, f)) {
        LOGR("auf: the game's font %s was not read", name);
        return nullptr;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (!g_inner) {
        g_inner = io.Fonts->FontBuilderIO ? io.Fonts->FontBuilderIO : ImFontAtlasGetBuilderForStbTruetype();
        static ImFontBuilderIO wrapped;
        wrapped.FontBuilder_Build = Build;
        io.Fonts->FontBuilderIO = &wrapped;
    }
    // a font of its own: the default's space alone, the game's glyphs as custom ones (the later of a code wins)
    ImFontConfig cfg;
    cfg.SizePixels = float(f.line);
    static const ImWchar kSpace[] = {0x20, 0x20, 0};
    cfg.GlyphRanges = kSpace;
    std::snprintf(cfg.Name, sizeof cfg.Name, "%s", name);
    f.font = io.Fonts->AddFontDefault(&cfg);
    for (Glyph& g : f.glyphs)
        g.rect = io.Fonts->AddCustomRectFontGlyph(f.font, ImWchar(g.code), std::max(g.w, 1), std::max(g.h, 1), float(g.advance),
                                                   ImVec2(float(g.x), float(g.y)));
    LOGR("auf: the game's font %s read from the disc: %zu glyphs, %d px lines", name, f.glyphs.size(), f.line);
    g_fonts.push_back(std::move(f));
    return g_fonts.back().font;
}
}  // namespace auf
