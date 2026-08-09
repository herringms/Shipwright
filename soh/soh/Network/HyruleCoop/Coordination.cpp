#include "Coordination.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>
#include <random>

namespace HyruleCoop {
namespace {

size_t HashCombine(size_t seed, uint64_t value) {
    return seed ^ (std::hash<uint64_t>{}(value) + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2));
}

} // namespace

std::optional<CapabilityList> NormalizeCapabilities(const CapabilityList& capabilities) {
    if (capabilities.size() > 64) {
        return std::nullopt;
    }
    CapabilityList normalized = capabilities;
    for (const std::string& capability : normalized) {
        if (capability.empty() || capability.size() > 96) {
            return std::nullopt;
        }
    }
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

bool SupportsCapabilities(const CapabilityList& supported, const CapabilityList& required) {
    const auto normalizedSupported = NormalizeCapabilities(supported);
    const auto normalizedRequired = NormalizeCapabilities(required);
    if (!normalizedSupported.has_value() || !normalizedRequired.has_value()) {
        return false;
    }
    return std::includes(normalizedSupported->begin(), normalizedSupported->end(), normalizedRequired->begin(),
                         normalizedRequired->end());
}

CapabilityList IntersectCapabilities(const CapabilityList& first, const CapabilityList& second) {
    const auto normalizedFirst = NormalizeCapabilities(first);
    const auto normalizedSecond = NormalizeCapabilities(second);
    if (!normalizedFirst.has_value() || !normalizedSecond.has_value()) {
        return {};
    }
    CapabilityList intersection;
    std::set_intersection(normalizedFirst->begin(), normalizedFirst->end(), normalizedSecond->begin(),
                          normalizedSecond->end(), std::back_inserter(intersection));
    return intersection;
}

std::string HashCapabilities(const CapabilityList& capabilities) {
    const auto normalized = NormalizeCapabilities(capabilities);
    if (!normalized.has_value()) {
        return {};
    }
    uint64_t hash = 1469598103934665603ULL;
    for (const std::string& capability : normalized.value()) {
        for (uint8_t byte : capability) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        hash ^= 0;
        hash *= 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

uint64_t GenerateNonce64() {
    static std::atomic<uint64_t> counter = 1;
    std::random_device random;
    const uint64_t randomBits = (static_cast<uint64_t>(random()) << 32) ^ random();
    const uint64_t timeBits = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const uint64_t value = randomBits ^ timeBits ^ counter.fetch_add(1);
    return value == 0 ? counter.fetch_add(1) : value;
}

bool BarrierRequiresParticipantRelocation(BarrierKind kind) {
    return kind != BarrierKind::ReconnectSnapshot;
}

BarrierKind HandshakeBarrierKind(bool requestedCurrentSession) {
    return requestedCurrentSession ? BarrierKind::ReconnectSnapshot : BarrierKind::SessionPreparation;
}

bool BarrierParticipantLocationReady(const BarrierState& state, int16_t currentScene, int16_t currentRoom,
                                     bool timelineReady) {
    if (!BarrierRequiresParticipantRelocation(state.kind)) {
        return true;
    }
    return timelineReady && currentScene == state.targetScene &&
           (state.targetRoom < 0 || currentRoom == state.targetRoom);
}

bool ClockSnapshotMayApply(bool messageActive, bool ocarinaActive, bool cutsceneActive, bool playerCutsceneActive) {
    return !messageActive && !ocarinaActive && !cutsceneActive && !playerCutsceneActive;
}

void RequestLedger::BeginScope(SessionScope scope) {
    currentScope = scope;
    outcomes.clear();
}

RequestLookup RequestLedger::Lookup(const RequestKey& key, RequestOutcome* outcome) const {
    if (key.participantId == 0 || key.requestId == 0 || key.scope.sessionEpoch == 0) {
        return RequestLookup::Invalid;
    }
    if (key.scope != currentScope) {
        return RequestLookup::StaleScope;
    }
    const auto found = outcomes.find(key);
    if (found == outcomes.end()) {
        return RequestLookup::New;
    }
    if (outcome != nullptr) {
        *outcome = found->second;
    }
    return RequestLookup::Replay;
}

bool RequestLedger::Record(const RequestKey& key, const RequestOutcome& outcome) {
    if (Lookup(key) != RequestLookup::New) {
        return false;
    }
    return outcomes.emplace(key, outcome).second;
}

size_t RequestLedger::Size() const {
    return outcomes.size();
}

size_t RequestLedger::RequestKeyHash::operator()(const RequestKey& key) const {
    size_t hash = std::hash<uint64_t>{}(key.scope.sessionEpoch);
    hash = HashCombine(hash, key.scope.worldGeneration);
    hash = HashCombine(hash, key.participantId);
    return HashCombine(hash, key.requestId);
}

uint64_t DomainRevisionTracker::Advance(uint64_t streamId) {
    uint64_t& revision = revisions[streamId];
    if (revision != std::numeric_limits<uint64_t>::max()) {
        ++revision;
    }
    return revision;
}

uint64_t DomainRevisionTracker::Current(uint64_t streamId) const {
    const auto found = revisions.find(streamId);
    return found == revisions.end() ? 0 : found->second;
}

RevisionDecision DomainRevisionTracker::Observe(uint64_t streamId, uint64_t revision) {
    const auto found = revisions.find(streamId);
    if (found == revisions.end()) {
        revisions.emplace(streamId, revision);
        return RevisionDecision::Apply;
    }
    if (revision < found->second) {
        return RevisionDecision::Stale;
    }
    if (revision == found->second) {
        return RevisionDecision::Replay;
    }
    found->second = revision;
    return RevisionDecision::Apply;
}

void DomainRevisionTracker::Clear() {
    revisions.clear();
}

bool BarrierCoordinator::Begin(const BarrierState& operation) {
    if (operation.operationEpoch == 0 || operation.scope.sessionEpoch == 0 ||
        operation.phase != BarrierPhase::Prepare || operation.participants.empty() ||
        (state.phase != BarrierPhase::Idle && state.phase != BarrierPhase::Complete &&
         state.phase != BarrierPhase::Aborted)) {
        return false;
    }

    std::unordered_set<uint64_t> participants;
    for (uint64_t participantId : operation.participants) {
        if (participantId == 0 || !participants.insert(participantId).second) {
            return false;
        }
    }

    state = operation;
    state.readyParticipants.clear();
    required = std::move(participants);
    ready.clear();
    return true;
}

bool BarrierCoordinator::Reconcile(const BarrierState& operation) {
    if (operation.operationEpoch == 0 || operation.scope.sessionEpoch == 0 || operation.participants.empty() ||
        operation.phase == BarrierPhase::Idle) {
        return false;
    }
    if (state.operationEpoch > operation.operationEpoch ||
        (state.operationEpoch == operation.operationEpoch &&
         static_cast<uint8_t>(state.phase) > static_cast<uint8_t>(operation.phase))) {
        return false;
    }
    std::unordered_set<uint64_t> participants;
    for (uint64_t participantId : operation.participants) {
        if (participantId == 0 || !participants.insert(participantId).second) {
            return false;
        }
    }
    std::unordered_set<uint64_t> readyParticipants;
    for (uint64_t participantId : operation.readyParticipants) {
        if (!participants.contains(participantId) || !readyParticipants.insert(participantId).second) {
            return false;
        }
    }
    state = operation;
    required = std::move(participants);
    ready = std::move(readyParticipants);
    RefreshReadyParticipants();
    return true;
}

bool BarrierCoordinator::WaitForParticipants() {
    if (state.phase != BarrierPhase::Prepare) {
        return false;
    }
    state.phase = BarrierPhase::WaitingForParticipants;
    return true;
}

bool BarrierCoordinator::MarkReady(uint64_t participantId) {
    if (state.phase != BarrierPhase::WaitingForParticipants || !required.contains(participantId)) {
        return false;
    }
    ready.insert(participantId);
    RefreshReadyParticipants();
    return true;
}

bool BarrierCoordinator::CanCommit() const {
    return state.phase == BarrierPhase::WaitingForParticipants && ready.size() == required.size();
}

bool BarrierCoordinator::Commit() {
    if (!CanCommit()) {
        return false;
    }
    state.phase = BarrierPhase::Commit;
    return true;
}

bool BarrierCoordinator::Activate() {
    if (state.phase != BarrierPhase::Commit) {
        return false;
    }
    state.phase = BarrierPhase::Active;
    return true;
}

bool BarrierCoordinator::Complete() {
    if (state.phase != BarrierPhase::Active && state.phase != BarrierPhase::Commit) {
        return false;
    }
    state.phase = BarrierPhase::Complete;
    return true;
}

bool BarrierCoordinator::Abort() {
    if (state.phase == BarrierPhase::Idle || state.phase == BarrierPhase::Complete ||
        state.phase == BarrierPhase::Aborted) {
        return false;
    }
    state.phase = BarrierPhase::Aborted;
    return true;
}

bool BarrierCoordinator::IsExpired(uint64_t currentTick) const {
    return state.deadlineTick != 0 && currentTick >= state.deadlineTick &&
           state.phase != BarrierPhase::Complete && state.phase != BarrierPhase::Aborted;
}

const BarrierState& BarrierCoordinator::GetState() const {
    return state;
}

void BarrierCoordinator::RefreshReadyParticipants() {
    state.readyParticipants.assign(ready.begin(), ready.end());
    std::sort(state.readyParticipants.begin(), state.readyParticipants.end());
}

} // namespace HyruleCoop
