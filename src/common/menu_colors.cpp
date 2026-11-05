// Settings::menu_colors (blue and silver, after the PC version's menus): the front end's background movies recoloured
// as they are bound, the text, the widgets and the movies' animation left as the console's.
//
// The menus' backgrounds are XMV movies decoded into 640 x 480 linear YUY2 textures (format 0x24, two of them,
// written each frame). Their warm chroma (orange, red, gold: Cr above, Cb below the grey) is turned about the grey to
// a steel blue, pale golds to a blue silver; a pixel already cool is left, so a frame bound twice is not turned back.
// The main menu's movie (page 0x40000002) holds Bond: right of its menu he keeps his colours, only the strong
// orange of the background behind him (more chroma than any skin there) turns. A frame already done (a sparse sum of
// its bytes as it was left) is passed over.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "common/nightfire.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {
constexpr uint32_t kYuy2 = 0x24, kWidth = 640, kHeight = 480;
constexpr uint32_t kMainMenu = 0x40000002;  // the front end's page (MenuGeometry)
constexpr int kBondX = 382, kBondRight = 578, kBondEdge = 12, kBondBottom = 414;  // movie pixels: Bond's columns, above y
constexpr float kTurn = 3.40f;                                              // radians about the grey: orange to blue
constexpr float kKeep = 0.9f;                                               // of the chroma kept

std::unordered_map<uint32_t, uint64_t> g_done;  // a movie texture's data: its sum after the last turn

uint64_t Sum(const uint8_t* bytes, uint32_t n) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 1; i < n; i += 509) h = (h ^ bytes[i]) * 1099511628211ull;
    return h;
}

float Smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// a YUY2 pair's chroma (bytes 1 and 3), `amount` of the way to blue
void Turn(uint8_t* uv, float amount) {
    float u = uv[0] - 128.0f, v = uv[2] - 128.0f;
    if (v <= 0 || u >= v * 0.4f) return;  // not warm: already blue or grey
    float c = std::cos(kTurn) * kKeep, s = std::sin(kTurn) * kKeep;
    float nu = u * c - v * s, nv = u * s + v * c;
    nu = u + (nu - u) * amount, nv = v + (nv - v) * amount;
    uv[0] = uint8_t(std::clamp(nu + 128.5f, 0.0f, 255.0f));
    uv[2] = uint8_t(std::clamp(nv + 128.5f, 0.0f, 255.0f));
}
}  // namespace

namespace nf {

// GameInfo::texture_bound: true when the movie's texels were changed
bool MenuMovie(uint32_t header) {
    if (!app::Get().menu_colors || !MenuUp()) return false;
    uint32_t format = kernel::R32(header + 12), size = kernel::R32(header + 16);
    if ((format >> 8 & 0xFF) != kYuy2 || !size || (size & 0xFFF) + 1 != kWidth || (size >> 12 & 0xFFF) + 1 != kHeight)
        return false;
    uint32_t pitch = ((size >> 24) + 1) * 64, data = mem::VirtualFromPhysical(kernel::R32(header + 4));
    if (pitch < kWidth * 2 || !mem::IsCommitted(data) || !mem::IsCommitted(data + pitch * kHeight - 1)) return false;
    auto* bytes = static_cast<uint8_t*>(kernel::HostPtr(data));
    auto done = g_done.find(data);
    if (done != g_done.end() && done->second == Sum(bytes, pitch * kHeight)) return false;
    uint32_t page = 0, focused = 0;
    std::vector<MenuWidget> widgets;
    bool bond = !kDriving && MenuGeometry(page, widgets, focused) && page == kMainMenu;
    for (uint32_t y = 0; y < kHeight; y++) {
        uint8_t* row = bytes + y * pitch;
        for (uint32_t x = 0; x < kWidth; x += 2) {
            uint8_t* p = row + x * 2;
            float amount = 1;
            if (bond && y < kBondBottom) {
                float in = Smooth(float(kBondX - kBondEdge), float(kBondX), float(x)) *
                           (1 - Smooth(float(kBondRight), float(kBondRight + kBondEdge), float(x)));
                if (in > 0) {  // Bond's side: the background's strong orange only
                    float u = p[1] - 128.0f, v = p[3] - 128.0f;
                    amount = 1 - in * (1 - Smooth(50, 62, std::sqrt(u * u + v * v)));
                }
            }
            if (amount > 0) Turn(p + 1, amount);
        }
    }
    g_done[data] = Sum(bytes, pitch * kHeight);
    return true;
}
}  // namespace nf
