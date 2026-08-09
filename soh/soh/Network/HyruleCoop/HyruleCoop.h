#pragma once

#include "DirectSession.h"
#include "PlayerInterpolation.h"
#include "RemotePlayerRoomPolicy.h"
#include "StalchildPolicy.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    TransportTelemetry GetTransportTelemetry() const;
    const PlayerSnapshotMessage* GetRemotePlayerSnapshot() const;
    const std::string& GetRemotePlayerName() const;
    TimelineScope GetLocalTimelineScope() const;
    bool SanitizeSaveCopy(void* saveContext) const;
    void PrepareRemotePlayer(void* actor);
    void NotifyRemotePlayerDestroyed(void* actor);
    void NotifyRemotePlayerPoseApplied(bool meleeActive);
    void NotifyRemotePlayerDrawApplied(bool meleeActive, uint8_t currentMask);
    void NotifyRemotePlayerMapPositionRead(int16_t scene);
    void NotifyMasterSwordPullStarted();
    bool ShouldRegisterStalchildAttack(void* actor, bool nativeAttackActive) const;
    bool ShouldProcessStalchildHit(void* actor, void* attacker);

  private:
    void RegisterHooks(bool enabled);
    void ResetPeerState();
    void ResetSessionState();
    void BeginHandshakeIfNeeded();
    void HandlePacket(const Packet& packet);
    void HandleHello(const Packet& packet);
    void HandleHelloAck(const Packet& packet);
    void HandleClockSnapshot(const Packet& packet);
    void ApplyPendingClockSnapshot();
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
    void HandleStoryEventIntent(const Packet& packet);
    void HandleStoryEventCommand(const Packet& packet);
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
    void SendAttackIntent(uint64_t entityId, int16_t scene, uint8_t attackKind, uint8_t damageEffect = 0,
                          uint8_t damage = 1);
    void ClearPendingGuestAttack(uint64_t entityId);
    void SendCollectibleIntent(int16_t scene, int16_t flagType, int16_t flag);
    void SendProgressionSnapshot();
    void SendProgressionItemIntent(uint16_t itemId, uint16_t modIndex, uint16_t mapIndex);
    void SendDungeonKeyIntent(uint16_t mapIndex);
    void SendGlobalFlagIntent(int16_t flagType, int16_t flag, bool set);
    void SendStoryEventIntent(StoryEventKind kind);
    uint64_t SendStoryEventCommand(StoryEventKind kind, uint64_t participantId, uint64_t requestId,
                                   uint64_t operationEpoch = 0);
    void SendDekuBabaSnapshot(void* actor, bool alive);
    void SendGohmaSnapshot(void* actor, bool alive);
    void SendGenericEnemySnapshot(void* actor, bool alive);
    void ApplyStalchildSpawnerAuthority(void* actor, bool* shouldUpdate);
    void HandleStalchildInitialized(void* actor);
    void SendStalchildSnapshot(void* actor, bool alive);
    void UpdateStalchild(void* actor);
    void ApplyStalchildAuthority(void* actor, bool* shouldUpdate);
    void ForgetStalchild(void* actor);
    void ClearStalchildSceneState();
    void ClearStalchildSessionState();
    void EnsureRemoteStalchild(const ActorSnapshotMessage& message);
    void UpdateDekuBaba(void* actor);
    void ApplyDekuBabaAuthority(void* actor, bool* shouldUpdate);
    void ForgetDekuBaba(void* actor);
    void UpdateGohma(void* actor);
    void ApplyGohmaAuthority(void* actor, bool* shouldUpdate);
    void ForgetGohma(void* actor);
    void CompleteGohma(void* actor);
    void SendJabuActorSnapshot(void* actor, bool alive);
    void UpdateJabuActor(void* actor);
    void ApplyJabuActorAuthority(void* actor, bool* shouldUpdate);
    void ForgetJabuActor(void* actor);
    void SendBarinadeSnapshot(void* actor, bool alive);
    void UpdateBarinade(void* actor);
    void ApplyBarinadeAuthority(void* actor, bool* shouldUpdate);
    void ForgetBarinade(void* actor);
    void UpdateGenericEnemy(void* actor);
    void ApplyGenericEnemyAuthority(void* actor, bool* shouldUpdate);
    void ForgetGenericEnemy(void* actor);
    void UpdateGenericGuestAttack();
    void UpdateTransportTelemetry();
    void InjectAutomatedTestInput(void* actor, bool* shouldUpdate);
    void RefreshRemotePlayer();
    void ReclaimRemotePlayerActor();
    void DestroyRemotePlayer();
    bool IsRemoteTimelineCompatible() const;
    bool PrepareBarrierTimeline(const BarrierState& state);
    bool IsBarrierTimelineReady(const BarrierState& state) const;
    void PopulateBarrierTimeline(BarrierState& state) const;
    void BeginReconnectBarrier();
    void NotifyLocalStoryEvent(StoryEventKind kind);
    bool ShouldCoordinateTempleStory(StoryEventKind kind, bool remoteInitiated) const;
    bool ShouldReplayTempleStoryPresentation(StoryEventKind kind) const;
    void ReplayTempleStoryPresentation(StoryEventKind kind);
    void BeginStoryBarrier(StoryEventKind storyEvent);
    void UpdateStoryEvent();
    void CompleteBarrierIfReady();
    void CaptureSaveOverlay();
    void RestoreSaveOverlay();
    void UpdateAutomatedTest();
    void SetAutomatedTestStage(uint8_t stage, const std::string& event, const std::string& detail = "");
    void ReportAutomatedTest(const std::string& event, const std::string& detail = "");
    void FailAutomatedTest(const std::string& reason);
    void BeginAutomatedForestBarrier();
    void BeginAutomatedStalchildBarrier();
    void BeginAutomatedBossBarrier();
    bool SpawnAutomatedTestDekuBaba();
    bool SpawnAutomatedTestKeese(const ActorSnapshotMessage& anchor);
    void CaptureCanonicalProgression();
    void ApplyCanonicalProgression(const SharedProgressionState& state);
    void ReconcileKingZora(void* actor);
    void ReconcileZorasFountainBombableWall(void* actor);
    void ReconcileDoorOfTime(void* actor);
    void ReconcileMasterSwordChamber(void* actor);
    bool IsCurrentScope(const SessionScope& candidate) const;
    bool IsSaveLoaded() const;

    DirectSession transport;
    uint64_t transportTelemetrySampleAtMs = 0;
    TransportTelemetry previousTransportTelemetry;
    uint64_t realtimeBytesSentPerSecond = 0;
    uint64_t realtimeBytesReceivedPerSecond = 0;
    uint64_t tcpBytesSentPerSecond = 0;
    uint64_t tcpBytesReceivedPerSecond = 0;
    uint64_t realtimeDatagramsSentPerSecond = 0;
    uint64_t realtimeDatagramsReceivedPerSecond = 0;
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
    PlayerSnapshotInterpolator remotePlayerInterpolator;
    mutable std::optional<PlayerSnapshotMessage> remotePlayerRenderSnapshot;
    std::optional<PlayerPresentationMessage> remotePlayerPresentation;
    std::optional<PlayerPresentationMessage> lastSentPlayerPresentation;
    uint32_t nextPlayerPresentationRevision = 1;
    uint32_t lastRemoteMeleeTick = 0;
    int16_t lastRemoteMeleeScene = -1;
    void* remotePlayer = nullptr;
    bool preparingRemotePlayer = false;
    int16_t loadedSceneLayer = -1;
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
    std::vector<uint8_t> originalSaveContext;
    std::unordered_map<int16_t, SceneFlagState> originalSceneFlags;
    SharedProgressionState originalProgression;
    SharedProgressionState canonicalProgression;
    bool canonicalProgressionCaptured = false;
    std::unordered_set<uint64_t> pendingGuestAttacks;
    std::unordered_map<uint64_t, uint64_t> pendingGuestAttackRequests;
    std::unordered_map<uint64_t, uint32_t> lastGuestAttackTick;
    std::unordered_map<uint64_t, void*> localDekuBabas;
    std::unordered_map<uint64_t, void*> localGohmas;
    std::unordered_set<uint64_t> localGohmaDeathPresentations;
    std::unordered_map<uint64_t, void*> localGenericEnemies;
    std::unordered_map<uint64_t, void*> localJabuActors;
    std::unordered_map<uint64_t, void*> localBarinadeActors;
    std::unordered_map<uint64_t, void*> localStalchildren;
    std::unordered_map<uint64_t, StalchildTarget> stalchildTargets;
    std::unordered_map<uint64_t, uint16_t> stalchildAttackSequences;
    std::unordered_map<uint64_t, uint16_t> appliedStalchildAttackSequences;
    std::unordered_set<uint64_t> retiredStalchildren;
    DynamicStalchildIdentityRegistry stalchildIdentityRegistry;
    DynamicStalchildSnapshotLifecycle stalchildSnapshotLifecycle;
    bool spawningReplicatedStalchild = false;
    std::unordered_map<uint64_t, ActorSnapshotMessage> actorSnapshots;
    std::unordered_set<uint64_t> genericGuestTargetsHitThisSwing;
    std::optional<uint64_t> guestStalchildTargetThisSwing;
    uint64_t lastAppliedStoryOperationEpoch = 0;
    std::optional<ClockSnapshotMessage> pendingClockSnapshot;
    StoryEventKind pendingStoryEvent = StoryEventKind::None;
    StoryEventKind activeStoryEvent = StoryEventKind::None;
    bool storyCutsceneObserved = false;
    bool storyPresentationBaselineApplied = false;
    bool doorOfTimeOpeningPresented = false;
    bool masterSwordEntrancePresented = false;
    bool masterSwordPullPresented = false;
    bool coordinatedMasterSwordPullActive = false;

    bool automatedTestEnabled = false;
    bool automatedTestClient = false;
    bool automatedTestRequireDraw = false;
    bool automatedTestSaveBootRequested = false;
    bool automatedTestActorSpawned = false;
    bool automatedTestReconnectStarted = false;
    bool automatedTestProgressionTriggered = false;
    bool automatedTestProgressionResourcesCaptured = false;
    bool automatedTestDungeonMapChestTriggered = false;
    bool automatedTestDungeonCompassIntentTriggered = false;
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
    bool automatedTestRemoteSwingDrawn = false;
    bool automatedTestRemotePresentationRendered = false;
    bool automatedTestRemoteMapPositionRead = false;
    bool automatedTestFirstDamageObserved = false;
    bool automatedTestBossDeathPresentationObserved = false;
    bool automatedTestPostDeathCleanupObserved = false;
    bool automatedTestStalchildDawnTriggered = false;
    bool automatedTestStalchildTargetAgreementObserved = false;
    bool automatedTestStalchildDeathTransitionObserved = false;
    bool automatedTestStalchildDropObserved = false;
    bool automatedTestStalchildBystanderObserved = false;
    bool automatedTestSaveRequested = false;
    std::atomic<bool> automatedTestSaveCompleted = false;
    uint8_t automatedTestCombatPhase = 0;
    uint32_t automatedTestCombatPhaseTick = 0;
    uint32_t automatedTestPhysicalHits = 0;
    uint32_t automatedTestAcceptedBossHits = 0;
    uint32_t automatedTestRecoveryStartedTick = 0;
    uint64_t automatedTestMissingTargetTick = 0;
    uint64_t automatedTestStalchildBystanderEntityId = 0;
    uint64_t automatedTestDungeonCompassIntentSnapshotRevision = 0;
    int16_t automatedTestLastObservedHealth = -1;
    int16_t automatedTestNonTargetHealth = -1;
    int16_t automatedTestProgressionRupees = 0;
    int8_t automatedTestProgressionArrows = 0;
    int8_t automatedTestProgressionMagic = 0;
    int16_t automatedTestHostReconnectRupees = 0;
    int8_t automatedTestHostReconnectArrows = 0;
    int8_t automatedTestHostReconnectMagic = 0;
    bool automatedTestHostReconnectResourcesCaptured = false;
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
