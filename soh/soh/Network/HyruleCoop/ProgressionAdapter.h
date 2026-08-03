#pragma once

#include "HyruleCoopProtocol.h"

#include <cstdint>

namespace HyruleCoop {

SharedProgressionState CaptureSharedProgression(void* saveContext);
void ApplySharedProgression(void* saveContext, const SharedProgressionState& state);
bool IsSharedProgressionItem(uint16_t itemId, uint16_t modIndex, uint8_t category);

} // namespace HyruleCoop
