// The level's sun for the Vulkan renderer's ray tracing (gpu/rt.h). AUF's maps are maps\<name>.xbc in the 1p_*.viv and
// mp_*.viv packs (EA BIGF: a big-endian directory of offset, size, name); a few name a q3map2 sun in their worldspawn,
// "sun1" "R G B intensity degrees elevation" (0-255 colour; emstel_1, m6_1 ... m6_3). The game ignores it at run time
// (its handler 0x9A530 does nothing). Reads of a pack are watched (kernel::SetReadObserver): the first read inside a
// map's entry is that map loading; its worldspawn's sun is handed to the renderer, or none (the renderer then takes the
// light the game shades its characters by).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "common/auf.h"
#include "core/log.h"
#include "gpu/rt.h"
#include "kernel/kernel.h"

namespace auf {
namespace {

struct Entry {
    uint32_t off, size;
    std::string name;
};

std::mutex g_lock;
std::map<std::string, std::vector<Entry>> g_packs;  // a pack's directory, by its Xbox path
std::string g_map;                                    // the map last read

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

const std::vector<Entry>& Directory(const std::string& path) {
    auto [it, fresh] = g_packs.try_emplace(path);
    if (!fresh) return it->second;
    std::vector<uint8_t> h;
    if (!kernel::ReadGameFileRange(path, 0, 16, h) || h.size() < 16 || std::memcmp(h.data(), "BIGF", 4)) return it->second;
    const uint32_t count = Be32(&h[8]), header = Be32(&h[12]);
    std::vector<uint8_t> d;
    if (count > 100000 || header > (16u << 20) || !kernel::ReadGameFileRange(path, 16, header, d)) return it->second;
    size_t at = 0;
    for (uint32_t i = 0; i < count && at + 9 <= d.size(); i++) {
        Entry e{Be32(&d[at]), Be32(&d[at + 4]), {}};
        at += 8;
        while (at < d.size() && d[at]) e.name += char(std::tolower(d[at++]));
        at++;
        it->second.push_back(std::move(e));
    }
    return it->second;
}

// the worldspawn's "sun1" in a map's bytes (the entity text is plain ASCII in the .xbc)
bool FindSun(const std::vector<uint8_t>& b, gpu::RtSun& sun) {
    static const char kKey[] = "\"sun1\" \"";
    const auto at = std::search(b.begin(), b.end(), kKey, kKey + sizeof(kKey) - 1);
    if (at == b.end()) return false;
    std::string v;
    for (auto p = at + (sizeof(kKey) - 1); p != b.end() && *p != '"' && v.size() < 128; ++p) v += char(*p);
    float r, g, bl, intensity, degrees, elevation;
    if (std::sscanf(v.c_str(), "%f %f %f %f %f %f", &r, &g, &bl, &intensity, &degrees, &elevation) != 6) return false;
    const float yaw = degrees * 3.14159265f / 180, el = elevation * 3.14159265f / 180;
    sun.dir[0] = std::cos(el) * std::cos(yaw);  // Quake III's world: z up
    sun.dir[1] = std::cos(el) * std::sin(yaw);
    sun.dir[2] = std::sin(el);
    sun.colour[0] = std::clamp(r / 255, 0.0f, 1.0f), sun.colour[1] = std::clamp(g / 255, 0.0f, 1.0f);
    sun.colour[2] = std::clamp(bl / 255, 0.0f, 1.0f);
    sun.strength = intensity > 0 ? std::clamp(intensity / 75, 0.25f, 2.0f) : 0.75f;
    LOGR("auf: the map's sun: \"%s\" (toward %.2f %.2f %.2f)", v.c_str(), sun.dir[0], sun.dir[1], sun.dir[2]);
    return true;
}

void Watch(const std::string& path, uint64_t offset, uint32_t len) {
    if (path.size() < 4) return;
    std::string low(path.size(), ' ');
    std::transform(path.begin(), path.end(), low.begin(), [](char c) { return char(std::tolower(c)); });
    if (low.compare(low.size() - 4, 4, ".viv")) return;
    std::lock_guard lock(g_lock);
    for (const Entry& e : Directory(path)) {
        if (offset < e.off || offset >= uint64_t(e.off) + e.size) continue;
        if (e.name.size() < 9 || e.name.compare(0, 5, "maps\\") || e.name.compare(e.name.size() - 4, 4, ".xbc") ||
            e.name == g_map)
            return;
        g_map = e.name;
        std::vector<uint8_t> b;
        gpu::RtSun sun{};
        const bool found = kernel::ReadGameFileRange(path, e.off, e.size, b) && FindSun(b, sun);
        gpu::SetRtSun(found ? &sun : nullptr);
        LOGR("auf: map %s loading: %s", g_map.c_str(), found ? "its sun for the ray tracing" : "no sun named");
        return;
    }
    (void)len;
}

}  // namespace

void WatchLevelSun() { kernel::SetReadObserver(Watch); }

}  // namespace auf
