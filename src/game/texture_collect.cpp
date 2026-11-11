// Collecting the game's textures without playing (HD texture packs): at the front end, each level file is read through
// the game's own archive (0xE2D50 name: its size; 0xE3280 name, buffer; 0xE2D90 pumps the read, as mp_anims.cpp does),
// its texture records found and dumped under the keys the renderer gives the same pictures (gpu::TexturePackCollect:
// mods/textures/dump/<key>.png), for hd_pack to upscale. Turned on by NF_COLLECT_TEXTURES=1 (the launcher's Collect
// button); the game quits when the files are done.
//
// Then the front end's eight movies (the menus' backgrounds and the two logos), for the movie pack (gpu/movie_pack.h):
// each played once through the game's own player (0xDCCD0: the movie's id, a flag, the volume 0-100; [0x2AE288] the
// id playing, 0 once it ends), every frame of it dumped as it is bound (nf::CollectMovieFrame: mods/movies/dump/
// <key>.png and <movie>.txt, the order). They play in real time: about eight minutes.
//
// A texture record in a level file (found by its "KXT" tag, K at offset 0; the data follows the name):
//   +0 "KXT" 06, +4 ffffffff, +8 size, +12 width, +16 height, +20 format code, +24 mips, ... +52 name[36], +88 pixels
// The format code: 0 DXT1 (0x0C), 4 DXT5 (0x0F), 8 A8R8G8B8 swizzled (0x06); 1 is a palette picture (not dumped
// anywhere yet). Checked against the renderer's own dump of The Exchange: 158 of the 163 pictures it dumped came out
// of the file under the same key (the rest are menu and render pictures).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "app/app.h"
#include "common/nightfire.h"
#include "core/log.h"
#include "gpu/texture_pack.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
// the level files (generated/private/levels): the missions and the multiplayer maps
constexpr uint32_t kLevels[] = {0x07000001, 0x07000002, 0x07000003, 0x07000004, 0x07000005, 0x07000006, 0x07000007,
                                0x07000008, 0x07000009, 0x0700000A, 0x0700000B, 0x0700000C, 0x0700000D, 0x07000011,
                                0x07000012, 0x07000013, 0x07000014, 0x07000015, 0x07000016, 0x0700001B, 0x07000023,
                                0x07000024, 0x07000025, 0x07000026, 0x07000027, 0x07000028, 0x07000029, 0x07000048,
                                0x0700004A, 0x0700004B};
constexpr uint32_t kStartFrame = 600;  // the front end's own loading done
constexpr uint32_t kMovies[] = {0x07370048, 0x07380048, 0x07330048, 0x07350048,
                                0x07390048, 0x073A0048, 0x073B0048, 0x073F0048};
constexpr uint32_t kMovieMax = 60 * 180;  // frames (of the game's 60) one may play at most

uint32_t g_name = 0;

uint32_t FormatOf(uint32_t code) {
    switch (code) {
        case 0: return 0x0C;
        case 4: return 0x0F;
        case 8: return 0x06;
        default: return 0;
    }
}

uint32_t Size0(uint32_t format, uint32_t w, uint32_t h) {
    if (format == 0x06) return w * h * 4;
    return ((w + 3) / 4) * ((h + 3) / 4) * (format == 0x0C ? 8 : 16);
}

bool Pow2(uint32_t v) { return v >= 4 && v <= 2048 && (v & (v - 1)) == 0; }

struct Totals {
    uint32_t files = 0, records = 0, dumped = 0;
    std::map<uint32_t, uint32_t> skipped;  // format code -> records
};

void Scan(const uint8_t* b, uint32_t size, Totals& t) {
    for (uint32_t p = 0; p + 52 + 36 < size; p++) {
        if (std::memcmp(b + p, "KXT\x06", 4) != 0) continue;
        uint32_t w, h, code;
        std::memcpy(&w, b + p + 12, 4);
        std::memcpy(&h, b + p + 16, 4);
        std::memcpy(&code, b + p + 20, 4);
        const uint8_t* name = b + p + 52;
        if (!Pow2(w) || !Pow2(h) || name[0] < 0x20 || name[0] > 0x7E) continue;  // a chance "KXT", not a record
        t.records++;
        const uint32_t format = FormatOf(code);
        if (!format) {
            t.skipped[code]++;
            continue;
        }
        const uint32_t data = p + 88, bytes = Size0(format, w, h);
        if (data + bytes > size) continue;
#ifndef XBE_HEADLESS  // the headless harness has no renderer (no texture pack code to dump with)
        if (gpu::TexturePackCollect(format, w, h, b + data)) t.dumped++;
#endif
    }
}
}  // namespace

namespace nf {
bool CollectingTextures() {
    static const bool on = [] {
        const char* e = std::getenv("NF_COLLECT_TEXTURES");
        return e && *e == '1';
    }();
    return on;
}

// the pads' frame (the front end too): one level file a frame once the front end is up
void CollectTextures(X86Context& ctx) {
    static uint32_t frame = 0, next = 0, done_at = 0, movie = 0, movie_at = 0;
    static Totals totals;
    static char movie_name[16];
    if (!CollectingTextures() || done_at) return;
    if (++frame < kStartFrame) return;
    if (next >= sizeof kLevels / sizeof kLevels[0] && !movie_at && movie == 0) {
        LOGR("nightfire: textures: %u files, %u records, %u new pictures dumped", totals.files, totals.records, totals.dumped);
        for (auto [code, n] : totals.skipped) LOGR("nightfire: textures: %u records with format code %u skipped", n, code);
        movie_at = 1;  // the movies next
    }
    if (movie_at) {
        if (movie_at > 1) {
            const uint32_t playing = kernel::R32(0x2AE288);
            if (playing && frame - movie_at < kMovieMax) return;
            LOGR("nightfire: movies: %s done after %.1f s%s", movie_name, (frame - movie_at) / 60.0, playing ? " (too long, cut)" : "");
            nf::collect_movie = nullptr;
            movie++;
        }
        if (movie >= sizeof kMovies / sizeof kMovies[0]) {
            LOGR("nightfire: movies: all %u played", movie);
            done_at = frame;
            app::Quit();
            return;
        }
        std::snprintf(movie_name, sizeof movie_name, "%08x", kMovies[movie]);
        nf::collect_movie = movie_name;
        kernel::CallGuest(ctx, 0xDCCD0, {kMovies[movie], 0, 0});
        movie_at = frame;
        LOGR("nightfire: movies: playing %s (now %08X)", movie_name, kernel::R32(0x2AE288));
        logx::Flush();
        return;
    }
    if (kernel::CallGuest(ctx, 0xE2D90, {}) != 0) return;  // a read running: next frame
    if (!g_name) g_name = mem::AllocSystem(32);
    char name[16];
    std::snprintf(name, sizeof name, "%08x.bin", kLevels[next++]);
    std::memcpy(kernel::HostPtr(g_name), name, std::strlen(name) + 1);
    const uint32_t size = kernel::CallGuest(ctx, 0xE2D50, {g_name});
    if (!size || size > 0x10000000) {
        LOGR("nightfire: textures: %s not found (%u)", name, size);
        return;
    }
    const uint32_t buf = mem::AllocHostContiguous(size + 0x1000, 0x80);
    if (!buf) return;
    kernel::CallGuest(ctx, 0xE3280, {g_name, buf});
    for (int i = 0; i < 1000000 && kernel::CallGuest(ctx, 0xE2D90, {}) != 0; i++) {
    }
    const uint32_t before = totals.dumped;
    totals.files++;
    Scan(static_cast<const uint8_t*>(kernel::HostPtr(buf)), size, totals);
    mem::FreeContiguous(buf);
    LOGR("nightfire: textures: %s read, %u pictures dumped (%u so far)", name, totals.dumped - before, totals.dumped);
}
}  // namespace nf
