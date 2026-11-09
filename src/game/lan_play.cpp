// LAN play (work in progress): client/server over UDP, the host the authority. Each copy plays its own players itself
// (their movement is the game's own, at once: nothing to predict or wait for) and sends their state to the host 30
// times a second; the host sends everyone's state back to every copy 30 times a second. Another copy's players are
// the match's bot characters, driven from the network instead of by their AI ("puppets"), drawn 100 ms in the past
// between the two states around that time (interpolation; at most 250 ms past the newest it is extrapolated), so
// they move smoothly whatever the packets' timing. The clients keep the host's clock (pings, half the round trip).
//
// A multiplayer match (0xA25F0) makes the humans (up to 4, [0x260044]) and the bots ([0x260048], clamped to 6): bot
// i is score slot 4+i, its character object at 0x262814 + i*0x30 (the score table 0x262738, 0x30 a slot, +0x1C the
// object). Each frame 0x2DD00 calls a character's think by its class (object +0xDB): 3 a player's (0xAD520), 2 an
// NPC's or bot's (0x31530, cdecl: the object). A puppet's think runs (a bot's body is spawned and animated by its AI:
// skipped, none was drawn) with no target (0x37C10) and no shots (the projectile spawner 0x211F0, cdecl: the shooter,
// ...; 0 none made: a bot's animation fires through it whatever its target); then its position (object +0x24,
// +0x28, +0x2C, metres, +0x28 up) and heading (+0x40, radians) are set from the network. In a LAN match the bots are
// the other copies' players (as many as were connected when it started, at most 6).
//
// Joining a match already running: the host keeps the match's roster (the lobby's list at its start, everyone in
// score slot order) and sends it while the match runs. A player who joins then takes the place of one of the host's
// AI bots: the host makes that bot slot its puppet, the other copies move their puppet of that bot to the new player
// (its name in the score table), and the new copy, waiting in the lobby, starts the same match with the roster's
// players as its bots and its own player as the bot's character and team. With no AI bot left the new player waits
// in the lobby for the next match. The match's clock (0x262928 seconds played, 0x26292C the limit) goes with the
// roster: every client keeps the host's. Its scores and the bot's character are not carried over. The
// bot slots no one has (up to six bots, seven players) are made open places: bots named "Open" on every copy, kept
// out of sight above the map (puppets with no player), that a joining player takes before an AI bot's.
//
// NF_LAN=host hosts (UDP port 27787, NF_LAN_PORT another); NF_LAN=<address> joins that host. NF_LAN_TRACE=1 logs
// player 1's and the puppets' places every 5 s.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Winsock on Windows; elsewhere sdk/runtime/compat's names on POSIX sockets
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef _WIN32
#include <unistd.h>  // gethostname
#endif

#include "common/nightfire.h"
#include "gpu/style.h"
#include "game/menu_page.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;
using kernel::W32;

constexpr uint32_t kHumans = 0x260044, kBots = 0x260048;
constexpr uint32_t kScores = 0x262738, kSlotSize = 0x30, kSlotObject = 0x1C;
constexpr uint32_t kPos = 0x24, kYaw = 0x40;
constexpr int kFirstBot = 4, kMostBots = 6;
constexpr uint8_t kOpen = 0xFE;  // a roster entry's peer: an open place (a hidden bot slot kept for a player joining)
// the MP block's player slots (names, C strings), the join page's records (+0 joined, +4 team, +8 character), the
// bot setup (0x245280 its own bots, 0x245281 how many, records of 0x12: +0 name, +0xF team, +0x10 character)
constexpr uint32_t kSlots = 0x25FE38, kJoined = 0x245240, kJoinSize = 0x10, kJoinTeam = 4, kJoinCharacter = 8;
constexpr uint32_t kCodenameLoaded = 0x17D550, kCodename = 0x17D558;  // the join page names players so when >= 2
constexpr uint32_t kOwnBots = 0x245280, kBotCount = 0x245281, kBotRecords = 0x245282, kBotRecordSize = 0x12;

float GetF(uint32_t va) {
    float f;
    std::memcpy(&f, &kernel::G<uint32_t>(va), 4);
    return f;
}
void SetF(uint32_t va, float f) { std::memcpy(&kernel::G<uint32_t>(va), &f, 4); }

uint32_t SlotObject(int slot) { return R32(kScores + uint32_t(slot) * kSlotSize + kSlotObject); }

// the score slot whose character obj is, else -1
int SlotOf(uint32_t obj) {
    for (int s = 0; s < 10; s++)
        if (obj && SlotObject(s) == obj) return s;
    return -1;
}

// ---- the network --------------------------------------------------------------------------------------------------

constexpr uint32_t kMagic = 0x314C464E;  // "NFL1"
enum : uint8_t { kHello = 1, kWelcome, kState, kSnap, kPing, kPong, kInfo, kLobby, kQuery, kServer, kEvent, kRoster };
constexpr uint16_t kPort = 27787;
constexpr int kRate = 30;                // states and snapshots a second
constexpr int64_t kDelay = 100;          // ms in the past remote players are drawn
constexpr int64_t kExtrapolate = 250;    // ms past the newest state they are carried on
constexpr int64_t kTimeout = 5000;       // ms without a packet: a client gone

struct Body {
    float pos[3], yaw;
};
struct Entry {          // one player's state on the wire
    uint8_t peer, local;
    uint16_t valid;     // in a match (else only there: its copy in the menus)
    uint32_t time;      // the host's clock (ms) at which it was taken or arrived
    Body body;
};
struct Named {          // a player in the lobby: who and as what
    uint8_t peer, local;
    uint8_t character;   // the join record's +8 (an index into the characters 0x17CB98, 0x18 each)
    uint8_t team;        // the join record's +4 (1 the first team)
    char name[16];
};
constexpr uint32_t kSettings = 0x260030, kSettingsSize = 0x40;  // the match's options (the MP block's, 0x260030-6F)
struct Event {          // a hit on another copy's player, or a death; relayed by the host to every copy
    uint32_t id;        // the origin's count (with its peer: repeats dropped)
    uint8_t kind;       // kHit or kDeath
    uint8_t origin;     // the copy that sent it
    uint8_t victim_peer, victim_local;
    uint8_t killer_peer, killer_local;  // 0xFF none (a fall, a suicide)
    uint16_t weapon;    // the shooter's weapon index
    uint8_t node[0x40]; // the hit's record from +8 (damage, time, directions, type, zone)
};
enum : uint8_t { kHitEvent = 1, kDeathEvent = 2, kShotEvent = 3 };  // a shot: node holds origin, direction, sound
struct ServerInfo {     // a host's answer to a search (kQuery, broadcast to the port)
    char host[24];      // the PC's name
    char map[24];
    uint8_t players, pad;
    uint16_t port;
};
struct Header {
    uint32_t magic;
    uint8_t type, peer, count, pad;
    uint32_t time;      // the sender's clock; a pong: the ping's, then the host's in the first entry's slot
};

int64_t Now() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

struct Sample {
    int64_t time;
    Body body;
};
struct Remote {
    std::deque<Sample> samples;  // oldest first, by the host's clock
    int64_t heard = 0;           // our clock
};

std::mutex g_lock;
bool g_on = false, g_host = false;
std::atomic<int> g_me{-1};                // this copy's peer id (the host 0), -1 not yet welcomed
std::atomic<int64_t> g_offset{0};         // the host's clock minus ours
std::vector<std::pair<uint8_t, Body>> g_local;  // this copy's players (0-3) and the host's bots (their slot)
bool g_in_match = false;                  // g_local is theirs (else one player in the menus)
std::map<uint16_t, Remote> g_remotes;     // (peer << 8 | local): other copies' players
std::map<uint16_t, int> g_slot_of;        // a remote's bot slot in this match
std::map<uint16_t, Entry> g_latest;       // the host's: everyone's newest
// the lobby: everyone's names in order (the host's list, sent to all), the host's options and its starts
std::vector<Named> g_local_names;         // this copy's players, from the game's thread
std::map<int, std::vector<Named>> g_infos;  // the host's: each client's
std::vector<Named> g_lobby;
uint8_t g_settings[kSettingsSize];
bool g_have_settings = false;
bool g_movement = false;  // host rule under g_lock; clients never write their preference into it
uint32_t g_starts = 0;                    // the host's count of matches started from the lobby
std::atomic<bool> g_send_now{false};
// the host's bot setup (0x245280..0x2452EE: its own bots, how many, six records), as its options left it; the lobby
// and the match put the network's players first, then as many of these as there is room for (six bots in all)
constexpr uint32_t kBotSetup = 0x245280, kBotSetupSize = 2 + 6 * 0x12;
uint8_t g_bot_cfg[kBotSetupSize];
bool g_bot_cfg_saved = false;
bool g_launching = false;  // the host's A: the match loads (its bot setup is the match's, not restored)
std::string g_server_name, g_map_name;    // the host's PC and the lobby's map (its answer to a search)
uint16_t g_port = 0;
std::atomic<bool> g_quit{false};
std::thread g_thread;
std::deque<Event> g_events_in, g_events_out;  // to apply on the game's thread; to send
std::map<uint32_t, bool> g_events_seen;       // origin << 24 | id
uint32_t g_event_id = 0;
// joining a running match: the host's roster (its lobby at the match's start; a late player in an AI bot's place)
std::vector<Named> g_roster;           // the host's, and as a client last heard it
uint32_t g_roster_id = 0;              // the match it is for (the host's start count)
int64_t g_roster_heard = -100000;      // a client's: when it last came
std::vector<Named> g_roster_applied;   // this copy's view of the match's players (their puppets' slots follow it)
bool g_roster_pending = false;         // the host's: a match was started, its roster made once it is played
bool g_was_in_match = false;
bool g_late_start = false;             // a client starting a running match: its bots from the roster
uint32_t g_joined_id = ~0u;            // a client's: the match it last started (not joined again)
constexpr uint32_t kPlayed = 0x262928, kLimit = 0x26292C;  // the match's clock: seconds played, the limit (0x9CD09)
float g_clock[2] = {};                 // the host's played and limit (sent with the roster)
bool g_clock_read = false;             // the host's: read in this match (the roster waits for it)

SOCKET g_sock = INVALID_SOCKET;
sockaddr_in g_host_addr{};
struct Client {
    sockaddr_in addr;
    int64_t heard;
};
std::map<int, Client> g_clients;  // the host's, by peer id
int64_t g_heard_host = 0;         // a client's: the host's last packet

void Send(const sockaddr_in& to, uint8_t type, const std::vector<Entry>& entries, uint32_t time) {
    char buf[1200];
    Header h{kMagic, type, uint8_t(g_me.load() < 0 ? 0 : g_me.load()), uint8_t(std::min<size_t>(entries.size(), 40)),
             0, time};
    std::memcpy(buf, &h, sizeof h);
    std::memcpy(buf + sizeof h, entries.data(), h.count * sizeof(Entry));
    sendto(g_sock, buf, int(sizeof h + h.count * sizeof(Entry)), 0, (const sockaddr*)&to, sizeof to);
}

void SendRaw(const sockaddr_in& to, uint8_t type, const void* body, size_t size, uint8_t count) {
    char buf[1400];
    Header h{kMagic, type, uint8_t(g_me.load() < 0 ? 0 : g_me.load()), count, 0, uint32_t(Now())};
    size = std::min(size, sizeof buf - sizeof h);
    std::memcpy(buf, &h, sizeof h);
    std::memcpy(buf + sizeof h, body, size);
    sendto(g_sock, buf, int(sizeof h + size), 0, (const sockaddr*)&to, sizeof to);
}

// the host's AI bots (its bot setup, or the game's count without one), as many as fit after `others` players from
// the network: their score slots follow those players'
std::vector<Named> HostBots(size_t others) {
    std::vector<Named> bots;
    if (!g_bot_cfg_saved) return bots;
    size_t want = g_bot_cfg[1], room = others < size_t(kMostBots) ? kMostBots - others : 0;
    for (size_t i = 0; i < std::min(want, room); i++) {
        const uint8_t* rec = g_bot_cfg + 2 + i * 0x12;
        Named n{0, uint8_t(kFirstBot + others + i), rec[0x10], uint8_t(rec[0xF] == 1 ? 1 : 2), {}};
        std::memcpy(n.name, rec, 14);
        if (!n.name[0]) std::snprintf(n.name, sizeof n.name, "Bot %zu", i + 1);
        bots.push_back(n);
    }
    return bots;
}

// the host's lobby: its own players, then each client's, then its AI bots in the room left
std::vector<Named> LobbyList() {
    std::vector<Named> all;
    for (Named n : g_local_names) n.peer = 0, all.push_back(n);
    size_t others = 0;
    for (auto& [peer, names] : g_infos)
        if (g_clients.count(peer))
            for (Named n : names) n.peer = uint8_t(peer), all.push_back(n), others++;
    static const int fake_n = [] {
        const char* e = std::getenv("NF_LAN_FAKE");
        return e ? std::atoi(e) : 0;
    }();
    others += size_t(fake_n);
    std::vector<Named> bots = HostBots(others);
    static const int fake = [] {  // NF_LAN_FAKE=N (diagnostic): N made-up players in the lobby, alternate teams
        const char* e = std::getenv("NF_LAN_FAKE");
        return e ? std::atoi(e) : 0;
    }();
    for (int i = 0; i < fake; i++) {
        Named n{uint8_t(200 + i), 0, uint8_t(i % 8), uint8_t(i % 2 ? 2 : 1), {}};
        std::snprintf(n.name, sizeof n.name, "Test %d", i + 1);
        all.push_back(n);
    }
    all.insert(all.end(), bots.begin(), bots.end());
    // the open places: bot slots no one has (each copy has at most six bots: seven players with one each)
    int used = int(others + bots.size()), here = int(g_local_names.size());
    int open = std::max(0, std::min(kMostBots - used, kMostBots + 1 - here - used));
    for (int i = 0; i < open; i++) {
        Named n{kOpen, uint8_t(kFirstBot + used + i), 0, uint8_t(i % 2 ? 2 : 1), "Open"};
        all.push_back(n);
    }
    return all;
}
void SendLobby() {
    g_lobby = LobbyList();
    std::vector<uint8_t> body(kSettingsSize + 4);
    std::memcpy(body.data(), g_settings, kSettingsSize);
    std::memcpy(body.data() + kSettingsSize, &g_starts, 4);
    size_t n = std::min<size_t>(g_lobby.size(), 40);
    body.insert(body.end(), (const uint8_t*)g_lobby.data(), (const uint8_t*)(g_lobby.data() + n));
    // Optional rule tail follows the names, preserving the native settings block and old packet offsets.
    const uint32_t rules = g_movement ? 1u : 0u;
    const auto* rule_bytes = reinterpret_cast<const uint8_t*>(&rules);
    body.insert(body.end(), rule_bytes, rule_bytes + sizeof rules);
    for (auto& [id, c] : g_clients) SendRaw(c.addr, kLobby, body.data(), body.size(), uint8_t(n));
}

void Store(const Entry& e) {  // a remote player's state, by the host's clock
    if (e.peer == g_me.load()) return;
    Remote& r = g_remotes[uint16_t(e.peer << 8 | e.local)];
    r.heard = Now();
    if (!e.valid) return;
    auto& s = r.samples;
    if (!s.empty() && int64_t(e.time) <= s.back().time) return;  // old or repeated
    s.push_back({int64_t(e.time), e.body});
    while (s.size() > 32) s.pop_front();
}

void Receive() {
    char buf[1500];
    sockaddr_in from{};
    int len = sizeof from;
    for (;;) {
        int n = recvfrom(g_sock, buf, sizeof buf, 0, (sockaddr*)&from, &len);
        if (n < int(sizeof(Header))) return;
        Header h;
        std::memcpy(&h, buf, sizeof h);
        if (h.magic != kMagic) continue;
        if (h.type == kQuery) {
            if (!g_host) continue;
            ServerInfo info{};
            std::lock_guard<std::mutex> lock(g_lock);
            std::strncpy(info.host, g_server_name.c_str(), sizeof info.host - 1);
            std::strncpy(info.map, g_map_name.c_str(), sizeof info.map - 1);
            size_t players = g_local_names.size();
            for (auto& [peer, names] : g_infos)
                if (g_clients.count(peer)) players += names.size();
            info.players = uint8_t(players), info.port = g_port;
            SendRaw(from, kServer, &info, sizeof info, 1);
            continue;
        }
        if (h.type == kEvent) {
            if (n < int(sizeof h + sizeof(Event))) continue;
            Event e;
            std::memcpy(&e, buf + sizeof h, sizeof e);
            std::lock_guard<std::mutex> lock(g_lock);
            uint32_t key = uint32_t(e.origin) << 24 | (e.id & 0xFFFFFF);
            if (g_events_seen.count(key)) continue;
            g_events_seen[key] = true;
            if (g_events_seen.size() > 4096) g_events_seen.erase(g_events_seen.begin());
            if (g_host) g_events_out.push_back(e);  // to the other clients too
            g_events_in.push_back(e);
            continue;
        }
        if (h.type == kRoster) {
            if (g_host || n < int(sizeof h + 12 + h.count * sizeof(Named))) continue;
            std::lock_guard<std::mutex> lock(g_lock);
            std::memcpy(&g_roster_id, buf + sizeof h, 4);
            std::memcpy(g_clock, buf + sizeof h + 4, 8);
            g_roster.assign(h.count, Named{});
            std::memcpy(g_roster.data(), buf + sizeof h + 12, h.count * sizeof(Named));
            for (Named& m : g_roster) m.name[15] = 0;
            g_roster_heard = Now();
            continue;
        }
        if (h.type == kInfo || h.type == kLobby) {
            std::lock_guard<std::mutex> lock(g_lock);
            const char* p = buf + sizeof h;
            size_t head = h.type == kLobby ? kSettingsSize + 4 : 0;
            if (n < int(sizeof h + head + h.count * sizeof(Named))) continue;
            std::vector<Named> names(h.count);
            std::memcpy(names.data(), p + head, h.count * sizeof(Named));
            for (Named& m : names) m.name[15] = 0;
            if (h.type == kInfo && g_host) {
                auto c = g_clients.find(h.peer);
                if (c == g_clients.end()) continue;
                c->second.heard = Now();
                if (g_infos[h.peer].size() != names.size()) g_send_now = true;
                g_infos[h.peer] = names;
            } else if (h.type == kLobby && !g_host) {
                if (from.sin_addr.s_addr != g_host_addr.sin_addr.s_addr || from.sin_port != g_host_addr.sin_port) continue;
                const size_t tail = sizeof h + head + h.count * sizeof(Named);
                uint32_t rules = 0;
                if (size_t(n) >= tail + sizeof rules) std::memcpy(&rules, buf + tail, sizeof rules);
                g_movement = (rules & 1) != 0;
                g_heard_host = Now();
                std::memcpy(g_settings, p, kSettingsSize);
                g_have_settings = true;
                std::memcpy(&g_starts, p + kSettingsSize, 4);
                g_lobby = names;
            }
            continue;
        }
        if (n < int(sizeof h + h.count * sizeof(Entry))) continue;
        std::vector<Entry> es(h.count);
        std::memcpy(es.data(), buf + sizeof h, h.count * sizeof(Entry));
        int64_t now = Now();
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_host) {
            int peer = h.peer;
            if (h.type == kHello) {
                peer = 0;
                for (auto& [id, c] : g_clients)
                    if (c.addr.sin_addr.s_addr == from.sin_addr.s_addr && c.addr.sin_port == from.sin_port) peer = id;
                if (!peer) {
                    peer = 1;
                    while (g_clients.count(peer)) peer++;
                    LOGR("nightfire: LAN: client %d joined (%s:%u)", peer, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
                }
                g_clients[peer] = {from, now};
                Header w{kMagic, kWelcome, uint8_t(peer), 0, 0, uint32_t(now)};
                sendto(g_sock, (const char*)&w, sizeof w, 0, (const sockaddr*)&from, sizeof from);
                continue;
            }
            auto c = g_clients.find(peer);
            if (c == g_clients.end()) continue;
            c->second.heard = now;
            if (h.type == kPing) {
                Entry e{};
                e.time = uint32_t(now);
                Send(from, kPong, {e}, h.time);
            } else if (h.type == kState) {
                for (Entry e : es) {
                    e.peer = uint8_t(peer), e.time = uint32_t(now);
                    g_latest[uint16_t(e.peer << 8 | e.local)] = e;
                    Store(e);
                }
            }
        } else {
            g_heard_host = now;
            if (h.type == kWelcome && g_me.load() != h.peer) {
                g_me = h.peer;
                g_offset = int64_t(h.time) - now;
                LOGR("nightfire: LAN: joined the host as client %d", h.peer);
            } else if (h.type == kPong && h.count) {
                int64_t rtt = now - int64_t(h.time), offset = int64_t(es[0].time) - (int64_t(h.time) + rtt / 2);
                g_offset = g_offset.load() + (offset - g_offset.load()) / 8;  // smoothed
                static int n = 0;
                if (++n % 10 == 1) LOGR("nightfire: LAN: round trip %lld ms", (long long)rtt);
            } else if (h.type == kSnap) {
                for (const Entry& e : es) Store(e);
            }
        }
    }
}

void Loop() {
    int64_t next = Now(), last_ping = 0, last_hello = -10000, last_lobby = 0;
    while (!g_quit) {
        Receive();
        int64_t now = Now();
        if (now >= next) {
            next += 1000 / kRate;
            if (next < now) next = now;
            std::lock_guard<std::mutex> lock(g_lock);
            std::vector<Entry> mine;
            for (size_t i = 0; i < g_local.size(); i++) {
                Entry e{};
                e.peer = uint8_t(std::max(0, g_me.load())), e.local = g_local[i].first, e.time = uint32_t(now + g_offset);
                e.body = g_local[i].second, e.valid = g_in_match;
                mine.push_back(e);
            }
            if (g_host) {
                for (auto it = g_clients.begin(); it != g_clients.end();) {
                    if (now - it->second.heard > kTimeout) {
                        LOGR("nightfire: LAN: client %d left", it->first);
                        for (auto l = g_latest.begin(); l != g_latest.end();)
                            l = (l->first >> 8) == it->first ? g_latest.erase(l) : std::next(l);
                        it = g_clients.erase(it);
                    } else {
                        ++it;
                    }
                }
                if (now - last_lobby > 500 || g_send_now.exchange(false)) {
                    last_lobby = now, SendLobby();
                    if (!g_roster.empty() && g_clock_read) {  // the match running: its players, for those joining it
                        std::vector<uint8_t> body(12);
                        std::memcpy(body.data(), &g_roster_id, 4);
                        std::memcpy(body.data() + 4, g_clock, 8);
                        size_t k = std::min<size_t>(g_roster.size(), 40);
                        body.insert(body.end(), (const uint8_t*)g_roster.data(), (const uint8_t*)(g_roster.data() + k));
                        for (auto& [id, c] : g_clients) SendRaw(c.addr, kRoster, body.data(), body.size(), uint8_t(k));
                    }
                }
                for (const Event& e : g_events_out)
                    for (auto& [id, c] : g_clients)
                        if (id != e.origin)
                            for (int k = 0; k < 2; k++) SendRaw(c.addr, kEvent, &e, sizeof e, 1);
                g_events_out.clear();
                std::vector<Entry> all = mine;
                for (auto& [k, e] : g_latest) all.push_back(e);
                for (auto& [id, c] : g_clients) Send(c.addr, kSnap, all, uint32_t(now));
            } else if (g_me.load() >= 0 && now - g_heard_host > 2000) {
                LOGR("nightfire: LAN: nothing from the host for 2 s: joining again");
                g_me = -1;
            } else if (g_me.load() < 0) {
                if (now - last_hello > 500) last_hello = now, Send(g_host_addr, kHello, {}, uint32_t(now));
            } else {
                Send(g_host_addr, kState, mine, uint32_t(now));
                for (const Event& e : g_events_out)
                    for (int k = 0; k < 2; k++) SendRaw(g_host_addr, kEvent, &e, sizeof e, 1);
                g_events_out.clear();
                if (now - last_lobby > 500 || g_send_now.exchange(false)) {
                    last_lobby = now;
                    SendRaw(g_host_addr, kInfo, g_local_names.data(), g_local_names.size() * sizeof(Named),
                            uint8_t(g_local_names.size()));
                }
                if (now - last_ping > 500) last_ping = now, Send(g_host_addr, kPing, {}, uint32_t(now));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// a session: hosting (the port) or joining (the host's address); ended by End (the menus: Netplay)
bool Begin(bool host, uint32_t ip, uint16_t port) {
    static bool wsa = [] {
        WSADATA w;
        return WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }();
    (void)wsa;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_remotes.clear(), g_latest.clear(), g_infos.clear(), g_lobby.clear(), g_clients.clear(), g_slot_of.clear();
        g_have_settings = false, g_starts = 0, g_heard_host = Now();
        g_roster.clear(), g_roster_applied.clear(), g_roster_pending = false, g_was_in_match = false;
        g_roster_heard = -100000, g_late_start = false, g_joined_id = ~0u;
        g_events_in.clear(), g_events_out.clear(), g_events_seen.clear();
        char name[64] = {};
#ifdef _WIN32
        DWORD n = sizeof name;
        if (!GetComputerNameA(name, &n)) std::strcpy(name, "NightFire");
#else
        if (gethostname(name, sizeof name - 1) != 0 || !name[0]) std::strcpy(name, "NightFire");
#endif
        g_server_name = name;
    }
    g_host = host, g_me = host ? 0 : -1, g_port = port;
    g_host_addr = {};
    g_host_addr.sin_family = AF_INET;
    g_host_addr.sin_port = htons(port);
    g_host_addr.sin_addr.s_addr = ip;
    g_quit = false;
    g_on = true;
    g_thread = std::thread([port] {
        // the port is retried a while: the game relaunches itself (the copy before still has it as it hands over)
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in me{};
        me.sin_family = AF_INET;
        me.sin_addr.s_addr = htonl(INADDR_ANY);
        me.sin_port = htons(g_host ? port : 0);
        for (int i = 0; bind(sock, (sockaddr*)&me, sizeof me) != 0; i++) {
            if (i == 60 || g_quit) {
                LOGR("nightfire: LAN: UDP port %u could not be opened (%d)", port, WSAGetLastError());
                closesocket(sock);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        u_long nb = 1;
        ioctlsocket(sock, FIONBIO, &nb);
        g_sock = sock;
        char addr[32] = {};
        inet_ntop(AF_INET, &g_host_addr.sin_addr, addr, sizeof addr);
        if (g_host) LOGR("nightfire: LAN: hosting on UDP port %u", port);
        else LOGR("nightfire: LAN: joining %s:%u", addr, port);
        Loop();
        g_sock = INVALID_SOCKET;
        closesocket(sock);
    });
    return true;
}

void End() {
    if (!g_on) return;
    g_quit = true;
    if (g_thread.joinable()) g_thread.join();
    g_on = false, g_host = false, g_me = -1;
    LOGR("nightfire: LAN: session ended");
}

// NF_LAN=host or NF_LAN=<address> at the start (diagnostics; the menus' Netplay otherwise)
void Start() {
    static bool started = false;
    if (started) return;
    started = true;
    const char* e = std::getenv("NF_LAN");
    if (!e || !*e) return;
    uint16_t port = kPort;
    if (const char* p = std::getenv("NF_LAN_PORT"); p && *p) port = uint16_t(std::atoi(p));
    bool host = std::strcmp(e, "host") == 0;
    uint32_t ip = 0;
    if (!host && inet_pton(AF_INET, e, &ip) != 1) {
        LOGR("nightfire: LAN: '%s' is not an address", e);
        return;
    }
    Begin(host, ip, port);
}

// ---- the search: hosts on this network (a broadcast to the port, and this PC), from the Netplay page --------------
struct Server {
    ServerInfo info;
    uint32_t ip;
    int64_t heard;
};
std::vector<Server> g_servers;
bool g_searching = false;

void Search() {
    static SOCKET s = INVALID_SOCKET;
    static int64_t last = -10000;
    if (!g_searching) {
        if (s != INVALID_SOCKET) closesocket(s), s = INVALID_SOCKET;
        return;
    }
    if (s == INVALID_SOCKET) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        int yes = 1;  // Windows' BOOL
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes);
        sockaddr_in me{};
        me.sin_family = AF_INET;
        bind(s, (sockaddr*)&me, sizeof me);
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        g_servers.clear();
        last = -10000;
    }
    int64_t now = Now();
    if (now - last > 1000) {
        last = now;
        Header h{kMagic, kQuery, 0, 0, 0, uint32_t(now)};
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(kPort);
        for (uint32_t ip : {htonl(INADDR_BROADCAST), htonl(INADDR_LOOPBACK)}) {
            to.sin_addr.s_addr = ip;
            sendto(s, (const char*)&h, sizeof h, 0, (const sockaddr*)&to, sizeof to);
        }
    }
    char buf[256];
    sockaddr_in from{};
    int len = sizeof from;
    for (int n; (n = recvfrom(s, buf, sizeof buf, 0, (sockaddr*)&from, &len)) >= int(sizeof(Header) + sizeof(ServerInfo));) {
        Header h;
        std::memcpy(&h, buf, sizeof h);
        if (h.magic != kMagic || h.type != kServer) continue;
        Server sv{};
        std::memcpy(&sv.info, buf + sizeof h, sizeof sv.info);
        sv.info.host[23] = sv.info.map[23] = 0;
        // this PC's own answer comes from the loopback and its network address: one entry, by port and name
        sv.ip = from.sin_addr.s_addr, sv.heard = now;
        bool known = false;
        for (Server& o : g_servers)
            if (o.info.port == sv.info.port && !std::strcmp(o.info.host, sv.info.host)) o.info = sv.info, o.heard = now, known = true;
        if (!known) {
            g_servers.push_back(sv);
            char a[32] = {};
            inet_ntop(AF_INET, &from.sin_addr, a, sizeof a);
            LOGR("nightfire: LAN: found %s at %s:%u (%s, %u players)", sv.info.host, a, sv.info.port, sv.info.map,
                 sv.info.players);
        }
    }
    g_servers.erase(std::remove_if(g_servers.begin(), g_servers.end(), [&](const Server& o) { return now - o.heard > 3500; }),
                    g_servers.end());
}

// a remote player's place drawn now: between the samples around kDelay ago, by the host's clock
bool Interpolate(const std::deque<Sample>& s, int64_t t, Body& out) {
    if (s.empty()) return false;
    if (t <= s.front().time) return out = s.front().body, true;
    for (size_t i = 1; i < s.size(); i++) {
        if (s[i].time < t) continue;
        const Sample &a = s[i - 1], &b = s[i];
        float f = float(t - a.time) / float(std::max<int64_t>(1, b.time - a.time));
        for (int k = 0; k < 3; k++) out.pos[k] = a.body.pos[k] + (b.body.pos[k] - a.body.pos[k]) * f;
        float d = std::remainder(b.body.yaw - a.body.yaw, 6.2831853f);
        out.yaw = a.body.yaw + d * f;
        return true;
    }
    // past the newest: carried on along the last two, at most kExtrapolate
    const Sample& b = s.back();
    out = b.body;
    if (s.size() >= 2) {
        const Sample& a = s[s.size() - 2];
        float f = float(std::min(t - b.time, kExtrapolate)) / float(std::max<int64_t>(1, b.time - a.time));
        for (int k = 0; k < 3; k++) out.pos[k] += (b.body.pos[k] - a.body.pos[k]) * f;
    }
    return true;
}

void TestFrame();  // below
void Unlocks();
void Queue(Event e);
bool g_replaying = false;  // a remote shot being fired from its puppet (0x211F0 lets it)
void Inject(X86Context& ctx, uint32_t obj);
void HitBot(X86Context& ctx, uint32_t obj);
void CollectHits(X86Context& ctx, uint32_t obj);
struct NetHitter {
    int slot = -1;
    int64_t when = -100000;
};
NetHitter g_net_hitter[16];  // the host's bots: who from the network hit each last (0xA1FC0 credits no one for it)
void ApplyEvents(X86Context& ctx);
void WatchDeaths();
// a puppet's posture: standing, walking, running, sprinting by its speed (m/s); ours asked, the AI's refused
std::atomic<bool> g_own_posture{false};
int Posture(float speed) { return speed < 0.4f ? 0x0A : speed < 2.2f ? 0x08 : speed < 3.4f ? 0x1E : 0x1F; }
// a match being played (0xA25F0 started one; the bot think ran lately)
std::atomic<int64_t> g_match_seen{-100000};
bool MatchOn() { return Now() - g_match_seen.load() < 1000; }

// a player's name as shown: as sent, numbered if an earlier player in the lobby has the same ("Julie", "Julie 2")
std::string ShownName(const Named& m, size_t place) {
    std::string n(m.name, strnlen(m.name, 15));
    int same = 0;
    for (size_t i = 0; i < place && i < g_lobby.size(); i++)
        same += std::string(g_lobby[i].name, strnlen(g_lobby[i].name, 15)) == n ? 1 : 0;
    if (same) n = n.substr(0, 12) + " " + std::to_string(same + 1);
    return n;
}

// this PC's player name: settings.toml's player_name, else the Windows user's name
std::string PlayerName() {
    static const std::string name = [] {
        std::string n;
        if (FILE* f = std::fopen("settings.toml", "r")) {
            char line[256];
            while (std::fgets(line, sizeof line, f)) {
                std::string l = line;
                if (l.rfind("player_name", 0) != 0) continue;
                size_t a = l.find('"'), b = l.rfind('"');
                if (a != std::string::npos && b > a) n = l.substr(a + 1, b - a - 1);
            }
            std::fclose(f);
        }
        if (n.empty()) {
#ifdef _WIN32
            char user[64] = {};
            DWORD size = sizeof user;
            if (GetUserNameA(user, &size)) n = user;
#else
            if (const char* user = std::getenv("USER"); user && *user) n = user;
#endif
        }
        if (n.empty()) n = "Bond";
        return n.substr(0, 15);
    }();
    return name;
}
size_t Place(const Named& m) {
    for (size_t i = 0; i < g_lobby.size(); i++)
        if (g_lobby[i].peer == m.peer && g_lobby[i].local == m.local) return i;
    return 0;
}

// the lobby's players from other copies, in its order (the host's list; before it came, those heard from)
std::vector<Named> Remotes() {
    std::vector<Named> out;
    for (const Named& m : g_lobby)
        if (m.peer != g_me.load()) out.push_back(m);
    if (g_lobby.empty())
        for (auto& [k, r] : g_remotes)
            if (Now() - r.heard < 2000) out.push_back(Named{uint8_t(k >> 8), uint8_t(k & 0xFF), 0, 1, "Player"});
    return out;
}

// a human's team (the join record's: 1 the first) as a bot record's (1 the first, 0 the second: 0x897E2)
uint8_t BotTeam(uint8_t team) { return team == 1 ? 1 : 0; }

int PuppetKey(int slot) {  // the remote driving bot slot, else -1
    for (auto& [k, s] : g_slot_of)
        if (s == slot) return k;
    return -1;
}

std::vector<Named> RosterOthers() {  // the running match's players but this copy's, in its order
    std::vector<Named> out;
    for (const Named& m : g_roster)
        if (m.peer != g_me.load()) out.push_back(m);
    return out;
}

void SlotName(int slot, const Named& m) {  // a score slot's name (its puppet's player)
    char* name = &kernel::G<char>(kSlots + uint32_t(slot) * kSlotSize);
    std::memset(name, 0, 16);
    std::memcpy(name, m.name, strnlen(m.name, 15));
}

// each frame (g_lock held): in a match, the host gives a player who joined it an AI bot's place and the clients move
// their puppet of that bot to the new player
void RosterFrame(bool in_match) {
    if (g_host && !in_match && g_was_in_match) g_roster.clear();  // the match over
    g_was_in_match = in_match;
    if (!in_match) return;
    auto key = [](const Named& m) { return uint16_t(m.peer << 8 | m.local); };
    if (g_host) {
        if (g_roster.empty()) return;
        g_clock[0] = GetF(kPlayed), g_clock[1] = GetF(kLimit), g_clock_read = true;
        // a player who left (its copy gone): its slot an AI bot again, the host's (its AI runs, its state sent as the
        // host's bot's: the clients move their puppet of it to that, as for a join)
        for (Named& m : g_roster) {
            if (m.peer == 0 || m.peer == kOpen || g_clients.count(m.peer)) continue;
            auto it = g_slot_of.find(key(m));
            if (it == g_slot_of.end()) continue;
            int slot = it->second;
            g_slot_of.erase(it);
            g_remotes.erase(key(m));
            LOGR("nightfire: LAN: client %d's player %d left the match: an AI bot takes slot %d", m.peer, m.local + 1, slot);
            Named bot{0, uint8_t(slot), m.character, m.team, {}};
            std::snprintf(bot.name, sizeof bot.name, "Bot %d", slot - kFirstBot + 1);
            m = bot;
            SlotName(slot, m);
            g_send_now = true;
        }
        for (auto& [peer, names] : g_infos) {
            if (!g_clients.count(peer)) continue;
            for (const Named& n : names) {
                Named r = n;
                r.peer = uint8_t(peer);
                uint16_t k = key(r);
                if (std::any_of(g_roster.begin(), g_roster.end(), [&](const Named& m) { return key(m) == k; })) continue;
                int j = -1;
                for (int pass = 0; pass < 2 && j < 0; pass++)  // an open place, else an AI bot's
                    for (size_t i = 0; i < g_roster.size() && j < 0; i++)
                        if (g_roster[i].peer == (pass ? 0 : kOpen) && g_roster[i].local >= kFirstBot &&
                            g_roster[i].local < 10 && SlotObject(g_roster[i].local))
                            j = int(i);
                static std::map<uint16_t, bool> said_full;
                if (j < 0) {
                    if (!said_full[k])
                        said_full[k] = true,
                        LOGR("nightfire: LAN: client %d's player %d waits for the next match (no AI bot to replace)",
                             peer, n.local + 1);
                    continue;
                }
                int slot = g_roster[j].local;
                bool was_open = g_roster[j].peer == kOpen;
                g_slot_of.erase(key(g_roster[j]));  // an open place's
                r.character = g_roster[j].character, r.team = g_roster[j].team;
                g_roster[j] = r;
                g_slot_of[k] = slot;
                g_remotes[k].samples.clear();
                SlotName(slot, r);
                g_send_now = true;
                LOGR("nightfire: LAN: client %d's player %d joins the match running, in slot %d (%s)", peer,
                     n.local + 1, slot, was_open ? "an open place" : "an AI bot's");
            }
        }
        return;
    }
    if (Now() - g_roster_heard > 1500 || g_roster_id != g_joined_id) return;
    // the host's clock (as it was when the roster came), when this copy's is a second or more off it
    float played = g_clock[0] + float(Now() - g_roster_heard) / 1000.0f;
    if (std::fabs(GetF(kPlayed) - played) > 1.0f || GetF(kLimit) != g_clock[1]) {
        static bool said = false;
        if (!said) said = true, LOGR("nightfire: LAN: the match's clock set to the host's (%.0f of %.0f s)", played, g_clock[1]);
        SetF(kPlayed, played), SetF(kLimit, g_clock[1]);
    }
    if (g_roster.size() != g_roster_applied.size()) return;
    for (size_t i = 0; i < g_roster.size(); i++) {
        uint16_t was = key(g_roster_applied[i]), now = key(g_roster[i]);
        if (was == now || g_roster[i].peer == g_me.load() || g_slot_of.count(now)) continue;
        auto it = g_slot_of.find(was);
        if (it == g_slot_of.end()) continue;
        int slot = it->second;
        g_slot_of.erase(it);
        g_slot_of[now] = slot;
        SlotName(slot, g_roster[i]);
        if ((now >> 8) == 0 && (now & 0xFF) >= kFirstBot && (was >> 8) != kOpen)
            LOGR("nightfire: LAN: client %d's player %d left the match: the host's AI bot in slot %d", was >> 8,
                 (was & 0xFF) + 1, slot);
        else
            LOGR("nightfire: LAN: client %d's player %d joined the match, in slot %d", now >> 8, (now & 0xFF) + 1, slot);
    }
    g_roster_applied = g_roster;
}
}  // namespace

namespace {
void LobbyFrame(X86Context& ctx);
}

namespace nf {
void NetSearch(bool on) { g_searching = on; }
std::vector<LanServer> NetServers() {
    std::vector<LanServer> out;
    for (const Server& sv : g_servers) {
        std::string label = sv.info.host;  // "PC - map, 2 players": the map once chosen, the players once joined
        if (sv.info.map[0] && std::strcmp(sv.info.map, "?")) label += std::string(" - ") + sv.info.map;
        if (sv.info.players)
            label += (sv.info.map[0] && std::strcmp(sv.info.map, "?") ? ", " : " - ") + std::to_string(sv.info.players) +
                     (sv.info.players == 1 ? " player" : " players");
        out.push_back({label, sv.ip, sv.info.port});
    }
    return out;
}
void NetHost() {
    End();
    Begin(true, 0, kPort);
}
void NetJoin(uint32_t ip, uint16_t port) {
    End();
    Begin(false, ip, port);
}
void NetStop() { End(); }
bool NetOn() { return g_on; }
bool NetGuest() { return g_on && !g_host; }
bool NetMovement(bool& enabled) {
    if (!NetGuest()) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    enabled = g_have_settings && g_movement;
    return true;
}

// a character driven from the network (its weapon is not the game's to fire: src/game/aim.cpp skips its update)
bool LanPuppet(uint32_t obj) {
    if (!g_on) return false;
    int slot = SlotOf(obj);
    if (slot < kFirstBot) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    return PuppetKey(slot) >= 0;
}

// each frame, after 0xDE720 read the controls (src/game/controls.cpp): this copy's players' state for the network
void LanFrame(X86Context& ctx) {
    Start();
    if (g_on) {
        static int last_rule = -1;
        const int rule = nf::TightMovement() ? 1 : 0;
        if (last_rule != rule) {
            last_rule = rule;
            LOGR("nightfire: LAN movement: host rule %s (%s)", rule ? "tighter" : "original", g_host ? "host" : "client");
        }
    }
    Search();
    NetplayPageFrame(ctx);
    LobbyFrame(ctx);
    if (g_on) ApplyEvents(ctx), WatchDeaths(), TestFrame();
    Unlocks();
    if (g_on && g_host) {  // the map's name for searches (the maps' table 0x17C6A0: 0x18 each, +4 its text, +0xC its id)
        static uint32_t shown = ~0u;
        uint32_t map = R32(0x260040);
        if (map != shown) {
            shown = map;
            std::string name = "?";
            for (uint32_t i = 0; i < 8; i++)
                if (R32(0x17C6A0 + i * 0x18 + 0xC) == map) {
                    uint32_t eax = ctx.eax, text = kernel::CallGuest(ctx, 0x6D460, {R32(0x17C6A0 + i * 0x18 + 4), 0});
                    ctx.eax = eax;
                    if (text) name.assign(&kernel::G<char>(text), strnlen(&kernel::G<char>(text), 23));
                }
            std::lock_guard<std::mutex> lock(g_lock);
            g_map_name = name;
        }
    }
    if (!g_on) return;
    uint32_t humans = std::min<uint32_t>(R32(kHumans), 4);
    std::vector<std::pair<uint8_t, Body>> local;
    bool in_match = MatchOn();
    auto body = [](uint32_t obj) {
        Body b;
        for (int k = 0; k < 3; k++) b.pos[k] = GetF(obj + kPos + k * 4);
        b.yaw = GetF(obj + kYaw);
        return b;
    };
    for (uint32_t i = 0; in_match && i < humans; i++) {
        uint32_t obj = SlotObject(int(i));
        if (!obj) break;
        local.push_back({uint8_t(i), body(obj)});
    }
    in_match = !local.empty();
    if (in_match && g_host)  // the host's AI bots: the clients' puppets
        for (int sl = kFirstBot; sl < 10; sl++)
            if (uint32_t obj = SlotObject(sl); obj && !LanPuppet(obj)) local.push_back({uint8_t(sl), body(obj)});
    static const bool trace = [] {
        const char* e = std::getenv("NF_LAN_TRACE");
        return e && *e == '1';
    }();
    static int64_t traced = 0;
    if (trace && in_match && g_host && Now() - traced > 5000) {  // the host's AI bots' places
        traced = Now();
        std::string line;
        char buf[64];
        for (auto& [sl, b] : local)
            if (sl >= kFirstBot) std::snprintf(buf, sizeof buf, " %d:%.1f,%.1f", sl, b.pos[0], b.pos[2]), line += buf;
        LOGR("nightfire: LAN: the host's AI bots%s", line.c_str());
    }
    if (!in_match)
        for (uint32_t i = 0; i < std::max<uint32_t>(humans, 1); i++) local.push_back({uint8_t(i), Body{}});
    std::vector<Named> names;
    for (uint32_t i = 0; i < 4; i++) {
        if (!kernel::G<uint8_t>(kJoined + i * kJoinSize)) continue;
        Named n{};
        n.local = uint8_t(i);
        n.character = uint8_t(R32(kJoined + i * kJoinSize + kJoinCharacter));
        n.team = uint8_t(R32(kJoined + i * kJoinSize + kJoinTeam));
        // the name: the codename loaded (the game's own), else this PC's player name; 2, 3, 4 after it for the others
        std::string base = R32(kCodenameLoaded) >= 2
                               ? std::string(&kernel::G<char>(kCodename), strnlen(&kernel::G<char>(kCodename), 15))
                               : PlayerName();
        std::string mine = i ? base.substr(0, 13) + " " + std::to_string(i + 1) : base;
        std::memcpy(n.name, mine.data(), std::min<size_t>(mine.size(), 15));
        // the slot (the lists, the match) as the lobby shows it: numbered after an earlier player of the same name
        n.peer = uint8_t(std::max(0, g_me.load()));
        {
            std::lock_guard<std::mutex> lock(g_lock);
            for (size_t k = 0; k < g_lobby.size(); k++)
                if (g_lobby[k].peer == n.peer && g_lobby[k].local == n.local) mine = ShownName(n, k);
        }
        char* slot = &kernel::G<char>(kSlots + i * kSlotSize);
        if (std::strncmp(slot, mine.c_str(), 16)) std::memset(slot, 0, 16), std::memcpy(slot, mine.data(), mine.size());
        names.push_back(n);
    }
    std::lock_guard<std::mutex> lock(g_lock);
    RosterFrame(in_match);
    g_local = local;
    g_in_match = in_match;
    if (names.size() != g_local_names.size()) g_send_now = true;
    g_local_names = names;
    if (g_host) {
        if (g_movement != nf::tight_movement) g_send_now = true;
        g_movement = nf::tight_movement;
        std::memcpy(g_settings, &kernel::G<uint8_t>(kSettings), kSettingsSize);
        g_have_settings = true;
    }
}
}  // namespace nf

X86_EXTERN_FUNC(__imp__sub_000A25F0);
X86_FUNC(sub_000A25F0) {  // a multiplayer match starts: the bots are the other copies' players
    Start();
    if (g_on) {
        std::lock_guard<std::mutex> lock(g_lock);
        g_slot_of.clear();
        int n = 0;
        std::vector<Named> others = g_late_start ? RosterOthers() : Remotes();
        for (const Named& m : others) {
            if (n >= kMostBots) break;
            if (g_host && m.peer == kOpen) continue;  // the host's open places: after its AI bots
            uint32_t rec = kBotRecords + uint32_t(n) * kBotRecordSize;  // its look and team in the match (0xA27D1)
            std::string shown = g_late_start ? std::string(m.name, strnlen(m.name, 15)) : ShownName(m, Place(m));
            std::memset(&kernel::G<uint8_t>(rec), 0, 0xF);
            std::memcpy(&kernel::G<char>(rec), shown.data(), std::min<size_t>(shown.size(), 14));
            kernel::G<uint8_t>(rec + 0xF) = BotTeam(m.team);
            kernel::G<uint8_t>(rec + 0x10) = m.character;
            g_slot_of[uint16_t(m.peer << 8 | m.local)] = kFirstBot + n++;
        }
        int puppets = n;
        if (g_host)  // its AI bots after them, as many as fit
            for (const Named& m : HostBots(size_t(n))) {
                uint32_t rec = kBotRecords + uint32_t(n) * kBotRecordSize;
                std::memset(&kernel::G<uint8_t>(rec), 0, kBotRecordSize);
                std::memcpy(&kernel::G<char>(rec), m.name, 14);
                kernel::G<uint8_t>(rec + 0xF) = BotTeam(m.team);
                kernel::G<uint8_t>(rec + 0x10) = m.character;
                n++;
            }
        if (g_host)  // the open places, in their slots
            for (const Named& m : g_lobby) {
                if (m.peer != kOpen || n >= kMostBots) continue;
                uint32_t rec = kBotRecords + uint32_t(n) * kBotRecordSize;
                std::memset(&kernel::G<uint8_t>(rec), 0, kBotRecordSize);
                std::memcpy(&kernel::G<char>(rec), m.name, 14);
                kernel::G<uint8_t>(rec + 0xF) = BotTeam(m.team);
                g_slot_of[uint16_t(m.peer << 8 | m.local)] = kFirstBot + n++;
            }
        g_launching = false, g_bot_cfg_saved = false;
        if (g_host) g_roster = g_lobby, g_roster_id = g_starts, g_clock_read = false;  // for those joining it later
        else g_roster_applied = g_late_start ? g_roster : g_lobby, g_joined_id = g_late_start ? g_roster_id : g_starts;
        kernel::G<uint8_t>(kOwnBots) = 1;
        kernel::G<uint8_t>(kBotCount) = uint8_t(n);
        W32(kBots, uint32_t(n));
        LOGR("nightfire: LAN match: %u players here, %d from the network, %d AI bots", R32(kHumans), puppets, n - puppets);
        std::string map;
        for (auto& [k, sl] : g_slot_of) map += " " + std::to_string(k >> 8) + ":" + std::to_string(k & 0xFF) + "=" + std::to_string(sl);
        LOGR("nightfire: LAN match: puppets (peer:player=slot)%s", map.c_str());
    }
    __imp__sub_000A25F0(ctx, base);
    if (g_on) {  // the puppets go by their players' names
        std::lock_guard<std::mutex> lock(g_lock);
        for (const Named& m : g_late_start ? RosterOthers() : Remotes()) {
            auto it = g_slot_of.find(uint16_t(m.peer << 8 | m.local));
            if (it == g_slot_of.end()) continue;
            char* name = &kernel::G<char>(kSlots + uint32_t(it->second) * kSlotSize);
            std::string shown = g_late_start ? std::string(m.name, strnlen(m.name, 15)) : ShownName(m, Place(m));
            std::memset(name, 0, 16);
            std::memcpy(name, shown.data(), std::min<size_t>(shown.size(), 15));
        }
        for (auto& [k, sl] : g_slot_of)  // an open place nameless (a team game shows a teammate's name over it)
            if ((k >> 8) == kOpen) std::memset(&kernel::G<char>(kSlots + uint32_t(sl) * kSlotSize), 0, 16);
        g_late_start = false;
    }
}

X86_EXTERN_FUNC(__imp__sub_000AD520);
X86_FUNC(sub_000AD520) {  // a player's think (cdecl: the object): a match is on while it runs for a score slot
    if (g_on && SlotOf(R32(ctx.esp + 4)) >= 0) g_match_seen = Now();
    __imp__sub_000AD520(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00031530);
X86_FUNC(sub_00031530) {  // an NPC's or bot's think (cdecl: the object)
    uint32_t obj = R32(ctx.esp + 4);
    // diagnostic: NF_WATER_TEST=bots holds the bots still (looking at a level's water with no one shooting)
    static const bool hold = [] { const char* e = std::getenv("NF_WATER_TEST"); return e && !std::strcmp(e, "bots"); }();
    if (hold) { ctx.esp += 4; return; }  // its return (cdecl: the caller takes the argument off)
    if (gpu::Style().weather) nf::NoteWalker(obj);
    nf::CoopNpc(obj);
    int slot = g_on ? SlotOf(obj) : -1;
    float speed = 0, move_yaw = 0;
    int key0 = -1;
    if (slot >= kFirstBot) {
        std::lock_guard<std::mutex> lock(g_lock);
        key0 = PuppetKey(slot);
        Body now, before;
        int64_t t = Now() + g_offset - kDelay;
        if (key0 >= 0 && Interpolate(g_remotes[uint16_t(key0)].samples, t, now) &&
            Interpolate(g_remotes[uint16_t(key0)].samples, t - 100, before)) {
            float dx = now.pos[0] - before.pos[0], dz = now.pos[2] - before.pos[2];
            speed = std::sqrt(dx * dx + dz * dz) / 0.1f;
            move_yaw = std::atan2(dx, dz);
        }
    }
    if (key0 >= 0) CollectHits(ctx, obj);
    bool dead = key0 >= 0 && R32(obj + 0xBC) && GetF(R32(obj + 0xBC) + 0x90) <= 0;  // its death playing
    if (key0 >= 0 && R32(obj + 0xBC) && !dead) {  // its legs: the posture for its speed, played by the think's last step
        static uint32_t pair = mem::AllocHostContiguous(8, 16);
        W32(pair, obj), W32(pair + 4, R32(obj + 0xBC));
        uint32_t eax = ctx.eax;
        g_own_posture = true;
        kernel::CallGuest(ctx, 0x341C0, {uint32_t(Posture(speed)), 0, 0, 0, 0, 0, pair});
        g_own_posture = false;
        ctx.eax = eax;
    }
    __imp__sub_00031530(ctx, base);
    if (slot >= kFirstBot && key0 < 0) HitBot(ctx, obj);  // the host's own bot hit from the network: its next think
    if (slot < kFirstBot) return;
    // a puppet: the AI's own frame ran (it spawns the body and animates it; 0x37C10 gives it no target), then moved
    std::lock_guard<std::mutex> lock(g_lock);
    int key = PuppetKey(slot);
    if (key < 0 || dead) return;
    Body b;
    if ((key >> 8) == kOpen) {  // an open place: kept out of sight, high above the map's middle
        SetF(obj + kPos, 0), SetF(obj + kPos + 4, 400.0f), SetF(obj + kPos + 8, 0);
        return;
    }
    if (!Interpolate(g_remotes[uint16_t(key)].samples, Now() + g_offset - kDelay, b)) return;
    for (int k = 0; k < 3; k++) SetF(obj + kPos + k * 4, b.pos[k]);
    SetF(obj + kYaw, speed >= 0.4f ? move_yaw : b.yaw);  // running the way it goes (bots only run forward)
    static bool said = false;
    if (!said) said = true, LOGR("nightfire: LAN: slot %d is client %d's player %d", slot, key >> 8, (key & 0xFF) + 1);
    static const bool trace = [] {
        const char* e = std::getenv("NF_LAN_TRACE");
        return e && *e == '1';
    }();
    static int n = 0;
    if (trace && ++n % 300 == 1) {
        uint32_t me = SlotObject(0);
        LOGR("nightfire: LAN: here %.2f %.2f %.2f, slot %d (client %d) %.2f %.2f %.2f yaw %.2f", me ? GetF(me + kPos) : 0,
             me ? GetF(me + kPos + 4) : 0, me ? GetF(me + kPos + 8) : 0, slot, key >> 8, b.pos[0], b.pos[1], b.pos[2],
             b.yaw);
    }
}

// a bot's choice of target (0x37C10, cdecl: the actor; MP only): a puppet has none, as a dead bot's (0x37A10(actor,
// 0) drops the one it had): its AI's frame still runs (spawning, animation) but never aims or fires
X86_EXTERN_FUNC(__imp__sub_00037C10);
X86_FUNC(sub_00037C10) {
    uint32_t actor = R32(ctx.esp + 4);
    int slot = g_on && actor ? SlotOf(R32(actor + 0x0C)) : -1;
    bool puppet = false;
    if (slot >= kFirstBot) {
        std::lock_guard<std::mutex> lock(g_lock);
        puppet = PuppetKey(slot) >= 0;
    }
    if (puppet) {
        if (R32(actor + 0x148)) kernel::CallGuest(ctx, 0x37A10, {actor, 0});
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    __imp__sub_00037C10(ctx, base);
}

// a projectile fired (0x211F0, cdecl: -1, the owner, the shooter, the weapon entry, origin*, direction*, sound; the
// object made, 0 none, the bots' fire event at 0x18302
// then plays no flash or sound): none from a puppet, whose shots are its own copy's
X86_EXTERN_FUNC(__imp__sub_000211F0);
X86_FUNC(sub_000211F0) {
    uint32_t shooter = R32(ctx.esp + 8);  // (-1, the owner: data +0x24, the shooter, ...): 0x2128F reads [esp+0x34]
    if (!g_replaying && nf::LanPuppet(shooter)) {
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    // (?, owner, shooter, weapon entry, origin*, direction*, sound): one of ours fired, the others see it
    uint32_t entry = R32(ctx.esp + 0x10), origin = R32(ctx.esp + 0x14), dir = R32(ctx.esp + 0x18);
    uint32_t snd = R32(ctx.esp + 0x1C) & 0xFFFF;
    int slot = g_on && !g_replaying ? SlotOf(shooter) : -1;
    static const bool trace = std::getenv("NF_SHOT_TRACE") != nullptr;  // diagnostic: player 1's shots against the view
    if (trace && shooter && dir && R32(shooter + 0xBC) && kernel::G<int8_t>(R32(shooter + 0xBC) + 0x8DE) == 0) {
        float d[3], yaw, pitch;
        std::memcpy(d, kernel::HostPtr(dir), 12);
        std::memcpy(&yaw, kernel::HostPtr(shooter + 0x40), 4);
        std::memcpy(&pitch, kernel::HostPtr(R32(shooter + 0xBC) + 0x838), 4);
        LOGR("nightfire: shot: dir %+.4f %+.4f %+.4f (yaw %.2f deg, up %.2f deg), view yaw %.2f deg, pitch %.2f deg",
             d[0], d[1], d[2], std::atan2(d[0], d[2]) * 57.29578f, std::asin(std::clamp(d[1], -1.0f, 1.0f)) * 57.29578f,
             yaw * 57.29578f, pitch * 57.29578f);
    }
    __imp__sub_000211F0(ctx, base);
    if (slot < 0 || !origin || !dir || entry < 0x18CFA0) return;
    if (slot >= kFirstBot && (!g_host || nf::LanPuppet(shooter))) return;  // the host's bots' rounds go out too
    Event e{};
    e.kind = kShotEvent, e.killer_peer = uint8_t(std::max(0, g_me.load())), e.killer_local = uint8_t(slot);
    e.victim_peer = e.victim_local = 0xFF;
    e.weapon = uint16_t((entry - 0x18CFA0) / 0x10C);
    std::memcpy(e.node, kernel::HostPtr(origin), 12);
    std::memcpy(e.node + 12, kernel::HostPtr(dir), 12);
    std::memcpy(e.node + 24, &snd, 2);
    Queue(e);
}

// ---- the lobby: the multiplayer summary page (40000049, 0x891E0) ---------------------------------------------------
// The pages' messages: 0x4C opened (filled), 0x50 each frame, 0x4B A, 0x63 B, 0x4D closed. Its fill (message 0x4C) lists the joined players (names) and the bots (the bot setup's records), the map, the
// scenario, the weapons and the limits; A (0x4B, 0x8985C) starts the match: saves the block, sets the humans and the
// level ([0x1F6588] from 0x260040) and loads it. In LAN play it is the lobby: its bots are the other copies' players
// (their names written over the list's rows), a client shows the host's options (copied in before the fill) and its
// A does nothing and B leaves to the main menu; the host's A starts the match on every copy (its start count, in the
// lobby packets). A client goes from the join page straight to it (not to the scenario list 4000001A).
namespace {
constexpr uint32_t kLobbyPage = 0x40000049, kHumanCount = 0x260044, kBotTotal = 0x260048;
constexpr uint32_t kScenarioPage = 0x4000001A, kMainMenu = 0x40000002;
uint32_t g_refill = 0;       // the lobby to fill again (its player + 1)
uint32_t g_seen_starts = 0;  // a client's: the host's start count when it came to the lobby
std::string g_shown;         // the lobby as last filled (names, options), to fill again when it changes
enum class Page { kOther, kJoin, kScenario, kLobby } g_page = Page::kOther;  // the page open (0x4C to 0x4D)

void ApplyHost() {  // a client: the host's options (its own humans and bots kept)
    if (g_host || !g_have_settings) return;
    uint32_t humans = R32(kHumanCount), bots = R32(kBotTotal);
    std::memcpy(&kernel::G<uint8_t>(kSettings), g_settings, kSettingsSize);
    W32(kHumanCount, humans), W32(kBotTotal, bots);
}

// the lobby's other players in the players' two lists (the fill's: team 1's at 0x10000237 / 35 / 38, the other
// team's at 0x10000239 / 36 / 3A, four rows each, their rows used counted at 0x25E95B and 0x25E95A): by team in a
// team game, else the first list then the second; the rest as bots (the bot list shows a face, no name)
constexpr uint32_t kListRows = 4, kTeams = 0x20000000;
struct PlayerList {
    uint32_t name, face, score, used;
};
constexpr PlayerList kLists[2] = {{0x10000237, 0x10000235, 0x10000238, 0x25E95B},
                                  {0x10000239, 0x10000236, 0x1000023A, 0x25E95A}};
constexpr uint32_t kListSet = 0x728F0, kCharacters = 0x17CB98, kSecondHeader = 0x1000023E;
struct Listed {
    Named who;
    std::string name;
    int list;
};
std::vector<Named> LobbyBots(std::vector<Listed>& listed) {
    std::vector<Named> rs, bots;
    for (const Named& m : g_lobby)  // everyone but this copy's players: the network's, and the host's bots
        if ((m.peer != g_me.load() || m.local >= kFirstBot) && m.peer != kOpen) rs.push_back(m);
    if (rs.size() > size_t(kMostBots)) rs.resize(kMostBots);
    listed.clear();
    bool teams = R32(0x26003C) & kTeams;
    uint32_t used[2] = {0, 0};
    for (uint32_t i = 0; i < 4; i++)
        if (kernel::G<uint8_t>(kJoined + i * kJoinSize))
            used[teams && R32(kJoined + i * kJoinSize + kJoinTeam) != 1 ? 1 : 0]++;
    for (const Named& m : rs) {
        if (m.local >= kFirstBot) {  // an AI bot: the bot list
            bots.push_back(m);
            continue;
        }
        int list = teams ? (m.team == 1 ? 0 : 1) : (used[0] < kListRows ? 0 : 1);
        if (used[list] < kListRows) used[list]++, listed.push_back({m, ShownName(m, Place(m)), list});
        else bots.push_back(m);
    }
    kernel::G<uint8_t>(kOwnBots) = 1;
    kernel::G<uint8_t>(kBotCount) = uint8_t(bots.size());
    for (size_t i = 0; i < bots.size(); i++) {
        uint32_t rec = kBotRecords + uint32_t(i) * kBotRecordSize;
        std::memset(&kernel::G<uint8_t>(rec), 0, 0xF);
        std::memcpy(&kernel::G<char>(rec), bots[i].name, 14);
        kernel::G<uint8_t>(rec + 0xF) = BotTeam(bots[i].team);
        kernel::G<uint8_t>(rec + 0x10) = bots[i].character;
    }
    return bots;
}

std::string Shown() {
    std::string k = g_have_settings ? "s" : "";
    for (const Named& m : g_lobby) k += std::string(m.name) + char('0' + m.team) + char('A' + m.character) + "|";
    if (!g_host && g_have_settings) k.append((const char*)g_settings, kSettingsSize);
    return k;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000891E0);
X86_FUNC(sub_000891E0) {  // the summary page's messages (player, ?, ?, message, ...)
    uint32_t player = R32(ctx.esp + 4), msg = R32(ctx.esp + 0x10);
    if (g_on && msg == 0x4C) {
        g_page = Page::kLobby;
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_host && !g_bot_cfg_saved) {  // the host's bots as its options left them (the lobby writes its own)
            std::memcpy(g_bot_cfg, kernel::HostPtr(kBotSetup), kBotSetupSize);
            if (!g_bot_cfg[0]) g_bot_cfg[1] = uint8_t(std::min<uint32_t>(R32(kBotTotal), kMostBots));
            g_bot_cfg_saved = true, g_send_now = true;
            LOGR("nightfire: LAN lobby: the host's options have %u AI bots", g_bot_cfg[1]);
        }
    }
    if (g_on && msg == 0x4D && g_page == Page::kLobby) {
        g_page = Page::kOther;
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_host && g_bot_cfg_saved && !g_launching) {  // back to the options: theirs again
            std::memcpy(kernel::HostPtr(kBotSetup), g_bot_cfg, kBotSetupSize);
            g_bot_cfg_saved = false;
        }
    }
    if (!g_on || g_page != Page::kLobby) return __imp__sub_000891E0(ctx, base);
    auto fill = [&] {
        std::vector<Named> bots;
        std::vector<Listed> listed;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            ApplyHost();
            bots = LobbyBots(listed);
            g_shown = Shown();
        }
        W32(ctx.esp + 0x10, 0x4C);
        __imp__sub_000891E0(ctx, base);
        W32(ctx.esp + 0x10, msg);
        // as the fill shows a joined player (0x89375): the name, the score line, the face (the character's)
        uint32_t eax = ctx.eax;
        bool second = false;
        for (const Listed& l : listed) {
            const PlayerList& pl = kLists[l.list];
            uint32_t row = kernel::G<uint8_t>(pl.used);
            if (row >= kListRows) continue;
            kernel::G<uint8_t>(pl.used) = uint8_t(row + 1);
            second |= l.list == 1;
            auto set = [&](uint32_t id, uint32_t m, uint32_t a) { kernel::CallGuest(ctx, kListSet, {player, id, row, m, a, 0}); };
            set(pl.name, 0x2B, 2);
            set(pl.name, 0x18, nf::MenuString(l.name.c_str()));
            set(pl.score, 0x2B, 2);
            set(pl.score, 0x18, nf::MenuString("+0"));
            set(pl.face, 0x2B, 2);
            set(pl.face, 0x24, R32(kCharacters + uint32_t(l.who.character) * 0x18));
        }
        // the second list's panel shown (its title stays the second team's: text there crashed the game)
        if (second && !(R32(0x26003C) & kTeams)) nf::MenuSet(ctx, player, kSecondHeader, 0x2B, 2);
        ctx.eax = eax;
        static size_t said = ~size_t(0);
        if (said != listed.size() + bots.size()) {
            said = listed.size() + bots.size();
            LOGR("nightfire: LAN lobby: %zu players from the network (%zu named in the players' lists), %s",
                 listed.size() + bots.size(), listed.size(), g_host ? "the host" : "a client");
        }
        return;
    };
    if (msg == 0x4C) {
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_seen_starts = g_starts;
        }
        return fill();
    }
    bool start = false, refill = false;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        start = !g_host && g_starts != g_seen_starts;
        refill = Shown() != g_shown;
        if (start) g_seen_starts = g_starts;
        // a match running that has a place for this copy's players: started here too, they as the bots they replace
        if (!start && !g_host && Now() - g_roster_heard < 1500 && g_roster_id != g_joined_id &&
            std::any_of(g_roster.begin(), g_roster.end(), [](const Named& m) { return m.peer == g_me.load(); })) {
            for (const Named& m : g_roster)
                if (m.peer == g_me.load() && m.local < 4) {
                    W32(kJoined + m.local * kJoinSize + kJoinTeam, m.team);
                    W32(kJoined + m.local * kJoinSize + kJoinCharacter, m.character);
                }
            start = g_late_start = true;
            LOGR("nightfire: LAN lobby: joining the match running");
        }
    }
    if (start) {  // the host started the match: this copy too, with its options
        {
            std::lock_guard<std::mutex> lock(g_lock);
            ApplyHost();
        }
        LOGR("nightfire: LAN lobby: the host started the match (map %08X)", R32(0x260040));
        W32(ctx.esp + 0x10, 0x4B);
        __imp__sub_000891E0(ctx, base);
        W32(ctx.esp + 0x10, msg);
        return;
    }
    if (refill) g_refill = player + 1;  // filled again from the frame (nf::LanFrame), not inside the menu's update
    if (msg == 0x63 && !g_host) {  // a client's B: out to the main menu (the scenario list was never its)
        nf::MenuGoTo(ctx, player, kMainMenu);
        ctx.eax = 1;
        ctx.esp += 4;
        return;
    }
    if (msg == 0x4B) {
        if (!g_host) {  // a client's A: only the host starts; in a team game it changes its player's team
            // (the join page opened again skips the team choice: it goes straight back here)
            if ((R32(0x26003C) & kTeams) && player < 4 && kernel::G<uint8_t>(kJoined + player * kJoinSize)) {
                uint32_t at = kJoined + player * kJoinSize + kJoinTeam, team = R32(at) == 1 ? 0 : 1;
                W32(at, team);
                g_send_now = true, g_refill = player + 1;
                LOGR("nightfire: LAN lobby: a client's player %u changes team (%u)", player + 1, team);
            }
            ctx.eax = 1;
            ctx.esp += 4;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_starts++;
            g_seen_starts = g_starts;
        }
        g_send_now = true, g_launching = true;
        LOGR("nightfire: LAN lobby: the host starts the match (map %08X)", R32(0x260040));
    }
    __imp__sub_000891E0(ctx, base);
}

// the scenario options page's messages (0x88EA0; its Continue, 0x89006, counts the players and bots of each team from
// the join records and the bot setup, and refuses a team game with an empty team): the host's A with the network's
// players counted as bots of their teams (added to the bot setup for the call, then the setup put back)
X86_EXTERN_FUNC(__imp__sub_00088EA0);
X86_FUNC(sub_00088EA0) {
    if (!g_on || !g_host || R32(ctx.esp + 0x10) != 0x4B) return __imp__sub_00088EA0(ctx, base);
    // kept: the bot setup, the bots' count and their score slots' names (0x75DD0 packs the records in use, +0xE set,
    // and copies their names there)
    constexpr uint32_t kBotNames = kSlots + kFirstBot * kSlotSize, kBotNamesSize = kMostBots * kSlotSize;
    std::vector<uint8_t> keep(kBotSetupSize), names(kBotNamesSize);
    std::memcpy(keep.data(), kernel::HostPtr(kBotSetup), kBotSetupSize);
    std::memcpy(names.data(), kernel::HostPtr(kBotNames), kBotNamesSize);
    uint32_t bots = R32(kBotTotal);
    int added = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        uint32_t i = 0;
        for (const Named& m : g_lobby) {
            if (m.peer == 0 || m.peer == kOpen || m.local >= kFirstBot) continue;
            while (i < uint32_t(kMostBots) && kernel::G<uint8_t>(kBotRecords + i * kBotRecordSize + 0xE)) i++;
            if (i >= uint32_t(kMostBots)) break;
            uint32_t rec = kBotRecords + i * kBotRecordSize;
            std::memset(&kernel::G<uint8_t>(rec), 0, kBotRecordSize);
            std::memcpy(&kernel::G<char>(rec), m.name, 13);
            kernel::G<uint8_t>(rec + 0xE) = 1;
            kernel::G<uint8_t>(rec + 0xF) = BotTeam(m.team);
            kernel::G<uint8_t>(rec + 0x10) = m.character;
            added++;
        }
    }
    __imp__sub_00088EA0(ctx, base);
    if (added) {
        {  // the lobby opened in the call: the host's bots it kept are the setup's from before
            std::lock_guard<std::mutex> lock(g_lock);
            if (g_bot_cfg_saved) {
                std::memcpy(g_bot_cfg, keep.data(), kBotSetupSize);
                if (!g_bot_cfg[0]) g_bot_cfg[1] = uint8_t(std::min<uint32_t>(bots, kMostBots));
                g_send_now = true;
            }
        }
        std::memcpy(kernel::HostPtr(kBotSetup), keep.data(), kBotSetupSize);
        std::memcpy(kernel::HostPtr(kBotNames), names.data(), kBotNamesSize);
        W32(kBotTotal, bots);
        LOGR("nightfire: LAN: the scenario's Continue with %d players from the network counted", added);
    }
}

namespace {
// the lobby filled again (its options or players changed): its fill message from outside the menu's update
void LobbyFrame(X86Context& ctx) {
    if (!g_refill) return;
    uint32_t player = g_refill - 1;
    g_refill = 0;
    if (g_page != Page::kLobby) return;
    uint32_t eax = ctx.eax;
    kernel::CallGuest(ctx, 0x891E0, {player, 0, 0, 0x4C, 0, 0});
    ctx.eax = eax;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00087880);
X86_FUNC(sub_00087880) {  // the join page's messages
    if (g_on && R32(ctx.esp + 0x10) == 0x4C) g_page = Page::kJoin;
    __imp__sub_00087880(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_000873B0);
X86_FUNC(sub_000873B0) {  // the scenario list's messages
    if (g_on && R32(ctx.esp + 0x10) == 0x4C) g_page = Page::kScenario;
    __imp__sub_000873B0(ctx, base);
}

// a page change (0x92C40, cdecl: the player's menu context, 0x44 to a page, the page, 0): a client's join page goes on
// to the lobby, not the scenario list (0x87D07; only the host chooses)
X86_EXTERN_FUNC(__imp__sub_00092C40);
X86_FUNC(sub_00092C40) {
    if (g_on && !g_host && g_page == Page::kJoin && R32(ctx.esp + 8) == 0x44 && R32(ctx.esp + 12) == kScenarioPage) {
        W32(ctx.esp + 12, kLobbyPage);
        LOGR("nightfire: LAN lobby: a client goes from the join page to the lobby");
    }
    __imp__sub_00092C40(ctx, base);
}

// ---- combat over the network ---------------------------------------------------------------------------------------
// Each copy's players are its own: their health, deaths and respawns are the game's there. A shot of ours that hits a
// puppet (its hit record, 0x3D900 from the bot's hit list +0xAC) is not applied here: the record goes to the victim's
// copy, which puts it in its player's hit list (0x29560: list, time, contact, source) with a stand-in projectile
// whose shooter is the shooter's puppet there (the projectile's data +0x24 the shooter, +0x38 the weapon; class 5),
// so the game's own player update (0xADD00 / 0xADE00 / 0xAC0A0) takes the health and, on a death, 0xA1FC0 credits the
// shooter (the hit list's sources) and shows "Defeated by". A copy whose player died tells every copy (the score
// slot's deaths +0x08 went up; its killer's slot +0x28), which count it in their score table as the game does, so
// kills, deaths and scores agree on every copy. Puppets take no damage (their death is not played yet: the puppet
// goes where its player respawns). The records go in after the player's collisions (0xAB450), before 0xAE39F walks
// the list: the list is emptied and filled again in the player's update.
namespace {
constexpr uint32_t kHitList = 0xAC, kAddHit = 0x29560, kProjClass = 5;
constexpr uint32_t kDeaths = 0x08, kKiller = 0x28;
// records to put in a character's hit list just before its own update (the lists are emptied each frame before)
struct Pending {
    uint32_t source, time;
    uint8_t node[0x40];
};
std::map<uint32_t, std::vector<Pending>> g_pending;

// a stand-in projectile for a shooter (one each, made once)
uint32_t StandIn(uint32_t shooter, uint32_t weapon_entry) {
    static std::map<uint32_t, uint32_t> made;
    uint32_t& p = made[shooter];
    if (!p) {
        p = mem::AllocHostContiguous(0x200, 16);
        std::memset(kernel::HostPtr(p), 0, 0x200);
        kernel::G<uint8_t>(p + 0xDB) = kProjClass;
        W32(p + 0xBC, p + 0x100);  // its data, after it
    }
    W32(p + 0x100 + 0x24, shooter);
    W32(p + 0x100 + 0x38, weapon_entry);
    for (int k = 0; k < 3; k++) W32(p + kPos + k * 4, R32(shooter + kPos + k * 4));
    return p;
}

uint32_t WeaponEntry(uint32_t obj) {
    uint32_t state = obj ? R32(obj + 0xB8) : 0;
    int idx = state ? kernel::G<int8_t>(state + 0x52) : 0;
    return 0x18CFA0 + uint32_t(std::max(0, idx)) * 0x10C;
}

// the object of a player (peer, local) in this copy's match: ours, or its puppet
uint32_t ObjectOf(uint8_t peer, uint8_t local) {
    if (peer == g_me.load()) return local < 10 ? SlotObject(local) : 0;
    auto it = g_slot_of.find(uint16_t(peer << 8 | local));
    return it == g_slot_of.end() ? 0 : SlotObject(it->second);
}
// a score slot's player (peer, local), false if none
bool WhoIs(int slot, uint8_t& peer, uint8_t& local) {
    if (slot >= 0 && slot < kFirstBot) return peer = uint8_t(std::max(0, g_me.load())), local = uint8_t(slot), true;
    int key = PuppetKey(slot);
    if (key < 0) {  // the host's own AI bot: by its slot
        if (!g_host || slot < kFirstBot || slot >= 10 || !SlotObject(slot)) return false;
        return peer = 0, local = uint8_t(slot), true;
    }
    peer = uint8_t(key >> 8), local = uint8_t(key & 0xFF);
    return true;
}

void Queue(Event e) {  // ours: applied here as well when it is for this copy (the host's own, a client's to itself)
    e.origin = uint8_t(std::max(0, g_me.load())), e.id = ++g_event_id;
    std::lock_guard<std::mutex> lock(g_lock);
    g_events_out.push_back(e);
}

// a replayed shot's muzzle flash, as a bot's fire event makes it after the projectile (0x18307): the gun's muzzle
// (0x13630: its node, 0x13070: its place), a light in the weapon's colour (entry +0x54, its size +0x58; 0x6D530) and,
// unless the weapon has none (entry +0x68 & 0x2000), the flash sprite 0x2000039 turned at random (0xCD140)
void MuzzleFlash(X86Context& ctx, uint32_t obj, uint32_t entry, uint32_t origin) {
    static uint32_t buf = mem::AllocHostContiguous(0x100, 16);
    uint32_t pos = buf + 0x10, rot = buf + 0x20, mat = buf + 0x40;
    int node = int(kernel::CallGuest(ctx, 0x13630, {obj, 0, buf + 4, buf}));
    if (node >= 0) kernel::CallGuest(ctx, 0x13070, {obj, uint32_t(node), 1, pos, mat});
    else std::memcpy(kernel::HostPtr(pos), kernel::HostPtr(origin), 12);
    uint32_t color = R32(entry + 0x54);
    kernel::CallGuest(ctx, 0x6D530, {pos, color >> 16, color >> 8, color & 0xFF, R32(entry + 0x58), 0, 3, 1,
                                     0x40000000, 0, 0, 0, 0xFFFF});
    if (R32(entry + 0x68) & 0x2000) return;
    SetF(rot, 0), SetF(rot + 4, 0), SetF(rot + 8, float(std::rand()) / float(RAND_MAX) * 6.2831853f);
    kernel::CallGuest(ctx, 0xCD140, {R32(obj + 0x20), pos, rot, 0, 0x3D4CCCCD, 0x3DCCCCCD, 0x2000039, 7, 0x48403080, 1,
                                     0x80, 0x40C90FDB});
}

// a puppet whose player died on its copy: killed here too, as a bot dies (HitBot: no health at actor +0x90, then the
// message 0x42), so its AI plays the death and later respawns it (it stays where it fell meanwhile: the bot think's
// hook). The death is the victim copy's, already counted (ApplyEvents): the score slot's numbers are kept as they are
// for a while (the game would count the bot's death again, as its own)
struct Kept {
    uint8_t score[0x1C];
    int16_t killer;
    int64_t until;
};
std::map<int, Kept> g_kept;
void KillPuppet(X86Context& ctx, uint32_t obj, int slot) {
    static uint32_t h = mem::AllocHostContiguous(16, 16);
    if (!(kernel::CallGuest(ctx, 0x2FAC0, {obj, h}) & 0xFF)) {
        LOGR("nightfire: LAN: puppet slot %d: no handle", slot);
        return;
    }
    uint32_t actor = R32(h + 4);
    if (!actor || GetF(actor + 0x90) <= 0) {
        LOGR("nightfire: LAN: puppet slot %d: actor %08X health %.1f", slot, actor, actor ? GetF(actor + 0x90) : 0.0f);
        return;
    }
    SetF(actor + 0x90, 0);
    kernel::CallGuest(ctx, 0x4E320, {R32(h + 0xC), 0x42, 0});
    static int said = 0;
    if (said++ < 10) LOGR("nightfire: LAN: puppet slot %d killed (its player died)", slot);
}
void KeepScores() {
    for (auto it = g_kept.begin(); it != g_kept.end();) {
        uint32_t v = kScores + uint32_t(it->first) * kSlotSize;
        if (std::memcmp(kernel::HostPtr(v), it->second.score, 0x1C) ||
            kernel::G<int16_t>(v + kKiller) != it->second.killer) {
            std::memcpy(kernel::HostPtr(v), it->second.score, 0x1C);
            kernel::G<int16_t>(v + kKiller) = it->second.killer;
        }
        it = Now() > it->second.until ? g_kept.erase(it) : std::next(it);
    }
}

void ApplyEvents(X86Context& ctx) {
    KeepScores();
    std::deque<Event> in;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        in.swap(g_events_in);
    }
    for (const Event& e : in) {
        uint32_t victim = ObjectOf(e.victim_peer, e.victim_local);
        uint32_t killer = e.killer_peer == 0xFF ? 0 : ObjectOf(e.killer_peer, e.killer_local);
        if (!victim && e.kind != kShotEvent) continue;
        uint32_t eax = ctx.eax;
        if (e.kind == kHitEvent && e.victim_peer == g_me.load()) {
            if (!killer) continue;
            uint32_t src = StandIn(killer, 0x18CFA0 + uint32_t(e.weapon) * 0x10C), time;
            std::memcpy(&time, e.node + 4, 4);  // the record's +0xC
            Pending p{src, time, {}};
            std::memcpy(p.node, e.node, 0x40);
            g_pending[victim].push_back(p);  // a player: after its collisions (0xAB450); a bot: before its think
            static int said = 0;
            float dmg;
            std::memcpy(&dmg, e.node, 4);
            uint32_t data = R32(victim + 0xBC);
            if (said++ < 20) LOGR("nightfire: LAN: hit by %u:%u (%.1f damage, zone %u), health %.1f", e.killer_peer,
                                  e.killer_local, dmg, unsigned(e.node[0x3E]) | unsigned(e.node[0x3F]) << 8,
                                  data ? GetF(data + 0x824) : -1.0f);
        } else if (e.kind == kShotEvent && e.killer_peer != g_me.load()) {
            // another copy's player fired: the same round from its puppet (its tracer and sound; it does no harm here)
            uint32_t shooter = killer;
            if (!shooter) continue;
            static uint32_t vec = mem::AllocHostContiguous(32, 16);
            std::memcpy(kernel::HostPtr(vec), e.node, 24);
            uint16_t snd;
            std::memcpy(&snd, e.node + 24, 2);
            g_replaying = true;
            uint32_t entry = 0x18CFA0 + uint32_t(e.weapon) * 0x10C;
            uint32_t made = kernel::CallGuest(ctx, 0x211F0, {0xFFFFFFFF, shooter, shooter, entry, vec, vec + 12, snd});
            g_replaying = false;
            if (made) MuzzleFlash(ctx, shooter, entry, vec);
            static const char* flash_shots = std::getenv("NF_FLASH_SHOTS");  // diagnostic: a picture of each flash
            static int flashes = 0;
            uint32_t me = SlotObject(0);
            float dx = me ? GetF(me + kPos) - GetF(shooter + kPos) : 99, dz = me ? GetF(me + kPos + 8) - GetF(shooter + kPos + 8) : 99;
            if (made && flash_shots && flashes < 8 && dx * dx + dz * dz < 100)  // the shooter within 10 m
                if (FILE* sf = std::fopen("shot", "w"))
                    std::fprintf(sf, "%s\\flash_%d.png\n", flash_shots, ++flashes), std::fclose(sf);
            static int said = 0;
            if (said++ < 5) LOGR("nightfire: LAN: shot replayed from slot %d (weapon %u): projectile %08X", SlotOf(shooter), e.weapon, made);
        } else if (e.kind == kDeathEvent && e.victim_peer != g_me.load()) {
            // the scores as the victim's copy counted them (0xA2211-0xA230E): the killer's kills, streak and score,
            // the victim's deaths and killer; a death of one's own costs a kill and a point
            int vslot = SlotOf(victim), kslot = killer ? SlotOf(killer) : -1;
            if (vslot < 0) continue;
            uint32_t v = kScores + uint32_t(vslot) * kSlotSize;
            W32(v + kDeaths, R32(v + kDeaths) + 1);
            kernel::G<int16_t>(v + kKiller) = int16_t(kslot);
            bool points = R32(0x260028) == 0;
            if (kslot >= 0 && kslot != vslot) {
                uint32_t k = kScores + uint32_t(kslot) * kSlotSize;
                W32(k + 0x04, R32(k + 0x04) + 1), W32(k + 0x10, R32(k + 0x10) + 1);
                if (points) SetF(k + 0x18, GetF(k + 0x18) + 1);
            } else {
                W32(v + 0x04, R32(v + 0x04) - 1), W32(v + 0x10, 0);
                if (points) SetF(v + 0x18, GetF(v + 0x18) - 1);
            }
            LOGR("nightfire: LAN: %u:%u died (slot %d, by slot %d)", e.victim_peer, e.victim_local, vslot, kslot);
            if (vslot >= kFirstBot && nf::LanPuppet(victim)) {
                Kept k{};
                std::memcpy(k.score, kernel::HostPtr(v), 0x1C);
                k.killer = kernel::G<int16_t>(v + kKiller);
                k.until = Now() + 4000;
                g_kept[vslot] = k;
                KillPuppet(ctx, victim, vslot);
            }
        }
        ctx.eax = eax;
    }
}

// a character's pending records into its hit list (from its update's hook, before the update)
void Inject(X86Context& ctx, uint32_t obj) {
    auto it = g_pending.find(obj);
    if (it == g_pending.end()) return;
    uint32_t eax = ctx.eax;
    for (const Pending& p : it->second) {
        uint32_t node = kernel::CallGuest(ctx, kAddHit, {obj + kHitList, p.time, 0, p.source});
        if (node) std::memcpy(kernel::HostPtr(node + 8), p.node, 0x40);
    }
    ctx.eax = eax;
    g_pending.erase(it);
}

// a hit from the network on the host's AI bot, after its think: in its hit list (a death's credit reads the sources
// there) and through the bots' damage as their hit handler does (0x5DCE8): 0x3D900 with the bot's handle (0x2FAC0:
// object, actor, [object+0x20], actor+0xEC), then at no health the message 0x42 (0x4E320: handle +0xC, 0x42, 0)
void HitBot(X86Context& ctx, uint32_t obj) {
    auto it = g_pending.find(obj);
    if (it == g_pending.end()) return;
    static uint32_t h = mem::AllocHostContiguous(16, 16);
    uint32_t eax = ctx.eax;
    if (kernel::CallGuest(ctx, 0x2FAC0, {obj, h}) & 0xFF)
        for (const Pending& p : it->second) {
            uint32_t node = kernel::CallGuest(ctx, kAddHit, {obj + kHitList, p.time, 0, p.source});
            if (!node) continue;
            std::memcpy(kernel::HostPtr(node + 8), p.node, 0x40);
            uint32_t actor = R32(h + 4);
            float before = GetF(actor + 0x90);
            kernel::CallGuest(ctx, 0x3D900, {h, node, 0x3F800000});
            float after = GetF(actor + 0x90);
            if (before > 0 && after <= 0) kernel::CallGuest(ctx, 0x4E320, {R32(h + 0xC), 0x42, 0});
            uint32_t sd = R32(p.source + 0xBC);  // its shooter (a puppet here): the kill's if the bot dies of it
            g_net_hitter[SlotOf(obj) & 15] = {SlotOf(sd ? R32(sd + 0x24) : 0), Now()};
            static int said = 0;
            if (said++ < 5) LOGR("nightfire: LAN: bot slot %d hit from the network: health %.1f -> %.1f", SlotOf(obj), before, after);
        }
    ctx.eax = eax;
    g_pending.erase(it);
}

// our rounds in a puppet's hit list (before its think; a bot's list is handled by its AI's messages, which a puppet's
// AI did not always take): each to the victim's copy, its damage zeroed here; our player's shot shows its hit mark
void CollectHits(X86Context& ctx, uint32_t obj) {
    int vslot = SlotOf(obj);
    uint8_t vpeer, vlocal;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!WhoIs(vslot, vpeer, vlocal)) return;
    }
    for (uint32_t n = R32(obj + kHitList), guard = 0; n && guard < 64; n = R32(n + 4), guard++) {
        uint32_t src = R32(n + 0x4C);
        if (!src || kernel::G<uint8_t>(src + 0xDB) != kProjClass || R32(src + 0xBC) == src + 0x100) continue;
        if (!(GetF(n + 8) > 0)) continue;  // sent already, or harmless
        uint32_t pdata = R32(src + 0xBC), shooter = pdata ? R32(pdata + 0x24) : 0;
        int sslot = SlotOf(shooter);
        uint8_t kpeer, klocal;
        bool ours = sslot >= 0 && !nf::LanPuppet(shooter);  // (it locks)
        if (ours) {
            std::lock_guard<std::mutex> lock(g_lock);
            ours = WhoIs(sslot, kpeer, klocal);
        }
        if (ours) {
            Event e{};
            e.kind = kHitEvent, e.victim_peer = vpeer, e.victim_local = vlocal;
            e.killer_peer = kpeer, e.killer_local = klocal;
            uint32_t entry = pdata ? R32(pdata + 0x38) : 0;
            e.weapon = uint16_t(entry >= 0x18CFA0 ? (entry - 0x18CFA0) / 0x10C : 0);
            std::memcpy(e.node, kernel::HostPtr(n + 8), 0x40);
            Queue(e);
            nf::BloodHit(ctx, obj, n);
            if (sslot < kFirstBot) nf::LanHitMark(sslot, kernel::G<uint16_t>(n + 0x46) == 5);
            static int said = 0;
            if (said++ < 5) LOGR("nightfire: LAN: our slot %d hit %u:%u: sent to its copy", sslot, vpeer, vlocal);
        }
        SetF(n + 8, 0);
    }
}

// our players' deaths (the score slot's deaths went up): to every copy
void WatchDeaths() {
    static uint32_t deaths[10] = {};
    static bool was_on = false;
    bool on = MatchOn();
    for (int i = 0; i < 10; i++) {
        uint32_t d = R32(kScores + uint32_t(i) * kSlotSize + kDeaths);
        bool ours = i < kFirstBot ? i < int(R32(kHumans)) : g_host && SlotObject(i) && !nf::LanPuppet(SlotObject(i));
        if (on && was_on && SlotObject(i) && d > deaths[i] && ours) {
            Event e{};
            e.kind = kDeathEvent, e.victim_peer = uint8_t(std::max(0, g_me.load())), e.victim_local = uint8_t(i);
            e.killer_peer = e.killer_local = 0xFF;
            int by = kernel::G<int16_t>(kScores + uint32_t(i) * kSlotSize + kKiller);
            NetHitter& nh = g_net_hitter[i];
            if (i >= kFirstBot && (by < 0 || by == i) && nh.slot >= 0 && Now() - nh.when < 2000) {
                // a host's bot killed by a hit from the network, counted as its own death: the kill is the hitter's
                uint32_t v = kScores + uint32_t(i) * kSlotSize, k = kScores + uint32_t(nh.slot) * kSlotSize;
                bool points = R32(0x260028) == 0;
                W32(v + 0x04, R32(v + 0x04) + 1);
                if (points) SetF(v + 0x18, GetF(v + 0x18) + 1), SetF(k + 0x18, GetF(k + 0x18) + 1);
                W32(k + 0x04, R32(k + 0x04) + 1), W32(k + 0x10, R32(k + 0x10) + 1);
                kernel::G<int16_t>(v + kKiller) = int16_t(nh.slot);
                by = nh.slot, nh.slot = -1;
            }
            if (by >= 0 && by != i) WhoIs(by, e.killer_peer, e.killer_local);
            Queue(e);
            LOGR("nightfire: LAN: slot %d died (killer slot %d), told the others", i, by);
        }
        deaths[i] = d;
    }
    was_on = on;
}

// ---- puppets' legs: the bots' posture requests (0x341C0: node, variant, ?, ?, ?, ?, {object, data}*) are refused for
// a puppet; before its think we ask for standing (0x0A), walking (0x08), running (0x1E) or sprinting (0x1F) by its
// speed over the network, and the think's last step (0x35CB0) plays it
}  // namespace

namespace nf {
// from src/game/hit_feedback.cpp before 0x3D900 (the pair {victim, data}, the hit record): 0 the game's, 1 refused,
// 2 refused, a hit of ours on a puppet sent to its copy (the hit mark shown)
int LanDamage(uint32_t pair, uint32_t hit) {
    if (!g_on || !pair || !hit) return 0;
    uint32_t victim = R32(pair);
    int vslot = SlotOf(victim);
    if (vslot < kFirstBot) return 0;
    uint32_t proj = R32(hit + 0x4C);
    if (proj && R32(proj + 0xBC) == proj + 0x100) return 0;  // a stand-in's record: a hit from the network, applied
    uint32_t pdata = proj ? R32(proj + 0xBC) : 0, shooter = pdata ? R32(pdata + 0x24) : 0;
    int sslot = SlotOf(shooter);
    bool shooter_puppet = shooter && nf::LanPuppet(shooter);
    if (!nf::LanPuppet(victim)) return shooter_puppet ? 1 : 0;  // the host's own bot: the game's, but replayed rounds
    return 1;  // a puppet takes nothing here (our rounds' records were sent before its think: CollectHits)
    (void)sslot;
    uint8_t vpeer, vlocal, kpeer, klocal;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!WhoIs(vslot, vpeer, vlocal) || !WhoIs(sslot, kpeer, klocal)) return 1;
    }
    Event e{};
    e.kind = kHitEvent, e.victim_peer = vpeer, e.victim_local = vlocal;
    e.killer_peer = kpeer, e.killer_local = klocal;
    uint32_t entry = pdata ? R32(pdata + 0x38) : 0;
    e.weapon = uint16_t(entry >= 0x18CFA0 ? (entry - 0x18CFA0) / 0x10C : 0);
    std::memcpy(e.node, kernel::HostPtr(hit + 8), 0x40);
    Queue(e);
    static int said = 0;
    if (said++ < 5) LOGR("nightfire: LAN: our slot %d hit %u:%u: sent to its copy", sslot, vpeer, vlocal);
    return sslot < kFirstBot ? 2 : 1;  // our player's hit: its mark
}
}  // namespace nf

X86_EXTERN_FUNC(__imp__sub_000341C0);
X86_FUNC(sub_000341C0) {  // a bot's posture request
    uint32_t pair = R32(ctx.esp + 0x1C);
    if (!g_own_posture && g_on && pair && nf::LanPuppet(R32(pair))) {
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    __imp__sub_000341C0(ctx, base);
}

// NF_LAN_TEST (diagnostics): "aim" turns player 1 to the nearest puppet each frame ("aimbot": of the host's bots); "meet" stands player 1 5 m in
// front of the first puppet, facing it, once a second
namespace {
void TestFrame() {
    static const std::string test = [] {
        const char* e = std::getenv("NF_LAN_TEST");
        return std::string(e ? e : "");
    }();
    if (test.empty() || !MatchOn()) return;
    uint32_t me = SlotObject(0);
    if (!me) return;
    uint32_t best = 0;
    float bd = 1e9f;
    for (int s = kFirstBot; s < 10; s++) {
        uint32_t o = SlotObject(s);
        if (!o || !nf::LanPuppet(o) || (PuppetKey(s) >> 8) == kOpen) continue;  // not an open place
        if ((test == "aimbot" || test == "huntbot") && (PuppetKey(s) & 0xFF) < kFirstBot) continue;  // the host's AI bots only
        float dx = GetF(o + kPos) - GetF(me + kPos), dz = GetF(o + kPos + 8) - GetF(me + kPos + 8);
        if (dx * dx + dz * dz < bd) bd = dx * dx + dz * dz, best = o;
    }
    if (!best) return;
    if (test == "huntbot") {  // before it, 5 m away, once a second; then aimed as "aim"
        static int64_t last = 0;
        if (Now() - last > 1000) {
            last = Now();
            float yaw = GetF(best + kYaw);
            SetF(me + kPos, GetF(best + kPos) + std::sin(yaw) * 5);
            SetF(me + kPos + 4, GetF(best + kPos + 4));
            SetF(me + kPos + 8, GetF(best + kPos + 8) + std::cos(yaw) * 5);
        }
    }
    if (test == "aim" || test == "aimbot" || test == "huntbot") {
        float dx = GetF(best + kPos) - GetF(me + kPos), dz = GetF(best + kPos + 8) - GetF(me + kPos + 8);
        float dy = GetF(best + kPos + 4) - GetF(me + kPos + 4);  // feet to feet: the chest from the eye is about as low
        SetF(me + kYaw, std::atan2(dx, dz));
        if (uint32_t actor = R32(me + 0xBC))  // the pitch: a quarter turn up is 1 (mouse.cpp)
            SetF(actor + 0x838, std::atan2(dy - 0.5f, std::sqrt(dx * dx + dz * dz)) / 1.5707964f), kernel::G<uint8_t>(actor + 0xF0) = 1;
        static int n = 0;
        if (n++ % 120 == 0)
            LOGR("nightfire: LAN test: aim at the puppet %.1f m away, %.1f m up", std::sqrt(dx * dx + dz * dz), dy);
    } else if (test == "meet") {
        static int64_t last = 0;
        if (Now() - last < 1000) return;
        last = Now();
        float yaw = GetF(best + kYaw);
        SetF(me + kPos, GetF(best + kPos) + std::sin(yaw) * 5);
        SetF(me + kPos + 4, GetF(best + kPos + 4));
        SetF(me + kPos + 8, GetF(best + kPos + 8) + std::cos(yaw) * 5);
        SetF(me + kYaw, yaw + 3.14159265f);
    }
}
}  // namespace

// a player's movement and collisions (0xAB450, cdecl: the object; from 0xAE27E): its hit list was emptied and filled
// again here, and 0xAE39F walks it next: the records from the network go in now
X86_EXTERN_FUNC(__imp__sub_000AB450);
X86_FUNC(sub_000AB450) {
    uint32_t obj = R32(ctx.esp + 4);
    __imp__sub_000AB450(ctx, base);
    nf::CoopHits(obj);  // story co-op: the other player's rounds (friendly fire)
    if (!g_on) return;
    // a round from a puppet (another copy's shot, replayed for its tracer) does nothing: its copy sends the hits
    for (uint32_t n = R32(obj + kHitList), guard = 0; n && guard < 64; n = R32(n + 4), guard++) {
        uint32_t src = R32(n + 0x4C);
        if (!src || kernel::G<uint8_t>(src + 0xDB) != kProjClass || R32(src + 0xBC) == src + 0x100) continue;
        uint32_t data = R32(src + 0xBC);
        if (data && nf::LanPuppet(R32(data + 0x24))) W32(n + 8, 0);
    }
    Inject(ctx, obj);
}

// ---- everything unlocked in network play: the game's own "all unlocked" byte (0x25D79E: a locked entry can be chosen,
// 0x874DA, 0x87809 ...) and each entry's own unlock byte in its list (+0x10 of 0x18-byte entries, what the lists draw
// dimmed and call locked: 0x17C580 12, the maps 0x17C6A0 8, 0x17C760 3, the scenarios 0x17C7A8 13, the characters
// 0x17C8E0 29; one run of tables to 0x17CB98) are set while a session is on and put back after. The profile (0x19A10
// saves them, 0x19B05) is saved with the player's own; a profile loaded meanwhile is taken as the player's own.
namespace {
constexpr uint32_t kUnlockAll = 0x25D79E, kUnlockTables = 0x17C580, kUnlockEnd = 0x17CB98, kEntry = 0x18;
bool g_unlocking = false;
std::vector<std::pair<uint32_t, uint8_t>> g_own_unlocks;  // each byte and the player's own value

std::vector<uint32_t> UnlockBytes() {
    std::vector<uint32_t> v = {kUnlockAll};
    for (uint32_t a = kUnlockTables; a < kUnlockEnd; a += kEntry) v.push_back(a + 0x10);
    return v;
}
void PutOwn(bool own) {
    for (auto& [a, v] : g_own_unlocks) kernel::G<uint8_t>(a) = own ? v : 1;
}

void Unlocks() {
    if (g_on) {
        if (!g_unlocking) {
            g_unlocking = true;
            g_own_unlocks.clear();
            for (uint32_t a : UnlockBytes()) g_own_unlocks.push_back({a, kernel::G<uint8_t>(a)});
            LOGR("nightfire: LAN: everything unlocked for network play (%zu entries; the profile keeps its own)",
                 g_own_unlocks.size());
        } else {
            for (auto& [a, v] : g_own_unlocks)  // a profile loaded meanwhile: its own
                if (kernel::G<uint8_t>(a) != 1) v = kernel::G<uint8_t>(a);
        }
        PutOwn(false);
    } else if (g_unlocking) {
        g_unlocking = false;
        PutOwn(true);
        LOGR("nightfire: LAN: unlocks as the profile has them again");
    }
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00019A10);
X86_FUNC(sub_00019A10) {  // the profile saved: with the player's own unlocks, not network play's
    if (g_unlocking) PutOwn(true);
    __imp__sub_00019A10(ctx, base);
    if (g_unlocking) PutOwn(false);
}
