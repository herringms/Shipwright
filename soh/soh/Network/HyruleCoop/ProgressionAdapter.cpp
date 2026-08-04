#include "ProgressionAdapter.h"

extern "C" {
#include "z64item.h"
#include "z64save.h"
}

#include "soh/Enhancements/item-tables/ItemTableTypes.h"

#include <algorithm>
#include <iterator>

namespace HyruleCoop {
namespace {

constexpr size_t kDurableItemCount = 18;
constexpr size_t kBottleCount = 4;

uint8_t DurableMagicLevel(const SaveContext* saveContext) {
    if (saveContext->isDoubleMagicAcquired) {
        return 2;
    }
    return saveContext->isMagicAcquired ? 1 : 0;
}

uint8_t DurableMagicLevel(const SharedProgressionState& state) {
    if (state.isDoubleMagicAcquired) {
        return 2;
    }
    return state.isMagicAcquired ? 1 : 0;
}

void ReconcileSpellAcquisitionFlags(SaveContext* saveContext) {
    uint16_t& spellFlags = saveContext->itemGetInf[ITEMGETINF_18_19_1A_INDEX];
    spellFlags &= ~(ITEMGETINF_18_MASK | ITEMGETINF_19_MASK | ITEMGETINF_1A_MASK);
    if (saveContext->inventory.items[SLOT_FARORES_WIND] == ITEM_FARORES_WIND) {
        spellFlags |= ITEMGETINF_18_MASK;
    }
    if (saveContext->inventory.items[SLOT_DINS_FIRE] == ITEM_DINS_FIRE) {
        spellFlags |= ITEMGETINF_19_MASK;
    }
    if (saveContext->inventory.items[SLOT_NAYRUS_LOVE] == ITEM_NAYRUS_LOVE) {
        spellFlags |= ITEMGETINF_1A_MASK;
    }
}

} // namespace

SharedProgressionState CaptureSharedProgression(void* saveContextRef) {
    const SaveContext* saveContext = static_cast<const SaveContext*>(saveContextRef);
    SharedProgressionState state;
    if (saveContext == nullptr) {
        return state;
    }

    for (size_t index = 0; index < kDurableItemCount; ++index) {
        state.durableItems[index] = saveContext->inventory.items[index];
    }
    state.tradeItems[0] = saveContext->inventory.items[SLOT_TRADE_ADULT];
    state.tradeItems[1] = saveContext->inventory.items[SLOT_TRADE_CHILD];
    for (size_t index = 0; index < kBottleCount; ++index) {
        if (saveContext->inventory.items[SLOT_BOTTLE_1 + index] != ITEM_NONE) {
            state.bottleOwnershipMask |= static_cast<uint8_t>(1u << index);
        }
    }
    state.equipment = saveContext->inventory.equipment;
    state.upgrades = saveContext->inventory.upgrades;
    state.questItems = saveContext->inventory.questItems;
    std::copy(std::begin(saveContext->inventory.dungeonItems), std::end(saveContext->inventory.dungeonItems),
              state.dungeonItems.begin());
    std::copy(std::begin(saveContext->inventory.dungeonKeys), std::end(saveContext->inventory.dungeonKeys),
              state.dungeonKeys.begin());
    state.healthCapacity = saveContext->healthCapacity;
    // magicLevel is also used as transient HUD state while the meter is being
    // constructed. Only advertise the durable ownership represented by the
    // acquisition flags.
    state.magicLevel = DurableMagicLevel(saveContext);
    state.isMagicAcquired = saveContext->isMagicAcquired;
    state.isDoubleMagicAcquired = saveContext->isDoubleMagicAcquired;
    state.isDoubleDefenseAcquired = saveContext->isDoubleDefenseAcquired;
    state.bgsFlag = saveContext->bgsFlag;
    state.gsTokens = saveContext->inventory.gsTokens;
    std::copy(std::begin(saveContext->eventChkInf), std::end(saveContext->eventChkInf),
              state.eventChkInf.begin());
    return state;
}

void ApplySharedProgression(void* saveContextRef, const SharedProgressionState& state) {
    SaveContext* saveContext = static_cast<SaveContext*>(saveContextRef);
    if (saveContext == nullptr) {
        return;
    }

    for (size_t index = 0; index < kDurableItemCount; ++index) {
        saveContext->inventory.items[index] = state.durableItems[index];
    }
    saveContext->inventory.items[SLOT_TRADE_ADULT] = state.tradeItems[0];
    saveContext->inventory.items[SLOT_TRADE_CHILD] = state.tradeItems[1];
    for (size_t index = 0; index < kBottleCount; ++index) {
        uint8_t& item = saveContext->inventory.items[SLOT_BOTTLE_1 + index];
        const bool canonicalBottle = (state.bottleOwnershipMask & (1u << index)) != 0;
        if (!canonicalBottle) {
            item = ITEM_NONE;
        } else if (item == ITEM_NONE) {
            item = ITEM_BOTTLE;
        }
    }
    saveContext->inventory.equipment = state.equipment;
    saveContext->inventory.upgrades = state.upgrades;
    saveContext->inventory.questItems = state.questItems;
    std::copy(state.dungeonItems.begin(), state.dungeonItems.end(),
              std::begin(saveContext->inventory.dungeonItems));
    std::copy(state.dungeonKeys.begin(), state.dungeonKeys.end(),
              std::begin(saveContext->inventory.dungeonKeys));
    saveContext->healthCapacity = state.healthCapacity;
    saveContext->health = std::min(saveContext->health, saveContext->healthCapacity);
    const bool hadMagic = saveContext->isMagicAcquired != 0;
    const bool hadDoubleMagic = saveContext->isDoubleMagicAcquired != 0;
    const int8_t localMagic = saveContext->magic;
    const uint8_t durableMagicLevel = DurableMagicLevel(state);
    const int16_t durableMagicCapacity = durableMagicLevel * MAGIC_NORMAL_METER;

    saveContext->isMagicAcquired = durableMagicLevel > 0;
    saveContext->isDoubleMagicAcquired = durableMagicLevel > 1;

    if (durableMagicLevel == 0) {
        saveContext->magicLevel = 0;
        saveContext->magic = 0;
        saveContext->magicCapacity = 0;
        saveContext->magicFillTarget = 0;
        saveContext->magicTarget = 0;
        saveContext->magicState = MAGIC_STATE_IDLE;
        saveContext->prevMagicState = MAGIC_STATE_IDLE;
    } else if (!hadMagic || hadDoubleMagic != (durableMagicLevel > 1)) {
        // Let the original meter state machine animate a newly shared magic
        // acquisition or capacity upgrade. Current magic is initialized by the
        // acquisition itself, not copied from the other player.
        saveContext->magicLevel = 0;
        saveContext->magic = std::clamp<int16_t>(localMagic, 0, durableMagicCapacity);
        saveContext->magicFillTarget = durableMagicCapacity;
        saveContext->magicTarget = saveContext->magic;
        saveContext->magicState = MAGIC_STATE_IDLE;
        saveContext->prevMagicState = MAGIC_STATE_IDLE;
    } else {
        // Ownership is shared, but consumption is local. Keep this player's
        // current amount and leave an in-flight consume/fill operation intact.
        saveContext->magicLevel = durableMagicLevel;
        saveContext->magicCapacity = durableMagicCapacity;
        saveContext->magic = std::clamp<int16_t>(localMagic, 0, durableMagicCapacity);
        saveContext->magicFillTarget = std::clamp<int16_t>(saveContext->magicFillTarget, 0, durableMagicCapacity);
        saveContext->magicTarget = std::clamp<int16_t>(saveContext->magicTarget, 0, durableMagicCapacity);
    }
    saveContext->isDoubleDefenseAcquired = state.isDoubleDefenseAcquired;
    saveContext->bgsFlag = state.bgsFlag;
    saveContext->inventory.gsTokens = state.gsTokens;
    std::copy(state.eventChkInf.begin(), state.eventChkInf.end(), std::begin(saveContext->eventChkInf));
    ReconcileSpellAcquisitionFlags(saveContext);
}

void ReconcileSharedProgressionDerivedFlags(void* saveContextRef) {
    SaveContext* saveContext = static_cast<SaveContext*>(saveContextRef);
    if (saveContext != nullptr) {
        ReconcileSpellAcquisitionFlags(saveContext);
    }
}

bool IsSharedProgressionItem(uint16_t itemId, uint16_t modIndex, uint8_t category) {
    if (modIndex != MOD_NONE || itemId > ITEM_DOUBLE_DEFENSE) {
        return false;
    }
    return category == ITEM_CATEGORY_MAJOR || category == ITEM_CATEGORY_BOSS_KEY ||
           category == ITEM_CATEGORY_SMALL_KEY || category == ITEM_CATEGORY_SKULLTULA_TOKEN;
}

} // namespace HyruleCoop
