// NightFire's achievements. Conditions are in RetroAchievements' syntax, on default.xbe's memory
// (.local/progress_re.md has the map and how each part was found).
//
// Every mission's completion is scored in default.xbe, driving ones too: after Driving.xbe hands back, the Results
// page (0x40000036 at 0x25F388) comes up with the mission mode 6 (success, 0x17E544) and the level just done at
// 0x1F65C8. Loading a codename restores the whole profile at once and never makes that state, so a finished save
// awards nothing: each achievement is that state for the mission it names, plus what it asks for. Mission i's score
// record is 0x3C bytes at 0x17F198 + 0x3C i (bronze/silver/gold/platinum targets at +4..+0x10, the best total +0x30,
// this run's +0x34), its category stats 0x14 bytes each from 0x17E8F8 + 0xB8 i (Bond Moves first: +0 the target, +8
// this run's count). The difficulty played is 0x1F65A8 (1 Operative, 2 Agent, 3 00 Agent). Driving.xbe gets the same
// list with a condition that never holds: the awards are default.xbe's alone, both exes show them.
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "core/achievements.h"

namespace nf {
extern const bool kDriving;  // src/driving/hooks.cpp, src/game/hooks.cpp

namespace {

struct Mission {
    const char* id;
    const char* name;
    uint32_t level;  // its level id (0x09xxxxxx Driving.xbe's)
    bool bond_moves;  // it has Bond Moves to find (a target above 0) and they are counted in default.xbe
};
constexpr Mission kMissions[12] = {
    {"paris_prelude", "Paris Prelude", 0x09000001, false},
    {"the_exchange", "The Exchange", 0x07000005, true},
    {"alpine_escape", "Alpine Escape", 0x09000005, true},
    {"enemies_vanquished", "Enemies Vanquished", 0x09000006, true},
    {"double_cross", "Double Cross", 0x07000001, true},
    {"night_shift", "Night Shift", 0x07000009, true},
    {"chain_reaction", "Chain Reaction", 0x0700000C, true},
    {"phoenix_fire", "Phoenix Fire", 0x07000011, true},
    {"deep_descent", "Deep Descent", 0x09000002, true},
    {"island_infiltration", "Island Infiltration", 0x09000003, true},
    {"countdown", "Countdown", 0x07000014, true},
    {"equinox", "Equinox", 0x0700001B, false},
};
constexpr int kMedalGold = 0xC, kMedalPlatinum = 0x10;  // the targets' offsets in a score record

const char* Keep(std::string s) {
    static std::deque<std::string> kept;
    return kept.emplace_back(std::move(s)).c_str();
}
std::string Hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%x", v);
    return b;
}
uint32_t Record(int i) { return 0x17F198 + 0x3C * uint32_t(i); }
uint32_t Stats(int i) { return 0x17E8F8 + 0xB8 * uint32_t(i); }

const std::string kResults = "0xX25f388=h40000036_0xX17e544=6";  // a mission just completed
std::string Done(int i) { return "0xX1f65c8=h" + Hex(kMissions[i].level); }
// this run's total at least the medal's target
std::string Medal(int i, int medal) { return "0xX" + Hex(Record(i) + 0x34) + ">=0xX" + Hex(Record(i) + uint32_t(medal)); }
// the best total at least the medal's target
std::string BestMedal(int i, int medal) {
    return "0xX" + Hex(Record(i) + 0x30) + ">=0xX" + Hex(Record(i) + uint32_t(medal));
}

// mission i completed (and what else is asked)
const char* Completed(int i, const std::string& also = "") {
    return Keep(kResults + "_" + Done(i) + (also.empty() ? "" : "_" + also));
}
// any mission completed with what `each` asks of it: one alternative group a mission
template <class Each>
const char* AnyMission(Each each, bool (*which)(int) = nullptr) {
    std::string c = kResults;
    for (int i = 0; i < 12; i++)
        if (!which || which(i)) c += "S" + Done(i) + "_" + each(i);
    return Keep(c);
}
// a completion with every mission's best at least the medal
const char* EveryBest(int medal) {
    std::string c = kResults;
    for (int i = 0; i < 12; i++) c += "_" + BestMedal(i, medal);
    return Keep(c);
}

}  // namespace

// a story mission's name by its level id (a level's variants, 0x0710.., as the mission's), else null
const char* MissionName(uint32_t level) {
    for (const Mission& m : kMissions)
        if ((level & 0xFF0FFFFF) == m.level) return m.name;
    return nullptr;
}

std::vector<achievements::Def> AchievementSet() {
    std::vector<achievements::Def> set;
    for (int i = 0; i < 12; i++) {
        const Mission& m = kMissions[i];
        set.push_back({m.id, m.name, Keep(std::string("Complete ") + m.name + "."), i == 11 ? 25u : 10u,
                       Completed(i)});
    }
    set.push_back({"gold_standard", "Gold Standard", "Earn a gold medal on any mission.", 10,
                   AnyMission([](int i) { return Medal(i, kMedalGold); })});
    set.push_back({"platinum_standard", "Platinum Standard", "Earn a platinum medal on any mission.", 25,
                   AnyMission([](int i) { return Medal(i, kMedalPlatinum); })});
    set.push_back({"golden_service", "Golden Service", "Hold a gold medal on every mission.", 50, EveryBest(kMedalGold)});
    set.push_back({"platinum_service", "Platinum Service", "Hold a platinum medal on every mission.", 100,
                   EveryBest(kMedalPlatinum)});
    set.push_back({"double_o", "Double-O Status", "Complete any mission on 00 Agent.", 10,
                   AnyMission([](int i) { return std::string("0xX1f65a8=3"); })});
    set.push_back({"licensed_to_thrill", "Licensed to Thrill", "Complete Equinox on 00 Agent.", 50,
                   Completed(11, "0xX1f65a8=3")});
    set.push_back({"bond_moves", "Shaken, Not Stirred", "Find every Bond Move in a mission.", 10,
                   AnyMission([](int i) { return "0xX" + Hex(Stats(i) + 8) + ">=0xX" + Hex(Stats(i)); },
                              [](int i) { return kMissions[i].bond_moves; })});
    if (kDriving)  // the awards are default.xbe's (the Results page); Driving.xbe shows them
        for (achievements::Def& d : set) d.condition = "0xX0=1_0xX0=0";
    return set;
}
}  // namespace nf
