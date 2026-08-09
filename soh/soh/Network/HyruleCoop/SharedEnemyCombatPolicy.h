#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace HyruleCoop {

// This remains deliberately separate from GenericEnemyBridge. These enemies
// retain their own AI, animation, and targeting; only physical damage outcomes
// are shared through the host.
enum class SharedEnemyAdapterFamily : uint8_t {
    None,
    RedeadGibdo,
    LostWoodsMoblin,
    BlueBubble,
    Stalfos,
    GoldSkulltula,
    Skulltula,
    SkulltulaFather,
};

enum class SharedEnemyNativeOutcome : uint8_t {
    Death,
    Stun,
};

struct SharedEnemyAdapterContract {
    int16_t actorId;
    SharedEnemyAdapterFamily family;
    SharedEnemyNativeOutcome nativeOutcome;
    bool suppressGuestDrop;
    bool suppressGuestDefeatHook;
};

// Actor IDs are verified against z64.h in HyruleCoop.cpp. Keeping this table
// independent from game headers lets the transport regression test run alone.
constexpr std::array<SharedEnemyAdapterContract, 7> kSharedEnemyAdapterContracts = {{
    { 0x0090, SharedEnemyAdapterFamily::RedeadGibdo, SharedEnemyNativeOutcome::Death, true, true },
    { 0x004B, SharedEnemyAdapterFamily::LostWoodsMoblin, SharedEnemyNativeOutcome::Death, true, true },
    { 0x0069, SharedEnemyAdapterFamily::BlueBubble, SharedEnemyNativeOutcome::Death, true, true },
    { 0x0002, SharedEnemyAdapterFamily::Stalfos, SharedEnemyNativeOutcome::Death, true, true },
    { 0x0095, SharedEnemyAdapterFamily::GoldSkulltula, SharedEnemyNativeOutcome::Death, true, true },
    { 0x0037, SharedEnemyAdapterFamily::Skulltula, SharedEnemyNativeOutcome::Death, true, true },
    { 0x0188, SharedEnemyAdapterFamily::SkulltulaFather, SharedEnemyNativeOutcome::Stun, false, false },
}};

constexpr const SharedEnemyAdapterContract* FindSharedEnemyAdapterContract(int16_t actorId) {
    for (size_t index = 0; index < kSharedEnemyAdapterContracts.size(); ++index) {
        if (kSharedEnemyAdapterContracts[index].actorId == actorId) {
            return &kSharedEnemyAdapterContracts[index];
        }
    }
    return nullptr;
}

constexpr bool SupportsGuestPhysicalDamageIntent(const SharedEnemyAdapterContract* contract) {
    return contract != nullptr;
}

constexpr bool PreservesNativeSharedEnemySimulation(const SharedEnemyAdapterContract* contract) {
    return contract != nullptr;
}

constexpr bool ShouldSuppressGuestSharedEnemyDrop(const SharedEnemyAdapterContract* contract, bool guestReplica) {
    return guestReplica && contract != nullptr && contract->suppressGuestDrop;
}

constexpr bool ShouldSuppressGuestSharedEnemyDefeatHook(const SharedEnemyAdapterContract* contract, bool guestReplica) {
    return guestReplica && contract != nullptr && contract->suppressGuestDefeatHook;
}

constexpr bool ShouldApplySharedEnemyHostOutcome(uint32_t appliedSequence, uint32_t incomingSequence) {
    return incomingSequence != 0 &&
           (appliedSequence == 0 || static_cast<int32_t>(incomingSequence - appliedSequence) > 0);
}

constexpr bool AreIndependentSharedEnemyEntities(uint64_t firstEntityId, uint64_t secondEntityId) {
    return firstEntityId != 0 && secondEntityId != 0 && firstEntityId != secondEntityId;
}

} // namespace HyruleCoop
