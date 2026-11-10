// The online list (Netplay's Online row): the master server's games (api.php?a=list&format=lines, a line a server:
// address, port, name, map, players, max, mode, version, tab-separated), fetched on a background thread when the
// Netplay page opens. A server joins by its address (nf::NetJoin, the same as a LAN host), and the game answers the
// master's own check, so a server listed is one that answered.
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "common/nightfire.h"
#include "core/http.h"
#include "core/log.h"

namespace nf {
namespace {
std::mutex g_online_lock;
std::vector<OnlineServer> g_online;
std::atomic<bool> g_online_busy{false};
std::atomic<uint32_t> g_online_gen{0};  // a refresh's number: an older one's answer is dropped

// "a.b.c.d" as the network-order value nf::NetJoin takes; false when it is not an IPv4 address
bool ParseIp(const std::string& text, uint32_t& ip) {
    uint32_t part[4] = {};
    size_t at = 0;
    for (int i = 0; i < 4; i++) {
        size_t e = text.find('.', at);
        if (e == std::string::npos && i < 3) return false;
        const std::string piece = text.substr(at, e == std::string::npos ? std::string::npos : e - at);
        if (piece.empty() || piece.size() > 3 || piece.find_first_not_of("0123456789") != std::string::npos) return false;
        part[i] = uint32_t(std::atoi(piece.c_str()));
        if (part[i] > 255) return false;
        at = e + 1;
    }
    ip = part[0] | part[1] << 8 | part[2] << 16 | part[3] << 24;  // (bytes in order: as a.b.c.d)
    return true;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    while (true) {
        size_t e = s.find(sep, at);
        out.push_back(s.substr(at, e == std::string::npos ? std::string::npos : e - at));
        if (e == std::string::npos) break;
        at = e + 1;
    }
    return out;
}
}  // namespace

void OnlineRefresh() {
    std::string base = app::Get().master_url;
    if (base.empty() || g_online_busy.exchange(true)) return;
    const uint32_t gen = ++g_online_gen;
    std::thread([base, gen] {
        std::string body, url = base + "?a=list&game=nightfire&format=lines";
        std::vector<OnlineServer> found;
        if (http::Get(url, &body)) {
            for (const std::string& line : Split(body, '\n')) {
                const auto f = Split(line, '\t');
                if (f.size() < 8) continue;
                OnlineServer sv;
                if (!ParseIp(f[0], sv.ip)) continue;  // a bad address: skipped
                sv.port = uint16_t(std::atoi(f[1].c_str()));
                sv.name = f[2], sv.map = f[3];
                sv.players = std::atoi(f[4].c_str()), sv.max = std::atoi(f[5].c_str());
                sv.mode = f[6], sv.version = f[7];
                sv.address = f[0];
                found.push_back(sv);
            }
        } else {
            LOGR("nightfire: online list: the master server did not answer");
        }
        if (gen == g_online_gen.load()) {
            std::lock_guard<std::mutex> lock(g_online_lock);
            g_online = found;
        }
        g_online_busy = false;
    }).detach();
}

std::vector<OnlineServer> OnlineServers() {
    std::lock_guard<std::mutex> lock(g_online_lock);
    return g_online;
}

bool OnlineSearching() { return g_online_busy.load(); }
}  // namespace nf
