// The sight tuner's host side (auf::sight_tuner, debug): src/bond/aim.cpp does the tuning and fills auf::sight_view each
// frame; here its panel and a red dot at the true centre of the screen (where the shots go) are drawn, and its keys are
// kept from the game while it tunes (as NightFire's input::HoldBackToolKeys).
#include <chrono>
#include <cstdio>

#include "common/auf.h"
#include "core/input.h"

#ifndef XBE_HEADLESS  // ImGui: not in the headless harness
#include <imgui.h>
#endif

namespace auf {
bool sight_tuner = false;
SightTunerView sight_view;

// the tuner's keys (src/bond/aim.cpp, as NightFire's input::HoldBackToolKeys): arrows, Home, End, Enter, Delete, comma,
// full stop, I J K L U O (Windows virtual keys)
bool ToolKey(uint16_t k) {
    switch (k) {
        case 0x25: case 0x27: case 0x26: case 0x28: case 0x24: case 0x23: case 0x0D: case 0x2E:  // arrows, Home, End, Enter, Delete
        case 0xBC: case 0xBE: case 'I': case 'J': case 'K': case 'L': case 'U': case 'O': return true;
        default: return false;
    }
}
void SightTunerPad(int port, input::Pad& p) {
    if (port != 0 || !sight_tuner || !sight_view.on) return;
    // each control bound to a tool key that is held now gives the game nothing
    for (int c = 0; c < input::kControls; c++) {
        bool held = false;
        for (uint16_t k : input::ControlKeys(c)) held |= k && ToolKey(k) && input::KeyDown(k);
        if (!held) continue;
        if (c < 8) p.buttons &= uint16_t(~(1u << c));
        else if (c < input::kLSLeft) p.analog[c - input::kA] = 0;
        else if (c <= input::kLSRight) p.lx = 0;
        else if (c <= input::kLSDown) p.ly = 0;
        else if (c <= input::kRSRight) p.rx = 0;
        else p.ry = 0;
    }
}

void SightTunerUi(double) {
#ifndef XBE_HEADLESS
    if (!sight_tuner || !sight_view.on) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
    dl->AddCircleFilled(c, 4.5f, IM_COL32(0, 0, 0, 200));
    dl->AddCircleFilled(c, 3.0f, IM_COL32(255, 40, 40, 255));
    const SightTunerView& v = sight_view;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
    char line[512];
    if (v.scoped)
        std::snprintf(line, sizeof line, "SIGHT TUNER  %s: scoped, nothing to tune. , and . or Q pick another gun.",
                      v.weapon[0] ? v.weapon : "?");
    else
        std::snprintf(line, sizeof line,
                      "SIGHT TUNER  %s   right %+.2f   up %+.2f   farther %+.2f   turn %+.1f   tilt %+.1f   roll %+.1f\n"
                      ", and . (or Q) pick the gun   Arrows move it   Home/End nearer/farther   J L turn   I K tilt   U O roll\n"
                      "Shift finer   Enter save all   Delete reset this one. Put the sights on the red dot: the shots go there.%s",
                      v.weapon[0] ? v.weapon : "?", v.x, v.y, v.z, v.turn, v.tilt, v.roll,
                      v.saved_ms && now - v.saved_ms < 2500 ? "\nSaved to sight_offsets.txt" : "");
    const ImVec2 at(vp->Pos.x + 24, vp->Pos.y + vp->Size.y * 0.72f);
    const ImVec2 size = ImGui::CalcTextSize(line);
    dl->AddRectFilled(ImVec2(at.x - 10, at.y - 8), ImVec2(at.x + size.x + 10, at.y + size.y + 8), IM_COL32(0, 0, 0, 170), 6);
    dl->AddText(at, IM_COL32(255, 255, 255, 255), line);
#endif
}
}  // namespace auf
