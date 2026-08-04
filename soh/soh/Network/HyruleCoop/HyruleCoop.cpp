#include "HyruleCoop.h"
#include "DekuBabaAdapter.h"
#include "GohmaAdapter.h"
#include "ProgressionAdapter.h"
#include "RemotePlayer.h"

#include "soh/SaveManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/item-tables/ItemTableManager.h"
#include "soh/OTRGlobals.h"
#include "ship/Context.h"
#include "ship/utils/StrHash64.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "functions.h"
#include "macros.h"
#include "src/overlays/actors/ovl_Bg_Spot08_Bakudankabe/z_bg_spot08_bakudankabe.h"
#include "src/overlays/actors/ovl_En_Kz/z_en_kz.h"
#include "variables.h"
#include "z64.h"

extern PlayState* gPlayState;
void Sram_OpenSave(void);
GetItemID RetrieveGetItemIDFromItemID(ItemID itemID);
void Player_UseItem(PlayState* play, Player* player, s32 item);
s32 EnKz_SetMovedPos(EnKz* thisx, PlayState* play);
void EnKz_PreMweepWait(EnKz* thisx, PlayState* play);
void EnKz_Wait(EnKz* thisx, PlayState* play);
}

namespace HyruleCoop {
namespace {

enum AutomatedTestStage : uint8_t {
    TestDisabled,
    TestAwaitingSave,
    TestAwaitingReady,
    TestAwaitingScene,
    TestAwaitingActors,
    TestAttacking,
    TestAwaitingCollection,
    TestAwaitingProgression,
    TestAwaitingWorldState,
    TestAwaitingBossScene,
    TestAwaitingBossReady,
    TestBossCombat,
    TestAwaitingBossCompletion,
    TestAwaitingReconnect,
    TestComplete,
    TestFailed,
};

enum AutomatedCombatPhase : uint8_t {
    CombatSetup,
    CombatMove,
    CombatAcquireTarget,
    CombatFirstSwing,
    CombatAwaitFirstDamage,
    CombatSecondSwing,
    CombatAwaitDeath,
    CombatVerifyCleanup,
    CombatDone,
};

constexpr int16_t kAutomatedTestCollectibleFlag = 0x1E;
constexpr int16_t kAutomatedTestSwitchFlag = 0x1F;
constexpr int8_t kAutomatedTestHostMagic = 12;
constexpr int8_t kAutomatedTestClientMagic = 36;
constexpr int16_t kGohmaRoom = 1;
constexpr uint32_t kGohmaRoomMask = 1u << kGohmaRoom;
constexpr uint32_t kGuestAttackCooldownFrames = 6;
constexpr int64_t kGuestAttackStateWindowFrames = 90;
constexpr const char* kHyruleCoopCompatibilityId = "hyrule-coop-poc.3";

uint64_t HashFile(const std::filesystem::path& path, uint64_t hash) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return hash;
    }

    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), buffer.size());
        const std::streamsize count = stream.gcount();
        if (count > 0) {
            hash = update_crc64(buffer.data(), static_cast<uint32_t>(count), hash);
        }
    }
    return hash;
}

std::filesystem::path CurrentExecutablePath() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length > 0 && length < path.size()) {
        path.resize(length);
        return path;
    }
#elif defined(__linux__)
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        return path;
    }
#endif
    return {};
}

std::string CurrentBuildId() {
    static const std::string buildId = []() {
        uint64_t runtimeHash = INITIAL_CRC64;
        const auto executablePath = CurrentExecutablePath();
        if (!executablePath.empty()) {
            runtimeHash = HashFile(executablePath, runtimeHash);
        } else {
            const auto* buildDate = reinterpret_cast<const char*>(gBuildDate);
            runtimeHash = update_crc64(buildDate, static_cast<uint32_t>(std::strlen(buildDate)), runtimeHash);
        }

        const std::string portArchive = Ship::Context::LocateFileAcrossAppDirs("soh.o2r");
        runtimeHash = HashFile(portArchive, runtimeHash);

        std::ostringstream output;
        output << kHyruleCoopCompatibilityId << "+" << gGitCommitHash << "+runtime-" << std::hex
               << std::setfill('0') << std::setw(16) << runtimeHash;
        return output.str();
    }();
    return buildId;
}

bool IsValidDurableSceneFlag(int16_t scene, int16_t flagType, int16_t flag) {
    if (scene < 0 || scene >= SCENE_ID_MAX || flag < 0 || flag >= 0x20) {
        return false;
    }
    if (flagType == FLAG_SCENE_COLLECTIBLE && flag == 0) {
        return false;
    }
    return flagType == FLAG_SCENE_SWITCH || flagType == FLAG_SCENE_TREASURE ||
           flagType == FLAG_SCENE_CLEAR || flagType == FLAG_SCENE_COLLECTIBLE;
}

bool IsValidDurableGlobalFlag(int16_t flagType, int16_t flag) {
    return flagType == FLAG_EVENT_CHECK_INF && flag >= 0 && flag < 14 * 16;
}

uint64_t SceneStreamId(int16_t scene) {
    return static_cast<uint64_t>(static_cast<uint16_t>(scene)) + 1;
}

bool SamePresentation(const PlayerPresentationMessage& first, const PlayerPresentationMessage& second) {
    return first.scope == second.scope && first.boots == second.boots && first.shield == second.shield &&
           first.tunic == second.tunic && first.currentMask == second.currentMask &&
           first.buttonItem == second.buttonItem && first.itemAction == second.itemAction &&
           first.heldItemAction == second.heldItemAction && first.modelGroup == second.modelGroup;
}

void ApplyPresentation(PlayerSnapshotMessage& player, const PlayerPresentationMessage& presentation) {
    player.boots = presentation.boots;
    player.shield = presentation.shield;
    player.tunic = presentation.tunic;
    player.currentMask = presentation.currentMask;
    player.buttonItem = presentation.buttonItem;
    player.itemAction = presentation.itemAction;
    player.heldItemAction = presentation.heldItemAction;
    player.modelGroup = presentation.modelGroup;
}

const CapabilityList& SupportedCapabilities() {
    static const CapabilityList capabilities = NormalizeCapabilities(
        { Capability::Coordination, Capability::RequestLedger, Capability::OotClock, Capability::OotPlayer,
          Capability::OotPlayerPresentation, Capability::OotSceneFlags, Capability::OotDekuBaba,
          Capability::OotGuestAttack,
          Capability::OotCollectible, Capability::OotSharedProgression, Capability::OotGohma })
                                                   .value();
    return capabilities;
}

const CapabilityList& RequiredCapabilities() {
    return SupportedCapabilities();
}

uint64_t CollectibleLocationId(int16_t scene, int16_t flagType, int16_t flag) {
    return (static_cast<uint64_t>(static_cast<uint16_t>(scene) + 1) << 32) |
           (static_cast<uint64_t>(static_cast<uint16_t>(flagType)) << 16) |
           static_cast<uint16_t>(flag);
}

float DistanceSquared(const float* first, const float* second) {
    const float x = first[0] - second[0];
    const float y = first[1] - second[1];
    const float z = first[2] - second[2];
    return x * x + y * y + z * z;
}

float HorizontalDistanceSquared(const float* first, const float* second) {
    const float x = first[0] - second[0];
    const float z = first[2] - second[2];
    return x * x + z * z;
}

void EquipAutomatedTestSword(Player* player) {
    gSaveContext.inventory.equipment |=
        OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI);
    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
    player->currentSwordItemId = ITEM_SWORD_KOKIRI;
}

void PositionAutomatedTestPlayer(Player* player, Actor* target, float distance) {
    player->actor.world.pos = { target->world.pos.x, target->world.pos.y, target->world.pos.z - distance };
    player->actor.prevPos = player->actor.world.pos;
    player->actor.home.pos = player->actor.world.pos;
    const int16_t yaw = Actor_WorldYawTowardActor(&player->actor, target);
    player->actor.world.rot.y = yaw;
    player->actor.shape.rot.y = yaw;
    player->yaw = yaw;
    player->linearVelocity = 0.0f;
}

} // namespace

Manager::~Manager() {
    Disconnect();
}

void Manager::ConfigureAutomatedTestFromEnvironment() {
    const char* role = std::getenv("HYRULE_COOP_TEST_ROLE");
    if (role == nullptr) {
        return;
    }

    const std::string requestedRole = role;
    if (requestedRole != "host" && requestedRole != "client") {
        SPDLOG_ERROR("[HyruleCoopTest] HYRULE_COOP_TEST_ROLE must be host or client");
        return;
    }

    if (const char* port = std::getenv("HYRULE_COOP_TEST_PORT"); port != nullptr) {
        try {
            const unsigned long parsed = std::stoul(port);
            if (parsed > 1024 && parsed <= 65535) {
                automatedTestPort = static_cast<uint16_t>(parsed);
            }
        } catch (...) {
            SPDLOG_ERROR("[HyruleCoopTest] Invalid HYRULE_COOP_TEST_PORT: {}", port);
            return;
        }
    }
    if (const char* address = std::getenv("HYRULE_COOP_TEST_ADDRESS"); address != nullptr && address[0] != '\0') {
        automatedTestAddress = address;
    }
    if (const char* report = std::getenv("HYRULE_COOP_TEST_REPORT"); report != nullptr && report[0] != '\0') {
        automatedTestReportPath = report;
        std::ofstream clearReport(automatedTestReportPath, std::ios::trunc);
    }

    automatedTestEnabled = true;
    automatedTestClient = requestedRole == "client";
    automatedTestStage = TestAwaitingSave;
    automatedTestStageTick = 0;
    ReportAutomatedTest("configured", "port=" + std::to_string(automatedTestPort) + " build=" + CurrentBuildId());

    const bool started = automatedTestClient
                             ? Join(automatedTestAddress, automatedTestPort, "Local Guest")
                             : Host(automatedTestPort, "Local Host");
    if (!started) {
        FailAutomatedTest("transport could not start");
    }
}

bool Manager::Host(uint16_t port, const std::string& requestedPlayerName) {
    Disconnect();
    playerName = requestedPlayerName.empty() ? "Host" : requestedPlayerName;
    sessionScope = { GenerateNonce64(), 0 };
    playerId = 1;
    guestTokenHigh = GenerateNonce64();
    guestTokenLow = GenerateNonce64();
    requestLedger.BeginScope(sessionScope);
    CaptureSaveOverlay();
    phase = ConnectionPhase::WaitingForPeer;
    RegisterHooks(true);
    if (!transport.StartHost(port)) {
        protocolError = "Invalid host settings";
        phase = ConnectionPhase::Failed;
        RegisterHooks(false);
        return false;
    }
    return true;
}

bool Manager::Join(const std::string& address, uint16_t port, const std::string& requestedPlayerName) {
    Disconnect();
    playerName = requestedPlayerName.empty() ? "Guest" : requestedPlayerName;
    CaptureSaveOverlay();
    phase = ConnectionPhase::Connecting;
    RegisterHooks(true);
    if (!transport.StartClient(address, port)) {
        protocolError = "Invalid join settings";
        phase = ConnectionPhase::Failed;
        RegisterHooks(false);
        return false;
    }
    return true;
}

void Manager::Disconnect() {
    RestoreSaveOverlay();
    DestroyRemotePlayer();
    RegisterHooks(false);
    transport.Stop();
    ResetSessionState();
    phase = ConnectionPhase::Idle;
}

void Manager::Update() {
    frameCounter++;
    automatedTestTick++;

    const bool saveLoaded = IsSaveLoaded();
    if (saveLoaded && !saveOverlayCaptured) {
        CaptureSaveOverlay();
    }
    if (saveLoaded && !observedSaveLoaded && handshakeComplete) {
        if (transport.GetRole() == SessionRole::Host) {
            BeginReconnectBarrier();
        } else if (transport.GetRole() == SessionRole::Client) {
            SendSnapshotRequest();
        }
    }
    observedSaveLoaded = saveLoaded;

    const TransportState transportState = transport.GetState();
    const uint32_t connectionGeneration = transport.GetConnectionGeneration();
    if (connectionGeneration != 0 && connectionGeneration != observedConnectionGeneration) {
        ResetPeerState();
        observedConnectionGeneration = connectionGeneration;
    }

    // A peer can send a final protocol response and close immediately afterward.
    // Drain those packets before interpreting the terminal transport state so a
    // specific handshake rejection is not replaced by a generic disconnect.
    if (transportState != TransportState::Error && transportState != TransportState::Disconnected) {
        BeginHandshakeIfNeeded();
    }
    for (const Packet& packet : transport.TakeIncomingPackets()) {
        HandlePacket(packet);
    }

    if (transportState == TransportState::Error) {
        DestroyRemotePlayer();
        remotePlayerSnapshot.reset();
        remotePlayerInterpolator.Reset();
        remotePlayerRenderSnapshot.reset();
        if (phase != ConnectionPhase::Failed || protocolError.empty()) {
            protocolError = transport.GetLastError();
            phase = ConnectionPhase::Failed;
        }
        if (automatedTestEnabled && automatedTestStage != TestFailed) {
            FailAutomatedTest(protocolError);
        }
        return;
    }
    if (transportState == TransportState::Disconnected) {
        if (transport.GetRole() == SessionRole::Host) {
            ResetPeerState();
            phase = ConnectionPhase::WaitingForPeer;
            if (automatedTestEnabled && automatedTestStage >= TestAwaitingCollection &&
                automatedTestStage < TestComplete) {
                automatedTestReconnectStarted = true;
                SetAutomatedTestStage(TestAwaitingReconnect, "peer-disconnected-for-reconnect");
            }
        } else {
            DestroyRemotePlayer();
            remotePlayerSnapshot.reset();
            remotePlayerInterpolator.Reset();
            remotePlayerRenderSnapshot.reset();
            if (phase != ConnectionPhase::Failed || protocolError.empty()) {
                protocolError = "The host disconnected";
                phase = ConnectionPhase::Failed;
            }
            if (automatedTestEnabled && automatedTestStage != TestFailed) {
                FailAutomatedTest(protocolError);
            }
        }
        return;
    }
    if (transportState == TransportState::Listening) {
        if (helloSent || handshakeComplete || remotePlayerSnapshot.has_value()) {
            ResetPeerState();
        }
        phase = ConnectionPhase::WaitingForPeer;
    } else if (transportState == TransportState::Connecting || transportState == TransportState::Starting) {
        phase = ConnectionPhase::Connecting;
    }

    if (handshakeComplete && transport.GetRole() == SessionRole::Host &&
        barrierCoordinator.IsExpired(frameCounter)) {
        barrierCoordinator.Abort();
        SendBarrierSnapshot();
        protocolError = "The other player did not become ready for the coordinated world state";
        transport.DisconnectPeer();
        return;
    }

    if (handshakeComplete && transport.GetRole() == SessionRole::Host && frameCounter % 30 == 0 &&
        barrierCoordinator.GetState().phase == BarrierPhase::WaitingForParticipants) {
        SendBarrierSnapshot();
    }

    if (handshakeComplete && transport.GetRole() == SessionRole::Host && frameCounter % 30 == 0 && IsSaveLoaded()) {
        SendClockSnapshot();
    }

    UpdateAutomatedTest();
}

ConnectionPhase Manager::GetPhase() const {
    return phase;
}

SessionRole Manager::GetRole() const {
    return transport.GetRole();
}

std::string Manager::GetStatusText() const {
    switch (phase) {
        case ConnectionPhase::Idle:
            return "Not connected";
        case ConnectionPhase::WaitingForPeer:
            return "Waiting for another player...";
        case ConnectionPhase::Connecting:
            return "Connecting...";
        case ConnectionPhase::Handshaking:
            return "Coordinating the shared world...";
        case ConnectionPhase::Ready:
            return transport.GetRole() == SessionRole::Host ? "Hosting - player connected" : "Connected to host";
        case ConnectionPhase::Failed:
            return protocolError.empty() ? "Connection failed" : protocolError;
    }
    return "Unknown connection state";
}

bool Manager::IsActive() const {
    return phase != ConnectionPhase::Idle;
}

bool Manager::IsReady() const {
    return phase == ConnectionPhase::Ready;
}

bool Manager::IsPreparingRemotePlayer() const {
    return preparingRemotePlayer;
}

TransportTelemetry Manager::GetTransportTelemetry() const {
    return transport.GetTelemetry();
}

const PlayerSnapshotMessage* Manager::GetRemotePlayerSnapshot() const {
    PlayerSnapshotMessage sampled;
    if (!remotePlayerInterpolator.Sample(SDL_GetTicks64(), sampled)) {
        return nullptr;
    }
    if (remotePlayerPresentation.has_value()) {
        ApplyPresentation(sampled, *remotePlayerPresentation);
    }
    remotePlayerRenderSnapshot = sampled;
    return &remotePlayerRenderSnapshot.value();
}

const std::string& Manager::GetRemotePlayerName() const {
    return remotePlayerName;
}

void Manager::SanitizeSaveCopy(void* saveContextRef) const {
    if (!saveOverlayCaptured || saveContextRef == nullptr) {
        return;
    }
    SaveContext* saveContext = static_cast<SaveContext*>(saveContextRef);
    for (const auto& [scene, state] : originalSceneFlags) {
        SavedSceneFlags& flags = saveContext->sceneFlags[scene];
        flags.chest = state.chest;
        flags.swch = state.switches;
        flags.clear = state.clear;
        flags.collect = state.collectible;
    }
    ApplySharedProgression(saveContext, originalProgression);
}

void Manager::PrepareRemotePlayer(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    remotePlayer = actor;
    Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
    actor->id = ACTOR_EN_OE2;
    actor->category = ACTORCAT_NPC;
    actor->init = HyruleCoopRemotePlayer_Init;
    actor->update = HyruleCoopRemotePlayer_Update;
    actor->draw = HyruleCoopRemotePlayer_Draw;
    actor->destroy = HyruleCoopRemotePlayer_Destroy;
}

void Manager::NotifyRemotePlayerDestroyed(void* actor) {
    if (remotePlayer == actor) {
        remotePlayer = nullptr;
    }
}

void Manager::NotifyRemotePlayerPoseApplied(bool meleeActive) {
    if (!meleeActive || !automatedTestEnabled || automatedTestClient || automatedTestRemoteSwingRendered) {
        return;
    }
    if (automatedTestStage == TestAttacking) {
        automatedTestRemoteSwingRendered = true;
        ReportAutomatedTest("deku-baba-remote-swing-rendered",
                            "remote Link renderer applied the guest sword pose");
    } else if (automatedTestStage == TestBossCombat) {
        automatedTestRemoteSwingRendered = true;
        ReportAutomatedTest("gohma-remote-swing-rendered",
                            "remote Link renderer applied the guest sword pose");
    }
}

void Manager::RegisterHooks(bool enabled) {
    COND_HOOK(OnZTitleUpdate, enabled && automatedTestEnabled, [this](void* gameStateRef) {
        if (automatedTestSaveBootRequested || !SaveManager::Instance->SaveFile_Exist(0)) {
            return;
        }
        automatedTestSaveBootRequested = true;
        gSaveContext.fileNum = 0;
        gSaveContext.gameMode = GAMEMODE_NORMAL;
        Sram_OpenSave();
        GameState* gameState = static_cast<GameState*>(gameStateRef);
        SET_NEXT_GAMESTATE(gameState, Play_Init, PlayState);
        gameState->running = false;
        gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = ENTR_LOAD_OPENING;
        gSaveContext.respawnFlag = 0;
        gSaveContext.seqId = static_cast<uint8_t>(NA_BGM_DISABLED);
        gSaveContext.natureAmbienceId = 0xFF;
        gSaveContext.showTitleCard = true;
        gSaveContext.timerState = TIMER_STATE_OFF;
        gSaveContext.subTimerState = SUBTIMER_STATE_OFF;
        gSaveContext.nextTransitionType = TRANS_NEXT_TYPE_DEFAULT;
        GameInteractor_ExecuteOnLoadGame(0);
        ReportAutomatedTest("save-boot-requested", "slot=1");
    });
    COND_HOOK(OnGameFrameUpdate, enabled, [this]() { Update(); });
    COND_HOOK(OnSceneSpawnActors, enabled, [this]() {
        remotePlayer = nullptr;
        localDekuBabas.clear();
        localGohmas.clear();
        if (automatedTestEnabled && !automatedTestClient && gPlayState != nullptr &&
            gPlayState->sceneNum == SCENE_DEKU_TREE_BOSS) {
            // Keep the authoritative test player outside Gohma's entrance trigger. The harness prepares the
            // live boss directly after the scene barrier, so starting the intro would disable real player input.
            Player* player = GET_PLAYER(gPlayState);
            player->actor.world.pos.x = 0.0f;
            player->actor.world.pos.z = 0.0f;
            player->actor.prevPos = player->actor.world.pos;
            player->actor.home.pos = player->actor.world.pos;
        }
        RefreshRemotePlayer();
        SendSnapshotRequest();
        SendBarrierReady();
        const BarrierState& barrier = barrierCoordinator.GetState();
        if (transport.GetRole() == SessionRole::Host && barrier.phase == BarrierPhase::WaitingForParticipants &&
            IsCurrentScope(barrier.scope) && gPlayState != nullptr && gPlayState->sceneNum == barrier.targetScene) {
            barrierCoordinator.MarkReady(1);
            CompleteBarrierIfReady();
        }
    });
    COND_ID_HOOK(ShouldActorInit, ACTOR_PLAYER, enabled, [this](void* actor, bool*) {
        if (preparingRemotePlayer) {
            PrepareRemotePlayer(actor);
        }
    });
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_PLAYER, enabled,
                 [this](void* actor, bool* shouldUpdate) { InjectAutomatedTestInput(actor, shouldUpdate); });
    COND_HOOK(OnPlayerUpdate, enabled, [this]() {
        if (!handshakeComplete) {
            return;
        }
        SendPlayerPresentation();
        SendPlayerSnapshot();
        if (remotePlayer == nullptr) {
            RefreshRemotePlayer();
        }
    });
    COND_HOOK(OnSceneFlagSet, enabled, [this](int16_t scene, int16_t flagType, int16_t flag) {
        if (applyingAuthoritativeState || !IsValidDurableSceneFlag(scene, flagType, flag)) {
            return;
        }
        if (transport.GetRole() == SessionRole::Host) {
            sceneRevisions.Advance(SceneStreamId(scene));
            if (flagType == FLAG_SCENE_COLLECTIBLE) {
                const uint64_t locationId = CollectibleLocationId(scene, flagType, flag);
                if (!collectedLocations.contains(locationId)) {
                    CollectedLocation location;
                    location.locationId = locationId;
                    location.scene = scene;
                    location.flagType = flagType;
                    location.flag = flag;
                    collectedLocations[locationId] = location;
                    ++progressionRevision;
                }
            }
            if (handshakeComplete) {
                SendSceneFlagsSnapshot(scene);
                if (flagType == FLAG_SCENE_COLLECTIBLE) {
                    SendProgressionSnapshot();
                }
            }
        } else if (handshakeComplete) {
            if (flagType == FLAG_SCENE_COLLECTIBLE) {
                SendCollectibleIntent(scene, flagType, flag);
            } else {
                SendSceneFlagIntent(scene, flagType, flag, true);
            }
        }
    });
    COND_HOOK(OnSceneFlagUnset, enabled, [this](int16_t scene, int16_t flagType, int16_t flag) {
        if (applyingAuthoritativeState || !IsValidDurableSceneFlag(scene, flagType, flag)) {
            return;
        }
        if (transport.GetRole() == SessionRole::Host) {
            sceneRevisions.Advance(SceneStreamId(scene));
            if (handshakeComplete) {
                SendSceneFlagsSnapshot(scene);
            }
        } else if (handshakeComplete) {
            SendSceneFlagIntent(scene, flagType, flag, false);
        }
    });
    COND_HOOK(OnFlagSet, enabled, [this](int16_t flagType, int16_t flag) {
        if (applyingAuthoritativeState || !IsValidDurableGlobalFlag(flagType, flag)) {
            return;
        }
        if (transport.GetRole() == SessionRole::Host) {
            CaptureCanonicalProgression();
            ++progressionRevision;
            if (handshakeComplete) {
                SendProgressionSnapshot();
            }
        } else if (handshakeComplete) {
            SendGlobalFlagIntent(flagType, flag, true);
        }
    });
    COND_HOOK(OnFlagUnset, enabled, [this](int16_t flagType, int16_t flag) {
        if (applyingAuthoritativeState || !IsValidDurableGlobalFlag(flagType, flag)) {
            return;
        }
        if (transport.GetRole() == SessionRole::Host) {
            CaptureCanonicalProgression();
            ++progressionRevision;
            if (handshakeComplete) {
                SendProgressionSnapshot();
            }
        } else if (handshakeComplete) {
            SendGlobalFlagIntent(flagType, flag, false);
        }
    });
    COND_HOOK(OnItemReceive, enabled, [this](GetItemEntry itemEntry) {
        if (applyingAuthoritativeState || !IsSharedProgressionItem(itemEntry.itemId, itemEntry.modIndex,
                                                                   itemEntry.getItemCategory)) {
            return;
        }
        if (transport.GetRole() == SessionRole::Host) {
            CaptureCanonicalProgression();
            ++progressionRevision;
            SendProgressionSnapshot();
        } else if (handshakeComplete) {
            SendProgressionItemIntent(itemEntry.itemId, itemEntry.modIndex, gSaveContext.mapIndex);
        }
    });
    COND_HOOK(OnDungeonKeyUsed, enabled, [this](uint16_t mapIndex) {
        if (applyingAuthoritativeState || mapIndex >= std::size(gSaveContext.inventory.dungeonKeys)) {
            return;
        }
        if (transport.GetRole() == SessionRole::Host) {
            CaptureCanonicalProgression();
            ++progressionRevision;
            SendProgressionSnapshot();
        } else if (handshakeComplete) {
            SendDungeonKeyIntent(mapIndex);
        }
    });
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_DEKUBABA, enabled, [this](void* actor) { UpdateDekuBaba(actor); });
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_DEKUBABA, enabled,
                 [this](void* actor, bool* shouldUpdate) { ApplyDekuBabaAuthority(actor, shouldUpdate); });
    COND_ID_HOOK(OnActorKill, ACTOR_EN_DEKUBABA, enabled, [this](void* actor) {
        if (transport.GetRole() == SessionRole::Host) {
            SendDekuBabaSnapshot(actor, false);
        }
    });
    COND_ID_HOOK(OnActorDestroy, ACTOR_EN_DEKUBABA, enabled, [this](void* actor) { ForgetDekuBaba(actor); });
    COND_ID_HOOK(OnActorUpdate, ACTOR_BOSS_GOMA, enabled, [this](void* actor) { UpdateGohma(actor); });
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BOSS_GOMA, enabled,
                 [this](void* actor, bool* shouldUpdate) { ApplyGohmaAuthority(actor, shouldUpdate); });
    COND_ID_HOOK(OnActorKill, ACTOR_BOSS_GOMA, enabled, [this](void* actor) {
        if (transport.GetRole() == SessionRole::Host) {
            SendGohmaSnapshot(actor, false);
        }
    });
    COND_ID_HOOK(OnActorDestroy, ACTOR_BOSS_GOMA, enabled, [this](void* actor) { ForgetGohma(actor); });
    COND_ID_HOOK(OnBossDefeat, ACTOR_BOSS_GOMA, enabled, [this](void* actor) { CompleteGohma(actor); });
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_KZ, enabled, [this](void* actor) { ReconcileKingZora(actor); });
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_SPOT08_BAKUDANKABE, enabled,
                 [this](void* actor, bool*) { ReconcileZorasFountainBombableWall(actor); });
}

void Manager::ResetPeerState() {
    DestroyRemotePlayer();
    transport.DisableRealtime();
    helloSent = false;
    handshakeComplete = false;
    playerId = 0;
    remotePlayerName.clear();
    remotePlayerSnapshot.reset();
    remotePlayerInterpolator.Reset();
    remotePlayerRenderSnapshot.reset();
    remotePlayerPresentation.reset();
    lastSentPlayerPresentation.reset();
    nextPlayerPresentationRevision = 1;
    lastRemoteMeleeTick = 0;
    lastRemoteMeleeScene = -1;
    preparingRemotePlayer = false;
    applyingAuthoritativeState = false;
    negotiatedCapabilities.clear();
    pendingGuestAttacks.clear();
    pendingGuestAttackRequests.clear();
    lastGuestAttackTick.clear();
    protocolError.clear();
}

void Manager::ResetSessionState() {
    ResetPeerState();
    frameCounter = 0;
    observedConnectionGeneration = 0;
    sessionScope = {};
    guestTokenHigh = 0;
    guestTokenLow = 0;
    nextOperationEpoch = 1;
    sceneRevisions.Clear();
    appliedSceneRevisions.Clear();
    requestLedger.BeginScope({});
    barrierCoordinator = BarrierCoordinator();
    progressionRevision = 0;
    lastAppliedProgressionRevision = 0;
    collectedLocations.clear();
    saveOverlayCaptured = false;
    observedSaveLoaded = false;
    originalSceneFlags.clear();
    originalProgression = {};
    canonicalProgression = {};
    canonicalProgressionCaptured = false;
    localDekuBabas.clear();
    localGohmas.clear();
    actorSnapshots.clear();
}

void Manager::BeginHandshakeIfNeeded() {
    if (transport.GetState() != TransportState::Connected || helloSent) {
        return;
    }

    phase = ConnectionPhase::Handshaking;
    if (transport.GetRole() == SessionRole::Client) {
        SendHello();
    }
    helloSent = true;
}

void Manager::HandlePacket(const Packet& packet) {
    switch (packet.type) {
        case MessageType::Hello:
            HandleHello(packet);
            break;
        case MessageType::HelloAck:
            HandleHelloAck(packet);
            break;
        case MessageType::ClockSnapshot:
            HandleClockSnapshot(packet);
            break;
        case MessageType::PlayerSnapshot:
            HandlePlayerSnapshot(packet);
            break;
        case MessageType::PlayerPresentation:
            HandlePlayerPresentation(packet);
            break;
        case MessageType::SnapshotRequest:
            HandleSnapshotRequest(packet);
            break;
        case MessageType::SceneFlagIntent:
            HandleSceneFlagIntent(packet);
            break;
        case MessageType::SceneFlagsSnapshot:
            HandleSceneFlagsSnapshot(packet);
            break;
        case MessageType::ActorSnapshot:
            HandleActorSnapshot(packet);
            break;
        case MessageType::BarrierSnapshot:
            HandleBarrierSnapshot(packet);
            break;
        case MessageType::BarrierReady:
            HandleBarrierReady(packet);
            break;
        case MessageType::AttackIntent:
            HandleAttackIntent(packet);
            break;
        case MessageType::CollectibleIntent:
            HandleCollectibleIntent(packet);
            break;
        case MessageType::ProgressionSnapshot:
            HandleProgressionSnapshot(packet);
            break;
        case MessageType::ProgressionIntent:
            HandleProgressionIntent(packet);
            break;
        default:
            SPDLOG_WARN("[HyruleCoop] Ignoring unsupported packet type {}", static_cast<uint16_t>(packet.type));
            break;
    }
}

void Manager::HandleHello(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host) {
        return;
    }

    const auto message = DecodeHello(packet.payload);
    if (!message.has_value()) {
        SendHelloAck(false, "Malformed handshake");
        transport.DisconnectPeer();
        return;
    }
    if (message->gameId != GameId::OcarinaOfTime) {
        SendHelloAck(false, "The host and guest selected different games");
        transport.DisconnectPeer();
        return;
    }
    if (message->buildId != CurrentBuildId()) {
        SPDLOG_WARN("[HyruleCoop] Rejected guest build {} because the host uses {}", message->buildId,
                    CurrentBuildId());
        SendHelloAck(false, "The host and guest are running different builds");
        transport.DisconnectPeer();
        return;
    }
    if (!SupportsCapabilities(message->capabilities, RequiredCapabilities())) {
        SendHelloAck(false, "The guest does not support the required co-op capabilities");
        transport.DisconnectPeer();
        return;
    }

    const bool requestedCurrentSession = message->requestedSessionEpoch == sessionScope.sessionEpoch;
    if (requestedCurrentSession &&
        (message->requestedParticipantId != 2 || message->resumeTokenHigh != guestTokenHigh ||
         message->resumeTokenLow != guestTokenLow)) {
        SendHelloAck(false, "The saved co-op identity is not valid for this session");
        transport.DisconnectPeer();
        return;
    }

    playerId = 1;
    remotePlayerName = message->playerName.empty() ? "Guest" : message->playerName.substr(0, 24);
    negotiatedCapabilities = IntersectCapabilities(SupportedCapabilities(), message->capabilities);
    transport.ConfigureRealtime(sessionScope, 2, guestTokenHigh, guestTokenLow);
    SendHelloAck(true, "");
    handshakeComplete = true;
    phase = IsSaveLoaded() ? ConnectionPhase::Handshaking : ConnectionPhase::Ready;
    if (IsSaveLoaded()) {
        SendPlayerSnapshot();
        SendClockSnapshot();
        SendSceneFlagsSnapshot(gPlayState->sceneNum);
        SendProgressionSnapshot();
        BeginReconnectBarrier();
    }
    SPDLOG_INFO("[HyruleCoop] Accepted player {}", message->playerName);
}

void Manager::HandleHelloAck(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client) {
        return;
    }

    const auto message = DecodeHelloAck(packet.payload);
    if (!message.has_value()) {
        protocolError = "The host sent an invalid handshake response";
        phase = ConnectionPhase::Failed;
        return;
    }
    if (!message->accepted) {
        protocolError = message->reason.empty() ? "The host rejected the connection" : message->reason;
        phase = ConnectionPhase::Failed;
        if (automatedTestEnabled) {
            FailAutomatedTest(protocolError);
        }
        return;
    }

    if (message->sessionEpoch == 0 || message->participantId == 0 ||
        !SupportsCapabilities(message->capabilities, RequiredCapabilities())) {
        protocolError = "The host returned an incomplete co-op session identity";
        phase = ConnectionPhase::Failed;
        return;
    }
    playerId = message->participantId;
    remotePlayerName = message->playerName.empty() ? "Host" : message->playerName.substr(0, 24);
    sessionScope = { message->sessionEpoch, message->worldGeneration };
    resumeSessionEpoch = message->sessionEpoch;
    resumeParticipantId = message->participantId;
    resumeTokenHigh = message->resumeTokenHigh;
    resumeTokenLow = message->resumeTokenLow;
    negotiatedCapabilities = message->capabilities;
    transport.ConfigureRealtime(sessionScope, playerId, resumeTokenHigh, resumeTokenLow);
    handshakeComplete = true;
    phase = IsSaveLoaded() ? ConnectionPhase::Handshaking : ConnectionPhase::Ready;
    SendSnapshotRequest();
}

void Manager::HandleClockSnapshot(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }

    const auto message = DecodeClockSnapshot(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope)) {
        return;
    }
    gSaveContext.dayTime = message->dayTime;
    gSaveContext.skyboxTime = message->skyboxTime;
    gSaveContext.nightFlag = message->night;
    gTimeSpeed = message->timeSpeed;
}

void Manager::HandlePlayerSnapshot(const Packet& packet) {
    if (!handshakeComplete) {
        return;
    }
    auto message = DecodePlayerSnapshot(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) ||
        (message->linkAge != LINK_AGE_ADULT && message->linkAge != LINK_AGE_CHILD) ||
        message->modelGroup >= PLAYER_MODELGROUP_MAX || message->currentMask >= PLAYER_MASK_MAX) {
        SPDLOG_WARN("[HyruleCoop] Ignoring invalid player snapshot");
        return;
    }

    if (message->meleeWeaponState > 0) {
        lastRemoteMeleeTick = message->tick;
        lastRemoteMeleeScene = message->scene;
        if (automatedTestEnabled && !automatedTestClient && !automatedTestRemoteSwingObserved) {
            if (automatedTestStage == TestAttacking) {
                automatedTestRemoteSwingObserved = true;
                ReportAutomatedTest("deku-baba-remote-swing-visible",
                                    "guest melee state reached the host player stream");
            } else if (automatedTestStage == TestBossCombat) {
                automatedTestRemoteSwingObserved = true;
                ReportAutomatedTest("gohma-remote-swing-visible",
                                    "guest melee state reached the host player stream");
            }
        }
    }

    const bool needsRespawn = !remotePlayerSnapshot.has_value() ||
                              remotePlayerSnapshot->linkAge != message->linkAge;
    if (remotePlayerPresentation.has_value()) {
        ApplyPresentation(*message, *remotePlayerPresentation);
    }
    remotePlayerSnapshot = message;
    remotePlayerInterpolator.Push(*message, SDL_GetTicks64());
    if (needsRespawn) {
        DestroyRemotePlayer();
        RefreshRemotePlayer();
    }
}

void Manager::HandlePlayerPresentation(const Packet& packet) {
    if (!handshakeComplete) {
        return;
    }
    const auto message = DecodePlayerPresentation(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->currentMask >= PLAYER_MASK_MAX ||
        message->modelGroup >= PLAYER_MODELGROUP_MAX ||
        (remotePlayerPresentation.has_value() && message->revision <= remotePlayerPresentation->revision)) {
        SPDLOG_WARN("[HyruleCoop] Ignoring invalid or stale player presentation");
        return;
    }

    remotePlayerPresentation = message;
    if (remotePlayerSnapshot.has_value()) {
        ApplyPresentation(*remotePlayerSnapshot, *message);
    }
}

void Manager::HandleSnapshotRequest(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeSnapshotRequest(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->scene < 0 ||
        message->scene >= SCENE_ID_MAX) {
        return;
    }
    SendClockSnapshot();
    SendSceneFlagsSnapshot(message->scene);
    SendProgressionSnapshot();
    for (const auto& [entityId, snapshot] : actorSnapshots) {
        if (snapshot.scene == message->scene) {
            transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(snapshot), entityId);
        }
    }
}

void Manager::HandleSceneFlagIntent(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeSceneFlagIntent(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->participantId != 2 ||
        !IsValidDurableSceneFlag(message->scene, message->flagType, message->flag)) {
        return;
    }

    const RequestKey request{ message->scope, message->participantId, message->requestId };
    const RequestLookup lookup = requestLedger.Lookup(request);
    if (lookup == RequestLookup::Replay) {
        SendSceneFlagsSnapshot(message->scene);
        return;
    }
    if (lookup != RequestLookup::New) {
        return;
    }

    applyingAuthoritativeState = true;
    if (message->set) {
        GameInteractor::RawAction::SetSceneFlag(message->scene, message->flagType, message->flag);
    } else {
        GameInteractor::RawAction::UnsetSceneFlag(message->scene, message->flagType, message->flag);
    }
    applyingAuthoritativeState = false;
    const uint64_t revision = sceneRevisions.Advance(SceneStreamId(message->scene));
    requestLedger.Record(request, { true, revision, revision });
    SendSceneFlagsSnapshot(message->scene);
}

void Manager::HandleSceneFlagsSnapshot(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeSceneFlagsSnapshot(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->scene < 0 ||
        message->scene >= SCENE_ID_MAX ||
        appliedSceneRevisions.Observe(SceneStreamId(message->scene), message->revision) == RevisionDecision::Stale) {
        return;
    }

    SavedSceneFlags& flags = gSaveContext.sceneFlags[message->scene];
    flags.chest = message->chest;
    flags.swch = message->switches;
    flags.clear = message->clear;
    flags.collect = message->collectible;
    if (gPlayState->sceneNum == message->scene) {
        gPlayState->actorCtx.flags.chest = message->chest;
        gPlayState->actorCtx.flags.swch = message->switches;
        gPlayState->actorCtx.flags.clear = message->clear;
        gPlayState->actorCtx.flags.collect = message->collectible;
    }
}

void Manager::HandleActorSnapshot(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeActorSnapshot(packet.payload);
    if (!message.has_value() || message->entityId == 0 ||
        (message->actorId != ACTOR_EN_DEKUBABA && message->actorId != ACTOR_BOSS_GOMA) ||
        !IsCurrentScope(message->scope) || message->scene < 0 || message->scene >= SCENE_ID_MAX) {
        return;
    }
    if (message->acknowledgedRequestId != 0) {
        const auto pending = pendingGuestAttackRequests.find(message->entityId);
        if (pending != pendingGuestAttackRequests.end() && pending->second == message->acknowledgedRequestId) {
            ClearPendingGuestAttack(message->entityId);
        }
    }
    const auto existing = actorSnapshots.find(message->entityId);
    if (existing != actorSnapshots.end() && existing->second.hostTick > message->hostTick) {
        return;
    }
    actorSnapshots[message->entityId] = *message;
    if (message->actorId == ACTOR_EN_DEKUBABA) {
        const auto local = localDekuBabas.find(message->entityId);
        if (local == localDekuBabas.end()) {
            return;
        }
        if (!message->alive) {
            Actor_Kill(static_cast<Actor*>(local->second));
            ClearPendingGuestAttack(message->entityId);
            lastGuestAttackTick.erase(message->entityId);
            localDekuBabas.erase(local);
            return;
        }
        ApplyDekuBabaSnapshot(local->second, *message);
    } else {
        const auto local = localGohmas.find(message->entityId);
        if (local == localGohmas.end()) {
            return;
        }
        if (!message->alive) {
            Actor_Kill(static_cast<Actor*>(local->second));
            ClearPendingGuestAttack(message->entityId);
            lastGuestAttackTick.erase(message->entityId);
            localGohmas.erase(local);
            return;
        }
        ApplyGohmaSnapshot(local->second, *message);
    }
}

void Manager::HandleBarrierSnapshot(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeBarrierSnapshot(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->state.scope) ||
        !barrierCoordinator.Reconcile(message->state)) {
        return;
    }
    if (message->state.phase == BarrierPhase::Aborted) {
        protocolError = "The host aborted shared-world preparation";
        phase = ConnectionPhase::Failed;
        return;
    }
    if (message->state.phase == BarrierPhase::Complete) {
        phase = ConnectionPhase::Ready;
        return;
    }
    if (message->state.phase != BarrierPhase::WaitingForParticipants) {
        return;
    }
    const Player* player = GET_PLAYER(gPlayState);
    if (gPlayState->sceneNum == message->state.targetScene &&
        (message->state.targetRoom < 0 || player->actor.room == message->state.targetRoom)) {
        SendBarrierReady();
        return;
    }
    if (message->state.targetEntrance >= 0) {
        GameInteractor::RawAction::TeleportPlayer(message->state.targetEntrance);
    }
}

void Manager::HandleBarrierReady(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete) {
        return;
    }
    const auto message = DecodeBarrierReady(packet.payload);
    const BarrierState& state = barrierCoordinator.GetState();
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->participantId != 2 ||
        message->operationEpoch != state.operationEpoch || message->currentScene != state.targetScene ||
        (state.targetRoom >= 0 && message->currentRoom != state.targetRoom) ||
        !barrierCoordinator.MarkReady(message->participantId)) {
        return;
    }
    CompleteBarrierIfReady();
}

void Manager::HandleAttackIntent(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeAttackIntent(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->participantId != 2 ||
        message->requestId == 0 || message->entityId == 0 ||
        (message->attackKind != 1 && message->attackKind != 2)) {
        return;
    }
    const RequestKey request{ message->scope, message->participantId, message->requestId };
    RequestOutcome prior;
    const RequestLookup lookup = requestLedger.Lookup(request, &prior);
    if (lookup == RequestLookup::Replay) {
        const auto snapshot = actorSnapshots.find(message->entityId);
        if (snapshot != actorSnapshots.end()) {
            ActorSnapshotMessage response = snapshot->second;
            response.acknowledgedRequestId = message->requestId;
            transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(response),
                                               message->entityId);
        }
        return;
    }
    if (lookup != RequestLookup::New) {
        return;
    }

    bool accepted = false;
    const auto state = actorSnapshots.find(message->entityId);
    const auto baba = localDekuBabas.find(message->entityId);
    const auto gohma = localGohmas.find(message->entityId);
    const int64_t attackStateDelta = static_cast<int64_t>(message->playerTick) -
                                     static_cast<int64_t>(remotePlayerSnapshot.has_value()
                                                              ? remotePlayerSnapshot->tick
                                                              : message->playerTick);
    const bool hasContemporaryPlayerState =
        attackStateDelta >= -kGuestAttackStateWindowFrames && attackStateDelta <= kGuestAttackStateWindowFrames;
    if (state != actorSnapshots.end() && state->second.alive &&
        remotePlayerSnapshot.has_value() && remotePlayerSnapshot->scene == message->scene &&
        hasContemporaryPlayerState &&
        state->second.scene == message->scene &&
        DistanceSquared(remotePlayerSnapshot->position, state->second.position) <= 350.0f * 350.0f) {
        if (message->attackKind == 1 && baba != localDekuBabas.end()) {
            const bool alive = DamageDekuBaba(baba->second, 1);
            SendDekuBabaSnapshot(baba->second, alive);
            accepted = true;
        } else if (message->attackKind == 2 && gohma != localGohmas.end() &&
                   CanDamageGohma(gohma->second)) {
            accepted = DamageGohma(gohma->second, gPlayState, 1);
            SendGohmaSnapshot(gohma->second, true);
            if (accepted && automatedTestEnabled) {
                ++automatedTestAcceptedBossHits;
                const auto* boss = static_cast<Actor*>(gohma->second);
                ReportAutomatedTest("gohma-host-damage-accepted",
                                    "hit=" + std::to_string(automatedTestAcceptedBossHits) +
                                        " health=" + std::to_string(boss->colChkInfo.health));
            }
        }
    }
    requestLedger.Record(request, { accepted, message->entityId, frameCounter });
    const auto authoritativeState = actorSnapshots.find(message->entityId);
    if (authoritativeState != actorSnapshots.end()) {
        ActorSnapshotMessage response = authoritativeState->second;
        response.acknowledgedRequestId = message->requestId;
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(response),
                                           message->entityId);
    }
}

void Manager::HandleCollectibleIntent(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeCollectibleIntent(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->participantId != 2 ||
        !IsValidDurableSceneFlag(message->scene, message->flagType, message->flag) ||
        message->flagType != FLAG_SCENE_COLLECTIBLE ||
        message->locationId != CollectibleLocationId(message->scene, message->flagType, message->flag)) {
        return;
    }
    const RequestKey request{ message->scope, message->participantId, message->requestId };
    const RequestLookup lookup = requestLedger.Lookup(request);
    if (lookup == RequestLookup::Replay) {
        SendProgressionSnapshot();
        SendSceneFlagsSnapshot(message->scene);
        return;
    }
    if (lookup != RequestLookup::New) {
        return;
    }

    if (!collectedLocations.contains(message->locationId)) {
        applyingAuthoritativeState = true;
        GameInteractor::RawAction::SetSceneFlag(message->scene, message->flagType, message->flag);
        applyingAuthoritativeState = false;
        collectedLocations[message->locationId] = { message->locationId, message->scene, message->flagType,
                                                    message->flag };
        ++progressionRevision;
        sceneRevisions.Advance(SceneStreamId(message->scene));
    }
    requestLedger.Record(request, { true, message->locationId, progressionRevision });
    SendProgressionSnapshot();
    SendSceneFlagsSnapshot(message->scene);
}

void Manager::HandleProgressionSnapshot(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeProgressionSnapshot(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) ||
        message->revision < lastAppliedProgressionRevision) {
        return;
    }
    applyingAuthoritativeState = true;
    for (const CollectedLocation& location : message->locations) {
        if (location.locationId != CollectibleLocationId(location.scene, location.flagType, location.flag) ||
            !IsValidDurableSceneFlag(location.scene, location.flagType, location.flag) ||
            location.flagType != FLAG_SCENE_COLLECTIBLE) {
            continue;
        }
        GameInteractor::RawAction::SetSceneFlag(location.scene, location.flagType, location.flag);
        collectedLocations[location.locationId] = location;
    }
    ApplyCanonicalProgression(message->shared);
    applyingAuthoritativeState = false;
    lastAppliedProgressionRevision = message->revision;
}

void Manager::HandleProgressionIntent(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeProgressionIntent(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->participantId != 2 ||
        message->requestId == 0) {
        return;
    }
    const RequestKey request{ message->scope, message->participantId, message->requestId };
    const RequestLookup lookup = requestLedger.Lookup(request);
    if (lookup == RequestLookup::Replay) {
        SendProgressionSnapshot();
        return;
    }
    if (lookup != RequestLookup::New) {
        return;
    }

    bool accepted = false;
    applyingAuthoritativeState = true;
    if (message->kind == ProgressionIntentKind::ItemReceived && message->modIndex == MOD_NONE) {
        const GetItemID getItemId = RetrieveGetItemIDFromItemID(static_cast<ItemID>(message->itemId));
        if (getItemId != GI_MAX) {
            const GetItemEntry entry = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, getItemId);
            if (entry.itemId == message->itemId &&
                IsSharedProgressionItem(entry.itemId, entry.modIndex, entry.getItemCategory)) {
                Item_Give(gPlayState, static_cast<uint8_t>(entry.itemId));
                accepted = true;
            }
        }
    } else if (message->kind == ProgressionIntentKind::DungeonKeyUsed &&
               message->mapIndex < std::size(gSaveContext.inventory.dungeonKeys)) {
        int8_t& canonicalKeys = gSaveContext.inventory.dungeonKeys[message->mapIndex];
        if (canonicalKeys > 0 && message->remainingDungeonKeys == canonicalKeys - 1) {
            canonicalKeys = message->remainingDungeonKeys;
            accepted = true;
        }
    } else if (message->kind == ProgressionIntentKind::GlobalFlagChanged &&
               IsValidDurableGlobalFlag(message->flagType, message->flag)) {
        const bool current = GameInteractor::RawAction::CheckFlag(message->flagType, message->flag);
        if (message->set != current) {
            if (message->set) {
                GameInteractor::RawAction::SetFlag(message->flagType, message->flag);
            } else {
                GameInteractor::RawAction::UnsetFlag(message->flagType, message->flag);
            }
        }
        accepted = true;
    }
    applyingAuthoritativeState = false;
    if (accepted) {
        CaptureCanonicalProgression();
        ++progressionRevision;
    }
    const uint64_t resultId = message->kind == ProgressionIntentKind::GlobalFlagChanged
                                  ? (static_cast<uint64_t>(static_cast<uint16_t>(message->flagType)) << 16) |
                                        static_cast<uint16_t>(message->flag)
                                  : message->itemId;
    requestLedger.Record(request, { accepted, resultId, progressionRevision });
    SendProgressionSnapshot();
}

void Manager::SendHello() {
    HelloMessage message;
    message.gameId = GameId::OcarinaOfTime;
    message.buildId = CurrentBuildId();
    message.playerName = playerName;
    message.requestedSessionEpoch = resumeSessionEpoch;
    message.requestedParticipantId = resumeParticipantId;
    message.resumeTokenHigh = resumeTokenHigh;
    message.resumeTokenLow = resumeTokenLow;
    message.capabilities = SupportedCapabilities();
    transport.Send(MessageType::Hello, EncodeHello(message));
}

void Manager::SendHelloAck(bool accepted, const std::string& reason) {
    HelloAckMessage message;
    message.accepted = accepted;
    message.playerName = accepted ? playerName.substr(0, 24) : "";
    message.participantId = accepted ? 2 : 0;
    message.sessionEpoch = sessionScope.sessionEpoch;
    message.worldGeneration = sessionScope.worldGeneration;
    message.resumeTokenHigh = accepted ? guestTokenHigh : 0;
    message.resumeTokenLow = accepted ? guestTokenLow : 0;
    message.capabilities = accepted ? negotiatedCapabilities : CapabilityList{};
    message.reason = reason;
    transport.Send(MessageType::HelloAck, EncodeHelloAck(message));
}

void Manager::SendClockSnapshot() {
    ClockSnapshotMessage message{ sessionScope, frameCounter, gSaveContext.dayTime, gSaveContext.skyboxTime,
                                  gTimeSpeed, gSaveContext.nightFlag != 0 };
    transport.Send(MessageType::ClockSnapshot, EncodeClockSnapshot(message));
}

void Manager::SendPlayerSnapshot() {
    if (!IsSaveLoaded()) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    PlayerSnapshotMessage message;
    message.scope = sessionScope;
    message.tick = frameCounter;
    message.scene = gPlayState->sceneNum;
    message.room = player->actor.room;
    message.entrance = gSaveContext.entranceIndex;
    message.linkAge = gSaveContext.linkAge;
    message.position[0] = player->actor.world.pos.x;
    message.position[1] = player->actor.world.pos.y;
    message.position[2] = player->actor.world.pos.z;
    message.rotation[0] = player->actor.shape.rot.x;
    message.rotation[1] = player->actor.shape.rot.y;
    message.rotation[2] = player->actor.shape.rot.z;
    for (size_t i = 0; i < 24; ++i) {
        message.joints[i * 3] = player->skelAnime.jointTable[i].x;
        message.joints[i * 3 + 1] = player->skelAnime.jointTable[i].y;
        message.joints[i * 3 + 2] = player->skelAnime.jointTable[i].z;
    }
    message.previousTranslation[0] = player->skelAnime.prevTransl.x;
    message.previousTranslation[1] = player->skelAnime.prevTransl.y;
    message.previousTranslation[2] = player->skelAnime.prevTransl.z;
    message.movementFlags = player->skelAnime.movementFlags;
    message.upperLimbRotation[0] = player->upperLimbRot.x;
    message.upperLimbRotation[1] = player->upperLimbRot.y;
    message.upperLimbRotation[2] = player->upperLimbRot.z;
    message.boots = player->currentBoots;
    message.shield = player->currentShield;
    message.tunic = player->currentTunic;
    message.currentMask = player->currentMask;
    message.stateFlags1 = player->stateFlags1;
    message.stateFlags2 = player->stateFlags2 & ~PLAYER_STATE2_DISABLE_DRAW;
    message.buttonItem = gSaveContext.equips.buttonItems[0];
    message.itemAction = player->itemAction;
    message.heldItemAction = player->heldItemAction;
    message.modelGroup = player->modelGroup;
    message.invincibilityTimer = player->invincibilityTimer;
    message.modelState = player->unk_862;
    message.modelBlend = player->unk_85C;
    message.actionVariable = player->av1.actionVar1;
    message.linearVelocity = player->linearVelocity;
    message.focusActorId = player->focusActor == nullptr ? -1 : player->focusActor->id;
    message.meleeWeaponState = player->meleeWeaponState;
    message.meleeWeaponAnimation = player->meleeWeaponAnimation;
    // Position snapshots may be coalesced, but a later idle frame must not replace a brief sword swing before it
    // reaches the peer. Combat-active frames use their tick as a one-shot stream while normal movement stays on 0.
    const uint64_t streamId = message.meleeWeaponState > 0 ? message.tick : 0;
    transport.Send(MessageType::PlayerSnapshot, EncodePlayerSnapshot(message), streamId);
}

void Manager::SendPlayerPresentation() {
    if (!handshakeComplete || !IsSaveLoaded()) {
        return;
    }

    const Player* player = GET_PLAYER(gPlayState);
    PlayerPresentationMessage message;
    message.scope = sessionScope;
    message.boots = player->currentBoots;
    message.shield = player->currentShield;
    message.tunic = player->currentTunic;
    message.currentMask = player->currentMask;
    message.buttonItem = gSaveContext.equips.buttonItems[0];
    message.itemAction = player->itemAction;
    message.heldItemAction = player->heldItemAction;
    message.modelGroup = player->modelGroup;
    if (lastSentPlayerPresentation.has_value() && SamePresentation(message, *lastSentPlayerPresentation)) {
        return;
    }

    message.revision = nextPlayerPresentationRevision++;
    if (transport.Send(MessageType::PlayerPresentation, EncodePlayerPresentation(message))) {
        lastSentPlayerPresentation = message;
    }
}

void Manager::SendSnapshotRequest() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded()) {
        return;
    }
    const Player* player = GET_PLAYER(gPlayState);
    transport.Send(MessageType::SnapshotRequest,
                   EncodeSnapshotRequest({ sessionScope, gPlayState->sceneNum, player->actor.room }));
}

void Manager::SendSceneFlagIntent(int16_t scene, int16_t flagType, int16_t flag, bool set) {
    if (transport.GetRole() != SessionRole::Client) {
        return;
    }
    transport.Send(MessageType::SceneFlagIntent,
                   EncodeSceneFlagIntent({ sessionScope, playerId, nextRequestId++, scene, flagType, flag, set }));
}

void Manager::SendSceneFlagsSnapshot(int16_t scene) {
    if (transport.GetRole() != SessionRole::Host || scene < 0 || scene >= SCENE_ID_MAX) {
        return;
    }
    SavedSceneFlags& flags = gSaveContext.sceneFlags[scene];
    if (gPlayState != nullptr && gPlayState->sceneNum == scene) {
        flags.chest = gPlayState->actorCtx.flags.chest;
        flags.swch = gPlayState->actorCtx.flags.swch;
        flags.clear = gPlayState->actorCtx.flags.clear;
        flags.collect = gPlayState->actorCtx.flags.collect;
    }
    const SceneFlagsSnapshotMessage message{ sessionScope, sceneRevisions.Current(SceneStreamId(scene)), scene,
                                             flags.chest, flags.swch, flags.clear, flags.collect };
    transport.Send(MessageType::SceneFlagsSnapshot, EncodeSceneFlagsSnapshot(message), SceneStreamId(scene));
}

void Manager::SendBarrierSnapshot() {
    const BarrierState& state = barrierCoordinator.GetState();
    if (!handshakeComplete || state.operationEpoch == 0 || !IsCurrentScope(state.scope)) {
        return;
    }
    transport.Send(MessageType::BarrierSnapshot, EncodeBarrierSnapshot({ state }));
}

void Manager::SendBarrierReady() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded()) {
        return;
    }
    const BarrierState& state = barrierCoordinator.GetState();
    const Player* player = GET_PLAYER(gPlayState);
    if (state.phase != BarrierPhase::WaitingForParticipants || !IsCurrentScope(state.scope) ||
        gPlayState->sceneNum != state.targetScene ||
        (state.targetRoom >= 0 && player->actor.room != state.targetRoom)) {
        return;
    }
    transport.Send(MessageType::BarrierReady,
                   EncodeBarrierReady({ sessionScope, state.operationEpoch, playerId, gPlayState->sceneNum,
                                        player->actor.room }));
}

void Manager::SendAttackIntent(uint64_t entityId, int16_t scene, uint8_t attackKind) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || entityId == 0) {
        return;
    }
    AttackIntentMessage message;
    message.scope = sessionScope;
    message.participantId = playerId;
    message.requestId = nextRequestId++;
    message.entityId = entityId;
    message.playerTick = frameCounter;
    message.scene = scene;
    message.attackKind = attackKind;
    if (transport.SendRepeatedRealtime(MessageType::AttackIntent, EncodeAttackIntent(message), message.entityId)) {
        pendingGuestAttackRequests[entityId] = message.requestId;
    } else {
        ClearPendingGuestAttack(entityId);
    }
}

void Manager::ClearPendingGuestAttack(uint64_t entityId) {
    transport.CancelRepeatedRealtime(MessageType::AttackIntent, entityId);
    pendingGuestAttackRequests.erase(entityId);
    pendingGuestAttacks.erase(entityId);
}

void Manager::SendCollectibleIntent(int16_t scene, int16_t flagType, int16_t flag) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client) {
        return;
    }
    CollectibleIntentMessage message;
    message.scope = sessionScope;
    message.participantId = playerId;
    message.requestId = nextRequestId++;
    message.locationId = CollectibleLocationId(scene, flagType, flag);
    message.scene = scene;
    message.flagType = flagType;
    message.flag = flag;
    transport.Send(MessageType::CollectibleIntent, EncodeCollectibleIntent(message));
}

void Manager::SendProgressionSnapshot() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Host) {
        return;
    }
    ProgressionSnapshotMessage message;
    message.scope = sessionScope;
    message.revision = progressionRevision;
    CaptureCanonicalProgression();
    message.shared = canonicalProgression;
    message.locations.reserve(collectedLocations.size());
    for (const auto& [locationId, location] : collectedLocations) {
        message.locations.push_back(location);
    }
    std::sort(message.locations.begin(), message.locations.end(),
              [](const CollectedLocation& first, const CollectedLocation& second) {
                  return first.locationId < second.locationId;
              });
    transport.Send(MessageType::ProgressionSnapshot, EncodeProgressionSnapshot(message), 1);
}

void Manager::SendProgressionItemIntent(uint16_t itemId, uint16_t modIndex, uint16_t mapIndex) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client) {
        return;
    }
    ProgressionIntentMessage message;
    message.scope = sessionScope;
    message.participantId = playerId;
    message.requestId = nextRequestId++;
    message.kind = ProgressionIntentKind::ItemReceived;
    message.itemId = itemId;
    message.modIndex = modIndex;
    message.mapIndex = mapIndex;
    transport.Send(MessageType::ProgressionIntent, EncodeProgressionIntent(message));
}

void Manager::SendDungeonKeyIntent(uint16_t mapIndex) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client ||
        mapIndex >= std::size(gSaveContext.inventory.dungeonKeys)) {
        return;
    }
    ProgressionIntentMessage message;
    message.scope = sessionScope;
    message.participantId = playerId;
    message.requestId = nextRequestId++;
    message.kind = ProgressionIntentKind::DungeonKeyUsed;
    message.mapIndex = mapIndex;
    message.remainingDungeonKeys = gSaveContext.inventory.dungeonKeys[mapIndex];
    transport.Send(MessageType::ProgressionIntent, EncodeProgressionIntent(message));
}

void Manager::SendGlobalFlagIntent(int16_t flagType, int16_t flag, bool set) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client ||
        !IsValidDurableGlobalFlag(flagType, flag)) {
        return;
    }
    ProgressionIntentMessage message;
    message.scope = sessionScope;
    message.participantId = playerId;
    message.requestId = nextRequestId++;
    message.kind = ProgressionIntentKind::GlobalFlagChanged;
    message.flagType = flagType;
    message.flag = flag;
    message.set = set;
    transport.Send(MessageType::ProgressionIntent, EncodeProgressionIntent(message));
}

void Manager::SendDekuBabaSnapshot(void* actor, bool alive) {
    if (transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }
    ActorSnapshotMessage message =
        CaptureDekuBabaSnapshot(actor, gPlayState->sceneNum, frameCounter, sessionScope, alive);
    const auto previous = actorSnapshots.find(message.entityId);
    const bool importantTransition = previous == actorSnapshots.end() ||
                                     previous->second.alive != message.alive ||
                                     previous->second.health != message.health;
    actorSnapshots[message.entityId] = message;
    localDekuBabas[message.entityId] = actor;
    if (importantTransition) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message),
                                           message.entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    }
}

void Manager::UpdateDekuBaba(void* actor) {
    if (transport.GetRole() != SessionRole::Host || frameCounter % 2 != 0) {
        return;
    }
    SendDekuBabaSnapshot(actor, true);
}

void Manager::ApplyDekuBabaAuthority(void* actor, bool* shouldUpdate) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded()) {
        return;
    }
    const uint64_t entityId = GetDekuBabaEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    localDekuBabas[entityId] = actor;
    if (ConsumeDekuBabaHit(actor)) {
        const auto lastAttack = lastGuestAttackTick.find(entityId);
        const bool newSwing = lastAttack == lastGuestAttackTick.end() ||
                              frameCounter - lastAttack->second >= kGuestAttackCooldownFrames;
        if (newSwing && !pendingGuestAttacks.contains(entityId)) {
            pendingGuestAttacks.insert(entityId);
            lastGuestAttackTick[entityId] = frameCounter;
            if (automatedTestEnabled) {
                ++automatedTestPhysicalHits;
                ReportAutomatedTest("deku-baba-physical-collision",
                                    "hit=" + std::to_string(automatedTestPhysicalHits));
            }
            SendAttackIntent(entityId, gPlayState->sceneNum, 1);
        }
    }
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot != actorSnapshots.end()) {
        if (!snapshot->second.alive) {
            Actor_Kill(static_cast<Actor*>(actor));
            ClearPendingGuestAttack(entityId);
            lastGuestAttackTick.erase(entityId);
            localDekuBabas.erase(entityId);
        } else {
            ApplyDekuBabaSnapshot(actor, snapshot->second);
            RegisterDekuBabaGuestCollision(actor, gPlayState);
        }
    } else {
        RegisterDekuBabaGuestCollision(actor, gPlayState);
    }
    *shouldUpdate = false;
}

void Manager::ForgetDekuBaba(void* actor) {
    for (auto iterator = localDekuBabas.begin(); iterator != localDekuBabas.end();) {
        if (iterator->second == actor) {
            ClearPendingGuestAttack(iterator->first);
            lastGuestAttackTick.erase(iterator->first);
            iterator = localDekuBabas.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::SendGohmaSnapshot(void* actor, bool alive) {
    if (transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }
    ActorSnapshotMessage message = CaptureGohmaSnapshot(actor, gPlayState->sceneNum, frameCounter, sessionScope, alive);
    const auto previous = actorSnapshots.find(message.entityId);
    const bool importantTransition = previous == actorSnapshots.end() ||
                                     previous->second.alive != message.alive ||
                                     previous->second.health != message.health;
    actorSnapshots[message.entityId] = message;
    if (alive) {
        localGohmas[message.entityId] = actor;
    }
    if (importantTransition) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message),
                                           message.entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    }
}

void Manager::UpdateGohma(void* actor) {
    if (transport.GetRole() != SessionRole::Host) {
        return;
    }
    Actor* boss = static_cast<Actor*>(actor);
    if (boss->colChkInfo.health == 0) {
        Player* player = GET_PLAYER(gPlayState);
        if (player->focusActor == boss) {
            Player_ClearZTargeting(player);
        }
    }
    if (frameCounter % 2 != 0) {
        return;
    }
    SendGohmaSnapshot(actor, true);
}

void Manager::ApplyGohmaAuthority(void* actor, bool* shouldUpdate) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded()) {
        return;
    }
    const uint64_t entityId = GetGohmaEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    localGohmas[entityId] = actor;
    if (ConsumeGohmaHit(actor)) {
        if (automatedTestEnabled && automatedTestStage == TestBossCombat &&
            automatedTestCombatPhase == CombatSecondSwing) {
            ReportAutomatedTest("gohma-second-collider-contact",
                                "guest Gohma collider observed Link's follow-up sword contact");
        }
        const auto lastAttack = lastGuestAttackTick.find(entityId);
        const bool newSwing = lastAttack == lastGuestAttackTick.end() ||
                              frameCounter - lastAttack->second >= kGuestAttackCooldownFrames;
        if (newSwing && !pendingGuestAttacks.contains(entityId)) {
            pendingGuestAttacks.insert(entityId);
            lastGuestAttackTick[entityId] = frameCounter;
            if (automatedTestEnabled) {
                ++automatedTestPhysicalHits;
                ReportAutomatedTest("gohma-physical-sword-collision",
                                    "hit=" + std::to_string(automatedTestPhysicalHits));
            }
            SendAttackIntent(entityId, gPlayState->sceneNum, 2);
        }
    }
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot != actorSnapshots.end()) {
        if (!snapshot->second.alive) {
            Actor_Kill(static_cast<Actor*>(actor));
            ClearPendingGuestAttack(entityId);
            lastGuestAttackTick.erase(entityId);
            localGohmas.erase(entityId);
        } else {
            ApplyGohmaSnapshot(actor, snapshot->second);
            RegisterGohmaGuestCollision(actor, gPlayState);
        }
    }
    *shouldUpdate = false;
}

void Manager::ForgetGohma(void* actor) {
    for (auto iterator = localGohmas.begin(); iterator != localGohmas.end();) {
        if (iterator->second == actor) {
            ClearPendingGuestAttack(iterator->first);
            lastGuestAttackTick.erase(iterator->first);
            iterator = localGohmas.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::CompleteGohma(void* actor) {
    if (transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }
    automatedTestBossCompleted = automatedTestEnabled;
    if (automatedTestEnabled) {
        automatedTestTargetActor = actor;
        automatedTestCombatPhase = CombatVerifyCleanup;
        automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
    }
    applyingAuthoritativeState = true;
    GameInteractor::RawAction::SetSceneFlag(SCENE_DEKU_TREE_BOSS, FLAG_SCENE_CLEAR,
                                             static_cast<Actor*>(actor)->room);
    applyingAuthoritativeState = false;
    SendGohmaSnapshot(actor, true);
    sceneRevisions.Advance(SceneStreamId(SCENE_DEKU_TREE_BOSS));
    SendSceneFlagsSnapshot(SCENE_DEKU_TREE_BOSS);
}

void Manager::InjectAutomatedTestInput(void* actorRef, bool*) {
    const bool clientGameplayInput = automatedTestClient &&
                                     (automatedTestStage == TestAttacking || automatedTestStage == TestBossCombat);
    const bool hostBossInput = !automatedTestClient && automatedTestStage == TestBossCombat;
    if (!automatedTestEnabled || (!clientGameplayInput && !hostBossInput) || !IsSaveLoaded() ||
        actorRef == nullptr || actorRef != GET_PLAYER(gPlayState)) {
        return;
    }

    uint32_t buttons = 0;
    int8_t stickX = 0;
    int8_t stickY = 0;
    if (hostBossInput) {
        if (!automatedTestBossCompleted) {
            buttons |= BTN_Z;
            if (automatedTestTargetActor != nullptr) {
                gPlayState->actorCtx.targetCtx.arrowPointedActor = static_cast<Actor*>(automatedTestTargetActor);
            }
            if (!automatedTestSwingObserved) {
                const Player* player = GET_PLAYER(gPlayState);
                const Actor* target = static_cast<Actor*>(automatedTestTargetActor);
                const float deltaX = target == nullptr ? 0.0f : player->actor.world.pos.x - target->world.pos.x;
                const float deltaZ = target == nullptr ? 0.0f : player->actor.world.pos.z - target->world.pos.z;
                const float distance = target == nullptr ? -1.0f : std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
                ReportAutomatedTest("gohma-host-target-input",
                                    "cs=" + std::to_string(gPlayState->csCtx.state) +
                                        " action=" + std::to_string(player->csAction) +
                                        " state=" + std::to_string(player->stateFlags1) +
                                        " targetFlags=" + std::to_string(target == nullptr ? 0 : target->flags) +
                                        " distance=" + std::to_string(distance));
                automatedTestSwingObserved = true;
            }
        }
    } else if (automatedTestStage == TestAttacking) {
        if (automatedTestCombatPhase >= CombatAcquireTarget && automatedTestCombatPhase <= CombatAwaitDeath) {
            buttons |= BTN_Z;
        }
        if ((automatedTestCombatPhase == CombatFirstSwing || automatedTestCombatPhase == CombatSecondSwing) &&
            (automatedTestTick - automatedTestCombatPhaseTick) % 36 == 1) {
            buttons |= BTN_B;
        }
    } else if (automatedTestStage == TestBossCombat) {
        if (automatedTestCombatPhase == CombatMove) {
            stickY = 55;
        }
        if (automatedTestCombatPhase >= CombatAcquireTarget && automatedTestCombatPhase <= CombatAwaitDeath) {
            buttons |= BTN_Z;
        }
        if ((automatedTestCombatPhase == CombatFirstSwing || automatedTestCombatPhase == CombatSecondSwing) &&
            (automatedTestTick - automatedTestCombatPhaseTick) % 18 == 1) {
            buttons |= BTN_B;
            if (automatedTestCombatPhase == CombatSecondSwing) {
                ReportAutomatedTest("gohma-second-swing-input",
                                    "B press injected after first synchronized damage");
            }
        }
        if (automatedTestCombatPhase == CombatVerifyCleanup &&
            automatedTestTick - automatedTestCombatPhaseTick == 1) {
            buttons |= BTN_Z | BTN_B;
        }
    }

    if (automatedTestCombatPhase == CombatAcquireTarget && automatedTestTargetActor != nullptr) {
        // The automated warp repositions Link without moving the headless camera. Prime Navi's candidate so the
        // injected Z press still exercises Player_UpdateZTargeting instead of depending on camera catch-up.
        gPlayState->actorCtx.targetCtx.arrowPointedActor = static_cast<Actor*>(automatedTestTargetActor);
    }

    Input& input = gPlayState->state.input[0];
    input.prev.button = automatedTestInputButtons;
    input.prev.stick_x = automatedTestInputStickX;
    input.prev.stick_y = automatedTestInputStickY;
    input.cur.button = buttons;
    input.cur.stick_x = stickX;
    input.cur.stick_y = stickY;
    input.cur.right_stick_x = 0;
    input.cur.right_stick_y = 0;
    input.press.button = static_cast<CONTROLLERBUTTONS_T>(buttons & ~automatedTestInputButtons);
    input.rel.button = static_cast<CONTROLLERBUTTONS_T>(automatedTestInputButtons & ~buttons);
    input.press.stick_x = static_cast<int8_t>(stickX - automatedTestInputStickX);
    input.press.stick_y = static_cast<int8_t>(stickY - automatedTestInputStickY);
    input.rel.stick_x = stickX;
    input.rel.stick_y = stickY;
    input.rel.right_stick_x = 0;
    input.rel.right_stick_y = 0;
    automatedTestInputButtons = buttons;
    automatedTestInputStickX = stickX;
    automatedTestInputStickY = stickY;
}

void Manager::CaptureCanonicalProgression() {
    if (!IsSaveLoaded() || transport.GetRole() != SessionRole::Host) {
        return;
    }
    ReconcileSharedProgressionDerivedFlags(&gSaveContext);
    canonicalProgression = CaptureSharedProgression(&gSaveContext);
    canonicalProgressionCaptured = true;
}

void Manager::ApplyCanonicalProgression(const SharedProgressionState& state) {
    ApplySharedProgression(&gSaveContext, state);
    if (gPlayState != nullptr && Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED) &&
        Inventory_HasSpecificBottle(ITEM_LETTER_RUTO)) {
        Inventory_ReplaceItem(gPlayState, ITEM_LETTER_RUTO, ITEM_BOTTLE);
    }
    canonicalProgression = state;
    canonicalProgressionCaptured = true;
}

void Manager::ReconcileKingZora(void* actorRef) {
    if (!IsSaveLoaded() || actorRef == nullptr ||
        !Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED)) {
        return;
    }
    EnKz* kingZora = static_cast<EnKz*>(actorRef);
    if (kingZora->actionFunc != EnKz_PreMweepWait) {
        return;
    }
    if (EnKz_SetMovedPos(kingZora, gPlayState)) {
        kingZora->actor.prevPos = kingZora->actor.world.pos;
        kingZora->actor.speedXZ = 0.0f;
        kingZora->interactInfo.talkState = NPC_TALK_STATE_IDLE;
        kingZora->actionFunc = EnKz_Wait;
    }
}

void Manager::ReconcileZorasFountainBombableWall(void* actorRef) {
    if (!IsSaveLoaded() || actorRef == nullptr || gPlayState->sceneNum != SCENE_ZORAS_FOUNTAIN) {
        return;
    }
    BgSpot08Bakudankabe* wall = static_cast<BgSpot08Bakudankabe*>(actorRef);
    if (Flags_GetSwitch(gPlayState, wall->dyna.actor.params & 0x3F)) {
        wall->collider.base.acFlags |= AC_HIT;
    }
}

void Manager::RefreshRemotePlayer() {
    if (!IsSaveLoaded() || !handshakeComplete || !remotePlayerSnapshot.has_value() || remotePlayer != nullptr ||
        preparingRemotePlayer) {
        return;
    }

    preparingRemotePlayer = true;
    const PlayerSnapshotMessage& state = remotePlayerSnapshot.value();
    remotePlayer = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, state.position[0], state.position[1],
                               state.position[2], state.rotation[0], state.rotation[1], state.rotation[2], 0);
    preparingRemotePlayer = false;
}

void Manager::DestroyRemotePlayer() {
    if (remotePlayer == nullptr || gPlayState == nullptr) {
        remotePlayer = nullptr;
        return;
    }
    Actor* actor = static_cast<Actor*>(remotePlayer);
    if (actor->update == HyruleCoopRemotePlayer_Update) {
        Actor_Kill(actor);
    }
    remotePlayer = nullptr;
}

void Manager::BeginReconnectBarrier() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }
    BarrierState state;
    state.operationEpoch = nextOperationEpoch++;
    state.scope = sessionScope;
    state.kind = BarrierKind::ReconnectSnapshot;
    state.phase = BarrierPhase::Prepare;
    state.manifestHash = HashCapabilities(negotiatedCapabilities);
    state.targetScene = gPlayState->sceneNum;
    state.targetRoom = -1;
    state.targetEntrance = gSaveContext.entranceIndex;
    state.deadlineTick = static_cast<uint64_t>(frameCounter) + 600;
    state.participants = { 1, 2 };
    if (!barrierCoordinator.Begin(state) || !barrierCoordinator.WaitForParticipants() ||
        !barrierCoordinator.MarkReady(1)) {
        protocolError = "Could not prepare the coordinated world state";
        transport.DisconnectPeer();
        return;
    }
    SendBarrierSnapshot();
}

void Manager::BeginAutomatedBossBarrier() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }

    applyingAuthoritativeState = true;
    GameInteractor::RawAction::UnsetSceneFlag(SCENE_DEKU_TREE_BOSS, FLAG_SCENE_CLEAR, kGohmaRoom);
    applyingAuthoritativeState = false;
    sceneRevisions.Advance(SceneStreamId(SCENE_DEKU_TREE_BOSS));
    SendSceneFlagsSnapshot(SCENE_DEKU_TREE_BOSS);

    BarrierState state;
    state.operationEpoch = nextOperationEpoch++;
    state.scope = sessionScope;
    state.kind = BarrierKind::BossEncounter;
    state.phase = BarrierPhase::Prepare;
    state.manifestHash = HashCapabilities(negotiatedCapabilities);
    state.targetScene = SCENE_DEKU_TREE_BOSS;
    state.targetRoom = -1;
    state.targetEntrance = ENTR_DEKU_TREE_BOSS_ENTRANCE;
    state.deadlineTick = static_cast<uint64_t>(frameCounter) + 1200;
    state.participants = { 1, 2 };
    if (!barrierCoordinator.Begin(state) || !barrierCoordinator.WaitForParticipants()) {
        FailAutomatedTest("could not prepare the Gohma encounter barrier");
        return;
    }
    SendBarrierSnapshot();
    GameInteractor::RawAction::TeleportPlayer(ENTR_DEKU_TREE_BOSS_ENTRANCE);
}

void Manager::CompleteBarrierIfReady() {
    if (transport.GetRole() != SessionRole::Host || !barrierCoordinator.CanCommit()) {
        return;
    }
    if (!barrierCoordinator.Commit()) {
        return;
    }
    SendBarrierSnapshot();

    // Replay each canonical domain only after both players are in the target scene.
    SendClockSnapshot();
    SendPlayerPresentation();
    SendPlayerSnapshot();
    SendSceneFlagsSnapshot(barrierCoordinator.GetState().targetScene);
    SendProgressionSnapshot();
    for (const auto& [entityId, snapshot] : actorSnapshots) {
        if (snapshot.scene == barrierCoordinator.GetState().targetScene) {
            transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(snapshot), entityId);
        }
    }

    if (barrierCoordinator.Activate()) {
        SendBarrierSnapshot();
    }
    if (barrierCoordinator.Complete()) {
        SendBarrierSnapshot();
        phase = ConnectionPhase::Ready;
    }
}

void Manager::SetAutomatedTestStage(uint8_t stage, const std::string& event) {
    automatedTestStage = stage;
    automatedTestStageTick = automatedTestTick;
    ReportAutomatedTest(event);
}

void Manager::ReportAutomatedTest(const std::string& event, const std::string& detail) {
    if (!automatedTestEnabled) {
        return;
    }
    const char* role = automatedTestClient ? "client" : "host";
    SPDLOG_INFO("[HyruleCoopTest] role={} event={} detail={}", role, event, detail);
    if (automatedTestReportPath.empty()) {
        return;
    }
    std::ofstream report(automatedTestReportPath, std::ios::app);
    report << automatedTestTick << '\t' << role << '\t' << event << '\t' << detail << '\n';
}

void Manager::FailAutomatedTest(const std::string& reason) {
    if (automatedTestStage == TestFailed || automatedTestStage == TestComplete) {
        return;
    }
    automatedTestStage = TestFailed;
    ReportAutomatedTest("FAIL", reason);
}

void Manager::WarpAutomatedTestToForest() {
    if (!IsSaveLoaded()) {
        return;
    }
    automatedTestActorSpawned = false;
    GameInteractor::RawAction::TeleportPlayer(ENTR_KOKIRI_FOREST_0);
}

bool Manager::SpawnAutomatedTestDekuBaba() {
    if (!IsSaveLoaded() || gPlayState->sceneNum != SCENE_KOKIRI_FOREST) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    const float x = std::round(player->actor.world.pos.x / 10.0f) * 10.0f;
    const float y = std::round(player->actor.world.pos.y / 10.0f) * 10.0f;
    const float z = std::round(player->actor.world.pos.z / 10.0f) * 10.0f + 120.0f;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_DEKUBABA, x, y, z, 0, 0, 0, 0);
    if (actor == nullptr) {
        return false;
    }
    automatedTestActorSpawned = true;
    return true;
}

void Manager::UpdateAutomatedTest() {
    if (!automatedTestEnabled || automatedTestStage == TestComplete || automatedTestStage == TestFailed) {
        return;
    }
    if (automatedTestTick - automatedTestStageTick > 3600) {
        FailAutomatedTest("stage timeout " + std::to_string(automatedTestStage));
        return;
    }

    const uint64_t collectibleId =
        CollectibleLocationId(SCENE_KOKIRI_FOREST, FLAG_SCENE_COLLECTIBLE, kAutomatedTestCollectibleFlag);
    const uint32_t collectibleMask = 1u << kAutomatedTestCollectibleFlag;
    const uint32_t switchMask = 1u << kAutomatedTestSwitchFlag;

    switch (automatedTestStage) {
        case TestAwaitingSave:
            if (IsSaveLoaded()) {
                SetAutomatedTestStage(TestAwaitingReady, "save-loaded");
            }
            return;
        case TestAwaitingReady:
            if (phase != ConnectionPhase::Ready || !IsSaveLoaded()) {
                return;
            }
            ReportAutomatedTest("connection-ready", "initial barrier complete");
            WarpAutomatedTestToForest();
            SetAutomatedTestStage(TestAwaitingScene, "warp-requested");
            return;
        case TestAwaitingScene:
            if (!IsSaveLoaded() || gPlayState->sceneNum != SCENE_KOKIRI_FOREST ||
                automatedTestTick - automatedTestStageTick < 30) {
                return;
            }
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].collect &= ~collectibleMask;
            gPlayState->actorCtx.flags.collect &= ~collectibleMask;
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch &= ~switchMask;
            gPlayState->actorCtx.flags.swch &= ~switchMask;
            GameInteractor::RawAction::UnsetFlag(FLAG_EVENT_CHECK_INF, EVENTCHKINF_KING_ZORA_MOVED);
            gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_NONE;
            gSaveContext.inventory.items[SLOT_FARORES_WIND] = ITEM_NONE;
            gSaveContext.itemGetInf[ITEMGETINF_18_19_1A_INDEX] &= ~ITEMGETINF_18_MASK;
            gSaveContext.inventory.equipment &=
                ~OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI);
            gSaveContext.rupees = automatedTestClient ? 222 : 111;
            gSaveContext.inventory.ammo[SLOT_BOW] = automatedTestClient ? 23 : 7;
            gSaveContext.isMagicAcquired = true;
            gSaveContext.isDoubleMagicAcquired = false;
            gSaveContext.magicLevel = 1;
            gSaveContext.magicCapacity = MAGIC_NORMAL_METER;
            gSaveContext.magic = automatedTestClient ? kAutomatedTestClientMagic : kAutomatedTestHostMagic;
            gSaveContext.magicFillTarget = gSaveContext.magic;
            gSaveContext.magicTarget = gSaveContext.magic;
            gSaveContext.magicState = MAGIC_STATE_IDLE;
            gSaveContext.prevMagicState = MAGIC_STATE_IDLE;
            if (saveOverlayCaptured) {
                originalProgression.magicLevel = 1;
                originalProgression.isMagicAcquired = true;
                originalProgression.isDoubleMagicAcquired = false;
                originalProgression.eventChkInf[EVENTCHKINF_KING_ZORA_MOVED >> 4] &=
                    ~(1u << (EVENTCHKINF_KING_ZORA_MOVED & 0xF));
            }
            if (!automatedTestClient) {
                CaptureCanonicalProgression();
                ++progressionRevision;
                SendProgressionSnapshot();
            }
            if (!SpawnAutomatedTestDekuBaba()) {
                FailAutomatedTest("could not spawn deterministic Deku Baba");
                return;
            }
            SetAutomatedTestStage(TestAwaitingActors, "forest-loaded");
            return;
        case TestAwaitingActors: {
            if (!automatedTestActorSpawned || localDekuBabas.empty() || remotePlayer == nullptr ||
                !remotePlayerSnapshot.has_value() || remotePlayerSnapshot->scene != SCENE_KOKIRI_FOREST) {
                return;
            }
            if (automatedTestClient && actorSnapshots.empty()) {
                return;
            }
            ReportAutomatedTest("both-links-render-ready", "remote player actor exists in shared scene");
            ReportAutomatedTest("deku-baba-ready", "host-owned actor snapshot available");
            automatedTestCombatPhase = CombatSetup;
            automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
            automatedTestPhysicalHits = 0;
            automatedTestTargetObserved = false;
            automatedTestRemoteTargetObserved = false;
            automatedTestSwingObserved = false;
            automatedTestRemoteSwingObserved = false;
            automatedTestRemoteSwingRendered = false;
            SetAutomatedTestStage(TestAttacking, "combat-started");
            return;
        }
        case TestAttacking: {
            const auto dead = std::find_if(actorSnapshots.begin(), actorSnapshots.end(), [](const auto& entry) {
                return entry.second.scene == SCENE_KOKIRI_FOREST && !entry.second.alive;
            });
            if (dead != actorSnapshots.end()) {
                if (automatedTestClient) {
                    Player* player = GET_PLAYER(gPlayState);
                    if (localDekuBabas.contains(dead->first) || player->focusActor == automatedTestTargetActor) {
                        return;
                    }
                    if (automatedTestPhysicalHits == 0 || !automatedTestTargetObserved ||
                        !automatedTestSwingObserved) {
                        FailAutomatedTest("Deku Baba died without a verified target, real sword swing, and collision");
                        return;
                    }
                    gSaveContext.equips.buttonItems[0] = ITEM_NONE;
                    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_NONE);
                    gSaveContext.inventory.equipment &=
                        ~OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI);
                    Player_UseItem(gPlayState, player, ITEM_NONE);
                    ReportAutomatedTest("deku-baba-target-released", "dead actor cannot remain targeted");
                } else if (!automatedTestRemoteTargetObserved || !automatedTestRemoteSwingObserved ||
                           !automatedTestRemoteSwingRendered) {
                    return;
                }
                ReportAutomatedTest("deku-baba-dead-synchronized", std::to_string(dead->first));
                if (automatedTestClient) {
                    Flags_SetCollectible(gPlayState, kAutomatedTestCollectibleFlag);
                    ReportAutomatedTest("collectible-picked-up", std::to_string(kAutomatedTestCollectibleFlag));
                }
                SetAutomatedTestStage(TestAwaitingCollection, "collectible-test-started");
                return;
            }
            if (!automatedTestClient) {
                if (remotePlayerSnapshot.has_value() &&
                    remotePlayerSnapshot->focusActorId == ACTOR_EN_DEKUBABA &&
                    !automatedTestRemoteTargetObserved) {
                    automatedTestRemoteTargetObserved = true;
                    ReportAutomatedTest("deku-baba-remote-target-visible", "guest lock-on reached host snapshot");
                }
                if (remotePlayerSnapshot.has_value() && remotePlayerSnapshot->meleeWeaponState > 0 &&
                    !automatedTestRemoteSwingObserved) {
                    automatedTestRemoteSwingObserved = true;
                    ReportAutomatedTest("deku-baba-remote-swing-visible", "guest melee state reached host snapshot");
                }
                return;
            }
            const auto target = std::find_if(actorSnapshots.begin(), actorSnapshots.end(), [](const auto& entry) {
                return entry.second.scene == SCENE_KOKIRI_FOREST && entry.second.alive;
            });
            if (target == actorSnapshots.end()) {
                return;
            }
            Player* player = GET_PLAYER(gPlayState);
            const auto local = localDekuBabas.find(target->first);
            if (local == localDekuBabas.end()) {
                return;
            }
            if (automatedTestCombatPhase == CombatSetup) {
                automatedTestTargetEntityId = target->first;
                automatedTestTargetActor = local->second;
                automatedTestLastObservedHealth = target->second.health;
                EquipAutomatedTestSword(player);
                PositionAutomatedTestPlayer(player, static_cast<Actor*>(local->second), 55.0f);
                automatedTestCombatPhase = CombatAcquireTarget;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                ReportAutomatedTest("deku-baba-physical-setup", std::to_string(target->first));
                return;
            }
            if (automatedTestCombatPhase == CombatAcquireTarget) {
                if (player->focusActor != automatedTestTargetActor) {
                    return;
                }
                automatedTestTargetObserved = true;
                automatedTestCombatPhase = CombatFirstSwing;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                ReportAutomatedTest("deku-baba-target-acquired", "Z input selected the live actor");
                return;
            }
            if ((automatedTestCombatPhase == CombatFirstSwing ||
                 automatedTestCombatPhase == CombatSecondSwing) &&
                player->meleeWeaponState > 0 && !automatedTestSwingObserved) {
                automatedTestSwingObserved = true;
                ReportAutomatedTest("deku-baba-sword-state-entered",
                                    "Player_Update entered a melee weapon state from B input");
            }
            if ((automatedTestCombatPhase == CombatFirstSwing ||
                 automatedTestCombatPhase == CombatSecondSwing) &&
                automatedTestPhysicalHits > 0 && pendingGuestAttacks.contains(target->first)) {
                automatedTestCombatPhase = CombatAwaitFirstDamage;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                return;
            }
            if (automatedTestCombatPhase == CombatAwaitFirstDamage &&
                !pendingGuestAttacks.contains(target->first) && player->meleeWeaponState == 0) {
                automatedTestLastObservedHealth = target->second.health;
                automatedTestCombatPhase = CombatSecondSwing;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
            }
            return;
        }
        case TestAwaitingCollection: {
            const bool collected = collectedLocations.contains(collectibleId) &&
                                   (gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].collect & collectibleMask) != 0;
            if (!collected) {
                return;
            }
            ReportAutomatedTest("collectible-synchronized", std::to_string(collectibleId));
            SetAutomatedTestStage(TestAwaitingProgression, "shared-progression-test-started");
            return;
        }
        case TestAwaitingProgression: {
            const int16_t expectedRupees = automatedTestClient ? 222 : 111;
            const int8_t expectedArrows = automatedTestClient ? 23 : 7;
            const int8_t expectedMagic = automatedTestClient ? kAutomatedTestClientMagic : kAutomatedTestHostMagic;
            if (automatedTestClient && !automatedTestProgressionTriggered) {
                automatedTestProgressionTriggered = true;
                Item_Give(gPlayState, ITEM_HOOKSHOT);
                Item_Give(gPlayState, ITEM_SWORD_KOKIRI);
                Item_Give(gPlayState, ITEM_FARORES_WIND);
                ReportAutomatedTest("guest-progression-intents-sent", "Hookshot, Kokiri Sword, and Farore's Wind");
                return;
            }
            const bool hookshotShared = gSaveContext.inventory.items[SLOT_HOOKSHOT] == ITEM_HOOKSHOT;
            const bool faroresWindShared =
                gSaveContext.inventory.items[SLOT_FARORES_WIND] == ITEM_FARORES_WIND &&
                Flags_GetItemGetInf(ITEMGETINF_18);
            const bool swordShared =
                CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI) != 0;
            if (!hookshotShared || !swordShared || !faroresWindShared) {
                return;
            }
            if (gSaveContext.rupees != expectedRupees ||
                gSaveContext.inventory.ammo[SLOT_BOW] != expectedArrows || gSaveContext.magic != expectedMagic ||
                gSaveContext.magicLevel != 1 || gSaveContext.magicCapacity != MAGIC_NORMAL_METER ||
                !gSaveContext.isMagicAcquired || gSaveContext.isDoubleMagicAcquired) {
                FailAutomatedTest("shared progression overwrote a local resource or corrupted the magic meter");
                return;
            }
            ReportAutomatedTest("shared-progression-synchronized",
                                "Hookshot, Kokiri Sword, and Farore's Wind shared; rupees, arrows, and current magic remained local");
            SetAutomatedTestStage(TestAwaitingWorldState, "global-world-state-test-started");
            return;
        }
        case TestAwaitingWorldState: {
            if (!automatedTestWorldStateTriggered) {
                automatedTestWorldStateTriggered = true;
                if (automatedTestClient) {
                    Flags_SetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED);
                    applyingAuthoritativeState = true;
                    GameInteractor::RawAction::UnsetFlag(FLAG_EVENT_CHECK_INF, EVENTCHKINF_KING_ZORA_MOVED);
                    applyingAuthoritativeState = false;
                    ReportAutomatedTest("guest-world-state-intent-sent", "King Zora moved flag 0x33");
                } else {
                    Flags_SetSwitch(gPlayState, kAutomatedTestSwitchFlag);
                    ReportAutomatedTest("host-scene-switch-set", "live Kokiri Forest switch 0x1F");
                }
                return;
            }
            if (!Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED) ||
                (gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch & switchMask) == 0) {
                return;
            }
            ReportAutomatedTest("global-world-state-synchronized",
                                "King Zora moved flag and host live scene switch replayed across peers");
            if (!automatedTestClient) {
                BeginAutomatedBossBarrier();
            }
            SetAutomatedTestStage(TestAwaitingBossScene, "gohma-barrier-started");
            return;
        }
        case TestAwaitingBossScene:
            if (!IsSaveLoaded() || gPlayState->sceneNum != SCENE_DEKU_TREE_BOSS ||
                phase != ConnectionPhase::Ready) {
                return;
            }
            SetAutomatedTestStage(TestAwaitingBossReady, "gohma-arena-ready");
            return;
        case TestAwaitingBossReady: {
            if (localGohmas.empty() || remotePlayer == nullptr || !remotePlayerSnapshot.has_value() ||
                remotePlayerSnapshot->scene != SCENE_DEKU_TREE_BOSS) {
                return;
            }
            const int8_t expectedMagic = automatedTestClient ? kAutomatedTestClientMagic : kAutomatedTestHostMagic;
            if (gSaveContext.magic != expectedMagic || gSaveContext.magicLevel != 1 ||
                gSaveContext.magicCapacity != MAGIC_NORMAL_METER || !gSaveContext.isMagicAcquired ||
                gSaveContext.isDoubleMagicAcquired) {
                FailAutomatedTest("Gohma barrier corrupted the local magic meter");
                return;
            }
            ReportAutomatedTest("gohma-magic-local",
                                "current magic and coherent HUD capacity survived the scene barrier");
            if (!automatedTestClient && !automatedTestBossPrepared) {
                automatedTestBossPrepared = true;
                automatedTestTargetActor = localGohmas.begin()->second;
                PrepareGohmaForAutomatedTest(automatedTestTargetActor);
                Player* player = GET_PLAYER(gPlayState);
                player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
                PositionAutomatedTestPlayer(player, static_cast<Actor*>(automatedTestTargetActor), -120.0f);
                SendGohmaSnapshot(automatedTestTargetActor, true);
                ReportAutomatedTest("gohma-host-authority-ready", "health=2, vulnerable");
            }
            if (automatedTestClient) {
                const auto snapshot = std::find_if(actorSnapshots.begin(), actorSnapshots.end(), [](const auto& entry) {
                    return entry.second.scene == SCENE_DEKU_TREE_BOSS && entry.second.actorId == ACTOR_BOSS_GOMA &&
                           entry.second.alive && entry.second.health == 2;
                });
                if (snapshot == actorSnapshots.end()) {
                    return;
                }
                ReportAutomatedTest("gohma-guest-replica-ready", std::to_string(snapshot->first));
            } else if (!automatedTestBossPrepared) {
                return;
            }
            automatedTestCombatPhase = CombatSetup;
            automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
            automatedTestPhysicalHits = 0;
            automatedTestAcceptedBossHits = 0;
            automatedTestLastObservedHealth = 2;
            automatedTestMovementObserved = false;
            automatedTestRemoteMovementObserved = false;
            automatedTestTargetObserved = false;
            automatedTestRemoteTargetObserved = false;
            automatedTestSwingObserved = false;
            automatedTestRemoteSwingObserved = false;
            automatedTestRemoteSwingRendered = false;
            automatedTestFirstDamageObserved = false;
            automatedTestPostDeathCleanupObserved = false;
            automatedTestTargetEntityId = 0;
            automatedTestTargetActor = automatedTestClient ? nullptr : localGohmas.begin()->second;
            automatedTestRemotePreviousTick = 0;
            SetAutomatedTestStage(TestBossCombat, "gohma-combat-started");
            return;
        }
        case TestBossCombat: {
            const bool bossClear = (gSaveContext.sceneFlags[SCENE_DEKU_TREE_BOSS].clear & kGohmaRoomMask) != 0;
            if (!automatedTestClient) {
                Player* player = GET_PLAYER(gPlayState);
                if (!automatedTestBossCompleted && automatedTestTargetActor != nullptr &&
                    player->focusActor == automatedTestTargetActor && !automatedTestTargetObserved) {
                    automatedTestTargetObserved = true;
                    ReportAutomatedTest("gohma-host-target-acquired",
                                        "host Z input selected the live authoritative actor");
                }
                if (!automatedTestTargetObserved && automatedTestTick - automatedTestStageTick > 80) {
                    FailAutomatedTest("host Z input did not acquire live Gohma");
                    return;
                }
                if (remotePlayerSnapshot.has_value() &&
                    remotePlayerSnapshot->scene == SCENE_DEKU_TREE_BOSS) {
                    const PlayerSnapshotMessage& remote = *remotePlayerSnapshot;
                    if (automatedTestRemotePreviousTick != 0 && remote.tick != automatedTestRemotePreviousTick &&
                        std::abs(remote.linearVelocity) > 0.5f &&
                        HorizontalDistanceSquared(remote.position, automatedTestRemotePreviousPosition) > 1.0f &&
                        !automatedTestRemoteMovementObserved) {
                        automatedTestRemoteMovementObserved = true;
                        ReportAutomatedTest("gohma-remote-movement-visible",
                                            "stick-driven position and velocity reached host snapshot");
                    }
                    if (remote.focusActorId == ACTOR_BOSS_GOMA && !automatedTestRemoteTargetObserved) {
                        automatedTestRemoteTargetObserved = true;
                        ReportAutomatedTest("gohma-remote-target-visible",
                                            "guest Gohma lock-on reached host snapshot");
                    }
                    if (remote.meleeWeaponState > 0 && !automatedTestRemoteSwingObserved) {
                        automatedTestRemoteSwingObserved = true;
                        ReportAutomatedTest("gohma-remote-swing-visible",
                                            "guest melee state reached the host player stream");
                    }
                    automatedTestRemotePreviousPosition[0] = remote.position[0];
                    automatedTestRemotePreviousPosition[1] = remote.position[1];
                    automatedTestRemotePreviousPosition[2] = remote.position[2];
                    automatedTestRemotePreviousTick = remote.tick;
                }
                if (automatedTestAcceptedBossHits > 2) {
                    FailAutomatedTest("one physical Gohma swing was counted more than once");
                    return;
                }
                if (!bossClear) {
                    return;
                }
                if (automatedTestAcceptedBossHits != 2 || !automatedTestTargetObserved ||
                    !automatedTestRemoteMovementObserved ||
                    !automatedTestRemoteTargetObserved || !automatedTestRemoteSwingObserved ||
                    !automatedTestRemoteSwingRendered) {
                    return;
                }
                if (automatedTestCombatPhase != CombatVerifyCleanup ||
                    automatedTestTick - automatedTestCombatPhaseTick < 30) {
                    return;
                }
                const auto liveBoss = std::find_if(localGohmas.begin(), localGohmas.end(), [](const auto& entry) {
                    return entry.second != nullptr;
                });
                if (liveBoss != localGohmas.end()) {
                    Actor* actor = static_cast<Actor*>(liveBoss->second);
                    if (actor->colChkInfo.health > 0 ||
                        (actor->flags & (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE)) != 0 ||
                        CanDamageGohma(liveBoss->second)) {
                        FailAutomatedTest("defeated host Gohma remained targetable or damageable");
                        return;
                    }
                }
                if (player->focusActor == automatedTestTargetActor || automatedTestAcceptedBossHits != 2) {
                    FailAutomatedTest("host post-death target or sword input reached defeated Gohma");
                    return;
                }
                ReportAutomatedTest("gohma-host-target-released",
                                    "authoritative Link lost focus and Gohma remained non-damageable");
                ReportAutomatedTest("gohma-defeat-synchronized",
                                    "two physical hits, remote movement, target, swing, and room clear verified");
                SetAutomatedTestStage(TestAwaitingBossCompletion, "boss-progression-test-complete");
                return;
            }
            const auto target = std::find_if(actorSnapshots.begin(), actorSnapshots.end(), [](const auto& entry) {
                return entry.second.scene == SCENE_DEKU_TREE_BOSS && entry.second.actorId == ACTOR_BOSS_GOMA;
            });
            if (target == actorSnapshots.end()) {
                return;
            }
            Player* player = GET_PLAYER(gPlayState);
            const auto local = localGohmas.find(target->first);
            if (automatedTestCombatPhase == CombatSetup) {
                if (!target->second.alive || target->second.health != 2 || local == localGohmas.end()) {
                    return;
                }
                automatedTestTargetEntityId = target->first;
                automatedTestTargetActor = local->second;
                EquipAutomatedTestSword(player);
                PositionAutomatedTestPlayer(player, static_cast<Actor*>(local->second), 220.0f);
                automatedTestMovementOrigin[0] = player->actor.world.pos.x;
                automatedTestMovementOrigin[1] = player->actor.world.pos.y;
                automatedTestMovementOrigin[2] = player->actor.world.pos.z;
                automatedTestCombatPhase = CombatMove;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                ReportAutomatedTest("gohma-input-movement-started", "analog stick injected before Player_Update");
                return;
            }
            if (automatedTestCombatPhase == CombatMove) {
                const float position[] = { player->actor.world.pos.x, player->actor.world.pos.y,
                                           player->actor.world.pos.z };
                if (HorizontalDistanceSquared(position, automatedTestMovementOrigin) <= 25.0f * 25.0f ||
                    std::abs(player->linearVelocity) <= 0.5f) {
                    return;
                }
                automatedTestMovementObserved = true;
                ReportAutomatedTest("gohma-local-movement-verified",
                                    "real Player_Update moved Link more than 25 units");
                if (local == localGohmas.end()) {
                    return;
                }
                PositionAutomatedTestPlayer(player, static_cast<Actor*>(local->second), 75.0f);
                automatedTestCombatPhase = CombatAcquireTarget;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                return;
            }
            if (automatedTestCombatPhase == CombatAcquireTarget) {
                if (player->focusActor != automatedTestTargetActor) {
                    return;
                }
                automatedTestTargetObserved = true;
                automatedTestCombatPhase = CombatFirstSwing;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                ReportAutomatedTest("gohma-target-acquired", "Z input selected the live guest replica");
                return;
            }
            if ((automatedTestCombatPhase == CombatFirstSwing ||
                 automatedTestCombatPhase == CombatSecondSwing) &&
                player->meleeWeaponState > 0 && !automatedTestSwingObserved) {
                automatedTestSwingObserved = true;
                ReportAutomatedTest("gohma-sword-state-entered",
                                    "Player_Update entered a melee weapon state from B input");
            }
            if (automatedTestCombatPhase == CombatFirstSwing && automatedTestPhysicalHits >= 1) {
                automatedTestCombatPhase = CombatAwaitFirstDamage;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                return;
            }
            if (automatedTestCombatPhase == CombatSecondSwing && player->meleeWeaponState > 0 &&
                automatedTestLastObservedHealth == 1) {
                automatedTestLastObservedHealth = -1;
                ReportAutomatedTest("gohma-second-sword-state-entered",
                                    "Player_Update accepted the follow-up B input");
            }
            if (automatedTestCombatPhase == CombatAwaitFirstDamage) {
                if (target->second.health <= 0 && !automatedTestFirstDamageObserved) {
                    FailAutomatedTest("Gohma skipped the required health 2 to 1 transition");
                    return;
                }
                if (target->second.health == 1 && !automatedTestFirstDamageObserved) {
                    automatedTestFirstDamageObserved = true;
                    automatedTestLastObservedHealth = 1;
                    ReportAutomatedTest("gohma-first-damage-synchronized", "health=2 -> health=1");
                }
                if (!automatedTestFirstDamageObserved || pendingGuestAttacks.contains(target->first) ||
                    player->meleeWeaponState != 0 || local == localGohmas.end() ||
                    !CanDamageGohma(local->second)) {
                    return;
                }
                PositionAutomatedTestPlayer(player, static_cast<Actor*>(local->second), 75.0f);
                automatedTestCombatPhase = CombatSecondSwing;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                return;
            }
            if (automatedTestCombatPhase == CombatSecondSwing && automatedTestPhysicalHits >= 2) {
                automatedTestCombatPhase = CombatAwaitDeath;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                return;
            }
            if (automatedTestCombatPhase == CombatAwaitDeath) {
                if (target->second.health > 0 || !bossClear) {
                    return;
                }
                if (automatedTestPhysicalHits != 2 || pendingGuestAttacks.contains(target->first) ||
                    player->focusActor == automatedTestTargetActor) {
                    return;
                }
                automatedTestCombatPhase = CombatVerifyCleanup;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                ReportAutomatedTest("gohma-death-cleanup-visible",
                                    "dead replica lost targeting and collision eligibility");
                return;
            }
            if (automatedTestCombatPhase == CombatVerifyCleanup &&
                automatedTestTick - automatedTestCombatPhaseTick >= 30) {
                if (automatedTestPhysicalHits != 2 || pendingGuestAttacks.contains(target->first) ||
                    player->focusActor == automatedTestTargetActor) {
                    FailAutomatedTest("post-death target or sword input reached the defeated Gohma collider");
                    return;
                }
                automatedTestPostDeathCleanupObserved = true;
                automatedTestCombatPhase = CombatDone;
                ReportAutomatedTest("gohma-post-death-attack-rejected",
                                    "B input produced no target, collision, or additional hit");
            }
            if (bossClear && automatedTestCombatPhase == CombatDone && automatedTestMovementObserved &&
                automatedTestTargetObserved && automatedTestSwingObserved && automatedTestFirstDamageObserved &&
                automatedTestPostDeathCleanupObserved && automatedTestPhysicalHits == 2) {
                ReportAutomatedTest("gohma-defeat-synchronized",
                                    "two physical sword collisions and post-death cleanup verified");
                SetAutomatedTestStage(TestAwaitingBossCompletion, "boss-progression-test-complete");
            }
            return;
        }
        case TestAwaitingBossCompletion: {
            const bool bossClear = (gSaveContext.sceneFlags[SCENE_DEKU_TREE_BOSS].clear & kGohmaRoomMask) != 0;
            if (!bossClear) {
                return;
            }
            if (!automatedTestClient) {
                SetAutomatedTestStage(TestAwaitingReconnect, "host-awaiting-client-reconnect");
                return;
            }
            automatedTestReconnectStarted = true;
            Disconnect();
            // Simulate a stale guest only after disconnect cleanup has restored its local save overlay.
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].collect &= ~collectibleMask;
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch &= ~switchMask;
            gSaveContext.sceneFlags[SCENE_DEKU_TREE_BOSS].clear &= ~kGohmaRoomMask;
            GameInteractor::RawAction::UnsetFlag(FLAG_EVENT_CHECK_INF, EVENTCHKINF_KING_ZORA_MOVED);
            gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_NONE;
            gSaveContext.inventory.items[SLOT_FARORES_WIND] = ITEM_NONE;
            gSaveContext.itemGetInf[ITEMGETINF_18_19_1A_INDEX] &= ~ITEMGETINF_18_MASK;
            gSaveContext.inventory.equipment &=
                ~OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI);
            if (!Join(automatedTestAddress, automatedTestPort, "Local Guest")) {
                FailAutomatedTest("client reconnect could not start");
                return;
            }
            SetAutomatedTestStage(TestAwaitingReconnect, "client-reconnect-started");
            return;
        }
        case TestAwaitingReconnect: {
            if (!automatedTestClient && observedConnectionGeneration >= 2) {
                automatedTestReconnectStarted = true;
            }
            if (!automatedTestReconnectStarted || phase != ConnectionPhase::Ready || !IsSaveLoaded()) {
                return;
            }
            const bool collected = collectedLocations.contains(collectibleId) &&
                                   (gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].collect & collectibleMask) != 0;
            const bool dead = automatedTestClient ||
                              std::any_of(actorSnapshots.begin(), actorSnapshots.end(), [](const auto& entry) {
                                  return entry.second.scene == SCENE_KOKIRI_FOREST && !entry.second.alive;
                              });
            const bool hookshotShared = gSaveContext.inventory.items[SLOT_HOOKSHOT] == ITEM_HOOKSHOT;
            const bool faroresWindShared =
                gSaveContext.inventory.items[SLOT_FARORES_WIND] == ITEM_FARORES_WIND &&
                Flags_GetItemGetInf(ITEMGETINF_18);
            const bool swordShared =
                CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI) != 0;
            const bool bossClear = (gSaveContext.sceneFlags[SCENE_DEKU_TREE_BOSS].clear & kGohmaRoomMask) != 0;
            const bool worldState = Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED) != 0;
            const int16_t expectedRupees = automatedTestClient ? 222 : 111;
            const int8_t expectedArrows = automatedTestClient ? 23 : 7;
            const int8_t expectedMagic = automatedTestClient ? kAutomatedTestClientMagic : kAutomatedTestHostMagic;
            if (!collected || !dead || !hookshotShared || !faroresWindShared || !swordShared || !bossClear ||
                !worldState || remotePlayer == nullptr) {
                return;
            }
            if (gSaveContext.rupees != expectedRupees ||
                gSaveContext.inventory.ammo[SLOT_BOW] != expectedArrows || gSaveContext.magic != expectedMagic ||
                gSaveContext.magicLevel != 1 || gSaveContext.magicCapacity != MAGIC_NORMAL_METER ||
                !gSaveContext.isMagicAcquired || gSaveContext.isDoubleMagicAcquired) {
                FailAutomatedTest("reconnect overwrote a local resource or corrupted the magic meter");
                return;
            }
            ReportAutomatedTest(
                "reconnect-state-restored",
                automatedTestClient
                    ? "pickup, spell, equipment, world event, boss completion, and remote Link replayed to stale guest"
                    : "enemy, pickup, spell, equipment, scene switch, world event, boss completion, and remote Link retained by host");
            automatedTestStage = TestComplete;
            ReportAutomatedTest("PASS", "full two-instance OoT vertical proof completed");
            return;
        }
        default:
            return;
    }
}

bool Manager::IsCurrentScope(const SessionScope& candidate) const {
    return candidate.sessionEpoch != 0 && candidate == sessionScope;
}

void Manager::CaptureSaveOverlay() {
    if (!IsSaveLoaded() || saveOverlayCaptured) {
        return;
    }
    originalSceneFlags.reserve(SCENE_ID_MAX);
    for (int16_t scene = 0; scene < SCENE_ID_MAX; ++scene) {
        const SavedSceneFlags& flags = gSaveContext.sceneFlags[scene];
        originalSceneFlags.emplace(scene,
                                   SceneFlagState{ flags.chest, flags.swch, flags.clear, flags.collect });
    }
    originalProgression = CaptureSharedProgression(&gSaveContext);
    if (transport.GetRole() == SessionRole::Host && !canonicalProgressionCaptured) {
        canonicalProgression = originalProgression;
        canonicalProgressionCaptured = true;
    }
    saveOverlayCaptured = true;
}

void Manager::RestoreSaveOverlay() {
    if (!saveOverlayCaptured || !IsSaveLoaded()) {
        return;
    }
    SanitizeSaveCopy(&gSaveContext);
    if (gPlayState != nullptr) {
        const auto state = originalSceneFlags.find(gPlayState->sceneNum);
        if (state != originalSceneFlags.end()) {
            gPlayState->actorCtx.flags.chest = state->second.chest;
            gPlayState->actorCtx.flags.swch = state->second.switches;
            gPlayState->actorCtx.flags.clear = state->second.clear;
            gPlayState->actorCtx.flags.collect = state->second.collectible;
        }
    }
}

bool Manager::IsSaveLoaded() const {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr && gSaveContext.fileNum >= 0 &&
           gSaveContext.fileNum <= 2 && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

} // namespace HyruleCoop
