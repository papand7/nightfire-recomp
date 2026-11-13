// Netplay (LAN and online play), as NightFire's Netplay page: Host Game, its players, input delay and Publish Online;
// the games found on this network and Join; the online list (the master server), Join Online and Refresh; a host's
// address from the settings' join_ip. Agent Under Fire's network play is the runtime's lockstep (core/netplay.h): every
// copy runs the whole game and only the controllers travel, so hosting or joining restarts the game linked to the
// others (every copy from the same boot); the players then pick Multiplayer together, each joining on their own
// controller, and each sees only their own view (src/bond/netplay_hash.cpp). The rows are made afresh each read, for
// whichever menu shows them (settings_page.cpp's Netplay page).
#include "common/netplay_page.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "core/netplay.h"

namespace auf::netplay_page {
namespace {
size_t g_lan = 0, g_online = 0;  // the game chosen in each list
std::chrono::steady_clock::time_point g_fetched{};

Row Button(const char* label, const char* value, const char* help, std::function<void()> run) {
    Row r;
    r.label = label, r.value = value, r.help = help, r.kind = kModernButton;
    r.act = [run](int d) {
        if (d == 0) run();
    };
    return r;
}
Row Choice(const char* label, std::vector<std::string> names, size_t cur, const char* help, std::function<void(size_t)> set) {
    Row r;
    r.label = label, r.help = help, r.kind = kModernChoice;
    if (names.empty()) names.push_back("-");
    cur = std::min(cur, names.size() - 1);
    r.value = names[cur];
    const size_t n = names.size();
    r.act = [cur, n, set](int d) { set((cur + n + size_t(d < 0 ? n - 1 : 1)) % n); };
    return r;
}

std::string HostLabel(const netplay::FoundHost& h) {
    return h.name + " - " + std::to_string(h.joined) + " of " + std::to_string(h.players) + " players";
}
std::string OnlineLabel(const netplay::OnlineServer& s) {
    char t[160];
    std::snprintf(t, sizeof t, "%s  %d/%d  %s", s.name.c_str(), s.players, s.max, s.mode.c_str());
    return t;
}
}  // namespace

std::vector<Row> Rows() {
    std::vector<Row> out;
    app::Settings& s = app::Get();
    if (netplay::Active()) {
        const std::string what = "Player " + std::to_string(netplay::Me() + 1) + " of " + std::to_string(netplay::Players());
        out.push_back(Button("Network Play", what.c_str(), "This game is linked to the others: every copy plays the same game. Leave restarts it alone.",
                             [] { netplay::Restart("off"); }));
        out.back().value = what + (netplay::Desynced() ? "  (out of step)" : "");
        out.push_back(Button("Leave Network Play", "Restart", "Restart the game on its own (the others go on without this player's controller).",
                             [] { netplay::Restart("off"); }));
        return out;
    }
    // the searches: this network's hosts (a broadcast a second while the page is read) and the online list
    const std::vector<netplay::FoundHost> lan = netplay::FindHosts();
    const auto now = std::chrono::steady_clock::now();
    if (now - g_fetched > std::chrono::seconds(30)) g_fetched = now, netplay::OnlineRefresh();
    const std::vector<netplay::OnlineServer> online = netplay::OnlineServers();

    out.push_back(Button("Host Game", "Start",
                         "Start a game others can join: the game restarts and waits for the players, then everyone picks Multiplayer together.",
                         [] {
                             netplay::StopFindingHosts();
                             netplay::Restart("host");
                         }));
    out.push_back(Choice("Players", {"2", "3", "4"}, size_t(std::clamp<uint32_t>(s.netplay_players, 2, 4) - 2),
                         "How many players the hosted game waits for (each plays on their own PC).", [](size_t v) {
                             app::Get().netplay_players = uint32_t(v + 2);
                             app::Save();
                         }));
    {
        std::vector<std::string> d;
        for (int k = 0; k <= 8; k++) d.push_back(std::to_string(k) + (k == 1 ? " frame" : " frames"));
        out.push_back(Choice("Input Delay", d, std::min<uint32_t>(s.netplay_delay, 8),
                             "Frames each controller waits before it plays (the host's counts): more rides out a slow or distant network "
                             "without stalls, fewer answers quicker. 2 suits a home network, 4 to 6 the internet.",
                             [](size_t v) {
                                 app::Get().netplay_delay = uint32_t(v);
                                 app::Save();
                             }));
    }
    out.push_back(Choice("Publish Online", {"Off", "On"}, s.publish_server ? 1 : 0,
                         "List the hosted game on the online list (the master server) while it waits for players. Your router must let "
                         "the game's UDP port (27015) in.",
                         [](size_t v) {
                             app::Get().publish_server = v == 1;
                             app::Save();
                         }));
    {
        std::vector<std::string> names;
        for (const auto& h : lan) names.push_back(HostLabel(h));
        if (names.empty()) names.push_back("Searching...");
        out.push_back(Choice("Game", names, g_lan, "The games waiting for players on this network: PC and players.",
                             [](size_t v) { g_lan = v; }));
    }
    out.push_back(Button("Join Game", "Join", "Join the game chosen above: the game restarts linked to it.", [lan] {
        if (g_lan >= lan.size()) return;
        const auto& h = lan[g_lan];
        netplay::StopFindingHosts();
        netplay::Restart(h.address + ":" + std::to_string(h.port));
    }));
    {
        std::vector<std::string> names;
        for (const auto& o : online) names.push_back(OnlineLabel(o));
        if (names.empty()) names.push_back(netplay::OnlineSearching() ? "Searching..." : "None listed");
        out.push_back(Choice("Online", names, g_online, "The games on the online list (the master server): PC, players.",
                             [](size_t v) { g_online = v; }));
    }
    out.push_back(Button("Join Online", "Join", "Join the online game chosen above (its UDP port must be open on its PC's router).", [online] {
        if (g_online >= online.size()) return;
        const auto& o = online[g_online];
        netplay::StopFindingHosts();
        netplay::Restart(o.address + ":" + std::to_string(o.port));
    }));
    out.push_back(Button("Refresh Online", "Refresh", "Fetch the online list again from the master server.", [] {
        g_fetched = std::chrono::steady_clock::now();
        netplay::OnlineRefresh();
    }));
    const std::string ip = s.join_ip;
    out.push_back(Button("Join Address", ip.empty() ? "-" : ip.c_str(),
                         "Join the host at this address (the launcher's Host address; UDP port 27015 unless it names another).", [ip] {
                             if (ip.empty()) return;
                             netplay::StopFindingHosts();
                             netplay::Restart(ip);
                         }));
    return out;
}

}  // namespace auf::netplay_page
