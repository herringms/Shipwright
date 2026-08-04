#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace HyruleCoop {

namespace Capability {
inline constexpr const char* Coordination = "core.coordination.v1";
inline constexpr const char* RequestLedger = "core.requestLedger.v1";
inline constexpr const char* OotClock = "oot.clock.v1";
inline constexpr const char* OotPlayer = "oot.player.v1";
inline constexpr const char* OotPlayerPresentation = "oot.player.presentation.v1";
inline constexpr const char* OotSceneFlags = "oot.sceneFlags.v1";
inline constexpr const char* OotDekuBaba = "oot.actor.dekuBaba.v1";
inline constexpr const char* OotGuestAttack = "oot.combat.guestIntent.v1";
inline constexpr const char* OotCollectible = "oot.collectible.v1";
inline constexpr const char* OotSharedProgression = "oot.sharedProgression.v1";
inline constexpr const char* OotGohma = "oot.boss.gohma.v1";
inline constexpr const char* MmCycle = "mm.cycle.v1";
} // namespace Capability

using CapabilityList = std::vector<std::string>;

std::optional<CapabilityList> NormalizeCapabilities(const CapabilityList& capabilities);
bool SupportsCapabilities(const CapabilityList& supported, const CapabilityList& required);
CapabilityList IntersectCapabilities(const CapabilityList& first, const CapabilityList& second);
std::string HashCapabilities(const CapabilityList& capabilities);
uint64_t GenerateNonce64();

struct SessionScope {
    uint64_t sessionEpoch = 0;
    uint32_t worldGeneration = 0;

    bool operator==(const SessionScope&) const = default;
};

struct RequestKey {
    SessionScope scope;
    uint64_t participantId = 0;
    uint64_t requestId = 0;

    bool operator==(const RequestKey&) const = default;
};

struct RequestOutcome {
    bool accepted = false;
    uint64_t commitId = 0;
    uint64_t domainRevision = 0;

    bool operator==(const RequestOutcome&) const = default;
};

enum class RequestLookup {
    New,
    Replay,
    StaleScope,
    Invalid,
};

class RequestLedger {
  public:
    void BeginScope(SessionScope scope);
    RequestLookup Lookup(const RequestKey& key, RequestOutcome* outcome = nullptr) const;
    bool Record(const RequestKey& key, const RequestOutcome& outcome);
    size_t Size() const;

  private:
    struct RequestKeyHash {
        size_t operator()(const RequestKey& key) const;
    };

    SessionScope currentScope;
    std::unordered_map<RequestKey, RequestOutcome, RequestKeyHash> outcomes;
};

enum class RevisionDecision {
    Apply,
    Replay,
    Stale,
};

class DomainRevisionTracker {
  public:
    uint64_t Advance(uint64_t streamId);
    uint64_t Current(uint64_t streamId) const;
    RevisionDecision Observe(uint64_t streamId, uint64_t revision);
    void Clear();

  private:
    std::unordered_map<uint64_t, uint64_t> revisions;
};

enum class BarrierKind : uint8_t {
    SessionPreparation,
    SceneTransition,
    BossEncounter,
    ReconnectSnapshot,
    CycleTransition,
};

enum class BarrierPhase : uint8_t {
    Idle,
    Prepare,
    WaitingForParticipants,
    Commit,
    Active,
    Complete,
    Aborted,
};

struct BarrierState {
    uint64_t operationEpoch = 0;
    SessionScope scope;
    BarrierKind kind = BarrierKind::SessionPreparation;
    BarrierPhase phase = BarrierPhase::Idle;
    std::string manifestHash;
    int16_t targetScene = -1;
    int16_t targetRoom = -1;
    int32_t targetEntrance = -1;
    uint64_t deadlineTick = 0;
    std::vector<uint64_t> participants;
    std::vector<uint64_t> readyParticipants;
};

class BarrierCoordinator {
  public:
    bool Begin(const BarrierState& operation);
    bool Reconcile(const BarrierState& operation);
    bool WaitForParticipants();
    bool MarkReady(uint64_t participantId);
    bool CanCommit() const;
    bool Commit();
    bool Activate();
    bool Complete();
    bool Abort();
    bool IsExpired(uint64_t currentTick) const;
    const BarrierState& GetState() const;

  private:
    void RefreshReadyParticipants();

    BarrierState state;
    std::unordered_set<uint64_t> required;
    std::unordered_set<uint64_t> ready;
};

} // namespace HyruleCoop
