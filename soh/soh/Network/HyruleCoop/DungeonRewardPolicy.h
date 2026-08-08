#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace HyruleCoop {

constexpr size_t kDungeonRewardDungeonCount = 10;
constexpr uint8_t kDungeonCompassItemBit = 1u << 1;
constexpr uint8_t kDungeonMapItemBit = 1u << 2;

struct DungeonRewardChestFlags {
    uint8_t map = 0;
    uint8_t compass = 0;
};

constexpr std::array<DungeonRewardChestFlags, kDungeonRewardDungeonCount> kVanillaDungeonRewardChests = {
    DungeonRewardChestFlags{ 0x03, 0x02 }, // Deku Tree
    DungeonRewardChestFlags{ 0x08, 0x05 }, // Dodongo's Cavern
    DungeonRewardChestFlags{ 0x02, 0x04 }, // Jabu-Jabu's Belly
    DungeonRewardChestFlags{ 0x01, 0x0F }, // Forest Temple
    DungeonRewardChestFlags{ 0x0A, 0x07 }, // Fire Temple
    DungeonRewardChestFlags{ 0x02, 0x09 }, // Water Temple
    DungeonRewardChestFlags{ 0x03, 0x04 }, // Spirit Temple
    DungeonRewardChestFlags{ 0x01, 0x03 }, // Shadow Temple
    DungeonRewardChestFlags{ 0x07, 0x01 }, // Bottom of the Well
    DungeonRewardChestFlags{ 0x00, 0x01 }, // Ice Cavern
};

constexpr std::array<DungeonRewardChestFlags, kDungeonRewardDungeonCount> kMasterQuestDungeonRewardChests = {
    DungeonRewardChestFlags{ 0x03, 0x01 }, // Deku Tree
    DungeonRewardChestFlags{ 0x00, 0x05 }, // Dodongo's Cavern
    DungeonRewardChestFlags{ 0x03, 0x00 }, // Jabu-Jabu's Belly
    DungeonRewardChestFlags{ 0x0D, 0x0F }, // Forest Temple
    DungeonRewardChestFlags{ 0x0C, 0x0B }, // Fire Temple
    DungeonRewardChestFlags{ 0x02, 0x01 }, // Water Temple
    DungeonRewardChestFlags{ 0x00, 0x03 }, // Spirit Temple
    DungeonRewardChestFlags{ 0x02, 0x01 }, // Shadow Temple
    DungeonRewardChestFlags{ 0x03, 0x02 }, // Bottom of the Well
    DungeonRewardChestFlags{ 0x01, 0x00 }, // Ice Cavern
};

constexpr uint8_t ReconcileDungeonRewardFromChest(uint8_t dungeonItems, uint32_t chestFlags,
                                                   size_t dungeonIndex, bool masterQuest) {
    if (dungeonIndex >= kDungeonRewardDungeonCount) {
        return dungeonItems;
    }
    const DungeonRewardChestFlags& reward =
        masterQuest ? kMasterQuestDungeonRewardChests[dungeonIndex] : kVanillaDungeonRewardChests[dungeonIndex];
    if ((chestFlags & (1u << reward.map)) != 0) {
        dungeonItems |= kDungeonMapItemBit;
    }
    if ((chestFlags & (1u << reward.compass)) != 0) {
        dungeonItems |= kDungeonCompassItemBit;
    }
    return dungeonItems;
}

} // namespace HyruleCoop
