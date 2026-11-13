// The sight tuner's host side (auf::sight_tuner, debug): src/bond/aim.cpp does the tuning and fills auf::sight_view each
// frame; here its panel and a red dot at the true centre of the screen (where the shots go) are drawn, and the arrow keys
// are kept from the game's d-pad (the shooter keys have them as the weapon and gadget switch) while it tunes.
#include <chrono>
#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "common/auf.h"
#include "core/input.h"

#ifndef XBE_HEADLESS  // ImGui: not in the headless harness
#include <imgui.h>
#endif

namespace auf {
bool sight_tuner = false;
SightTunerView sight_view;

void SightTunerPad(int port, input::Pad& p) {
#ifdef _WIN32
    if (port != 0 || !sight_tuner || !sight_view.on) return;
    // the d-pad bits (up, down, left, right) a held arrow key gave
    static const int kArrows[4] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT};
    for (int k = 0; k < 4; k++)
        if (GetAsyncKeyState(kArrows[k]) & 0x8000) p.buttons &= uint16_t(~(1u << k));
#else
    (void)port, (void)p;
#endif
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
        std::snprintf(line, sizeof line, "SIGHT TUNER  %s: scoped, nothing to tune. Switch weapon with the game's keys (Q, 1-9).",
                      v.weapon[0] ? v.weapon : "?");
    else
        std::snprintf(line, sizeof line,
                      "SIGHT TUNER  %s   across %+.2f   up %+.2f   ahead %+.2f\n"
                      "Arrows move the gun   Home/End nearer/farther   Shift finer   Enter save all   Delete reset this one\n"
                      "Switch weapon with the game's keys (Q, 1-9). Put the sights on the red dot: that is where the shots go.%s",
                      v.weapon[0] ? v.weapon : "?", v.x, v.y, v.z,
                      v.saved_ms && now - v.saved_ms < 2500 ? "\nSaved to sight_offsets.txt" : "");
    const ImVec2 at(vp->Pos.x + 24, vp->Pos.y + vp->Size.y * 0.72f);
    const ImVec2 size = ImGui::CalcTextSize(line);
    dl->AddRectFilled(ImVec2(at.x - 10, at.y - 8), ImVec2(at.x + size.x + 10, at.y + size.y + 8), IM_COL32(0, 0, 0, 170), 6);
    dl->AddText(at, IM_COL32(255, 255, 255, 255), line);
#endif
}
}  // namespace auf
