#pragma once

#include "HyruleCoopProtocol.h"

#include <cstdint>

namespace HyruleCoop {

uint64_t GetGohmaEntityId(void* actor, int16_t scene, uint32_t worldGeneration);
ActorSnapshotMessage CaptureGohmaSnapshot(void* actor, int16_t scene, uint32_t hostTick,
                                         SessionScope scope, bool alive);
bool ApplyGohmaSnapshot(void* actor, const ActorSnapshotMessage& message);
bool StartGohmaDefeatPresentation(void* actor, void* playState, const ActorSnapshotMessage& message);
bool ConsumeGohmaHit(void* actor);
bool CanDamageGohma(void* actor);
bool DamageGohma(void* actor, void* playState, int16_t damage);
void RegisterGohmaGuestCollision(void* actor, void* playState);
void PrepareGohmaForAutomatedTest(void* actor);

} // namespace HyruleCoop
