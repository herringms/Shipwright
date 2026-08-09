#pragma once

#include "HyruleCoopProtocol.h"

#include <array>
#include <cstdint>
#include <vector>

namespace HyruleCoop {

struct SongProgressionMapping {
    uint16_t eventFlag;
    uint8_t questBit;
};

inline bool IsPackedEventFlagSet(const std::array<uint16_t, 14>& eventFlags, uint16_t flag) {
    const size_t word = static_cast<size_t>(flag) >> 4;
    const uint16_t mask = static_cast<uint16_t>(1u << (flag & 0x0Fu));
    return word < eventFlags.size() && (eventFlags[word] & mask) != 0;
}

inline void PromoteEventFlagToQuestItem(std::array<uint16_t, 14>& eventFlags, uint32_t& questItems,
                                        uint16_t eventFlag, uint8_t questBit) {
    if (IsPackedEventFlagSet(eventFlags, eventFlag) && questBit < 32) {
        questItems |= (1u << questBit);
    }
}

inline std::vector<uint16_t> CollectNewlySetEventFlags(const std::array<uint16_t, 14>& previous,
                                                        const std::array<uint16_t, 14>& current) {
    std::vector<uint16_t> newlySet;
    for (size_t word = 0; word < current.size(); ++word) {
        const uint16_t added = static_cast<uint16_t>(current[word] & ~previous[word]);
        for (uint16_t bit = 0; bit < 16; ++bit) {
            if ((added & (1u << bit)) != 0) {
                newlySet.push_back(static_cast<uint16_t>((word << 4) | bit));
            }
        }
    }
    return newlySet;
}

inline std::array<uint8_t, 4> MergeBottleOwnership(const std::array<uint8_t, 4>& localBottleItems,
                                                    uint8_t bottleOwnershipMask, uint8_t emptyItem,
                                                    uint8_t bottleItem) {
    std::array<uint8_t, 4> merged = localBottleItems;
    for (size_t index = 0; index < merged.size(); ++index) {
        if ((bottleOwnershipMask & (1u << index)) == 0) {
            merged[index] = emptyItem;
        } else if (merged[index] == emptyItem) {
            merged[index] = bottleItem;
        }
    }
    return merged;
}

SharedProgressionState CaptureSharedProgression(void* saveContext);
// Age controls the loaded player skeleton and remains participant-local except during an explicit coordinated
// arrival. Shared campaign snapshots must not overwrite it while a scene is live.
void ApplySharedProgression(void* saveContext, const SharedProgressionState& state, bool applyLinkAge = false);
void ReconcileSharedProgressionDerivedFlags(void* saveContext);
bool IsSharedProgressionItem(uint16_t itemId, uint16_t modIndex, uint8_t category);

} // namespace HyruleCoop
