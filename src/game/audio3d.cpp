// 3D sound (app::Settings::audio_3d; the mixer's side in sdk/runtime/xdk/dsound.cpp): the world around the listener,
// told to the mixer each frame from the frame's 2D pass (hud.cpp). Through the game's ray (bones::Ray, 0x2C4E0, past
// player 1, in its zone):
//  - the room: every 4th frame thirteen rays from the listener, up, four ways up at 45 degrees and eight ways level,
//    kReach long. Indoor is how much is roofed (the ray up most, the slanted ones the rest); the size the mean of all
//    thirteen's lengths (a miss kReach). Eased, then audio::SetRoom (a reverb, longer the bigger).
//  - walls: each frame up to kPerFrame of the playing 3D voices in turn, two rays from the listener to the voice (one
//    0.6 higher at both ends, round a low wall): each that hits a wall short of it muffles it by half (SetOcclusion).
// The sound's places are the world's with z turned round (DirectSound's left-handed axes): turned back for the rays.
// Out of play (no player 1) the room is none, so the menus stay dry. NF_AUDIO_TRACE=1 logs the room and the voices.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "app/app.h"
#include "audio/audio.h"
#include "common/nightfire.h"
#include "core/log.h"
#include "game/bones.h"
#include "kernel/kernel.h"

namespace {
using namespace bones;

constexpr float kReach = 40.0f;  // the room rays' length (the game's units, about metres)
constexpr size_t kPerFrame = 6;  // voices checked for walls each frame

bool Trace() {
    static const bool on = [] {
        const char* e = std::getenv("NF_AUDIO_TRACE");
        return e && *e == '1';
    }();
    return on;
}

// how far a ray goes before a wall, kReach when it meets none
float Reach(X86Context& ctx, uint32_t obj, V from, V dir) {
    const Hit h = Ray(ctx, obj, from, from + dir * kReach);
    return h.ok ? std::min(Len(h.at - from), kReach) : kReach;
}
}  // namespace

namespace nf {
void AudioFrame(X86Context& ctx) {
    static float indoor = 0, size = 0;
    static uint32_t frame = 0;
    static size_t next = 0;
    static std::vector<audio::Source> sources;
    const uint32_t obj = R32(0x1F6654);
    float at[3];
    if (!Plausible(obj) || !Plausible(R32(obj + kZone)) || !audio::Sources(at, sources)) {
        indoor = size = 0;
        audio::SetRoom(0, 0);
        return;
    }
    const V ear{at[0], at[1], -at[2]};
    if (!Finite(ear)) return;
    if (frame++ % 4 == 0) {
        constexpr float kDiag = 0.70710678f;
        float up = Reach(ctx, obj, ear, {0, 1, 0}), total = up, slanted = 0;
        int roofed = 0;
        for (int i = 0; i < 4; i++) {
            const float a = i * 1.5707963f;
            const float d = Reach(ctx, obj, ear, {std::cos(a) * kDiag, kDiag, std::sin(a) * kDiag});
            total += d, roofed += d < kReach;
        }
        for (int i = 0; i < 8; i++) {
            const float a = i * 0.78539816f;
            total += Reach(ctx, obj, ear, {std::cos(a), 0, std::sin(a)});
        }
        slanted = roofed / 4.0f;
        const float now_indoor = 0.6f * (up < kReach) + 0.4f * slanted, now_size = total / 13;
        const bool first = size <= 0;
        indoor = first ? now_indoor : indoor + (now_indoor - indoor) * 0.3f;
        size = first ? now_size : size + (now_size - size) * 0.3f;
        audio::SetRoom(indoor, size);
        if (Trace() && frame % 120 == 1)
            LOGR("nightfire: audio: ear %.1f %.1f %.1f (camera %.1f %.1f %.1f), up %.1f, roofed %.2f, indoor %.2f, size %.1f, "
                 "%zu 3D voices", ear.x, ear.y, ear.z, F(R32(0x1F661C) + 0x138), F(R32(0x1F661C) + 0x13C),
                 F(R32(0x1F661C) + 0x140), up, slanted, indoor, size, sources.size()),
                logx::Flush();
    }
    for (size_t n = 0; n < std::min(kPerFrame, sources.size()); n++) {
        const auto& s = sources[next++ % sources.size()];
        const V src{s.pos[0], s.pos[1], -s.pos[2]}, lift{0, 0.6f, 0};
        const V way = src - ear;
        const float dist = Len(way);
        float amount = 0;
        if (Finite(src) && dist > 1.0f && dist < 200.0f) {
            // stop short of the voice: it is often on (or in) the thing that makes it
            const V end = ear + way * ((dist - 0.5f) / dist);
            amount += Ray(ctx, obj, ear, end).ok ? 0.5f : 0.0f;
            amount += Ray(ctx, obj, ear + lift, end + lift).ok ? 0.5f : 0.0f;
        }
        audio::SetOcclusion(s.id, amount);
        static int said = 0;
        if (Trace() && amount > 0 && said++ < 30)
            LOGR("nightfire: audio: voice %08X at %.1f %.1f %.1f (%.1f away) behind a wall: %.1f", s.id, src.x, src.y, src.z,
                 dist, amount);
    }
}
}  // namespace nf
