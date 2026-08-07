#pragma once

#include "HyruleCoopProtocol.h"

#include <cstdint>

namespace HyruleCoop {

SharedProgressionState CaptureSharedProgression(void* saveContext);
// Age controls the loaded player skeleton and remains participant-local except during an explicit coordinated
// arrival. Shared campaign snapshots must not overwrite it while a scene is live.
void ApplySharedProgression(void* saveContext, const SharedProgressionState& state, bool applyLinkAge = false);
void ReconcileSharedProgressionDerivedFlags(void* saveContext);
bool IsSharedProgressionItem(uint16_t itemId, uint16_t modIndex, uint8_t category);

} // namespace HyruleCoop
