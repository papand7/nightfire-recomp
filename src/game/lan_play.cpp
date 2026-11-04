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
// NF_LAN=host hosts (UDP port 27787, NF_LAN_PORT another); NF_LAN=<address> joins that host. NF_LAN_TRACE=1 logs
// player 1's and the puppets' places every 5 s.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "common/nightfire.h"
#include "game/menu_page.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
using kernel::R32;
using kernel::W32;

constexpr uint32_t kHumans = 0x260044, kBots = 0x260048;
constexpr uint32_t kScores = 0x262738, kSlotSize = 0x30, kSlotObject = 0x1C;
constexpr uint32_t kPos = 0x24, kYaw = 0x40;
constexpr int kFirstBot = 4, kMostBots = 6;
// the MP block's player slots (names, C strings; the character at +0x20), the join page's records (+0 joined), the
// bot setup (0x245280 its own bots, 0x245281 how many, records of 0x12: +0 name, +0xF team, +0x10 character)
constexpr uint32_t kSlots = 0x25FE38, kSlotCharacter = 0x20, kJoined = 0x245240, kJoinSize = 0x10;
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
enum : uint8_t { kHello = 1, kWelcome, kState, kSnap, kPing, kPong, kInfo, kLobby };
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
    uint16_t character;  // the slot's +0x20
    char name[16];
};
constexpr uint32_t kSettings = 0x260030, kSettingsSize = 0x40;  // the match's options (the MP block's, 0x260030-6F)
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
std::vector<Body> g_local;                // this copy's players, from the game's thread
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
uint32_t g_starts = 0;                    // the host's count of matches started from the lobby
std::atomic<bool> g_send_now{false};

#ifdef _WIN32
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

// the host's lobby: its own players, then each client's
std::vector<Named> LobbyList() {
    std::vector<Named> all;
    for (Named n : g_local_names) n.peer = 0, all.push_back(n);
    for (auto& [peer, names] : g_infos)
        if (g_clients.count(peer))
            for (Named n : names) n.peer = uint8_t(peer), all.push_back(n);
    return all;
}
void SendLobby() {
    g_lobby = LobbyList();
    std::vector<uint8_t> body(kSettingsSize + 4);
    std::memcpy(body.data(), g_settings, kSettingsSize);
    std::memcpy(body.data() + kSettingsSize, &g_starts, 4);
    size_t n = std::min<size_t>(g_lobby.size(), 40);
    body.insert(body.end(), (const uint8_t*)g_lobby.data(), (const uint8_t*)(g_lobby.data() + n));
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
    for (;;) {
        Receive();
        int64_t now = Now();
        if (now >= next) {
            next += 1000 / kRate;
            if (next < now) next = now;
            std::lock_guard<std::mutex> lock(g_lock);
            std::vector<Entry> mine;
            for (size_t i = 0; i < g_local.size(); i++) {
                Entry e{};
                e.peer = uint8_t(std::max(0, g_me.load())), e.local = uint8_t(i), e.time = uint32_t(now + g_offset);
                e.body = g_local[i], e.valid = g_in_match;
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
                if (now - last_lobby > 500 || g_send_now.exchange(false)) last_lobby = now, SendLobby();
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
#endif

void Start() {
    static bool started = false;
    if (started) return;
    started = true;
    const char* e = std::getenv("NF_LAN");
    if (!e || !*e) return;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_host = std::strcmp(e, "host") == 0;
    uint16_t port = kPort;
    if (const char* p = std::getenv("NF_LAN_PORT"); p && *p) port = uint16_t(std::atoi(p));
    if (!g_host) {
        g_host_addr.sin_family = AF_INET;
        g_host_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, e, &g_host_addr.sin_addr) != 1) {
            LOGR("nightfire: LAN: '%s' is not an address", e);
            return;
        }
    }
    g_on = true;
    std::string host = e;
    std::thread([port, host] {
        // the port is retried a while: the game relaunches itself (the copy before still has it as it hands over)
        g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in me{};
        me.sin_family = AF_INET;
        me.sin_addr.s_addr = htonl(INADDR_ANY);
        me.sin_port = htons(g_host ? port : 0);
        for (int i = 0; bind(g_sock, (sockaddr*)&me, sizeof me) != 0; i++) {
            if (i == 60) {
                LOGR("nightfire: LAN: UDP port %u could not be opened (%d)", port, WSAGetLastError());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        u_long nb = 1;
        ioctlsocket(g_sock, FIONBIO, &nb);
        if (g_host) g_me = 0, LOGR("nightfire: LAN: hosting on UDP port %u", port);
        else LOGR("nightfire: LAN: joining %s:%u", host.c_str(), port);
        Loop();
    }).detach();
#else
    LOGR("nightfire: LAN: not in this build");
#endif
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

// a match being played (0xA25F0 started one; the bot think ran lately)
std::atomic<int64_t> g_match_seen{-100000};
bool MatchOn() { return Now() - g_match_seen.load() < 1000; }

// a player's name as shown: a copy's default name ("Player 1", the join page's when no codename is loaded) is
// numbered by the player's place in the lobby instead, so two copies' first players differ
std::string ShownName(const Named& m, size_t place) {
    std::string n(m.name, strnlen(m.name, 15));
    size_t sp = n.rfind(' ');
    if (sp != std::string::npos && sp + 2 == n.size() && n.back() == char('1' + m.local))
        n = n.substr(0, sp + 1) + std::to_string(place + 1);
    return n;
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
            if (Now() - r.heard < 2000) out.push_back(Named{uint8_t(k >> 8), uint8_t(k & 0xFF), 0, "Player"});
    return out;
}

int PuppetKey(int slot) {  // the remote driving bot slot, else -1
    for (auto& [k, s] : g_slot_of)
        if (s == slot) return k;
    return -1;
}
}  // namespace

namespace {
void LobbyFrame(X86Context& ctx);
}

namespace nf {
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
    LobbyFrame(ctx);
    if (!g_on) return;
    uint32_t humans = std::min<uint32_t>(R32(kHumans), 4);
    std::vector<Body> local;
    bool in_match = MatchOn();
    for (uint32_t i = 0; in_match && i < humans; i++) {
        uint32_t obj = SlotObject(int(i));
        if (!obj) break;
        Body b;
        for (int k = 0; k < 3; k++) b.pos[k] = GetF(obj + kPos + k * 4);
        b.yaw = GetF(obj + kYaw);
        local.push_back(b);
    }
    in_match = !local.empty();
    if (!in_match) local.resize(std::max<uint32_t>(humans, 1), Body{});
    std::vector<Named> names;
    for (uint32_t i = 0; i < 4; i++) {
        if (!kernel::G<uint8_t>(kJoined + i * kJoinSize)) continue;
        Named n{};
        n.local = uint8_t(i);
        n.character = kernel::G<uint16_t>(kSlots + i * kSlotSize + kSlotCharacter);
        std::memcpy(n.name, &kernel::G<char>(kSlots + i * kSlotSize), 15);
        names.push_back(n);
    }
    std::lock_guard<std::mutex> lock(g_lock);
    g_local = local;
    g_in_match = in_match;
    if (names.size() != g_local_names.size()) g_send_now = true;
    g_local_names = names;
    if (g_host) std::memcpy(g_settings, &kernel::G<uint8_t>(kSettings), kSettingsSize), g_have_settings = true;
}
}  // namespace nf

X86_EXTERN_FUNC(__imp__sub_000A25F0);
X86_FUNC(sub_000A25F0) {  // a multiplayer match starts: the bots are the other copies' players
    Start();
    if (g_on) {
        std::lock_guard<std::mutex> lock(g_lock);
        g_slot_of.clear();
        int n = 0;
        for (const Named& m : Remotes())
            if (n < kMostBots) g_slot_of[uint16_t(m.peer << 8 | m.local)] = kFirstBot + n++;
        kernel::G<uint8_t>(kOwnBots) = 0;
        W32(kBots, uint32_t(n));
        LOGR("nightfire: LAN match: %u players here, %d from the network (bots %d..)", R32(kHumans), n, kFirstBot);
    }
    __imp__sub_000A25F0(ctx, base);
    if (g_on) {  // the puppets go by their players' names
        std::lock_guard<std::mutex> lock(g_lock);
        for (const Named& m : Remotes()) {
            auto it = g_slot_of.find(uint16_t(m.peer << 8 | m.local));
            if (it == g_slot_of.end()) continue;
            char* name = &kernel::G<char>(kSlots + uint32_t(it->second) * kSlotSize);
            std::string shown = ShownName(m, Place(m));
            std::memset(name, 0, 16);
            std::memcpy(name, shown.data(), std::min<size_t>(shown.size(), 15));
        }
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
    int slot = g_on ? SlotOf(obj) : -1;
    __imp__sub_00031530(ctx, base);
    if (slot < kFirstBot) return;
    // a puppet: the AI's own frame ran (it spawns the body and animates it; 0x37C10 gives it no target), then moved
    std::lock_guard<std::mutex> lock(g_lock);
    int key = PuppetKey(slot);
    if (key < 0) return;
    Body b;
    if (!Interpolate(g_remotes[uint16_t(key)].samples, Now() + g_offset - kDelay, b)) return;
    for (int k = 0; k < 3; k++) SetF(obj + kPos + k * 4, b.pos[k]);
    SetF(obj + kYaw, b.yaw);
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

// a projectile fired (0x211F0, cdecl: the shooter's object, ...; the object made, 0 none, the bots' fire event at 0x18302
// then plays no flash or sound): none from a puppet, whose shots are its own copy's
X86_EXTERN_FUNC(__imp__sub_000211F0);
X86_FUNC(sub_000211F0) {
    if (nf::LanPuppet(R32(ctx.esp + 4))) {
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    __imp__sub_000211F0(ctx, base);
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

// the lobby's other players: the first in the Playing list's free rows (after this copy's), the rest as bots
constexpr uint32_t kPlayingRows = 4, kTeams = 0x20000000;
constexpr uint32_t kPlayingName = 0x10000237, kPlayingFace = 0x10000235, kPlayingScore = 0x10000238;
constexpr uint32_t kRowsUsed = 0x25E95B, kListSet = 0x728F0, kCharacters = 0x17CB98;
std::vector<Named> LobbyBots(std::vector<Named>& playing) {
    std::vector<Named> rs = Remotes();
    if (rs.size() > size_t(kMostBots)) rs.resize(kMostBots);
    playing.clear();
    uint32_t local = 0;
    for (uint32_t i = 0; i < 4; i++) local += kernel::G<uint8_t>(kJoined + i * kJoinSize) ? 1 : 0;
    if (!(R32(0x26003C) & kTeams))
        while (!rs.empty() && local + playing.size() < kPlayingRows) playing.push_back(rs.front()), rs.erase(rs.begin());
    kernel::G<uint8_t>(kOwnBots) = 1;
    kernel::G<uint8_t>(kBotCount) = uint8_t(rs.size());
    for (size_t i = 0; i < rs.size(); i++) {
        uint32_t rec = kBotRecords + uint32_t(i) * kBotRecordSize;
        std::memset(&kernel::G<uint8_t>(rec), 0, 0xF);
        std::memcpy(&kernel::G<char>(rec), rs[i].name, 14);
        kernel::G<uint8_t>(rec + 0xF) = 0;
        kernel::G<uint8_t>(rec + 0x10) = uint8_t(rs[i].character);
    }
    return rs;
}

std::string Shown() {
    std::string k = g_have_settings ? "s" : "";
    for (const Named& m : g_lobby) k += std::string(m.name) + "|";
    if (!g_host && g_have_settings) k.append((const char*)g_settings, kSettingsSize);
    return k;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000891E0);
X86_FUNC(sub_000891E0) {  // the summary page's messages (player, ?, ?, message, ...)
    uint32_t player = R32(ctx.esp + 4), msg = R32(ctx.esp + 0x10);
    if (g_on && msg == 0x4C) g_page = Page::kLobby;
    if (g_on && msg == 0x4D && g_page == Page::kLobby) g_page = Page::kOther;
    if (!g_on || g_page != Page::kLobby) return __imp__sub_000891E0(ctx, base);
    auto fill = [&] {
        std::vector<Named> bots, playing;
        std::vector<std::string> names;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            ApplyHost();
            // this copy's players with a default name: numbered by their place too (their slot's name, in the match too)
            for (uint32_t i = 0; i < 4 && g_me.load() >= 0; i++) {
                if (!kernel::G<uint8_t>(kJoined + i * kJoinSize)) continue;
                Named me{uint8_t(g_me.load()), uint8_t(i), 0, {}};
                char* slot = &kernel::G<char>(kSlots + i * kSlotSize);
                std::memcpy(me.name, slot, 15);
                bool listed = false;
                for (const Named& m : g_lobby) listed |= m.peer == me.peer && m.local == me.local;
                if (!listed) continue;
                std::string n = ShownName(me, Place(me));
                std::memset(slot, 0, 16);
                std::memcpy(slot, n.data(), std::min<size_t>(n.size(), 15));
            }
            bots = LobbyBots(playing);
            for (const Named& m : playing) names.push_back(ShownName(m, Place(m)));
            g_shown = Shown();
        }
        W32(ctx.esp + 0x10, 0x4C);
        __imp__sub_000891E0(ctx, base);
        W32(ctx.esp + 0x10, msg);
        // as the fill shows a joined player (0x89375): the name, the score line, the face (the character's)
        uint32_t eax = ctx.eax;
        for (size_t i = 0; i < playing.size(); i++) {
            uint32_t row = kernel::G<uint8_t>(kRowsUsed);
            if (row >= kPlayingRows) break;
            kernel::G<uint8_t>(kRowsUsed) = uint8_t(row + 1);
            auto set = [&](uint32_t id, uint32_t m, uint32_t a) { kernel::CallGuest(ctx, kListSet, {player, id, row, m, a, 0}); };
            set(kPlayingName, 0x2B, 2);
            set(kPlayingName, 0x18, nf::MenuString(names[i].c_str()));
            set(kPlayingScore, 0x2B, 2);
            set(kPlayingScore, 0x18, nf::MenuString("+0"));
            set(kPlayingFace, 0x2B, 2);
            set(kPlayingFace, 0x24, R32(kCharacters + uint32_t(playing[i].character) * 0x18));
        }
        ctx.eax = eax;
        bots.insert(bots.begin(), playing.begin(), playing.end());
        static size_t said = ~size_t(0);
        if (said != bots.size()) {
            said = bots.size();
            LOGR("nightfire: LAN lobby: %zu players from the network listed (%s)", bots.size(),
                 g_host ? "the host: its options" : "a client: the host's options, only the host changes them");
        }
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
        if (!g_host) {  // a client's A: only the host starts
            ctx.eax = 1;
            ctx.esp += 4;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_starts++;
            g_seen_starts = g_starts;
        }
        g_send_now = true;
        LOGR("nightfire: LAN lobby: the host starts the match (map %08X)", R32(0x260040));
    }
    __imp__sub_000891E0(ctx, base);
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
