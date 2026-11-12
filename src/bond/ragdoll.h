#pragma once
// src/bond/ragdoll.cpp: the shot that hurt an entity, for the push of its fall (from src/bond/hit_feedback.cpp)
#include <cstdint>

void RagdollNoteHit(uint32_t entity, float x, float y, float z);

// src/bond/hit_feedback.cpp: AUF_HIT_TEST=2's damage, from player 1's input handler (src/bond/aim.cpp)
struct X86Context;
void HitTestTick(X86Context& ctx, uint32_t player, float dt);
void PickupTick(X86Context& ctx, uint32_t player, uint32_t index);  // src/bond/pickup_physics.cpp: a player walking into a pickup
