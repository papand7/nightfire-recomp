// The keyboard and mouse play as the NightFire controller style whatever style the player chose.
//
// default.xbe reads the pads into each player's controls in 0xDE720 (cdecl: the players, their count; each player
// 0x158 bytes from 0x1FE6D0): a switch on the player's controller style (the u16 at +0xE: NightFire 0, Moonraker 1,
// Octopussy 2, Goldfinger 3, Dr. No 4, Thunderball 5, GoldenEye 6, Classic Bond 7) picks which stick axis is moving,
// strafing, turning and looking and which button does what. The keys are laid out for style 0's controls page. While
// controller 1 is played on the keyboard and mouse its style is temporarily 0 during pad decoding.
// Restore it immediately afterwards so menus and profile saves see the chosen joystick layout.
//
// Crouch on the keyboard is held, not toggled: the game toggles on A (style 0's controls +0x34, set while held). A
// character's posture is its object's word +0xD2, read by 0xA8A60 (its first person eye, a callback: the object):
// 4 crouched (the eye lowers by up to 0.45 at actor +0x89C), 5-9 and 13-15 standing (it rises), 10-12 and 16 keep the
// eye as it is. While the keyboard plays, A reaches the game as a single frame's press whenever the key held and the
// posture differ (again a few frames later if the posture did not change: no room to stand).
//
// Sprint (the original has none): its keys (the left stick click's binding, Shift; unused on foot by the game) held
// while the keyboard plays, moving forward and standing. 0xA9CF0 (cdecl: the actor, the object, a speed) makes a
// character's walk from its controls (2 forward, 0 and 1 to the side), the stick capped at full, eased, then times
// the speed: that speed is raised (nf::SprintFactor, src/game/mouse.cpp).
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/app.h"
#include "common/nightfire.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/style.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kControls = 0x1FE6D0, kPlayerSize = 0x158, kStyle = 0xE, kPort = 0x156;
int g_player = -1;     // controller 1's keyboard player
bool g_forced = false;
uint16_t g_chosen = 0;  // its own style meanwhile
constexpr uint32_t kCrouchSlot = 0x34, kActor = 0xBC, kPlayerIndex = 0x8DE, kPosture = 0xD2, kEyeDrop = 0x89C;
uint32_t g_objects[4] = {};  // each player's character object, from 0xA8A60 last frame
constexpr uint32_t kForward = 0x1C, kPos = 0x24;
constexpr float kSprint = 1.3f;  // 1.6 felt too fast
bool g_sprint[4] = {};  // the player sprints this frame

bool KeyHeld(int vk) { return input::KeyHeld(vk); }  // the game window in front

bool Crouched(uint32_t obj);

void Sprint() {
    static const int test = [] {  // NF_SPRINT_TEST=1 / 2: player 1 walks / sprints whatever plays it; its speed logged
        const char* e = std::getenv("NF_SPRINT_TEST");
        return e && *e ? std::atoi(e) : 0;
    }();
    for (bool& b : g_sprint) b = false;
    int player = test ? 0 : g_player;
    if (player < 0 || (!test && !input::UsingKeyboard())) return;
    uint32_t obj = g_objects[player];
    float& fwd = kernel::G<float>(kControls + uint32_t(player) * kPlayerSize + kForward);
    bool sprint = test == 2;
    for (uint16_t k : input::ControlKeys(input::kLThumb)) sprint |= !test && k && KeyHeld(k);
    g_sprint[player] = sprint && fwd > 0.3f && obj && !Crouched(obj);
    if (test && obj) {
        static float last[3];
        static int frames = 0;
        static float travelled = 0;
        float p[3];
        for (int k = 0; k < 3; k++) p[k] = kernel::G<float>(obj + kPos + 4 * k);
        float d = std::sqrt((p[0] - last[0]) * (p[0] - last[0]) + (p[2] - last[2]) * (p[2] - last[2]));
        std::copy(p, p + 3, last);
        if (fwd <= 0.3f || d > 50.0f) return;  // moving forward only
        travelled += d;
        if (++frames == 60) {
            LOGR("nightfire: sprint test: forward %.2f, %.2f units the last 60 frames", fwd, travelled);
            frames = 0, travelled = 0;
        }
    }
}

bool Crouched(uint32_t obj) {
    int posture = kernel::G<int16_t>(obj + kPosture);
    if (posture == 4) return true;
    if (posture == 10 || posture == 11 || posture == 12 || posture == 16)  // the eye kept: as low as it is
        return kernel::G<float>(kernel::R32(obj + kActor) + kEyeDrop) > 0.225f;
    return false;
}

// the keyboard's crouch key held: A pressed for a frame when the posture is not the key's
void HoldToCrouch() {
    static bool sent = false;
    static int wait = 0;
    static const bool test = [] {  // NF_CROUCH_TEST=1: the pad's A held as the key is, the posture logged
        const char* e = std::getenv("NF_CROUCH_TEST");
        return e && *e == '1';
    }();
    int player = test ? 0 : g_player;
    if (player < 0 || (!test && !input::UsingKeyboard())) return;
    float& a = kernel::G<float>(kControls + uint32_t(player) * kPlayerSize + kCrouchSlot);
    uint32_t obj = g_objects[player];
    if (!obj) return;
    if (test) {
        static int said = -1;
        int posture = kernel::G<int16_t>(obj + kPosture) * 2 + (a > 0.5f);
        if (posture != said) LOGR("nightfire: crouch test: held %d, posture %d", posture & 1, posture >> 1);
        said = posture;
    }
    bool held = a > 0.5f, want = held != Crouched(obj) && !sent && wait <= 0;
    a = want ? 1.0f : 0.0f;
    if (want) wait = 12;  // the posture's change waited for before another press
    wait--;
    sent = want;
}

}  // namespace

namespace nf {
float SprintFactor(int player) { return player >= 0 && player < 4 && g_sprint[player] ? kSprint : 1.0f; }
}  // namespace nf

X86_EXTERN_FUNC(__imp__sub_000DE720);
X86_FUNC(sub_000DE720) {
    uint32_t players = kernel::R32(ctx.esp + 4), count = kernel::R32(ctx.esp + 8) & 0xFFFF;
    // Apply the saved layout when it changes or controller 1 acquires a new player. Afterwards the original
    // Controller Setup page can still change the live layout; keep that choice in the PC settings too.
    static uint32_t applied = ~0u;
    static int owner = -1;
    int current = -1;
    if (players == kControls && count <= 4) {
        for (uint32_t i = 0; i < count; i++)
            if (kernel::G<uint8_t>(kControls + i * kPlayerSize + kPort) == 0) { current = int(i); break; }
        if (current >= 0) {
            auto& style = kernel::G<uint16_t>(kControls + uint32_t(current) * kPlayerSize + kStyle);
            nf::controller_scheme = std::min(nf::controller_scheme, 7u);
            if (applied != nf::controller_scheme || owner != current) {
                style = uint16_t(nf::controller_scheme);
                applied = nf::controller_scheme;
            } else {
                uint32_t chosen = style;
                if (chosen <= 7 && chosen != nf::controller_scheme) {
                    nf::controller_scheme = applied = chosen;
                    app::Save();
                }
            }
        }
        owner = current;
    }
    g_player = input::UsingKeyboard() ? current : -1;
    g_forced = g_player >= 0;
    if (g_forced) {
        auto& style = kernel::G<uint16_t>(kControls + uint32_t(g_player) * kPlayerSize + kStyle);
        g_chosen = style;
        style = 0;
    }
    __imp__sub_000DE720(ctx, base);
    if (g_forced) {
        kernel::G<uint16_t>(kControls + uint32_t(g_player) * kPlayerSize + kStyle) = g_chosen;
        g_forced = false;
    }
    if (players == kControls) {
        nf::AimDownSights(count), nf::AimFireTest(ctx), nf::LanFrame(ctx), nf::ModernMenuFrame(ctx);
        nf::PhotoFrame(ctx), nf::QuickFrame(ctx);  // photo mode, the quick restart key, Discord's presence
        gpu::SetAiming(nf::AimingDownSights());  // depth of field's focus blurs more while aiming
    }
    if (players == kControls) {
        static const bool trace = std::getenv("NF_MENU_TRACE") != nullptr;
        static uint32_t last_page = 0, last_focus = 0;
        if (trace && nf::MenuUp()) {
            uint32_t page = kernel::R32(0x25F1D0 + 0x1BC);
            uint32_t focus = page ? kernel::R32(page + 0xAC) : 0;
            if (page != last_page || focus != last_focus) {
                LOGR("nightfire: menu trace: page %08X focus %08X height %d scheme %u",
                     page ? kernel::R32(page + 0x18) : 0, focus ? kernel::R32(focus + 0x18) : 0,
                     focus ? kernel::G<int16_t>(focus + 0x76) : 0, nf::controller_scheme);
                last_page = page, last_focus = focus;
            }
        }
        HoldToCrouch(), Sprint();
    }
    nf::MouseAsStick();
}

X86_EXTERN_FUNC(__imp__sub_000A8A60);
X86_FUNC(sub_000A8A60) {  // a character's first person eye (cdecl: the object): the players' objects kept
    uint32_t obj = kernel::R32(ctx.esp + 4), actor = obj ? kernel::R32(obj + kActor) : 0;
    int i = actor ? kernel::G<int8_t>(actor + kPlayerIndex) : -1;
    if (i >= 0 && i < 4) g_objects[i] = obj;
    __imp__sub_000A8A60(ctx, base);
}

// a player's controls packed for the profile and the launch data (cdecl: a tag, the player, ...): with its own style
X86_EXTERN_FUNC(__imp__sub_0006ED30);
X86_FUNC(sub_0006ED30) {
    int player = int(kernel::R32(ctx.esp + 8) & 0xFFFF);
    if (!g_forced || player != g_player) return __imp__sub_0006ED30(ctx, base);
    uint16_t& style = kernel::G<uint16_t>(kControls + uint32_t(player) * kPlayerSize + kStyle);
    style = g_chosen;
    __imp__sub_0006ED30(ctx, base);
    style = 0;
}
