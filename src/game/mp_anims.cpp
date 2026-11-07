// The multiplayer's player animations in a story level (story co-op): a story level's files hold none of the walk,
// run, strafe and crouch sets a player's body plays in a match (.local/anim_load_notes.md, 2026-11-07), so each story
// level, before its player is made (0xAE870), they are copied in from a multiplayer level's file and registered:
// - the file read once a session through the game's archive (0xE2D50 name: its size; 0xE3280 name, buffer; 0xE2D90
//   pumped until 0: read and decompressed); a level file is a table of sub-files (u32 table size, u32 count, then from
//   +0x20 entries of 14 bytes: u32 size, u8 kind, 8 hex digits and a 0: the id), their data from table size + 0x20,
//   back to back, each 0x20 aligned; kind 4 a motion (0x04: keyframes), 5 an animation (0x06)
// - the 75 animations and their 75 motions kept host side; each level copied into one block of guest memory outside
//   the game's heap (nothing of the game's frees it), each registered (0x6B420 id, data; never an id already there:
//   0x6B500), a motion fixed up in place (0x138E0: its stream's pointer and size), the table's type ranges rebuilt
//   (0x6B540). The game's level load clears the table and heap at the next level.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

#include "common/nightfire.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;

constexpr uint32_t kTableCount = 0x1FE684, kTableMost = 4096;
constexpr const char* kSource = "07000023.bin";  // a multiplayer level (every one holds the same set)

// the animations (the walk tables 0x17E6D4..0x17E8C4 and the posture moves 0xA94C0 / 0xA9590 play)
const uint32_t kAnims[] = {
    0x60001CB, 0x60001CD, 0x60001CE, 0x60001CF, 0x60001D4, 0x60001D7, 0x60001D9, 0x60001DB, 0x60001E3, 0x60001E5,
    0x60001E6, 0x60001E7, 0x60001EE, 0x60001F0, 0x60001F1, 0x60001F2, 0x60008F9, 0x6000914, 0x6000915, 0x600091D,
    0x600091E, 0x600091F, 0x600092F, 0x6000930, 0x6000931, 0x600097C, 0x600097D, 0x60009AA, 0x60009AB, 0x60009AC,
    0x60009AD, 0x60009AF, 0x60009B0, 0x60009B2, 0x60009B3, 0x60009B4, 0x60009B5, 0x60009B7, 0x60009BA, 0x60009BB,
    0x60009BC, 0x60009D0, 0x60009D1, 0x60009D2, 0x60009D3, 0x60009D4, 0x60009D5, 0x60009D7, 0x60009D8, 0x60009D9,
    0x60009DB, 0x60009DC, 0x60009DE, 0x60009DF, 0x60009E0, 0x60009E4, 0x60009E5, 0x60009F3, 0x60009F4, 0x60009F5,
    0x60009FC, 0x60009FD, 0x60009FE, 0x60001D0, 0x60001D1, 0x60001E8, 0x60001E9, 0x60001F3, 0x60001F4, 0x60009AE,
    0x60009B1, 0x60009B6, 0x60009B9, 0x60009DA, 0x60009DD};

struct Record {
    uint32_t id, offset, size;  // offset into g_blob
};
std::vector<uint8_t> g_blob;       // the records, each 0x20 aligned
std::vector<Record> g_motions, g_anims;
bool g_read = false, g_failed = false;
uint32_t g_block = 0;               // guest memory the records are copied to each level
bool g_injected = false;            // this level has them

uint32_t GuestName() {
    static uint32_t name = 0;
    if (!name) {
        name = mem::AllocSystem(32);
        std::memcpy(kernel::HostPtr(name), kSource, std::strlen(kSource) + 1);
    }
    return name;
}

bool Wanted(uint32_t id, const std::unordered_set<uint32_t>& motions) {
    if ((id >> 24) == 6) return std::find(std::begin(kAnims), std::end(kAnims), id) != std::end(kAnims);
    return motions.count(id) != 0;
}

// the multiplayer file read and its records kept
bool ReadSource(X86Context& ctx) {
    if (g_read || g_failed) return g_read;
    if (kernel::CallGuest(ctx, 0xE2D90, {}) != 0) return false;  // a read running: next time
    const uint32_t size = kernel::CallGuest(ctx, 0xE2D50, {GuestName()});
    if (!size || size > 0x4000000) {
        g_failed = true;
        LOGR("nightfire: co-op: %s not found (%u bytes); no multiplayer animations", kSource, size);
        return false;
    }
    const uint32_t buf = mem::AllocHostContiguous(size + 0x1000, 0x80);
    if (!buf) {
        g_failed = true;
        return false;
    }
    kernel::CallGuest(ctx, 0xE3280, {GuestName(), buf});
    for (int i = 0; i < 100000 && kernel::CallGuest(ctx, 0xE2D90, {}) != 0; i++) {
    }
    const uint8_t* b = static_cast<const uint8_t*>(kernel::HostPtr(buf));
    auto u32 = [&](uint32_t o) {
        uint32_t v;
        std::memcpy(&v, b + o, 4);
        return v;
    };
    const uint32_t table = u32(0), count = u32(4);
    if (count == 0 || count > 20000 || table > size) {
        g_failed = true;
        mem::FreeContiguous(buf);
        LOGR("nightfire: co-op: %s not laid out as expected (%u entries)", kSource, count);
        return false;
    }
    // the anims' motions: each anim's motion tracks (type 0: 0x04000000 | its fourth word)
    struct Entry {
        uint32_t id, kind, offset, size;
    };
    std::vector<Entry> entries;
    for (uint32_t i = 0, data = table + 0x20; i < count; i++) {
        const uint32_t e = 0x20 + 14 * i;
        if (e + 14 > size) break;
        uint32_t sz = u32(e);
        char name[9] = {};
        std::memcpy(name, b + e + 5, 8);
        entries.push_back({uint32_t(std::strtoul(name, nullptr, 16)), b[e + 4], data, sz});
        data += (sz + 0x1F) & ~0x1Fu;
    }
    std::unordered_set<uint32_t> motions;
    for (const Entry& e : entries) {
        if (e.kind != 5 || (e.id >> 24) != 6 || !Wanted(e.id, motions) || e.offset + 8 > size) continue;
        const uint32_t tracks = b[e.offset + 6];
        uint32_t at = e.offset + 8;
        for (uint32_t t = 0; t < tracks && at + 2 <= e.offset + e.size; t++) {
            uint16_t hdr;
            std::memcpy(&hdr, b + at, 2);
            const uint32_t type = hdr & 0xFF, n = hdr >> 8;
            if (type == 0 && at + 8 <= size) {
                uint16_t low;
                std::memcpy(&low, b + at + 6, 2);
                motions.insert(0x4000000u | low);
            }
            at += 2 * (n + (type == 0 ? 4 : 3));
        }
    }
    for (const Entry& e : entries) {
        if (!((e.kind == 4 && (e.id >> 24) == 4) || (e.kind == 5 && (e.id >> 24) == 6)) || !Wanted(e.id, motions) ||
            e.offset + e.size > size)
            continue;
        Record r{e.id, uint32_t(g_blob.size()), e.size};
        g_blob.insert(g_blob.end(), b + e.offset, b + e.offset + e.size);
        g_blob.resize((g_blob.size() + 0x1F) & ~size_t(0x1F), 0);
        ((e.id >> 24) == 4 ? g_motions : g_anims).push_back(r);
    }
    mem::FreeContiguous(buf);
    g_read = !g_anims.empty();
    if (!g_read) g_failed = true;
    LOGR("nightfire: co-op: %zu multiplayer animations and %zu motions read from %s (%zu bytes)", g_anims.size(),
         g_motions.size(), kSource, g_blob.size());
    return g_read;
}
}  // namespace

namespace nf {
bool CoopMpAnims() { return g_injected; }

// before a story level's player is made (coop_test.cpp's 0xAE870 hook): the records copied and registered
void InjectMpAnims(X86Context& ctx) {
    g_injected = false;
    if (kernel::CallGuest(ctx, 0x6B500, {0x60009DC})) {  // the level has them (a multiplayer level)
        g_injected = true;
        return;
    }
    if (!ReadSource(ctx)) return;
    const uint32_t used = R32(kTableCount);
    if (used + g_motions.size() + g_anims.size() + 8 >= kTableMost) {
        LOGR("nightfire: co-op: the level's resource table is too full (%u) for the multiplayer animations", used);
        return;
    }
    if (!g_block) g_block = mem::AllocHostContiguous(uint32_t(g_blob.size()), 0x20);
    if (!g_block) return;
    std::memcpy(kernel::HostPtr(g_block), g_blob.data(), g_blob.size());
    uint32_t added = 0;
    for (const Record& r : g_motions)
        if (!kernel::CallGuest(ctx, 0x6B500, {r.id})) {
            kernel::CallGuest(ctx, 0x6B420, {r.id, g_block + r.offset});
            kernel::CallGuest(ctx, 0x138E0, {r.id});
            added++;
        }
    for (const Record& r : g_anims)
        if (!kernel::CallGuest(ctx, 0x6B500, {r.id})) kernel::CallGuest(ctx, 0x6B420, {r.id, g_block + r.offset}), added++;
    kernel::CallGuest(ctx, 0x6B540, {});
    g_injected = kernel::CallGuest(ctx, 0x6B500, {0x60009DC}) != 0;
    LOGR("nightfire: co-op: %u multiplayer animation records put in level %08X (%s)", added, R32(0x1F658C),
         g_injected ? "found" : "not found after");
}
}  // namespace nf
