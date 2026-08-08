#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace HyruleCoop {

// Stalchildren are ambient runtime spawns. Their position is intentionally random, so a map/static entity
// signature cannot identify the same enemy on two peers. Reserve a small namespace for host-issued identities.
constexpr uint64_t kDynamicStalchildEntityPrefix = 0x534B000000000000ULL;
constexpr uint64_t kDynamicStalchildEntityMask = 0xFFFF000000000000ULL;
constexpr uint64_t kDynamicStalchildOrdinalMask = 0x0000FFFFFFFFFFFFULL;
constexpr uint16_t kEncounterSpawnerTypeShift = 0xB;
constexpr uint16_t kStalchildSpawnerType = 2;
constexpr uint16_t kStalchildStateTargetable = 1u << 0;
constexpr uint16_t kStalchildStateTargetsGuest = 1u << 1;
constexpr uint16_t kStalchildStateAttackSequenceShift = 2;
constexpr uint16_t kStalchildStateAttackSequenceMask = 0x3FFF;
constexpr float kStalchildRetargetAdvantageSquared = 120.0f * 120.0f;
constexpr uint8_t kStalchildAdapterWordCount = 5;
constexpr size_t kStalchildAdapterTarget = 0;
constexpr size_t kStalchildAdapterBehavior = 1;
constexpr size_t kStalchildAdapterAttackActive = 2;
constexpr size_t kStalchildAdapterShapeYOffset = 3;
constexpr size_t kStalchildAdapterShadowScale = 4;
constexpr int16_t kStalchildMinimumShapeYOffset = -8000;
constexpr int16_t kStalchildMaximumShapeYOffset = 0;
constexpr int16_t kStalchildMinimumShadowScale = 0;
constexpr int16_t kStalchildMaximumShadowScale = 2500;
constexpr float kStalchildShadowScalePrecision = 100.0f;
// Ambient enemies do not need player-rate snapshots. Guest interpolation fills the frames between these updates,
// while attacks, damage, death, and target changes still publish immediately as acknowledged transitions.
constexpr uint32_t kStalchildSnapshotIntervalFrames = 4;

enum class StalchildTarget : uint8_t {
    Host,
    Guest,
};

constexpr bool IsDynamicStalchildEntityId(uint64_t entityId) {
    return (entityId & kDynamicStalchildEntityMask) == kDynamicStalchildEntityPrefix &&
           (entityId & kDynamicStalchildOrdinalMask) != 0;
}

constexpr bool IsStalchildSpawnerParams(uint16_t params) {
    return (params >> kEncounterSpawnerTypeShift) == kStalchildSpawnerType;
}

constexpr bool ShouldSuppressGuestStalchildSpawner(bool isGuest, bool inHyruleField, uint16_t params) {
    return isGuest && inHyruleField && IsStalchildSpawnerParams(params);
}

constexpr uint16_t EncodeStalchildState(bool targetable, StalchildTarget target, uint16_t attackSequence = 0) {
    return (targetable ? kStalchildStateTargetable : 0) |
           (target == StalchildTarget::Guest ? kStalchildStateTargetsGuest : 0) |
           ((attackSequence & kStalchildStateAttackSequenceMask) << kStalchildStateAttackSequenceShift);
}

constexpr bool IsStalchildTargetable(uint16_t state) {
    return (state & kStalchildStateTargetable) != 0;
}

constexpr StalchildTarget DecodeStalchildTarget(uint16_t state) {
    return (state & kStalchildStateTargetsGuest) != 0 ? StalchildTarget::Guest : StalchildTarget::Host;
}

constexpr uint16_t DecodeStalchildAttackSequence(uint16_t state) {
    return (state >> kStalchildStateAttackSequenceShift) & kStalchildStateAttackSequenceMask;
}

constexpr bool IsNewerStalchildAttackSequence(uint16_t incoming, uint16_t applied) {
    incoming &= kStalchildStateAttackSequenceMask;
    applied &= kStalchildStateAttackSequenceMask;
    if (incoming == 0 || incoming == applied) {
        return false;
    }
    if (applied == 0) {
        return true;
    }
    const uint16_t forwardDistance = incoming > applied
                                         ? incoming - applied
                                         : kStalchildStateAttackSequenceMask - applied + incoming;
    return forwardDistance <= kStalchildStateAttackSequenceMask / 2;
}

constexpr StalchildTarget SelectStalchildTarget(bool guestAvailable, float hostDistanceSquared,
                                                 float guestDistanceSquared, StalchildTarget previous,
                                                 bool targetLocked) {
    if (!guestAvailable) {
        return StalchildTarget::Host;
    }
    if (targetLocked) {
        return previous;
    }
    if (previous == StalchildTarget::Guest) {
        return hostDistanceSquared + kStalchildRetargetAdvantageSquared < guestDistanceSquared
                   ? StalchildTarget::Host
                   : StalchildTarget::Guest;
    }
    return guestDistanceSquared + kStalchildRetargetAdvantageSquared < hostDistanceSquared
               ? StalchildTarget::Guest
               : StalchildTarget::Host;
}

constexpr bool IsValidStalchildAdapterState(uint8_t wordCount, const int16_t* words) {
    return words != nullptr && wordCount == kStalchildAdapterWordCount &&
           (words[kStalchildAdapterTarget] == static_cast<int16_t>(StalchildTarget::Host) ||
            words[kStalchildAdapterTarget] == static_cast<int16_t>(StalchildTarget::Guest)) &&
           words[kStalchildAdapterBehavior] >= 0 && words[kStalchildAdapterBehavior] <= 6 &&
           (words[kStalchildAdapterAttackActive] == 0 || words[kStalchildAdapterAttackActive] == 1) &&
           words[kStalchildAdapterShapeYOffset] >= kStalchildMinimumShapeYOffset &&
           words[kStalchildAdapterShapeYOffset] <= kStalchildMaximumShapeYOffset &&
           words[kStalchildAdapterShadowScale] >= kStalchildMinimumShadowScale &&
           words[kStalchildAdapterShadowScale] <= kStalchildMaximumShadowScale;
}

class DynamicStalchildIdentityRegistry {
  public:
    uint64_t GetOrAssign(const void* actor) {
        const auto existing = actorIds.find(actor);
        if (existing != actorIds.end()) {
            return existing->second;
        }

        // Keep zero reserved as an invalid network entity ID. Do not reuse IDs after a dawn despawn; a late packet
        // can then only refer to a retired enemy, never a newly spawned one.
        if (nextOrdinal == 0 || nextOrdinal > kDynamicStalchildOrdinalMask) {
            return 0;
        }
        const uint64_t entityId = kDynamicStalchildEntityPrefix | nextOrdinal++;
        actorIds.emplace(actor, entityId);
        return entityId;
    }

    uint64_t Find(const void* actor) const {
        const auto existing = actorIds.find(actor);
        return existing == actorIds.end() ? 0 : existing->second;
    }

    bool Bind(const void* actor, uint64_t entityId) {
        if (actor == nullptr || !IsDynamicStalchildEntityId(entityId)) {
            return false;
        }
        return actorIds.emplace(actor, entityId).second;
    }

    void Forget(const void* actor) {
        actorIds.erase(actor);
    }

    void Clear() {
        actorIds.clear();
    }

    void Reset() {
        actorIds.clear();
        nextOrdinal = 1;
    }

  private:
    uint64_t nextOrdinal = 1;
    std::unordered_map<const void*, uint64_t> actorIds;
};

// A terminal dynamic-enemy snapshot must be sent exactly once. This keeps the dawn teardown reliable without
// creating an acknowledgement/retry storm if engine hooks report the same actor more than once while destroying it.
class DynamicStalchildSnapshotLifecycle {
  public:
    bool ShouldPublish(uint64_t entityId, bool alive) {
        if (!IsDynamicStalchildEntityId(entityId)) {
            return false;
        }

        State& state = states[entityId];
        if (alive) {
            if (state.terminalSent) {
                return false;
            }
            state.seenAlive = true;
            return true;
        }

        if (!state.seenAlive || state.terminalSent) {
            return false;
        }
        state.terminalSent = true;
        return true;
    }

    void Forget(uint64_t entityId) {
        states.erase(entityId);
    }

    void Clear() {
        states.clear();
    }

    size_t Size() const {
        return states.size();
    }

  private:
    struct State {
        bool seenAlive = false;
        bool terminalSent = false;
    };

    std::unordered_map<uint64_t, State> states;
};

} // namespace HyruleCoop
