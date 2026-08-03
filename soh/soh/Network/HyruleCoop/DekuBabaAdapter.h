#pragma once

#include "HyruleCoopProtocol.h"

#include <cstdint>

namespace HyruleCoop {

uint64_t GetDekuBabaEntityId(void* actor, int16_t scene, uint32_t worldGeneration);
ActorSnapshotMessage CaptureDekuBabaSnapshot(void* actor, int16_t scene, uint32_t hostTick,
                                             SessionScope scope, bool alive);
bool ApplyDekuBabaSnapshot(void* actor, const ActorSnapshotMessage& snapshot);
bool ConsumeDekuBabaHit(void* actor);
bool DamageDekuBaba(void* actor, int16_t damage);

} // namespace HyruleCoop
