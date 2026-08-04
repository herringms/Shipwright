#pragma once

#include "DirectSession.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace HyruleCoop {

enum class ConnectionPhase {
    Idle,
    WaitingForPeer,
    Connecting,
    Handshaking,
    Ready,
    Failed,
};

class Manager {
  public:
    static Manager* Instance;

    Manager() = default;
    ~Manager();

    bool Host(uint16_t port, const std::string& playerName);
    bool Join(const std::string& address, uint16_t port, const std::string& playerName);
    void ConfigureAutomatedTestFromEnvironment();
    void Disconnect();
    void Update();

    ConnectionPhase GetPhase() const;
    SessionRole GetRole() const;
    std::string GetStatusText() const;
    bool IsActive() const;
    bool IsReady() const;
    bool IsPreparingRemotePlayer() const;
    const PlayerSnapshotMessage* GetRemotePlayerSnapshot() const;
    const std::string& GetRemotePlayerName() const;
    void SanitizeSaveCopy(void* saveContext) const;
    void PrepareRemotePlayer(void* actor);
    void NotifyRemotePlayerDestroyed(void* actor);
    void NotifyRemotePlayerPoseApplied(bool meleeActive);

  private:
    void RegisterHooks(bool enabled);
    void ResetPeerState();
    void ResetSessionState();
    void BeginHandshakeIfNeeded();
    void HandlePacket(const Packet& packet);
    void HandleHello(const Packet& packet);
    void HandleHelloAck(const Packet& packet);
    void HandleClockSnapshot(const Packet& packet);
    void HandlePlayerSnapshot(const Packet& packet);
    void HandlePlayerPresentation(const Packet& packet);
    void HandleSnapshotRequest(const Packet& packet);
    void HandleSceneFlagIntent(const Packet& packet);
    void HandleSceneFlagsSnapshot(const Packet& packet);
    void HandleActorSnapshot(const Packet& packet);
    void HandleBarrierSnapshot(const Packet& packet);
    void HandleBarrierReady(const Packet& packet);
    void HandleAttackIntent(const Packet& packet);
    void HandleCollectibleIntent(const Packet& packet);
    void HandleProgressionSnapshot(const Packet& packet);
    void HandleProgressionIntent(const Packet& packet);
    void SendHello();
    void SendHelloAck(bool accepted, const std::string& reason);
    void SendClockSnapshot();
    void SendPlayerSnapshot();
    void SendPlayerPresentation();
    void SendSnapshotRequest();
    void SendSceneFlagIntent(int16_t scene, int16_t flagType, int16_t flag, bool set);
    void SendSceneFlagsSnapshot(int16_t scene);
    void SendBarrierSnapshot();
    void SendBarrierReady();
    void SendAttackIntent(uint64_t entityId, int16_t scene, uint8_t attackKind);
    void ClearPendingGuestAttack(uint64_t entityId);
    void SendCollectibleIntent(int16_t scene, int16_t flagType, int16_t flag);
    void SendProgressionSnapshot();
    void SendProgressionItemIntent(uint16_t itemId, uint16_t modIndex, uint16_t mapIndex);
    void SendDungeonKeyIntent(uint16_t mapIndex);
    void SendGlobalFlagIntent(int16_t flagType, int16_t flag, bool set);
    void SendDekuBabaSnapshot(void* actor, bool alive);
    void SendGohmaSnapshot(void* actor, bool alive);
    void UpdateDekuBaba(void* actor);
    void ApplyDekuBabaAuthority(void* actor, bool* shouldUpdate);
    void ForgetDekuBaba(void* actor);
    void UpdateGohma(void* actor);
    void ApplyGohmaAuthority(void* actor, bool* shouldUpdate);
    void ForgetGohma(void* actor);
    void CompleteGohma(void* actor);
    void InjectAutomatedTestInput(void* actor, bool* shouldUpdate);
    void RefreshRemotePlayer();
    void DestroyRemotePlayer();
    void BeginReconnectBarrier();
    void CompleteBarrierIfReady();
    void CaptureSaveOverlay();
    void RestoreSaveOverlay();
    void UpdateAutomatedTest();
    void SetAutomatedTestStage(uint8_t stage, const std::string& event);
    void ReportAutomatedTest(const std::string& event, const std::string& detail = "");
    void FailAutomatedTest(const std::string& reason);
    void WarpAutomatedTestToForest();
    void BeginAutomatedBossBarrier();
    bool SpawnAutomatedTestDekuBaba();
    void CaptureCanonicalProgression();
    void ApplyCanonicalProgression(const SharedProgressionState& state);
    void ReconcileKingZora(void* actor);
    void ReconcileZorasFountainBombableWall(void* actor);
    bool IsCurrentScope(const SessionScope& candidate) const;
    bool IsSaveLoaded() const;

    DirectSession transport;
    ConnectionPhase phase = ConnectionPhase::Idle;
    std::string playerName;
    std::string remotePlayerName;
    std::string protocolError;
    bool helloSent = false;
    bool handshakeComplete = false;
    uint32_t frameCounter = 0;
    uint32_t observedConnectionGeneration = 0;
    SessionScope sessionScope;
    uint64_t playerId = 0;
    uint64_t resumeSessionEpoch = 0;
    uint64_t resumeParticipantId = 0;
    uint64_t resumeTokenHigh = 0;
    uint64_t resumeTokenLow = 0;
    uint64_t guestTokenHigh = 0;
    uint64_t guestTokenLow = 0;
    CapabilityList negotiatedCapabilities;
    std::optional<PlayerSnapshotMessage> remotePlayerSnapshot;
    std::optional<PlayerPresentationMessage> remotePlayerPresentation;
    std::optional<PlayerPresentationMessage> lastSentPlayerPresentation;
    uint32_t nextPlayerPresentationRevision = 1;
    uint32_t lastRemoteMeleeTick = 0;
    int16_t lastRemoteMeleeScene = -1;
    void* remotePlayer = nullptr;
    bool preparingRemotePlayer = false;
    bool applyingAuthoritativeState = false;
    uint64_t nextRequestId = 1;
    uint64_t nextOperationEpoch = 1;
    DomainRevisionTracker sceneRevisions;
    DomainRevisionTracker appliedSceneRevisions;
    RequestLedger requestLedger;
    BarrierCoordinator barrierCoordinator;
    uint64_t progressionRevision = 0;
    uint64_t lastAppliedProgressionRevision = 0;
    std::unordered_map<uint64_t, CollectedLocation> collectedLocations;
    struct SceneFlagState {
        uint32_t chest = 0;
        uint32_t switches = 0;
        uint32_t clear = 0;
        uint32_t collectible = 0;
    };
    bool saveOverlayCaptured = false;
    bool observedSaveLoaded = false;
    std::unordered_map<int16_t, SceneFlagState> originalSceneFlags;
    SharedProgressionState originalProgression;
    SharedProgressionState canonicalProgression;
    bool canonicalProgressionCaptured = false;
    std::unordered_set<uint64_t> pendingGuestAttacks;
    std::unordered_map<uint64_t, uint64_t> pendingGuestAttackRequests;
    std::unordered_map<uint64_t, uint32_t> lastGuestAttackTick;
    std::unordered_map<uint64_t, void*> localDekuBabas;
    std::unordered_map<uint64_t, void*> localGohmas;
    std::unordered_map<uint64_t, ActorSnapshotMessage> actorSnapshots;

    bool automatedTestEnabled = false;
    bool automatedTestClient = false;
    bool automatedTestSaveBootRequested = false;
    bool automatedTestActorSpawned = false;
    bool automatedTestReconnectStarted = false;
    bool automatedTestProgressionTriggered = false;
    bool automatedTestWorldStateTriggered = false;
    bool automatedTestBossPrepared = false;
    bool automatedTestBossCompleted = false;
    bool automatedTestMovementObserved = false;
    bool automatedTestRemoteMovementObserved = false;
    bool automatedTestTargetObserved = false;
    bool automatedTestRemoteTargetObserved = false;
    bool automatedTestSwingObserved = false;
    bool automatedTestRemoteSwingObserved = false;
    bool automatedTestRemoteSwingRendered = false;
    bool automatedTestFirstDamageObserved = false;
    bool automatedTestPostDeathCleanupObserved = false;
    uint8_t automatedTestCombatPhase = 0;
    uint32_t automatedTestCombatPhaseTick = 0;
    uint32_t automatedTestPhysicalHits = 0;
    uint32_t automatedTestAcceptedBossHits = 0;
    int16_t automatedTestLastObservedHealth = -1;
    uint32_t automatedTestInputButtons = 0;
    int8_t automatedTestInputStickX = 0;
    int8_t automatedTestInputStickY = 0;
    uint64_t automatedTestTargetEntityId = 0;
    void* automatedTestTargetActor = nullptr;
    float automatedTestMovementOrigin[3] = {};
    float automatedTestRemotePreviousPosition[3] = {};
    uint32_t automatedTestRemotePreviousTick = 0;
    uint8_t automatedTestStage = 0;
    uint64_t automatedTestTick = 0;
    uint64_t automatedTestStageTick = 0;
    uint16_t automatedTestPort = 43390;
    std::string automatedTestAddress = "127.0.0.1";
    std::string automatedTestReportPath;
};

} // namespace HyruleCoop
