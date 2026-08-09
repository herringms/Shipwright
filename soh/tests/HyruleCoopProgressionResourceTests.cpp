#include "soh/Network/HyruleCoop/ProgressionAdapter.h"

extern "C" {
#include "z64item.h"
#include "z64save.h"
}

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

void SetEventFlag(std::array<uint16_t, 14>& flags, uint16_t flag) {
    flags[flag >> 4] |= static_cast<uint16_t>(1u << (flag & 0x0F));
}

void TestDurableSongPromotion() {
    std::array<uint16_t, 14> eventFlags = {};
    uint32_t questItems = 0;

    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_MINUET_OF_FOREST);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_BOLERO_OF_FIRE);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_SERENADE_OF_WATER);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_REQUIEM_OF_SPIRIT);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_NOCTURNE_OF_SHADOW);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_PRELUDE_OF_LIGHT);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_SONG_OF_STORMS);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_ZELDAS_LULLABY);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_SARIAS_SONG);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_SUNS_SONG);
    SetEventFlag(eventFlags, EVENTCHKINF_LEARNED_SONG_OF_TIME);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_MINUET_OF_FOREST, QUEST_SONG_MINUET);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_BOLERO_OF_FIRE, QUEST_SONG_BOLERO);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_SERENADE_OF_WATER, QUEST_SONG_SERENADE);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_REQUIEM_OF_SPIRIT, QUEST_SONG_REQUIEM);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_NOCTURNE_OF_SHADOW, QUEST_SONG_NOCTURNE);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_PRELUDE_OF_LIGHT, QUEST_SONG_PRELUDE);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_SONG_OF_STORMS, QUEST_SONG_STORMS);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_ZELDAS_LULLABY, QUEST_SONG_LULLABY);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_SARIAS_SONG, QUEST_SONG_SARIA);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_SUNS_SONG, QUEST_SONG_SUN);
    HyruleCoop::PromoteEventFlagToQuestItem(eventFlags, questItems,
                                            EVENTCHKINF_LEARNED_SONG_OF_TIME, QUEST_SONG_TIME);

    assert((questItems & (1u << QUEST_SONG_MINUET)) != 0);
    assert((questItems & (1u << QUEST_SONG_BOLERO)) != 0);
    assert((questItems & (1u << QUEST_SONG_SERENADE)) != 0);
    assert((questItems & (1u << QUEST_SONG_REQUIEM)) != 0);
    assert((questItems & (1u << QUEST_SONG_NOCTURNE)) != 0);
    assert((questItems & (1u << QUEST_SONG_PRELUDE)) != 0);
    assert((questItems & (1u << QUEST_SONG_STORMS)) != 0);
    assert((questItems & (1u << QUEST_SONG_LULLABY)) != 0);
    assert((questItems & (1u << QUEST_SONG_SARIA)) != 0);
    assert((questItems & (1u << QUEST_SONG_SUN)) != 0);
    assert((questItems & (1u << QUEST_SONG_TIME)) != 0);
}

void TestMissedEventDetectionIsBounded() {
    std::array<uint16_t, 14> previous = {};
    std::array<uint16_t, 14> current = {};
    SetEventFlag(current, EVENTCHKINF_LEARNED_SONG_OF_TIME);
    SetEventFlag(current, EVENTCHKINF_LEARNED_SONG_OF_STORMS);

    const std::vector<uint16_t> first = HyruleCoop::CollectNewlySetEventFlags(previous, current);
    const std::vector<uint16_t> second = HyruleCoop::CollectNewlySetEventFlags(current, current);

    assert(first.size() == 2);
    assert(std::find(first.begin(), first.end(), EVENTCHKINF_LEARNED_SONG_OF_TIME) != first.end());
    assert(std::find(first.begin(), first.end(), EVENTCHKINF_LEARNED_SONG_OF_STORMS) != first.end());
    assert(second.empty());
}

void TestBottleContentsRemainLocal() {
    const std::array<uint8_t, 4> local = { ITEM_MILK, ITEM_POE, ITEM_FISH, ITEM_LETTER_RUTO };
    const std::array<uint8_t, 4> preserved =
        HyruleCoop::MergeBottleOwnership(local, 0x0F, ITEM_NONE, ITEM_BOTTLE);
    const std::array<uint8_t, 4> reducedOwnership =
        HyruleCoop::MergeBottleOwnership(local, 0x05, ITEM_NONE, ITEM_BOTTLE);

    assert(preserved == local);
    assert(reducedOwnership[0] == ITEM_MILK);
    assert(reducedOwnership[1] == ITEM_NONE);
    assert(reducedOwnership[2] == ITEM_FISH);
    assert(reducedOwnership[3] == ITEM_NONE);
}

void TestDurableSongReconciliation() {
    SaveContext saveContext = {};
    const std::array<std::pair<uint16_t, uint8_t>, 11> songs = {{
        { EVENTCHKINF_LEARNED_MINUET_OF_FOREST, QUEST_SONG_MINUET },
        { EVENTCHKINF_LEARNED_BOLERO_OF_FIRE, QUEST_SONG_BOLERO },
        { EVENTCHKINF_LEARNED_SERENADE_OF_WATER, QUEST_SONG_SERENADE },
        { EVENTCHKINF_LEARNED_REQUIEM_OF_SPIRIT, QUEST_SONG_REQUIEM },
        { EVENTCHKINF_LEARNED_NOCTURNE_OF_SHADOW, QUEST_SONG_NOCTURNE },
        { EVENTCHKINF_LEARNED_PRELUDE_OF_LIGHT, QUEST_SONG_PRELUDE },
        { EVENTCHKINF_LEARNED_ZELDAS_LULLABY, QUEST_SONG_LULLABY },
        { EVENTCHKINF_LEARNED_SARIAS_SONG, QUEST_SONG_SARIA },
        { EVENTCHKINF_LEARNED_SUNS_SONG, QUEST_SONG_SUN },
        { EVENTCHKINF_LEARNED_SONG_OF_TIME, QUEST_SONG_TIME },
        { EVENTCHKINF_LEARNED_SONG_OF_STORMS, QUEST_SONG_STORMS },
    }};

    for (const auto& [eventFlag, questBit] : songs) {
        saveContext.eventChkInf[eventFlag >> 4] |= static_cast<uint16_t>(1u << (eventFlag & 0x0F));
    }

    HyruleCoop::ReconcileSharedProgressionDerivedFlags(&saveContext);

    for (const auto& [eventFlag, questBit] : songs) {
        (void)eventFlag;
        assert((saveContext.inventory.questItems & (1u << questBit)) != 0);
    }
}

void TestEquippedBottleBindingRemainsLocal() {
    SaveContext saveContext = {};
    HyruleCoop::SharedProgressionState shared = {};
    saveContext.inventory.items[SLOT_BOTTLE_1] = ITEM_MILK;
    saveContext.equips.cButtonSlots[0] = SLOT_BOTTLE_1;
    saveContext.equips.buttonItems[1] = ITEM_BOTTLE;
    shared.bottleOwnershipMask = 0x01;

    HyruleCoop::ApplySharedProgression(&saveContext, shared, false);

    assert(saveContext.inventory.items[SLOT_BOTTLE_1] == ITEM_MILK);
    assert(saveContext.equips.buttonItems[1] == ITEM_MILK);
}

} // namespace

int main() {
    TestDurableSongPromotion();
    TestMissedEventDetectionIsBounded();
    TestBottleContentsRemainLocal();
    TestDurableSongReconciliation();
    TestEquippedBottleBindingRemainsLocal();
    return 0;
}
