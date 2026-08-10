#include "HyruleCoop.h"
#include "BarinadeBridge.h"
#include "DekuBabaAdapter.h"
#include "DungeonRewardPolicy.h"
#include "EntityIdentity.h"
#include "GenericEnemyBridge.h"
#include "GohmaAdapter.h"
#include "JabuActorBridge.h"
#include "ProgressionAdapter.h"
#include "DampeRaceBridge.h"
#include "PushBlockBridge.h"
#include "RemotePlayer.h"
#include "RemotePlayerRoomPolicy.h"
#include "SharedEnemyCombatPolicy.h"
#include "StalchildPolicy.h"

#include "soh/SaveManager.h"
#include "soh/Enhancements/SwitchAge.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/item-tables/ItemTableManager.h"
#include "soh/OTRGlobals.h"
#include "ship/Context.h"
#include "ship/utils/StrHash64.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cstdarg>
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
#include "src/overlays/actors/ovl_Bg_Spot02_Objects/z_bg_spot02_objects.h"
#include "src/overlays/actors/ovl_Bg_Toki_Swd/z_bg_toki_swd.h"
#include "src/overlays/actors/ovl_Demo_Kankyo/z_demo_kankyo.h"
#include "src/overlays/actors/ovl_En_Encount1/z_en_encount1.h"
#include "src/overlays/actors/ovl_En_Horse/z_en_horse.h"
#include "src/overlays/actors/ovl_En_Kz/z_en_kz.h"
#include "src/overlays/actors/ovl_En_Md/z_en_md.h"
#include "src/overlays/actors/ovl_En_Okarina_Tag/z_en_okarina_tag.h"
#include "src/overlays/actors/ovl_En_Skb/z_en_skb.h"
#include "src/overlays/actors/ovl_Obj_Timeblock/z_obj_timeblock.h"
#include "variables.h"
#include "z64.h"

extern PlayState* gPlayState;
void Sram_OpenSave(void);
GetItemID RetrieveGetItemIDFromItemID(ItemID itemID);
void Player_UseItem(PlayState* play, Player* player, s32 item);
void func_80853080(Player* player, PlayState* play);
s32 EnKz_SetMovedPos(EnKz* thisx, PlayState* play);
void EnKz_PreMweepWait(EnKz* thisx, PlayState* play);
void EnKz_Wait(EnKz* thisx, PlayState* play);
s32 EnMd_SetMovedPos(EnMd* thisx, PlayState* play);
void EnMd_Idle(EnMd* thisx, PlayState* play);
u32 ObjTimeblock_CalculateIsVisible(ObjTimeblock* thisx);
void ObjTimeblock_SetupNormal(ObjTimeblock* thisx);
void ObjTimeblock_SetupAltBehaviorVisible(ObjTimeblock* thisx);
void ObjTimeblock_SetupAltBehaviourNotVisible(ObjTimeblock* thisx);
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
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
    TestAwaitingGenericEnemy,
    TestGenericCombat,
    TestAwaitingCollection,
    TestAwaitingProgression,
    TestAwaitingDungeonRewards,
    TestAwaitingWorldState,
    TestAwaitingStalchildScene,
    TestAwaitingStalchild,
    TestStalchildCombat,
    TestAwaitingStalchildDawn,
    TestAwaitingBossScene,
    TestAwaitingBossReady,
    TestBossCombat,
    TestAwaitingBossCompletion,
    TestAwaitingGuestProtection,
    TestAwaitingReconnect,
    TestAwaitingPersistence,
    TestComplete,
    TestFailed,
};

constexpr size_t kAutomatedTestJabuDungeonIndex = SCENE_JABU_JABU;
static_assert(kAutomatedTestJabuDungeonIndex < kDungeonRewardDungeonCount);
constexpr uint8_t kAutomatedTestJabuRewardMask = kDungeonMapItemBit | kDungeonCompassItemBit;
constexpr uint32_t kAutomatedTestJabuMapChestMask =
    1u << kVanillaDungeonRewardChests[kAutomatedTestJabuDungeonIndex].map;
constexpr uint32_t kAutomatedTestJabuCompassChestMask =
    1u << kVanillaDungeonRewardChests[kAutomatedTestJabuDungeonIndex].compass;

enum AutomatedCombatPhase : uint8_t {
    CombatSetup,
    CombatMove,
    CombatAwaitEnemyAttack,
    CombatAcquireTarget,
    CombatFirstSwing,
    CombatAwaitFirstDamage,
    CombatAwaitRecovery,
    CombatSecondSwing,
    CombatAwaitDeath,
    CombatVerifyCleanup,
    CombatDone,
};

constexpr int16_t kAutomatedTestCollectibleFlag = 0x1E;
constexpr int16_t kAutomatedTestSwitchFlag = 0x1F;
constexpr int16_t kAutomatedTestTempSwitchFlag = 0x3C;
constexpr int16_t kAutomatedTestTempCollectibleFlag = 0x3D;
constexpr int16_t kAutomatedTestTempClearFlag = 0x1D;
constexpr int8_t kAutomatedTestHostMagic = 12;
constexpr int8_t kAutomatedTestClientMagic = 36;
constexpr int16_t kGohmaRoom = 1;
constexpr uint32_t kGohmaRoomMask = 1u << kGohmaRoom;
constexpr uint32_t kGuestAttackCooldownFrames = 6;
constexpr uint32_t kGuestDekuBabaAttackCooldownFrames = 18;
constexpr uint32_t kAutomatedStalchildTargetRecoveryFrames = 30;
constexpr int64_t kGuestAttackStateWindowFrames = 90;
constexpr int16_t kGuestEnemyHealthSentinel = 127;
constexpr uint8_t kJabuActorAdapterWordCount = 6;
constexpr uint8_t kBarinadeAdapterWordCount =
    static_cast<uint8_t>((sizeof(HyruleCoopBarinadeState) + sizeof(int16_t) - 1) / sizeof(int16_t));
static_assert(kBarinadeAdapterWordCount <= kMaximumActorAdapterWords);

enum JabuActorAdapterWord : uint8_t {
    JabuActorFlags,
    JabuActorVariant,
    JabuActorTimer,
    JabuActorAuxTimer,
    JabuActorAuxState,
    JabuActorAlpha,
};
constexpr const char* kHyruleCoopCompatibilityId = "hyrule-coop-poc.3";

bool HasSharedEventFlag(const SharedProgressionState& state, uint16_t flag) {
    return (state.eventChkInf[flag >> 4] & (1u << (flag & 0xF))) != 0;
}

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

TimelineScope SnapshotTimelineScope(const PlayerSnapshotMessage& snapshot) {
    return { snapshot.linkAge, snapshot.sceneLayer };
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

bool IsValidReplicatedSceneFlag(int16_t scene, int16_t flagType, int16_t flag) {
    if (scene < 0 || scene >= SCENE_ID_MAX || flag < 0) {
        return false;
    }
    if (flagType == FLAG_SCENE_SWITCH) {
        return flag < 0x40;
    }
    if (flagType == FLAG_SCENE_TEMP_CLEAR) {
        return flag < 0x20;
    }
    if (flagType == FLAG_SCENE_COLLECTIBLE && flag == 0) {
        return false;
    }
    if (flagType == FLAG_SCENE_COLLECTIBLE) {
        return flag < 0x40;
    }
    return flag < 0x20 &&
           (flagType == FLAG_SCENE_TREASURE || flagType == FLAG_SCENE_CLEAR ||
            flagType == FLAG_SCENE_COLLECTIBLE);
}

bool IsEphemeralSceneFlag(int16_t scene, int16_t flagType, int16_t flag) {
    return IsValidReplicatedSceneFlag(scene, flagType, flag) &&
           (flagType == FLAG_SCENE_TEMP_CLEAR ||
            ((flagType == FLAG_SCENE_SWITCH || flagType == FLAG_SCENE_COLLECTIBLE) && flag >= 0x20));
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
          Capability::OotGuestAttack, Capability::OotEnemyBaseline, Capability::OotCollectible,
          Capability::OotSharedProgression, Capability::OotGohma, Capability::OotJabuActors,
          Capability::OotStoryEvents })
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

int16_t InterpolateRotation(int16_t from, int16_t to, float amount) {
    const int16_t delta = static_cast<int16_t>(to - from);
    return static_cast<int16_t>(from + std::lround(static_cast<float>(delta) * amount));
}

void OverrideActorPlayerTracking(Actor* actor, const float* targetPosition) {
    if (actor == nullptr || targetPosition == nullptr) {
        return;
    }

    Vec3f target{ targetPosition[0], targetPosition[1], targetPosition[2] };
    actor->xzDistToPlayer = Actor_WorldDistXZToPoint(actor, &target);
    actor->yDistToPlayer = target.y - actor->world.pos.y;
    actor->xyzDistToPlayerSq = SQ(actor->xzDistToPlayer) + SQ(actor->yDistToPlayer);
    actor->yawTowardsPlayer = Actor_WorldYawTowardPoint(actor, &target);
}

bool IsGenericEnemyAdapterActorId(int16_t actorId) {
    // Enemy-private action state is not interchangeable. Keep this baseline opt-in until each actor has passed the
    // physical-collision, damage, death, animation, and scene-transition proof used for Keese.
    return actorId == ACTOR_EN_FIREFLY;
}

ActorSnapshotMessage CaptureGenericEnemySnapshot(const Actor* actor, int16_t scene, uint32_t hostTick,
                                                 SessionScope scope, bool alive);
bool ApplyGenericEnemySnapshot(Actor* actor, const ActorSnapshotMessage& message);

bool IsJabuActorId(int16_t actorId) {
    return actorId == ACTOR_EN_BILI || actorId == ACTOR_EN_VALI || actorId == ACTOR_EN_TP || actorId == ACTOR_EN_BA ||
           actorId == ACTOR_EN_BIGOKUTA;
}

Actor* CanonicalJabuActor(Actor* actor) {
    if (actor == nullptr || !IsJabuActorId(actor->id)) {
        return nullptr;
    }
    if (actor->id == ACTOR_EN_TP) {
        return static_cast<Actor*>(HyruleCoop_EnTpCanonicalActor(actor));
    }
    if (actor->id == ACTOR_EN_BA) {
        return static_cast<Actor*>(HyruleCoop_EnBaCanonicalActor(actor));
    }
    return actor;
}

uint64_t GetJabuActorEntityId(const Actor* actor, int16_t scene, uint32_t worldGeneration) {
    StaticEntitySignature signature;
    signature.worldGeneration = worldGeneration;
    signature.scene = scene;
    signature.room = actor->room;
    signature.actorId = actor->id;
    // Several Jabu actors mutate params during their death path. Their placement remains stable.
    signature.params = 0;
    signature.homePosition[0] = std::lround(actor->home.pos.x);
    signature.homePosition[1] = std::lround(actor->home.pos.y);
    signature.homePosition[2] = std::lround(actor->home.pos.z);
    signature.homeRotation[0] = actor->home.rot.x;
    signature.homeRotation[1] = actor->home.rot.y;
    signature.homeRotation[2] = actor->home.rot.z;
    return BuildStaticEntityId(signature);
}

bool CaptureJabuActorState(const Actor* actor, HyruleCoopJabuActorState* state) {
    switch (actor->id) {
        case ACTOR_EN_BILI:
            return HyruleCoop_EnBiliCaptureState(actor, state) != 0;
        case ACTOR_EN_VALI:
            return HyruleCoop_EnValiCaptureState(actor, state) != 0;
        case ACTOR_EN_TP:
            return HyruleCoop_EnTpCaptureState(actor, state) != 0;
        case ACTOR_EN_BA:
            return HyruleCoop_EnBaCaptureState(actor, state) != 0;
        case ACTOR_EN_BIGOKUTA:
            return HyruleCoop_EnBigokutaCaptureState(actor, state) != 0;
        default:
            return false;
    }
}

bool ApplyJabuActorState(Actor* actor, const HyruleCoopJabuActorState& state) {
    switch (actor->id) {
        case ACTOR_EN_BILI:
            return HyruleCoop_EnBiliApplyState(actor, gPlayState, &state) != 0;
        case ACTOR_EN_VALI:
            return HyruleCoop_EnValiApplyState(actor, gPlayState, &state) != 0;
        case ACTOR_EN_TP:
            return HyruleCoop_EnTpApplyState(actor, gPlayState, &state) != 0;
        case ACTOR_EN_BA:
            return HyruleCoop_EnBaApplyState(actor, gPlayState, &state) != 0;
        case ACTOR_EN_BIGOKUTA:
            return HyruleCoop_EnBigokutaApplyState(actor, gPlayState, &state) != 0;
        default:
            return false;
    }
}

bool ApplyJabuActorDamage(Actor* actor, uint8_t damageEffect, uint8_t damage) {
    switch (actor->id) {
        case ACTOR_EN_BILI:
            return HyruleCoop_EnBiliApplyDamage(actor, gPlayState, damageEffect, damage) != 0;
        case ACTOR_EN_VALI:
            return HyruleCoop_EnValiApplyDamage(actor, gPlayState, damageEffect, damage) != 0;
        case ACTOR_EN_TP:
            return HyruleCoop_EnTpApplyDamage(actor, gPlayState, damageEffect, damage) != 0;
        case ACTOR_EN_BA:
            return HyruleCoop_EnBaApplyDamage(actor, gPlayState, damage) != 0;
        case ACTOR_EN_BIGOKUTA:
            return HyruleCoop_EnBigokutaApplyDamage(actor, gPlayState, damageEffect, damage) != 0;
        default:
            return false;
    }
}

bool PeekJabuActorDamage(const Actor* actor, uint8_t* damageEffect, uint8_t* damage) {
    switch (actor->id) {
        case ACTOR_EN_BILI:
            return HyruleCoop_EnBiliPeekDamage(actor, damageEffect, damage) != 0;
        case ACTOR_EN_VALI:
            return HyruleCoop_EnValiPeekDamage(actor, damageEffect, damage) != 0;
        case ACTOR_EN_TP:
            return HyruleCoop_EnTpPeekDamage(actor, damageEffect, damage) != 0;
        case ACTOR_EN_BA:
            return HyruleCoop_EnBaPeekDamage(actor, damageEffect, damage) != 0;
        case ACTOR_EN_BIGOKUTA:
            return HyruleCoop_EnBigokutaPeekDamage(actor, damageEffect, damage) != 0;
        default:
            return false;
    }
}

ActorSnapshotMessage CaptureJabuActorSnapshot(const Actor* actor, int16_t scene, uint32_t hostTick,
                                              SessionScope scope, bool alive) {
    ActorSnapshotMessage message = CaptureGenericEnemySnapshot(actor, scene, hostTick, scope, alive);
    message.entityId = GetJabuActorEntityId(actor, scene, scope.worldGeneration);
    HyruleCoopJabuActorState state{};
    if (!CaptureJabuActorState(actor, &state)) {
        message.adapterWordCount = 0;
        return message;
    }
    message.health = state.health;
    message.stateId = state.action;
    message.animationFrame = state.animationFrame;
    message.animationSpeed = state.animationSpeed;
    message.adapterWordCount = kJabuActorAdapterWordCount;
    message.adapterState[JabuActorFlags] = state.flags;
    message.adapterState[JabuActorVariant] = state.variant;
    message.adapterState[JabuActorTimer] = state.timer;
    message.adapterState[JabuActorAuxTimer] = state.auxTimer;
    message.adapterState[JabuActorAuxState] = state.auxState;
    message.adapterState[JabuActorAlpha] = state.alpha;
    return message;
}

bool ApplyJabuActorSnapshot(Actor* actor, const ActorSnapshotMessage& message) {
    if (actor == nullptr || actor->id != message.actorId || message.adapterWordCount != kJabuActorAdapterWordCount ||
        message.stateId > UINT8_MAX) {
        return false;
    }
    ActorSnapshotMessage common = message;
    common.params = actor->params;
    common.adapterWordCount = 0;
    common.adapterState = {};
    if (!ApplyGenericEnemySnapshot(actor, common)) {
        return false;
    }
    HyruleCoopJabuActorState state{};
    state.action = static_cast<uint8_t>(message.stateId);
    state.health = static_cast<uint8_t>(std::clamp<int16_t>(message.health, 0, UINT8_MAX));
    state.flags = static_cast<uint8_t>(message.adapterState[JabuActorFlags]);
    state.variant = message.adapterState[JabuActorVariant];
    state.timer = message.adapterState[JabuActorTimer];
    state.auxTimer = message.adapterState[JabuActorAuxTimer];
    state.auxState = message.adapterState[JabuActorAuxState];
    state.alpha = message.adapterState[JabuActorAlpha];
    state.animationFrame = message.animationFrame;
    state.animationSpeed = message.animationSpeed;
    return ApplyJabuActorState(actor, state);
}

uint64_t GetBarinadeEntityId(const Actor* actor, int16_t scene, uint32_t worldGeneration) {
    StaticEntitySignature signature;
    signature.worldGeneration = worldGeneration;
    signature.scene = scene;
    signature.room = actor->room;
    signature.actorId = actor->id;
    signature.params = actor->params;
    signature.homePosition[0] = std::lround(actor->home.pos.x);
    signature.homePosition[1] = std::lround(actor->home.pos.y);
    signature.homePosition[2] = std::lround(actor->home.pos.z);
    signature.homeRotation[0] = actor->home.rot.x;
    signature.homeRotation[1] = actor->home.rot.y;
    signature.homeRotation[2] = actor->home.rot.z;
    return BuildStaticEntityId(signature);
}

ActorSnapshotMessage CaptureBarinadeSnapshot(const Actor* actor, int16_t scene, uint32_t hostTick,
                                             SessionScope scope, bool alive) {
    ActorSnapshotMessage message = CaptureGenericEnemySnapshot(actor, scene, hostTick, scope, alive);
    message.entityId = GetBarinadeEntityId(actor, scene, scope.worldGeneration);
    HyruleCoopBarinadeState state{};
    if (!HyruleCoop_BarinadeCaptureState(actor, &state)) {
        message.adapterWordCount = 0;
        return message;
    }
    message.health = state.health;
    message.stateId = state.action;
    message.animationFrame = state.animationFrame;
    message.animationSpeed = state.animationSpeed;
    message.adapterWordCount = kBarinadeAdapterWordCount;
    std::memcpy(message.adapterState.data(), &state, sizeof(state));
    return message;
}

bool ApplyBarinadeSnapshot(Actor* actor, const ActorSnapshotMessage& message) {
    if (actor == nullptr || actor->id != ACTOR_BOSS_VA || message.actorId != ACTOR_BOSS_VA ||
        actor->params != message.params || message.adapterWordCount != kBarinadeAdapterWordCount) {
        return false;
    }
    ActorSnapshotMessage common = message;
    common.adapterWordCount = 0;
    common.adapterState = {};
    if (!ApplyGenericEnemySnapshot(actor, common)) {
        return false;
    }
    HyruleCoopBarinadeState state{};
    std::memcpy(&state, message.adapterState.data(), sizeof(state));
    return HyruleCoop_BarinadeApplyState(actor, gPlayState, &state) != 0;
}

bool IsGenericEnemyActor(const Actor* actor) {
    return actor != nullptr && actor->category == ACTORCAT_ENEMY && IsGenericEnemyAdapterActorId(actor->id) &&
           actor->colChkInfo.health > 0;
}

uint64_t GetGenericEnemyEntityId(const Actor* actor, int16_t scene, uint32_t worldGeneration) {
    StaticEntitySignature signature;
    signature.worldGeneration = worldGeneration;
    signature.scene = scene;
    signature.room = actor->room;
    signature.actorId = actor->id;
    signature.params = actor->params;
    signature.homePosition[0] = std::lround(actor->home.pos.x);
    signature.homePosition[1] = std::lround(actor->home.pos.y);
    signature.homePosition[2] = std::lround(actor->home.pos.z);
    signature.homeRotation[0] = actor->home.rot.x;
    signature.homeRotation[1] = actor->home.rot.y;
    signature.homeRotation[2] = actor->home.rot.z;
    return BuildStaticEntityId(signature);
}

ActorSnapshotMessage CaptureGenericEnemySnapshot(const Actor* actor, int16_t scene, uint32_t hostTick,
                                                 SessionScope scope, bool alive) {
    ActorSnapshotMessage message;
    message.scope = scope;
    message.hostTick = hostTick;
    message.entityId = GetGenericEnemyEntityId(actor, scene, scope.worldGeneration);
    message.scene = scene;
    message.room = actor->room;
    message.actorId = actor->id;
    message.params = actor->params;
    message.homePosition[0] = actor->home.pos.x;
    message.homePosition[1] = actor->home.pos.y;
    message.homePosition[2] = actor->home.pos.z;
    message.position[0] = actor->world.pos.x;
    message.position[1] = actor->world.pos.y;
    message.position[2] = actor->world.pos.z;
    message.velocity[0] = actor->velocity.x;
    message.velocity[1] = actor->velocity.y;
    message.velocity[2] = actor->velocity.z;
    message.scale[0] = actor->scale.x;
    message.scale[1] = actor->scale.y;
    message.scale[2] = actor->scale.z;
    message.worldRotation[0] = actor->world.rot.x;
    message.worldRotation[1] = actor->world.rot.y;
    message.worldRotation[2] = actor->world.rot.z;
    message.shapeRotation[0] = actor->shape.rot.x;
    message.shapeRotation[1] = actor->shape.rot.y;
    message.shapeRotation[2] = actor->shape.rot.z;
    message.speed = actor->speedXZ;
    message.gravity = actor->gravity;
    message.health = actor->colChkInfo.health;
    message.alive = alive;
    return message;
}

bool ApplyGenericEnemySnapshot(Actor* actor, const ActorSnapshotMessage& message) {
    if (actor == nullptr || actor->id != message.actorId || actor->params != message.params ||
        message.adapterWordCount != 0) {
        return false;
    }
    actor->world.pos = { message.position[0], message.position[1], message.position[2] };
    actor->prevPos = actor->world.pos;
    actor->velocity = { message.velocity[0], message.velocity[1], message.velocity[2] };
    actor->scale = { message.scale[0], message.scale[1], message.scale[2] };
    actor->world.rot = { message.worldRotation[0], message.worldRotation[1], message.worldRotation[2] };
    actor->shape.rot = { message.shapeRotation[0], message.shapeRotation[1], message.shapeRotation[2] };
    actor->speedXZ = message.speed;
    actor->gravity = message.gravity;
    actor->colChkInfo.health = std::max<int16_t>(0, message.health);
    return true;
}

constexpr uint8_t kDampeGhostAdapterWordCount = 9;
constexpr uint8_t kDampeDoorAdapterWordCount = 3;

bool IsDampeRaceActorId(int16_t actorId) {
    return actorId == ACTOR_EN_PO_RELAY || actorId == ACTOR_BG_RELAY_OBJECTS;
}

ActorSnapshotMessage CapturePushBlockSnapshot(const Actor* actor, int16_t scene, uint32_t hostTick,
                                              SessionScope scope) {
    ActorSnapshotMessage message = CaptureGenericEnemySnapshot(actor, scene, hostTick, scope, true);
    HyruleCoopPushBlockState state = {};
    if (!HyruleCoop_ObjOshihikiCaptureState(actor, &state)) {
        return message;
    }
    std::copy(std::begin(state.homePosition), std::end(state.homePosition), message.homePosition);
    std::copy(std::begin(state.position), std::end(state.position), message.position);
    std::copy(std::begin(state.velocity), std::end(state.velocity), message.velocity);
    message.speed = state.pushSpeed;
    message.gravity = state.pushDistance;
    message.animationFrame = state.direction;
    message.health = state.timer;
    message.worldRotation[1] = state.worldYaw;
    message.stateId = state.phase;
    return message;
}

bool ApplyPushBlockSnapshot(Actor* actor, const ActorSnapshotMessage& message) {
    HyruleCoopPushBlockState state = {};
    std::copy(std::begin(message.homePosition), std::end(message.homePosition), state.homePosition);
    std::copy(std::begin(message.position), std::end(message.position), state.position);
    std::copy(std::begin(message.velocity), std::end(message.velocity), state.velocity);
    state.pushSpeed = message.speed;
    state.pushDistance = message.gravity;
    state.direction = message.animationFrame;
    state.timer = message.health;
    state.worldYaw = message.worldRotation[1];
    state.phase = static_cast<uint8_t>(message.stateId);
    return HyruleCoop_ObjOshihikiApplyState(actor, gPlayState, &state) != 0;
}

ActorSnapshotMessage CaptureDampeRaceSnapshot(const Actor* actor, int16_t scene, uint32_t hostTick,
                                              SessionScope scope) {
    ActorSnapshotMessage message = CaptureGenericEnemySnapshot(actor, scene, hostTick, scope, true);
    if (actor->id == ACTOR_EN_PO_RELAY) {
        HyruleCoopDampeRaceGhostState state = {};
        if (!HyruleCoop_DampeRaceCaptureGhostState(actor, &state)) {
            return message;
        }
        message.adapterWordCount = kDampeGhostAdapterWordCount;
        message.stateId = state.action;
        message.adapterState[0] = state.hookshotSlotFull;
        message.adapterState[1] = state.bobTimer;
        message.adapterState[2] = state.eyeTextureIdx;
        message.adapterState[3] = state.actionTimer;
        message.adapterState[4] = state.pathIndex;
        message.adapterState[5] = state.yawTowardsPathPoint;
        message.adapterState[6] = static_cast<int16_t>(state.textId);
        message.adapterState[7] = state.timerState;
        message.adapterState[8] = state.timerSeconds;
        message.position[0] = state.worldPosX;
        message.position[1] = state.worldPosY;
        message.position[2] = state.worldPosZ;
        message.homePosition[1] = state.homePosY;
        message.speed = state.speedXZ;
        message.scale[0] = message.scale[1] = message.scale[2] = state.scale;
        message.worldRotation[1] = state.worldRotY;
        message.shapeRotation[1] = state.shapeRotY;
        message.animationFrame = state.animationFrame;
        message.animationSpeed = state.animationSpeed;
    } else if (actor->id == ACTOR_BG_RELAY_OBJECTS) {
        HyruleCoopDampeRaceDoorState state = {};
        if (!HyruleCoop_DampeRaceCaptureDoorState(actor, &state)) {
            return message;
        }
        message.adapterWordCount = kDampeDoorAdapterWordCount;
        message.stateId = state.action;
        message.adapterState[0] = state.switchFlag;
        message.adapterState[1] = state.switchIsSet;
        message.adapterState[2] = state.timer;
        message.room = state.room;
        message.position[0] = state.worldPosX;
        message.position[1] = state.worldPosY;
        message.position[2] = state.worldPosZ;
        message.velocity[1] = state.velocityY;
        message.worldRotation[1] = state.worldRotY;
    }
    return message;
}

bool ApplyDampeRaceSnapshot(Actor* actor, const ActorSnapshotMessage& message) {
    if (actor->id == ACTOR_EN_PO_RELAY && message.adapterWordCount == kDampeGhostAdapterWordCount) {
        HyruleCoopDampeRaceGhostState state = {};
        state.action = static_cast<uint8_t>(message.stateId);
        state.hookshotSlotFull = static_cast<uint8_t>(message.adapterState[0]);
        state.bobTimer = static_cast<uint8_t>(message.adapterState[1]);
        state.eyeTextureIdx = static_cast<uint8_t>(message.adapterState[2]);
        state.actionTimer = message.adapterState[3];
        state.pathIndex = message.adapterState[4];
        state.yawTowardsPathPoint = message.adapterState[5];
        state.textId = static_cast<uint16_t>(message.adapterState[6]);
        state.timerState = message.adapterState[7];
        state.timerSeconds = message.adapterState[8];
        state.worldPosX = message.position[0];
        state.worldPosY = message.position[1];
        state.worldPosZ = message.position[2];
        state.homePosY = message.homePosition[1];
        state.speedXZ = message.speed;
        state.scale = message.scale[0];
        state.worldRotY = message.worldRotation[1];
        state.shapeRotY = message.shapeRotation[1];
        state.animationFrame = message.animationFrame;
        state.animationSpeed = message.animationSpeed;
        return HyruleCoop_DampeRaceApplyGhostState(actor, gPlayState, &state) != 0;
    }
    if (actor->id == ACTOR_BG_RELAY_OBJECTS && message.adapterWordCount == kDampeDoorAdapterWordCount) {
        HyruleCoopDampeRaceDoorState state = {};
        state.action = static_cast<uint8_t>(message.stateId);
        state.switchFlag = static_cast<uint8_t>(message.adapterState[0]);
        state.switchIsSet = static_cast<uint8_t>(message.adapterState[1]);
        state.timer = message.adapterState[2];
        state.room = static_cast<int8_t>(message.room);
        state.worldPosX = message.position[0];
        state.worldPosY = message.position[1];
        state.worldPosZ = message.position[2];
        state.velocityY = message.velocity[1];
        state.worldRotY = message.worldRotation[1];
        return HyruleCoop_DampeRaceApplyDoorState(actor, gPlayState, &state) != 0;
    }
    return false;
}

constexpr uint8_t kSharedCombatAttackKind = 6;
constexpr size_t kSharedCombatEnemyAdapterWordCount = 6;
enum SharedCombatEnemyAdapterWord : size_t {
    SharedCombatEnemyOutcomeSequenceLow,
    SharedCombatEnemyOutcomeSequenceHigh,
    SharedCombatEnemyDamageEffect,
    SharedCombatEnemyDamage,
    SharedCombatEnemyDamageFlagsLow,
    SharedCombatEnemyDamageFlagsHigh,
};

bool IsSharedCombatEnemyActorId(int16_t actorId) {
    return FindSharedEnemyAdapterContract(actorId) != nullptr;
}

static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_RD)->family == SharedEnemyAdapterFamily::RedeadGibdo);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_MB)->family == SharedEnemyAdapterFamily::LostWoodsMoblin);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_BB)->family == SharedEnemyAdapterFamily::BlueBubble);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_TEST)->family == SharedEnemyAdapterFamily::Stalfos);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_SW)->family == SharedEnemyAdapterFamily::GoldSkulltula);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_ST)->family == SharedEnemyAdapterFamily::Skulltula);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_SSH)->family == SharedEnemyAdapterFamily::SkulltulaFather);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_POH)->family == SharedEnemyAdapterFamily::Poe);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_PO_SISTERS)->family == SharedEnemyAdapterFamily::PoeSister);
static_assert(FindSharedEnemyAdapterContract(ACTOR_EN_WF)->family == SharedEnemyAdapterFamily::Wolfos);
static_assert(FindSharedEnemyAdapterContract(ACTOR_BOSS_GANONDROF)->family == SharedEnemyAdapterFamily::PhantomGanon);

bool IsSharedCombatEnemyActor(const Actor* actor) {
    if (actor == nullptr || !IsSharedCombatEnemyActorId(actor->id)) {
        return false;
    }
    if (actor->id == ACTOR_EN_PO_SISTERS) {
        return HyruleCoop_EnPoSistersSupportsSharedCombat(actor) != 0;
    }
    if (actor->id == ACTOR_BOSS_GANONDROF) {
        return HyruleCoop_BossGanondrofSupportsSharedCombat(actor) != 0;
    }
    return true;
}

bool PeekSharedCombatEnemyDamage(const Actor* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags) {
    if (actor == nullptr) {
        return false;
    }
    switch (actor->id) {
        case ACTOR_EN_RD:
            return HyruleCoop_EnRdPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_MB:
            return HyruleCoop_EnMbPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_BB:
            return HyruleCoop_EnBbPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_TEST:
            return HyruleCoop_EnTestPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_SW:
            return HyruleCoop_EnSwPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_ST:
            return HyruleCoop_EnStPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_SSH:
            return HyruleCoop_EnSshPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_POH:
            return HyruleCoop_EnPohPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_PO_SISTERS:
            return HyruleCoop_EnPoSistersPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_WF:
            return HyruleCoop_EnWfPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_BOSS_GANONDROF:
            return HyruleCoop_BossGanondrofPeekDamage(actor, damageEffect, damage, damageFlags) != 0;
        default:
            return false;
    }
}

bool ConsumeSharedCombatEnemyDamage(Actor* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags) {
    if (actor == nullptr) {
        return false;
    }
    switch (actor->id) {
        case ACTOR_EN_RD:
            return HyruleCoop_EnRdConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_MB:
            return HyruleCoop_EnMbConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_BB:
            return HyruleCoop_EnBbConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_TEST:
            return HyruleCoop_EnTestConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_SW:
            return HyruleCoop_EnSwConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_ST:
            return HyruleCoop_EnStConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_SSH:
            return HyruleCoop_EnSshConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_POH:
            return HyruleCoop_EnPohConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_PO_SISTERS:
            return HyruleCoop_EnPoSistersConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_WF:
            return HyruleCoop_EnWfConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        case ACTOR_BOSS_GANONDROF:
            return HyruleCoop_BossGanondrofConsumeDamage(actor, damageEffect, damage, damageFlags) != 0;
        default:
            return false;
    }
}

bool ApplySharedCombatEnemyDamage(Actor* actor, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags,
                                  bool authoritativeReplay = false) {
    if (actor == nullptr || gPlayState == nullptr) {
        return false;
    }
    switch (actor->id) {
        case ACTOR_EN_RD:
            return HyruleCoop_EnRdApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_MB:
            return HyruleCoop_EnMbApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_BB:
            return HyruleCoop_EnBbApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_TEST:
            return HyruleCoop_EnTestApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_SW:
            return HyruleCoop_EnSwApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_ST:
            return HyruleCoop_EnStApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_SSH:
            return HyruleCoop_EnSshApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_POH:
            return HyruleCoop_EnPohApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_PO_SISTERS:
            return HyruleCoop_EnPoSistersApplyDamage(actor, gPlayState, damageEffect, damage, damageFlags) != 0;
        case ACTOR_EN_WF:
            return (authoritativeReplay ? HyruleCoop_EnWfReplayDamage(actor, gPlayState, damageEffect, damage,
                                                                      damageFlags)
                                        : HyruleCoop_EnWfApplyDamage(actor, gPlayState, damageEffect, damage,
                                                                     damageFlags)) != 0;
        case ACTOR_BOSS_GANONDROF:
            return (authoritativeReplay ? HyruleCoop_BossGanondrofReplayDamage(actor, gPlayState, damageEffect,
                                                                                damage, damageFlags)
                                        : HyruleCoop_BossGanondrofApplyDamage(actor, gPlayState, damageEffect,
                                                                               damage, damageFlags)) != 0;
        default:
            return false;
    }
}

ActorSnapshotMessage CaptureSharedCombatEnemySnapshot(const Actor* actor, int16_t scene, uint32_t hostTick,
                                                      SessionScope scope, bool alive, uint32_t outcomeSequence,
                                                      uint8_t damageEffect, uint8_t damage, uint32_t damageFlags) {
    ActorSnapshotMessage message = CaptureGenericEnemySnapshot(actor, scene, hostTick, scope, alive);
    message.adapterWordCount = kSharedCombatEnemyAdapterWordCount;
    message.adapterState[SharedCombatEnemyOutcomeSequenceLow] = static_cast<int16_t>(outcomeSequence & 0xFFFF);
    message.adapterState[SharedCombatEnemyOutcomeSequenceHigh] = static_cast<int16_t>(outcomeSequence >> 16);
    message.adapterState[SharedCombatEnemyDamageEffect] = damageEffect;
    message.adapterState[SharedCombatEnemyDamage] = damage;
    message.adapterState[SharedCombatEnemyDamageFlagsLow] = static_cast<int16_t>(damageFlags & 0xFFFF);
    message.adapterState[SharedCombatEnemyDamageFlagsHigh] = static_cast<int16_t>(damageFlags >> 16);
    return message;
}

bool DecodeSharedCombatEnemyOutcome(const ActorSnapshotMessage& message, uint32_t* outcomeSequence,
                                    uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags) {
    if (message.adapterWordCount != kSharedCombatEnemyAdapterWordCount || outcomeSequence == nullptr ||
        damageEffect == nullptr || damage == nullptr || damageFlags == nullptr) {
        return false;
    }
    *outcomeSequence = static_cast<uint16_t>(message.adapterState[SharedCombatEnemyOutcomeSequenceLow]) |
                       (static_cast<uint32_t>(static_cast<uint16_t>(
                            message.adapterState[SharedCombatEnemyOutcomeSequenceHigh]))
                        << 16);
    *damageEffect = static_cast<uint8_t>(message.adapterState[SharedCombatEnemyDamageEffect]);
    *damage = static_cast<uint8_t>(message.adapterState[SharedCombatEnemyDamage]);
    *damageFlags = static_cast<uint16_t>(message.adapterState[SharedCombatEnemyDamageFlagsLow]) |
                   (static_cast<uint32_t>(static_cast<uint16_t>(message.adapterState[SharedCombatEnemyDamageFlagsHigh]))
                    << 16);
    return true;
}

bool IsNewSharedCombatEnemyOutcome(uint32_t appliedSequence, uint32_t incomingSequence) {
    return ShouldApplySharedEnemyHostOutcome(appliedSequence, incomingSequence);
}

bool ApplyStalchildSnapshot(Actor* actor, const ActorSnapshotMessage& message, bool snapTransform) {
    if (actor == nullptr || actor->id != ACTOR_EN_SKB ||
        !IsValidStalchildAdapterState(message.adapterWordCount, message.adapterState.data()) ||
        DecodeStalchildTarget(message.stateId) !=
            static_cast<StalchildTarget>(message.adapterState[kStalchildAdapterTarget])) {
        return false;
    }

    const Vec3f previousPosition = actor->world.pos;
    const Vec3s previousWorldRotation = actor->world.rot;
    const Vec3s previousShapeRotation = actor->shape.rot;
    ActorSnapshotMessage common = message;
    common.adapterWordCount = 0;
    common.adapterState = {};
    if (!ApplyGenericEnemySnapshot(actor, common)) {
        return false;
    }

    EnSkb_ApplyCoopState(reinterpret_cast<EnSkb*>(actor),
                         static_cast<uint8_t>(message.adapterState[kStalchildAdapterBehavior]),
                         static_cast<uint8_t>(message.adapterState[kStalchildAdapterAttackActive]),
                         message.animationFrame, message.animationSpeed,
                         static_cast<float>(message.adapterState[kStalchildAdapterShapeYOffset]),
                         static_cast<float>(message.adapterState[kStalchildAdapterShadowScale]) /
                             kStalchildShadowScalePrecision);

    const float targetPosition[3] = { message.position[0], message.position[1], message.position[2] };
    const float priorPosition[3] = { previousPosition.x, previousPosition.y, previousPosition.z };
    if (!snapTransform && DistanceSquared(targetPosition, priorPosition) <= SQ(600.0f)) {
        constexpr float kReplicaBlend = 0.45f;
        actor->world.pos.x = previousPosition.x + (message.position[0] - previousPosition.x) * kReplicaBlend;
        actor->world.pos.y = previousPosition.y + (message.position[1] - previousPosition.y) * kReplicaBlend;
        actor->world.pos.z = previousPosition.z + (message.position[2] - previousPosition.z) * kReplicaBlend;
        actor->world.rot.x = InterpolateRotation(previousWorldRotation.x, message.worldRotation[0], kReplicaBlend);
        actor->world.rot.y = InterpolateRotation(previousWorldRotation.y, message.worldRotation[1], kReplicaBlend);
        actor->world.rot.z = InterpolateRotation(previousWorldRotation.z, message.worldRotation[2], kReplicaBlend);
        actor->shape.rot.x = InterpolateRotation(previousShapeRotation.x, message.shapeRotation[0], kReplicaBlend);
        actor->shape.rot.y = InterpolateRotation(previousShapeRotation.y, message.shapeRotation[1], kReplicaBlend);
        actor->shape.rot.z = InterpolateRotation(previousShapeRotation.z, message.shapeRotation[2], kReplicaBlend);
        actor->prevPos = previousPosition;
    } else {
        actor->world.pos = { message.position[0], message.position[1], message.position[2] };
        actor->world.rot = { message.worldRotation[0], message.worldRotation[1], message.worldRotation[2] };
        actor->shape.rot = { message.shapeRotation[0], message.shapeRotation[1], message.shapeRotation[2] };
        actor->prevPos = actor->world.pos;
    }
    return true;
}

bool DamageGenericEnemy(Actor* actor, int16_t damage) {
    if (actor == nullptr || damage <= 0 || actor->colChkInfo.health <= 0) {
        return false;
    }
    actor->colChkInfo.health = std::max<int16_t>(0, actor->colChkInfo.health - damage);
    if (actor->colChkInfo.health == 0) {
        Actor_Kill(actor);
        return false;
    }
    return true;
}

void ClearLocalTarget(Actor* actor) {
    if (gPlayState == nullptr || actor == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player != nullptr) {
        if (player->autoLockOnActor == actor) {
            player->autoLockOnActor = nullptr;
        }
        if (player->focusActor == actor) {
            Player_ClearZTargeting(player);
        }
    }
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
    automatedTestRequireDraw =
        std::getenv("HYRULE_COOP_TEST_REQUIRE_DRAW") != nullptr &&
        std::string(std::getenv("HYRULE_COOP_TEST_REQUIRE_DRAW")) == "1";

    automatedTestEnabled = true;
    automatedTestClient = requestedRole == "client";
    automatedTestSaveRequested = false;
    automatedTestSaveCompleted.store(false);
    automatedTestHostReconnectResourcesCaptured = false;
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
    DestroyRemoteHorse();
    DestroyRemotePlayer();
    RegisterHooks(false);
    transport.Stop();
    HyruleCoop_DampeRaceSetLocalAuthority(1);
    ResetSessionState();
    phase = ConnectionPhase::Idle;
}

void Manager::Update() {
    frameCounter++;
    automatedTestTick++;
    HyruleCoop_DampeRaceSetLocalAuthority(
        transport.GetRole() != SessionRole::Client || !handshakeComplete ? 1 : 0);

    for (auto iterator = lastActorInteractionTick.begin(); iterator != lastActorInteractionTick.end();) {
        if (frameCounter - iterator->second <= 120) {
            ++iterator;
            continue;
        }
        pendingActorInteractionRequests.erase(iterator->first);
        iterator = lastActorInteractionTick.erase(iterator);
    }

    const bool saveLoaded = IsSaveLoaded();
    if (saveLoaded && !saveOverlayCaptured) {
        CaptureSaveOverlay();
    }
    if (saveLoaded && transport.GetRole() == SessionRole::Client && !clientProgressionBaselineCaptured) {
        lastObservedClientProgression = CaptureSharedProgression(&gSaveContext);
        clientProgressionBaselineCaptured = true;
    }
    if (saveLoaded && !observedSaveLoaded && handshakeComplete) {
        if (transport.GetRole() == SessionRole::Host) {
            BeginHandshakeBarrier();
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
    ReconcileClientDurableProgression();
    ApplyPendingClockSnapshot();
    UpdateStoryEvent();

    if (transportState == TransportState::Error) {
        DestroyRemoteHorse();
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
            DestroyRemoteHorse();
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

    if (handshakeComplete && transport.GetRole() == SessionRole::Host && frameCounter % 60 == 0 && IsSaveLoaded()) {
        SendProgressionSnapshot();
    }

    if (handshakeComplete) {
        UpdateTransportTelemetry();
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
    TransportTelemetry telemetry = transport.GetTelemetry();
    telemetry.realtimeBytesSentPerSecond = realtimeBytesSentPerSecond;
    telemetry.realtimeBytesReceivedPerSecond = realtimeBytesReceivedPerSecond;
    telemetry.tcpBytesSentPerSecond = tcpBytesSentPerSecond;
    telemetry.tcpBytesReceivedPerSecond = tcpBytesReceivedPerSecond;
    telemetry.realtimeDatagramsSentPerSecond = realtimeDatagramsSentPerSecond;
    telemetry.realtimeDatagramsReceivedPerSecond = realtimeDatagramsReceivedPerSecond;
    return telemetry;
}

void Manager::UpdateTransportTelemetry() {
    constexpr uint64_t kTelemetryIntervalMs = 5000;
    const uint64_t now = SDL_GetTicks64();
    if (transportTelemetrySampleAtMs != 0 && now - transportTelemetrySampleAtMs < kTelemetryIntervalMs) {
        return;
    }

    const TransportTelemetry current = transport.GetTelemetry();
    if (transportTelemetrySampleAtMs == 0) {
        transportTelemetrySampleAtMs = now;
        previousTransportTelemetry = current;
        return;
    }

    const uint64_t elapsedMs = now - transportTelemetrySampleAtMs;
    const auto rate = [elapsedMs](uint64_t currentValue, uint64_t previousValue) {
        return currentValue >= previousValue ? (currentValue - previousValue) * 1000 / elapsedMs : 0ULL;
    };
    realtimeBytesSentPerSecond = rate(current.realtimeBytesSent, previousTransportTelemetry.realtimeBytesSent);
    realtimeBytesReceivedPerSecond =
        rate(current.realtimeBytesReceived, previousTransportTelemetry.realtimeBytesReceived);
    tcpBytesSentPerSecond = rate(current.tcpBytesSent, previousTransportTelemetry.tcpBytesSent);
    tcpBytesReceivedPerSecond = rate(current.tcpBytesReceived, previousTransportTelemetry.tcpBytesReceived);
    realtimeDatagramsSentPerSecond =
        rate(current.realtimeDatagramsSent, previousTransportTelemetry.realtimeDatagramsSent);
    realtimeDatagramsReceivedPerSecond =
        rate(current.realtimeDatagramsReceived, previousTransportTelemetry.realtimeDatagramsReceived);
    const uint64_t retryRate =
        rate(current.acknowledgedEventRetries, previousTransportTelemetry.acknowledgedEventRetries);
    const uint64_t fallbackRate =
        rate(current.acknowledgedEventFallbacks, previousTransportTelemetry.acknowledgedEventFallbacks);

    SPDLOG_INFO(
        "[HyruleCoop] Network traffic: tx={} B/s (UDP {}, TCP {}), rx={} B/s (UDP {}, TCP {}), UDP packets "
        "tx={}/s rx={}/s, RTT={} ms jitter={} ms, queues reliable={} incoming={} pendingAcks={}, retries={}/s "
        "fallbacks={}/s",
        realtimeBytesSentPerSecond + tcpBytesSentPerSecond, realtimeBytesSentPerSecond, tcpBytesSentPerSecond,
        realtimeBytesReceivedPerSecond + tcpBytesReceivedPerSecond, realtimeBytesReceivedPerSecond,
        tcpBytesReceivedPerSecond, realtimeDatagramsSentPerSecond, realtimeDatagramsReceivedPerSecond,
        current.roundTripMs, current.roundTripJitterMs, current.reliableQueueDepth, current.incomingQueueDepth,
        current.pendingAcknowledgements, retryRate, fallbackRate);

    previousTransportTelemetry = current;
    transportTelemetrySampleAtMs = now;
}

const PlayerSnapshotMessage* Manager::GetRemotePlayerSnapshot() const {
    PlayerSnapshotMessage sampled;
    if (!remotePlayerInterpolator.Sample(SDL_GetTicks64(), sampled)) {
        return nullptr;
    }
    if (!IsSameTimeline(GetLocalTimelineScope(), SnapshotTimelineScope(sampled))) {
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

TimelineScope Manager::GetLocalTimelineScope() const {
    return { gPlayState == nullptr ? gSaveContext.linkAge : gPlayState->linkAgeOnLoad,
             loadedSceneLayer >= 0 ? loadedSceneLayer : static_cast<int16_t>(gSaveContext.sceneLayer) };
}

bool Manager::ShouldRegisterStalchildAttack(void* actor, bool nativeAttackActive) const {
    if (!handshakeComplete || actor == nullptr || gPlayState == nullptr ||
        gPlayState->sceneNum != SCENE_HYRULE_FIELD) {
        return nativeAttackActive;
    }

    const uint64_t entityId = stalchildIdentityRegistry.Find(actor);
    if (entityId == 0) {
        return transport.GetRole() != SessionRole::Client && nativeAttackActive;
    }
    if (automatedTestEnabled && automatedTestStage == TestStalchildCombat && automatedTestTargetEntityId != 0 &&
        entityId != automatedTestTargetEntityId) {
        // Hyrule Field can naturally keep two Stalchildren active. Isolate the deterministic combat proof so player
        // damage can only come from the exact host-owned identity whose target and collider policy are under test.
        return false;
    }
    if (transport.GetRole() == SessionRole::Host) {
        const auto target = stalchildTargets.find(entityId);
        return nativeAttackActive &&
               (target == stalchildTargets.end() || target->second == StalchildTarget::Host);
    }
    if (transport.GetRole() == SessionRole::Client) {
        const auto snapshot = actorSnapshots.find(entityId);
        return snapshot != actorSnapshots.end() &&
               IsValidStalchildAdapterState(snapshot->second.adapterWordCount,
                                            snapshot->second.adapterState.data()) &&
               DecodeStalchildTarget(snapshot->second.stateId) == StalchildTarget::Guest &&
               snapshot->second.adapterState[kStalchildAdapterAttackActive] != 0;
    }
    return nativeAttackActive;
}

bool Manager::ShouldProcessStalchildHit(void* actor, void* attacker) {
    if (!handshakeComplete || actor == nullptr || gPlayState == nullptr ||
        gPlayState->sceneNum != SCENE_HYRULE_FIELD) {
        return true;
    }

    const uint64_t entityId = stalchildIdentityRegistry.Find(actor);
    if (entityId == 0) {
        return transport.GetRole() != SessionRole::Client;
    }
    if (transport.GetRole() == SessionRole::Client) {
        return false;
    }
    if (attacker != nullptr && attacker == remotePlayer) {
        if (automatedTestEnabled && automatedTestStage == TestStalchildCombat &&
            entityId == automatedTestTargetEntityId) {
            ReportAutomatedTest("stalchild-host-remote-collider-suppressed",
                                "guest sword damage is committed through its attack intent only");
        }
        return false;
    }
    return true;
}

bool Manager::ShouldSuppressSharedEnemyLocalReward(void* actor) const {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || actor == nullptr) {
        return false;
    }
    return std::any_of(localSharedCombatEnemies.begin(), localSharedCombatEnemies.end(),
                       [actor](const auto& entry) { return entry.second == actor; });
}

bool Manager::SanitizeSaveCopy(void* saveContextRef) const {
    if (transport.GetRole() != SessionRole::Client) {
        return true;
    }
    if (!saveOverlayCaptured || saveContextRef == nullptr || originalSaveContext.size() != sizeof(SaveContext)) {
        SPDLOG_ERROR("[HyruleCoop] Refusing a guest save because the pre-join save overlay is unavailable");
        return false;
    }
    std::memcpy(saveContextRef, originalSaveContext.data(), sizeof(SaveContext));
    return true;
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

void Manager::PrepareRemoteHorse(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    remoteHorse = actor;
    actor->init = HyruleCoopRemoteHorse_Init;
    actor->update = HyruleCoopRemoteHorse_Update;
    actor->draw = HyruleCoopRemoteHorse_Draw;
    actor->destroy = HyruleCoopRemoteHorse_Destroy;
}

void Manager::NotifyRemoteHorseDestroyed(void* actor) {
    if (remoteHorse == actor) {
        remoteHorse = nullptr;
    }
}

void Manager::NotifyRemotePlayerPoseApplied(bool meleeActive) {
    if (!meleeActive || !automatedTestEnabled || automatedTestRemoteSwingRendered) {
        return;
    }
    if (automatedTestStage == TestGenericCombat || automatedTestStage == TestStalchildCombat) {
        automatedTestRemoteSwingRendered = true;
        const char* enemy = automatedTestStage == TestStalchildCombat ? "stalchild" : "generic-enemy";
        ReportAutomatedTest(std::string(enemy) +
                                (automatedTestClient ? "-host-swing-state-applied"
                                                     : "-client-swing-state-applied"),
                            automatedTestClient ? "remote Link applied the host sword state"
                                                : "remote Link applied the client sword state");
    } else if (automatedTestClient) {
        return;
    } else if (automatedTestStage == TestAttacking) {
        automatedTestRemoteSwingRendered = true;
        ReportAutomatedTest("deku-baba-remote-swing-state-applied", "remote Link applied the guest sword state");
    } else if (automatedTestStage == TestBossCombat) {
        automatedTestRemoteSwingRendered = true;
        ReportAutomatedTest("gohma-remote-swing-state-applied", "remote Link applied the guest sword state");
    }
}

void Manager::NotifyRemotePlayerDrawApplied(bool meleeActive, uint8_t currentMask) {
    if (!automatedTestEnabled || !automatedTestRequireDraw) {
        return;
    }
    if (!automatedTestRemotePresentationRendered && currentMask == PLAYER_MASK_BUNNY &&
        (automatedTestStage == TestAwaitingActors || automatedTestStage == TestAttacking)) {
        automatedTestRemotePresentationRendered = true;
        ReportAutomatedTest(automatedTestClient ? "bunny-hood-host-draw-completed"
                                                : "bunny-hood-client-draw-completed",
                            "Player_Draw completed with Bunny Hood state");
    }
    if (!meleeActive || automatedTestRemoteSwingDrawn) {
        return;
    }
    if (automatedTestStage == TestGenericCombat || automatedTestStage == TestStalchildCombat) {
        automatedTestRemoteSwingDrawn = true;
        const char* enemy = automatedTestStage == TestStalchildCombat ? "stalchild" : "generic-enemy";
        ReportAutomatedTest(std::string(enemy) +
                                (automatedTestClient ? "-host-swing-draw-completed"
                                                     : "-client-swing-draw-completed"),
                            "Player_Draw completed with the remote sword state");
    } else if (!automatedTestClient && automatedTestStage == TestAttacking) {
        automatedTestRemoteSwingDrawn = true;
        ReportAutomatedTest("deku-baba-remote-swing-draw-completed",
                            "Player_Draw completed with the guest sword state");
    } else if (!automatedTestClient && automatedTestStage == TestBossCombat) {
        automatedTestRemoteSwingDrawn = true;
        ReportAutomatedTest("gohma-remote-swing-draw-completed",
                            "Player_Draw completed with the guest sword state");
    }
}

void Manager::NotifyRemotePlayerMapPositionRead(int16_t scene) {
    if (!automatedTestEnabled || automatedTestRemoteMapPositionRead || scene != SCENE_KOKIRI_FOREST) {
        return;
    }
    automatedTestRemoteMapPositionRead = true;
    ReportAutomatedTest("minimap-remote-position-read",
                        "minimap draw consumed the same-scene remote player coordinates");
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
    COND_HOOK(OnVanillaBehavior, enabled,
              [this](GIVanillaBehavior id, bool* should, va_list originalArgs) {
                  if (should == nullptr) {
                      return;
                  }
                  if (id == VB_PLAY_PULL_MASTER_SWORD_CS && coordinatedMasterSwordPullActive) {
                      // The peer may publish the durable pulled-sword flag before this local replay reaches its
                      // terminator. Keep the coordinated participant on the first-pull branch anyway.
                      *should = true;
                      return;
                  }
                  if (applyingAuthoritativeState || !*should || !IsSaveLoaded()) {
                      return;
                  }
                  if (id == VB_PLAY_DOOR_OF_TIME_CS) {
                      doorOfTimeOpeningPresented = true;
                      if (handshakeComplete) {
                          NotifyLocalStoryEvent(StoryEventKind::DoorOfTimeOpening);
                      }
                  } else if (id == VB_PLAY_ENTRANCE_CS) {
                      va_list args;
                      va_copy(args, originalArgs);
                      const s32 entranceFlag = va_arg(args, s32);
                      va_end(args);
                      if (entranceFlag == EVENTCHKINF_ENTERED_MASTER_SWORD_CHAMBER) {
                          masterSwordEntrancePresented = true;
                          if (handshakeComplete) {
                              NotifyLocalStoryEvent(StoryEventKind::MasterSwordChamberEntrance);
                          }
                      }
                  }
              });
    COND_HOOK(OnSaveFile, enabled && automatedTestEnabled, [this](int32_t fileNum, int32_t sectionId) {
        if (fileNum == 0 && sectionId == SECTION_ID_BASE && automatedTestSaveRequested) {
            automatedTestSaveCompleted.store(true);
        }
    });
    COND_HOOK(OnSceneSpawnActors, enabled, [this]() {
        // Save-overlay restoration protects the guest's permanent file, but it
        // must not redefine the scene header that is already loaded in memory.
        loadedSceneLayer = static_cast<int16_t>(gSaveContext.sceneLayer);
        remotePlayer = nullptr;
        remoteHorse = nullptr;
        preparingRemoteHorse = false;
        localDekuBabas.clear();
        localGohmas.clear();
        localGohmaDeathPresentations.clear();
        localGenericEnemies.clear();
        localSharedCombatEnemies.clear();
        sharedCombatEnemyOutcomes.clear();
        appliedSharedCombatEnemyOutcomeSequences.clear();
        localPushBlocks.clear();
        localDampeRaceActors.clear();
        pendingActorInteractionRequests.clear();
        lastActorInteractionTick.clear();
        localJabuActors.clear();
        localBarinadeActors.clear();
        ClearStalchildSceneState();
        if (gPlayState != nullptr && gPlayState->sceneNum != SCENE_TEMPLE_OF_TIME) {
            coordinatedMasterSwordPullActive = false;
        }
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
        RefreshRemoteHorse();
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
    COND_ID_HOOK(ShouldActorInit, ACTOR_EN_HORSE, enabled, [this](void* actor, bool*) {
        if (preparingRemoteHorse) {
            PrepareRemoteHorse(actor);
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
        UpdateGenericGuestAttack();
        if (remotePlayer == nullptr) {
            RefreshRemotePlayer();
        }
        RefreshRemoteHorse();
    });
    COND_HOOK(OnSceneFlagSet, enabled, [this](int16_t scene, int16_t flagType, int16_t flag) {
        if (applyingAuthoritativeState || !IsValidReplicatedSceneFlag(scene, flagType, flag)) {
            return;
        }
        if (transport.GetRole() == SessionRole::Host) {
            sceneRevisions.Advance(SceneStreamId(scene));
            if (flagType == FLAG_SCENE_COLLECTIBLE && !IsEphemeralSceneFlag(scene, flagType, flag)) {
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
                if (flagType == FLAG_SCENE_COLLECTIBLE && !IsEphemeralSceneFlag(scene, flagType, flag)) {
                    SendProgressionSnapshot();
                }
            }
        } else if (handshakeComplete) {
            if (flagType == FLAG_SCENE_COLLECTIBLE && !IsEphemeralSceneFlag(scene, flagType, flag)) {
                SendCollectibleIntent(scene, flagType, flag);
            } else {
                SendSceneFlagIntent(scene, flagType, flag, true);
            }
        }
    });
    COND_HOOK(OnSceneFlagUnset, enabled, [this](int16_t scene, int16_t flagType, int16_t flag) {
        if (applyingAuthoritativeState || !IsValidReplicatedSceneFlag(scene, flagType, flag)) {
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
            if (handshakeComplete && flagType == FLAG_EVENT_CHECK_INF &&
                flag == EVENTCHKINF_ZELDA_FLED_HYRULE_CASTLE) {
                pendingStoryEvent = StoryEventKind::CastleEscape;
            }
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
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_ENCOUNT1, enabled,
                 [this](void* actor, bool* shouldUpdate) { ApplyStalchildSpawnerAuthority(actor, shouldUpdate); });
    COND_ID_HOOK(OnActorInit, ACTOR_EN_SKB, enabled, [this](void* actor) { HandleStalchildInitialized(actor); });
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_SKB, enabled, [this](void* actor) { UpdateStalchild(actor); });
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_SKB, enabled,
                 [this](void* actor, bool* shouldUpdate) { ApplyStalchildAuthority(actor, shouldUpdate); });
    COND_ID_HOOK(OnActorKill, ACTOR_EN_SKB, enabled, [this](void* actor) {
        if (transport.GetRole() == SessionRole::Host) {
            if (automatedTestEnabled && automatedTestStage == TestStalchildCombat && actor != nullptr &&
                automatedTestCombatPhase == CombatFirstSwing && automatedTestTargetObserved &&
                automatedTestSwingObserved && automatedTestTargetActor == actor) {
                ++automatedTestPhysicalHits;
                ReportAutomatedTest("stalchild-host-physical-collision",
                                    "hit=" + std::to_string(automatedTestPhysicalHits));
            }
            if (automatedTestEnabled && automatedTestStage == TestStalchildCombat && actor != nullptr &&
                automatedTestTargetActor == actor && automatedTestStalchildDeathTransitionObserved) {
                ReportAutomatedTest("stalchild-native-death-complete",
                                    "native dying action reached its terminal Actor_Kill");
            }
            SendStalchildSnapshot(actor, false);
        }
    });
    COND_ID_HOOK(OnActorDestroy, ACTOR_EN_SKB, enabled, [this](void* actor) { ForgetStalchild(actor); });
    COND_ID_HOOK(OnActorInit, ACTOR_EN_ITEM00, enabled, [this](void* actorRef) {
        if (!automatedTestEnabled || automatedTestClient || automatedTestStage != TestStalchildCombat ||
            automatedTestTargetActor == nullptr || actorRef == nullptr || automatedTestStalchildDropObserved) {
            return;
        }
        const Actor* item = static_cast<const Actor*>(actorRef);
        const Actor* target = static_cast<const Actor*>(automatedTestTargetActor);
        const float deltaX = item->world.pos.x - target->world.pos.x;
        const float deltaY = item->world.pos.y - target->world.pos.y;
        const float deltaZ = item->world.pos.z - target->world.pos.z;
        if (SQ(deltaX) + SQ(deltaY) + SQ(deltaZ) <= SQ(180.0f)) {
            automatedTestStalchildDropObserved = true;
            ReportAutomatedTest("stalchild-native-drop-spawned",
                                "native death path created a nearby collectible");
        }
    });
    COND_HOOK(OnActorUpdate, enabled, [this](void* actor) {
        ReconcileAuthoritativeSceneActor(actor);
        UpdateBarinade(actor);
        UpdateJabuActor(actor);
        UpdateGenericEnemy(actor);
        UpdateSharedCombatEnemy(actor);
        UpdatePushBlock(actor);
        UpdateDampeRaceActor(actor);
    });
    COND_HOOK(ShouldActorUpdate, enabled, [this](void* actor, bool* shouldUpdate) {
        ApplyBarinadeAuthority(actor, shouldUpdate);
        ApplyJabuActorAuthority(actor, shouldUpdate);
        ApplyGenericEnemyAuthority(actor, shouldUpdate);
        ApplySharedCombatEnemyAuthority(actor, shouldUpdate);
    });
    COND_HOOK(OnActorKill, enabled, [this](void* actor) {
        if (transport.GetRole() == SessionRole::Host) {
            if (automatedTestEnabled && automatedTestStage == TestGenericCombat && actor != nullptr &&
                static_cast<Actor*>(actor)->id == ACTOR_EN_FIREFLY && gPlayState != nullptr) {
                if (automatedTestCombatPhase == CombatFirstSwing && automatedTestTargetObserved &&
                    automatedTestSwingObserved && automatedTestTargetActor == actor) {
                    ++automatedTestPhysicalHits;
                    ReportAutomatedTest("generic-enemy-host-physical-collision",
                                        "hit=" + std::to_string(automatedTestPhysicalHits));
                }
            }
            SendBarinadeSnapshot(actor, false);
            SendJabuActorSnapshot(actor, false);
            SendGenericEnemySnapshot(actor, false);
            SendSharedCombatEnemySnapshot(actor, false);
        }
    });
    COND_HOOK(OnActorDestroy, enabled, [this](void* actor) {
        ForgetBarinade(actor);
        ForgetJabuActor(actor);
        ForgetGenericEnemy(actor);
        ForgetSharedCombatEnemy(actor);
        ForgetPushBlock(actor);
        ForgetDampeRaceActor(actor);
    });
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_KZ, enabled, [this](void* actor) { ReconcileKingZora(actor); });
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_SPOT08_BAKUDANKABE, enabled,
                 [this](void* actor, bool*) { ReconcileZorasFountainBombableWall(actor); });
    COND_ID_HOOK(OnActorUpdate, ACTOR_DEMO_KANKYO, enabled, [this](void* actor) { ReconcileDoorOfTime(actor); });
    COND_ID_HOOK(OnActorUpdate, ACTOR_BG_TOKI_SWD, enabled,
                 [this](void* actor) { ReconcileMasterSwordChamber(actor); });
}

void Manager::ResetPeerState() {
    DestroyRemoteHorse();
    DestroyRemotePlayer();
    transport.DisableRealtime();
    helloSent = false;
    handshakeComplete = false;
    pendingHandshakeBarrierKind = BarrierKind::ReconnectSnapshot;
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
    preparingRemoteHorse = false;
    applyingAuthoritativeState = false;
    negotiatedCapabilities.clear();
    pendingGuestAttacks.clear();
    pendingGuestAttackRequests.clear();
    lastGuestAttackTick.clear();
    pendingActorInteractionRequests.clear();
    lastActorInteractionTick.clear();
    localGenericEnemies.clear();
    localSharedCombatEnemies.clear();
    sharedCombatEnemyOutcomes.clear();
    appliedSharedCombatEnemyOutcomeSequences.clear();
    localPushBlocks.clear();
    localDampeRaceActors.clear();
    localJabuActors.clear();
    localBarinadeActors.clear();
    localStalchildren.clear();
    retiredStalchildren.clear();
    stalchildIdentityRegistry.Clear();
    stalchildSnapshotLifecycle.Clear();
    spawningReplicatedStalchild = false;
    genericGuestTargetsHitThisSwing.clear();
    transportTelemetrySampleAtMs = 0;
    previousTransportTelemetry = {};
    realtimeBytesSentPerSecond = 0;
    realtimeBytesReceivedPerSecond = 0;
    tcpBytesSentPerSecond = 0;
    tcpBytesReceivedPerSecond = 0;
    realtimeDatagramsSentPerSecond = 0;
    realtimeDatagramsReceivedPerSecond = 0;
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
    originalSaveContext.clear();
    originalProgression = {};
    canonicalProgression = {};
    canonicalProgressionCaptured = false;
    lastObservedClientProgression = {};
    clientProgressionBaselineCaptured = false;
    pendingClientEventFlags.clear();
    localDekuBabas.clear();
    localGohmas.clear();
    localGohmaDeathPresentations.clear();
    localSharedCombatEnemies.clear();
    sharedCombatEnemyOutcomes.clear();
    appliedSharedCombatEnemyOutcomeSequences.clear();
    localPushBlocks.clear();
    localDampeRaceActors.clear();
    pendingActorInteractionRequests.clear();
    lastActorInteractionTick.clear();
    localJabuActors.clear();
    localBarinadeActors.clear();
    ClearStalchildSessionState();
    actorSnapshots.clear();
    lastAppliedStoryOperationEpoch = 0;
    pendingClockSnapshot.reset();
    pendingStoryEvent = StoryEventKind::None;
    activeStoryEvent = StoryEventKind::None;
    storyCutsceneObserved = false;
    storyPresentationBaselineApplied = false;
    doorOfTimeOpeningPresented = false;
    masterSwordEntrancePresented = false;
    masterSwordPullPresented = false;
    coordinatedMasterSwordPullActive = false;
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
        case MessageType::StoryEventIntent:
            HandleStoryEventIntent(packet);
            break;
        case MessageType::StoryEventCommand:
            HandleStoryEventCommand(packet);
            break;
        case MessageType::ActorInteractionIntent:
            HandleActorInteractionIntent(packet);
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
    pendingHandshakeBarrierKind = HandshakeBarrierKind(requestedCurrentSession);
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
        BeginHandshakeBarrier();
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
    pendingClockSnapshot = *message;
    ApplyPendingClockSnapshot();
}

void Manager::ApplyPendingClockSnapshot() {
    if (!pendingClockSnapshot.has_value() || gPlayState == nullptr) {
        return;
    }

    // Changing the world clock while an ocarina/message sequence owns actor and
    // player state can invalidate the active interaction. Keep only the newest
    // host value and apply it as soon as normal gameplay resumes.
    const bool ocarinaActive = gPlayState->msgCtx.ocarinaMode != OCARINA_MODE_00 &&
                               gPlayState->msgCtx.ocarinaMode != OCARINA_MODE_04;
    if (!ClockSnapshotMayApply(gPlayState->msgCtx.msgMode != MSGMODE_NONE, ocarinaActive,
                               gPlayState->csCtx.state != CS_STATE_IDLE, Player_InCsMode(gPlayState))) {
        return;
    }

    gSaveContext.dayTime = pendingClockSnapshot->dayTime;
    gSaveContext.skyboxTime = pendingClockSnapshot->skyboxTime;
    gSaveContext.nightFlag = pendingClockSnapshot->night;
    gTimeSpeed = pendingClockSnapshot->timeSpeed;
    pendingClockSnapshot.reset();
}

void Manager::HandlePlayerSnapshot(const Packet& packet) {
    if (!handshakeComplete) {
        return;
    }
    auto message = DecodePlayerSnapshot(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) ||
        !IsValidTimelineScope(SnapshotTimelineScope(*message)) ||
        message->modelGroup >= PLAYER_MODELGROUP_MAX || message->currentMask >= PLAYER_MASK_MAX) {
        SPDLOG_WARN("[HyruleCoop] Ignoring invalid player snapshot");
        return;
    }

    if (message->meleeWeaponState > 0) {
        lastRemoteMeleeTick = message->tick;
        lastRemoteMeleeScene = message->scene;
        if (automatedTestEnabled && !automatedTestRemoteSwingObserved) {
            if (!automatedTestClient && automatedTestStage == TestAttacking) {
                automatedTestRemoteSwingObserved = true;
                ReportAutomatedTest("deku-baba-remote-swing-visible",
                                    "guest melee state reached the host player stream");
            } else if (!automatedTestClient && automatedTestStage == TestBossCombat) {
                automatedTestRemoteSwingObserved = true;
                ReportAutomatedTest("gohma-remote-swing-visible",
                                    "guest melee state reached the host player stream");
            } else if (automatedTestStage == TestGenericCombat) {
                automatedTestRemoteSwingObserved = true;
                ReportAutomatedTest(automatedTestClient ? "generic-enemy-host-swing-visible"
                                                        : "generic-enemy-client-swing-visible",
                                    automatedTestClient ? "host melee state reached the guest player stream"
                                                        : "client melee state reached the host player stream");
            }
        }
    }

    const bool needsRespawn = !remotePlayerSnapshot.has_value() ||
                              remotePlayerSnapshot->linkAge != message->linkAge ||
                              remotePlayerSnapshot->sceneLayer != message->sceneLayer;
    if (remotePlayerPresentation.has_value()) {
        ApplyPresentation(*message, *remotePlayerPresentation);
    }
    remotePlayerSnapshot = message;
    remotePlayerInterpolator.Push(*message, SDL_GetTicks64());
    if (needsRespawn) {
        DestroyRemotePlayer();
        RefreshRemotePlayer();
    }
    RefreshRemoteHorse();
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
        !IsValidReplicatedSceneFlag(message->scene, message->flagType, message->flag) ||
        (IsEphemeralSceneFlag(message->scene, message->flagType, message->flag) &&
         (gPlayState == nullptr || gPlayState->sceneNum != message->scene))) {
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
        gPlayState->actorCtx.flags.tempSwch = message->tempSwitches;
        gPlayState->actorCtx.flags.clear = message->clear;
        gPlayState->actorCtx.flags.tempClear = message->tempClear;
        gPlayState->actorCtx.flags.collect = message->collectible;
        gPlayState->actorCtx.flags.tempCollect = message->tempCollectible;
    }
}

void Manager::HandleActorSnapshot(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeActorSnapshot(packet.payload);
    if (!message.has_value() || message->entityId == 0 || !IsCurrentScope(message->scope) || message->scene < 0 ||
        message->scene >= SCENE_ID_MAX) {
        return;
    }
    // Actor state is scoped to the sender's timeline. A child/adult or day/night split must leave local actors
    // private even if both players happen to occupy the same scene number and room.
    if (!IsRemoteTimelineCompatible()) {
        return;
    }
    if (message->actorId == ACTOR_EN_SKB && !IsDynamicStalchildEntityId(message->entityId)) {
        return;
    }
    const bool stalchildSnapshot = message->actorId == ACTOR_EN_SKB && IsDynamicStalchildEntityId(message->entityId) &&
                                   message->scene == SCENE_HYRULE_FIELD && message->room == 0;
    const bool jabuSnapshot = IsJabuActorId(message->actorId) &&
                              message->adapterWordCount == kJabuActorAdapterWordCount;
    const bool barinadeSnapshot = message->actorId == ACTOR_BOSS_VA &&
                                  message->adapterWordCount == kBarinadeAdapterWordCount;
    const bool sharedCombatSnapshot = IsSharedCombatEnemyActorId(message->actorId) &&
                                      message->adapterWordCount == kSharedCombatEnemyAdapterWordCount;
    const bool pushBlockSnapshot = message->actorId == ACTOR_OBJ_OSHIHIKI && message->adapterWordCount == 0;
    const bool dampeRaceSnapshot =
        (message->actorId == ACTOR_EN_PO_RELAY && message->adapterWordCount == kDampeGhostAdapterWordCount) ||
        (message->actorId == ACTOR_BG_RELAY_OBJECTS && message->adapterWordCount == kDampeDoorAdapterWordCount);
    const bool specializedSnapshot = message->actorId == ACTOR_EN_DEKUBABA || message->actorId == ACTOR_BOSS_GOMA ||
                                     stalchildSnapshot || jabuSnapshot || barinadeSnapshot || sharedCombatSnapshot ||
                                     pushBlockSnapshot || dampeRaceSnapshot;
    const bool genericBaselineSnapshot = message->adapterWordCount == 0 && !specializedSnapshot &&
                                         IsGenericEnemyAdapterActorId(message->actorId);
    if (!specializedSnapshot && !genericBaselineSnapshot) {
        return;
    }
    if (message->acknowledgedRequestId != 0) {
        const auto pending = pendingGuestAttackRequests.find(message->entityId);
        if (pending != pendingGuestAttackRequests.end() && pending->second == message->acknowledgedRequestId) {
            ClearPendingGuestAttack(message->entityId);
        }
        const auto interaction = pendingActorInteractionRequests.find(message->entityId);
        if (interaction != pendingActorInteractionRequests.end() &&
            interaction->second == message->acknowledgedRequestId) {
            ClearPendingActorInteraction(message->entityId);
        }
    }
    const auto existing = actorSnapshots.find(message->entityId);
    if (automatedTestEnabled && message->actorId == ACTOR_EN_DEKUBABA &&
        automatedTestStage == TestAttacking &&
        (message->acknowledgedRequestId != 0 || existing == actorSnapshots.end() ||
         existing->second.health != message->health || existing->second.alive != message->alive)) {
        ReportAutomatedTest("deku-baba-snapshot-received",
                            "health=" + std::to_string(message->health) +
                                " alive=" + std::to_string(message->alive) +
                                " request=" + std::to_string(message->acknowledgedRequestId) +
                                " pending=" + std::to_string(pendingGuestAttacks.contains(message->entityId)));
    }
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
            genericGuestTargetsHitThisSwing.erase(message->entityId);
            localDekuBabas.erase(local);
            return;
        }
        ApplyDekuBabaSnapshot(local->second, *message);
    } else if (message->actorId == ACTOR_BOSS_GOMA) {
        const auto local = localGohmas.find(message->entityId);
        if (local == localGohmas.end()) {
            return;
        }
        if (!message->alive) {
            Actor_Kill(static_cast<Actor*>(local->second));
            ClearPendingGuestAttack(message->entityId);
            lastGuestAttackTick.erase(message->entityId);
            genericGuestTargetsHitThisSwing.erase(message->entityId);
            localGohmas.erase(local);
            localGohmaDeathPresentations.erase(message->entityId);
            return;
        }
        if (message->health <= 0 && message->stateId == 2) {
            if (localGohmaDeathPresentations.insert(message->entityId).second) {
                if (StartGohmaDefeatPresentation(local->second, gPlayState, *message)) {
                    SPDLOG_INFO("[HyruleCoop] Starting local Gohma defeat presentation for entity {}",
                                message->entityId);
                } else {
                    localGohmaDeathPresentations.erase(message->entityId);
                }
            }
        } else {
            ApplyGohmaSnapshot(local->second, *message);
        }
    } else if (stalchildSnapshot) {
        if (!message->alive) {
            const auto local = localStalchildren.find(message->entityId);
            if (local != localStalchildren.end()) {
                Actor* actor = static_cast<Actor*>(local->second);
                ClearLocalTarget(actor);
                Actor_Kill(actor);
                localStalchildren.erase(local);
            }
            ClearPendingGuestAttack(message->entityId);
            lastGuestAttackTick.erase(message->entityId);
            genericGuestTargetsHitThisSwing.erase(message->entityId);
            if (guestStalchildTargetThisSwing == message->entityId) {
                guestStalchildTargetThisSwing.reset();
            }
            stalchildTargets.erase(message->entityId);
            stalchildAttackSequences.erase(message->entityId);
            appliedStalchildAttackSequences.erase(message->entityId);
            return;
        }
        EnsureRemoteStalchild(*message);
    } else if (jabuSnapshot) {
        const auto local = localJabuActors.find(message->entityId);
        if (local == localJabuActors.end()) {
            return;
        }
        Actor* actor = static_cast<Actor*>(local->second);
        if (!message->alive) {
            ClearLocalTarget(actor);
            Actor_Kill(actor);
            ClearPendingGuestAttack(message->entityId);
            lastGuestAttackTick.erase(message->entityId);
            genericGuestTargetsHitThisSwing.erase(message->entityId);
            localJabuActors.erase(local);
            return;
        }
        ApplyJabuActorSnapshot(actor, *message);
    } else if (barinadeSnapshot) {
        const auto local = localBarinadeActors.find(message->entityId);
        if (local == localBarinadeActors.end()) {
            return;
        }
        Actor* actor = static_cast<Actor*>(local->second);
        if (!message->alive) {
            ClearLocalTarget(actor);
            Actor_Kill(actor);
            ClearPendingGuestAttack(message->entityId);
            lastGuestAttackTick.erase(message->entityId);
            genericGuestTargetsHitThisSwing.erase(message->entityId);
            localBarinadeActors.erase(local);
            return;
        }
        ApplyBarinadeSnapshot(actor, *message);
    } else if (sharedCombatSnapshot) {
        const auto local = localSharedCombatEnemies.find(message->entityId);
        if (local == localSharedCombatEnemies.end()) {
            return;
        }
        Actor* actor = static_cast<Actor*>(local->second);
        if (!message->alive) {
            ClearLocalTarget(actor);
            Actor_Kill(actor);
            ClearPendingGuestAttack(message->entityId);
            lastGuestAttackTick.erase(message->entityId);
            genericGuestTargetsHitThisSwing.erase(message->entityId);
            sharedCombatEnemyOutcomes.erase(message->entityId);
            appliedSharedCombatEnemyOutcomeSequences.erase(message->entityId);
            localSharedCombatEnemies.erase(local);
            return;
        }
    } else if (pushBlockSnapshot) {
        const auto local = localPushBlocks.find(message->entityId);
        if (local != localPushBlocks.end()) {
            ApplyPushBlockSnapshot(static_cast<Actor*>(local->second), *message);
        }
    } else if (dampeRaceSnapshot) {
        const auto local = localDampeRaceActors.find(message->entityId);
        if (local != localDampeRaceActors.end()) {
            ApplyDampeRaceSnapshot(static_cast<Actor*>(local->second), *message);
        }
    } else {
        const auto local = localGenericEnemies.find(message->entityId);
        if (local == localGenericEnemies.end()) {
            return;
        }
        Actor* actor = static_cast<Actor*>(local->second);
        if (!message->alive) {
            ClearLocalTarget(actor);
            Actor_Kill(actor);
            ClearPendingGuestAttack(message->entityId);
            lastGuestAttackTick.erase(message->entityId);
            localGenericEnemies.erase(local);
            return;
        }
        ApplyGenericEnemySnapshot(actor, *message);
    }
}

void Manager::HandleBarrierSnapshot(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeBarrierSnapshot(packet.payload);
    const bool currentScope = message.has_value() && IsCurrentScope(message->state.scope);
    const bool reconciled = currentScope && barrierCoordinator.Reconcile(message->state);
    if (automatedTestEnabled && automatedTestStage == TestAwaitingReconnect) {
        ReportAutomatedTest(
            "client-reconnect-barrier-received",
            "decoded=" + std::to_string(message.has_value()) + " scope=" + std::to_string(currentScope) +
                " reconciled=" + std::to_string(reconciled) +
                " phase=" + std::to_string(message.has_value() ? static_cast<int>(message->state.phase) : -1) +
                " kind=" + std::to_string(message.has_value() ? static_cast<int>(message->state.kind) : -1) +
                " epoch=" + std::to_string(message.has_value() ? message->state.operationEpoch : 0));
    }
    if (!message.has_value() || !currentScope || !reconciled) {
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
    // Reconnect establishes campaign authority, not a rendezvous. Keep the
    // guest's valid local position and timeline; explicit story/scene barriers
    // remain responsible for moving participants together.
    if (!BarrierRequiresParticipantRelocation(message->state.kind)) {
        if (automatedTestEnabled && automatedTestStage == TestAwaitingReconnect) {
            ReportAutomatedTest("client-reconnect-barrier-ready-sent",
                                "epoch=" + std::to_string(message->state.operationEpoch));
        }
        SendBarrierReady();
        return;
    }
    if (message->state.targetCutsceneIndex >= 0) {
        gSaveContext.nextCutsceneIndex = static_cast<uint16_t>(message->state.targetCutsceneIndex);
    }
    if (!PrepareBarrierTimeline(message->state)) {
        return;
    }
    if (IsBarrierTimelineReady(message->state) && gPlayState->sceneNum == message->state.targetScene &&
        (message->state.targetRoom < 0 || gPlayState->roomCtx.curRoom.num == message->state.targetRoom)) {
        SendBarrierReady();
        return;
    }
    if (message->state.targetEntrance >= 0 && gPlayState->transitionTrigger != TRANS_TRIGGER_START) {
        GameInteractor::RawAction::TeleportPlayerSilent(message->state.targetEntrance);
    }
}

void Manager::HandleBarrierReady(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete) {
        return;
    }
    const auto message = DecodeBarrierReady(packet.payload);
    const BarrierState& state = barrierCoordinator.GetState();
    const bool currentScope = message.has_value() && IsCurrentScope(message->scope);
    const bool participantValid = message.has_value() && message->participantId == 2;
    const bool epochValid = message.has_value() && message->operationEpoch == state.operationEpoch;
    const bool locationReady = message.has_value() &&
                               BarrierParticipantLocationReady(state, message->currentScene, message->currentRoom, true);
    const bool readyMarked = currentScope && participantValid && epochValid && locationReady &&
                             barrierCoordinator.MarkReady(message->participantId);
    if (automatedTestEnabled && automatedTestStage == TestAwaitingReconnect) {
        ReportAutomatedTest(
            "host-reconnect-barrier-ready-received",
            "decoded=" + std::to_string(message.has_value()) + " scope=" + std::to_string(currentScope) +
                " participant=" + std::to_string(participantValid) + " epoch=" + std::to_string(epochValid) +
                " location=" + std::to_string(locationReady) + " marked=" + std::to_string(readyMarked) +
                " hostPhase=" + std::to_string(static_cast<int>(state.phase)) +
                " hostEpoch=" + std::to_string(state.operationEpoch) +
                " guestEpoch=" + std::to_string(message.has_value() ? message->operationEpoch : 0));
    }
    if (!message.has_value() || !currentScope || !participantValid || !epochValid || !locationReady || !readyMarked) {
        return;
    }
    CompleteBarrierIfReady();
}

void Manager::HandleAttackIntent(const Packet& packet) {
    if (automatedTestEnabled) {
        ReportAutomatedTest("attack-intent-received", "payload=" + std::to_string(packet.payload.size()));
    }
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    if (!IsRemoteTimelineCompatible()) {
        return;
    }
    const auto message = DecodeAttackIntent(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->participantId != 2 ||
        message->requestId == 0 || message->entityId == 0 ||
        (message->attackKind != 1 && message->attackKind != 2 && message->attackKind != 3 &&
         message->attackKind != 4 && message->attackKind != 5 && message->attackKind != kSharedCombatAttackKind)) {
        if (automatedTestEnabled) {
            ReportAutomatedTest("attack-intent-invalid",
                                "decoded=" + std::to_string(message.has_value()) +
                                    " scope=" +
                                    std::to_string(message.has_value() && IsCurrentScope(message->scope)) +
                                    " participant=" +
                                    std::to_string(message.has_value() ? message->participantId : 0) +
                                    " request=" + std::to_string(message.has_value() ? message->requestId : 0) +
                                    " entity=" + std::to_string(message.has_value() ? message->entityId : 0) +
                                    " kind=" + std::to_string(message.has_value() ? message->attackKind : 0));
        }
        return;
    }
    const RequestKey request{ message->scope, message->participantId, message->requestId };
    RequestOutcome prior;
    const RequestLookup lookup = requestLedger.Lookup(request, &prior);
    if (automatedTestEnabled && lookup != RequestLookup::New) {
        ReportAutomatedTest("attack-intent-ledger-blocked",
                            "lookup=" + std::to_string(static_cast<int>(lookup)) +
                                " accepted=" + std::to_string(prior.accepted) +
                                " commit=" + std::to_string(prior.commitId));
    }
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
    const auto genericEnemy = localGenericEnemies.find(message->entityId);
    const auto jabuActor = localJabuActors.find(message->entityId);
    const auto barinadeActor = localBarinadeActors.find(message->entityId);
    const auto sharedCombatEnemy = localSharedCombatEnemies.find(message->entityId);
    const auto stalchild = localStalchildren.find(message->entityId);
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
        } else if (message->attackKind == 3 && genericEnemy != localGenericEnemies.end()) {
            Actor* actor = static_cast<Actor*>(genericEnemy->second);
            actor->colChkInfo.health = state->second.health;
            const bool alive = DamageGenericEnemy(actor, 1);
            SendGenericEnemySnapshot(actor, alive);
            accepted = true;
            if (automatedTestEnabled && automatedTestStage == TestGenericCombat && actor->colChkInfo.health == 1) {
                automatedTestFirstDamageObserved = true;
                ReportAutomatedTest("generic-enemy-host-damage-accepted", "health=2 -> health=1");
            }
        } else if (message->attackKind == 3 && stalchild != localStalchildren.end()) {
            Actor* actor = static_cast<Actor*>(stalchild->second);
            EnSkb* stalchildActor = reinterpret_cast<EnSkb*>(actor);
            if (actor->colChkInfo.health > 0 && stalchildActor->actionState != 1) {
                const int16_t authoritativeHealth = EnSkb_ApplyCoopDamage(stalchildActor, gPlayState, 1);
                // A zero-health Stalchild remains alive while its native dying animation produces body parts and a
                // drop. The later OnActorKill hook sends the terminal snapshot.
                SendStalchildSnapshot(actor, true);
                accepted = true;
                if (automatedTestEnabled && automatedTestStage == TestStalchildCombat) {
                    ++automatedTestPhysicalHits;
                    ReportAutomatedTest("stalchild-host-guest-hit-accepted",
                                        "hit=" + std::to_string(automatedTestPhysicalHits) +
                                            " health=" + std::to_string(authoritativeHealth));
                    if (authoritativeHealth == 1) {
                        automatedTestFirstDamageObserved = true;
                    } else if (authoritativeHealth == 0 && stalchildActor->actionState == 1 &&
                               !automatedTestStalchildDeathTransitionObserved) {
                        automatedTestStalchildDeathTransitionObserved = true;
                        ReportAutomatedTest("stalchild-native-death-started",
                                            "guest lethal hit entered the native dying action");
                    }
                }
            }
        } else if (message->attackKind == 4 && jabuActor != localJabuActors.end()) {
            Actor* actor = static_cast<Actor*>(jabuActor->second);
            accepted = ApplyJabuActorDamage(actor, message->damageEffect,
                                            std::clamp<uint8_t>(message->damage, 0, 8));
            SendJabuActorSnapshot(actor, true);
        } else if (message->attackKind == 5 && barinadeActor != localBarinadeActors.end()) {
            Actor* actor = static_cast<Actor*>(barinadeActor->second);
            accepted = HyruleCoop_BarinadeApplyDamage(actor, gPlayState, message->damageEffect,
                                                      std::clamp<uint8_t>(message->damage, 0, 8)) != 0;
            SendBarinadeSnapshot(actor, true);
        } else if (message->attackKind == kSharedCombatAttackKind &&
                   sharedCombatEnemy != localSharedCombatEnemies.end()) {
            Actor* actor = static_cast<Actor*>(sharedCombatEnemy->second);
            accepted = ApplySharedCombatEnemyDamage(actor, message->damageEffect,
                                                    std::clamp<uint8_t>(message->damage, 1, 8),
                                                    message->damageFlags);
            if (accepted) {
                SharedCombatEnemyOutcome& outcome = sharedCombatEnemyOutcomes[message->entityId];
                outcome.sequence += 1;
                if (outcome.sequence == 0) {
                    outcome.sequence = 1;
                }
                outcome.damageEffect = message->damageEffect;
                outcome.damage = std::clamp<uint8_t>(message->damage, 1, 8);
                outcome.damageFlags = message->damageFlags;
                SendSharedCombatEnemySnapshot(actor, true);
            }
        }
    }
    if (automatedTestEnabled && !accepted) {
        const float distanceSquared = state != actorSnapshots.end() && remotePlayerSnapshot.has_value()
                                          ? DistanceSquared(remotePlayerSnapshot->position, state->second.position)
                                          : -1.0f;
        ReportAutomatedTest("attack-intent-rejected",
                            "kind=" + std::to_string(message->attackKind) +
                                " state=" + std::to_string(state != actorSnapshots.end()) +
                                " alive=" + std::to_string(state != actorSnapshots.end() && state->second.alive) +
                                " remote=" + std::to_string(remotePlayerSnapshot.has_value()) +
                                " message-scene=" + std::to_string(message->scene) +
                                " remote-scene=" +
                                std::to_string(remotePlayerSnapshot.has_value() ? remotePlayerSnapshot->scene : -1) +
                                " state-scene=" +
                                std::to_string(state != actorSnapshots.end() ? state->second.scene : -1) +
                                " tick-delta=" + std::to_string(attackStateDelta) +
                                " distance-squared=" + std::to_string(distanceSquared) +
                                " baba=" + std::to_string(baba != localDekuBabas.end()) +
                                " gohma=" + std::to_string(gohma != localGohmas.end()) +
                                " generic=" + std::to_string(genericEnemy != localGenericEnemies.end()) +
                                " jabu=" + std::to_string(jabuActor != localJabuActors.end()) +
                                " barinade=" + std::to_string(barinadeActor != localBarinadeActors.end()) +
                                " shared=" + std::to_string(sharedCombatEnemy != localSharedCombatEnemies.end()));
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

void Manager::HandleActorInteractionIntent(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || !IsSaveLoaded() ||
        !IsRemoteTimelineCompatible()) {
        return;
    }
    const auto message = DecodeActorInteractionIntent(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->participantId != 2 ||
        message->requestId == 0 || message->entityId == 0 || message->scene != gPlayState->sceneNum) {
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
    if (lookup != RequestLookup::New || !remotePlayerSnapshot.has_value() ||
        remotePlayerSnapshot->scene != message->scene) {
        return;
    }

    bool accepted = false;
    const auto snapshot = actorSnapshots.find(message->entityId);
    const bool nearby = snapshot != actorSnapshots.end() &&
                        DistanceSquared(remotePlayerSnapshot->position, snapshot->second.position) <= 300.0f * 300.0f;
    if (nearby && message->kind == ActorInteractionKind::PushBlockBegin) {
        const auto block = localPushBlocks.find(message->entityId);
        if (block != localPushBlocks.end()) {
            accepted = HyruleCoop_ObjOshihikiBeginPush(block->second, gPlayState, message->value) != 0;
            SendPushBlockSnapshot(block->second, message->requestId);
        }
    } else if (nearby && message->kind == ActorInteractionKind::DampeRaceStart) {
        const auto dampe = localDampeRaceActors.find(message->entityId);
        if (dampe != localDampeRaceActors.end() && static_cast<Actor*>(dampe->second)->id == ACTOR_EN_PO_RELAY) {
            accepted = HyruleCoop_DampeRaceBeginHostRace(dampe->second, gPlayState) != 0;
            SendDampeRaceSnapshot(dampe->second, message->requestId);
        }
    }
    requestLedger.Record(request, { accepted, message->entityId, frameCounter });
    if (!accepted && snapshot != actorSnapshots.end()) {
        ActorSnapshotMessage response = snapshot->second;
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
        !IsValidReplicatedSceneFlag(message->scene, message->flagType, message->flag) ||
        message->flagType != FLAG_SCENE_COLLECTIBLE ||
        IsEphemeralSceneFlag(message->scene, message->flagType, message->flag) ||
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
            !IsValidReplicatedSceneFlag(location.scene, location.flagType, location.flag) ||
            location.flagType != FLAG_SCENE_COLLECTIBLE ||
            IsEphemeralSceneFlag(location.scene, location.flagType, location.flag)) {
            continue;
        }
        GameInteractor::RawAction::SetSceneFlag(location.scene, location.flagType, location.flag);
        collectedLocations[location.locationId] = location;
    }
    ApplyCanonicalProgression(message->shared);
    for (auto pending = pendingClientEventFlags.begin(); pending != pendingClientEventFlags.end();) {
        if (IsPackedEventFlagSet(message->shared.eventChkInf, *pending)) {
            pending = pendingClientEventFlags.erase(pending);
            continue;
        }
        gSaveContext.eventChkInf[*pending >> 4] |= static_cast<uint16_t>(1u << (*pending & 0x0F));
        ++pending;
    }
    if (!pendingClientEventFlags.empty()) {
        ReconcileSharedProgressionDerivedFlags(&gSaveContext);
    }
    applyingAuthoritativeState = false;
    lastObservedClientProgression = CaptureSharedProgression(&gSaveContext);
    lastAppliedProgressionRevision = message->revision;
}

void Manager::ReconcileClientDurableProgression() {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded() ||
        !clientProgressionBaselineCaptured) {
        return;
    }

    const SharedProgressionState current = CaptureSharedProgression(&gSaveContext);
    if (current.eventChkInf == lastObservedClientProgression.eventChkInf) {
        return;
    }
    for (const uint16_t flag : CollectNewlySetEventFlags(lastObservedClientProgression.eventChkInf,
                                                         current.eventChkInf)) {
        if (!IsValidDurableGlobalFlag(FLAG_EVENT_CHECK_INF, static_cast<int16_t>(flag))) {
            continue;
        }
        pendingClientEventFlags.insert(flag);
        SendGlobalFlagIntent(FLAG_EVENT_CHECK_INF, static_cast<int16_t>(flag), true);
    }
    lastObservedClientProgression = current;
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
                if ((entry.itemId == ITEM_COMPASS || entry.itemId == ITEM_DUNGEON_MAP) &&
                    message->mapIndex < std::size(gSaveContext.inventory.dungeonItems)) {
                    const uint8_t dungeonItem = static_cast<uint8_t>(entry.itemId - ITEM_KEY_BOSS);
                    gSaveContext.inventory.dungeonItems[message->mapIndex] |= gBitFlags[dungeonItem];
                    accepted = true;
                } else if (entry.itemId != ITEM_COMPASS && entry.itemId != ITEM_DUNGEON_MAP) {
                    Item_Give(gPlayState, static_cast<uint8_t>(entry.itemId));
                    accepted = true;
                }
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
    if (accepted && message->kind == ProgressionIntentKind::GlobalFlagChanged && message->set &&
        message->flagType == FLAG_EVENT_CHECK_INF && message->flag == EVENTCHKINF_ZELDA_FLED_HYRULE_CASTLE) {
        pendingStoryEvent = StoryEventKind::CastleEscape;
    }
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

void Manager::HandleStoryEventIntent(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeStoryEvent(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->operationEpoch != 0 ||
        message->participantId != 2 || message->requestId == 0 || !remotePlayerSnapshot.has_value()) {
        return;
    }

    const RequestKey request{ message->scope, message->participantId, message->requestId };
    RequestOutcome prior;
    const RequestLookup lookup = requestLedger.Lookup(request, &prior);
    if (lookup == RequestLookup::Replay) {
        if (prior.accepted && prior.commitId != 0) {
            SendStoryEventCommand(message->kind, message->participantId, message->requestId, prior.commitId);
        }
        return;
    }
    if (lookup != RequestLookup::New) {
        return;
    }

    const TimelineScope sourceTimeline{ message->linkAge, message->sceneLayer };
    const PlayerSnapshotMessage& remote = remotePlayerSnapshot.value();
    const bool sourceMatchesSnapshot = message->scene == remote.scene &&
                                       IsSameTimeline(sourceTimeline, SnapshotTimelineScope(remote));
    const bool accepted = sourceMatchesSnapshot && ShouldCoordinateTempleStory(message->kind, true);
    uint64_t operationEpoch = 0;
    if (accepted) {
        applyingAuthoritativeState = true;
        ReplayTempleStoryPresentation(message->kind);
        applyingAuthoritativeState = false;
        operationEpoch = SendStoryEventCommand(message->kind, message->participantId, message->requestId);
    }
    requestLedger.Record(request, { accepted, operationEpoch, frameCounter });
}

void Manager::HandleStoryEventCommand(const Packet& packet) {
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    const auto message = DecodeStoryEvent(packet.payload);
    if (!message.has_value() || !IsCurrentScope(message->scope) || message->operationEpoch == 0 ||
        message->operationEpoch <= lastAppliedStoryOperationEpoch || message->requestId == 0 ||
        (message->participantId != 1 && message->participantId != 2)) {
        return;
    }
    const TimelineScope sourceTimeline{ message->linkAge, message->sceneLayer };
    if (!IsValidTimelineScope(sourceTimeline) || message->scene != SCENE_TEMPLE_OF_TIME) {
        return;
    }
    lastAppliedStoryOperationEpoch = message->operationEpoch;
    if (message->participantId == playerId) {
        return;
    }
    if (gPlayState->sceneNum != message->scene || !IsSameTimeline(GetLocalTimelineScope(), sourceTimeline)) {
        return;
    }

    applyingAuthoritativeState = true;
    ReplayTempleStoryPresentation(message->kind);
    applyingAuthoritativeState = false;
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
    message.room = gPlayState->roomCtx.curRoom.num;
    message.entrance = gSaveContext.entranceIndex;
    // A scene can retain an adult skeleton after an unrelated save snapshot writes child age. The live scene
    // value is authoritative for the player model and is the only safe age to advertise to another peer.
    const TimelineScope timeline = GetLocalTimelineScope();
    message.linkAge = timeline.linkAge;
    message.sceneLayer = timeline.sceneLayer;
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
    message.currentMask = automatedTestEnabled &&
                                  (automatedTestStage == TestAwaitingActors || automatedTestStage == TestAttacking)
                              ? static_cast<uint8_t>(PLAYER_MASK_BUNNY)
                              : player->currentMask;
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
    message.mounted = (player->stateFlags1 & PLAYER_STATE1_ON_HORSE) != 0 && player->rideActor != nullptr &&
                      player->rideActor->id == ACTOR_EN_HORSE;
    if (message.mounted) {
        const EnHorse* horse = reinterpret_cast<const EnHorse*>(player->rideActor);
        message.horsePosition[0] = horse->actor.world.pos.x;
        message.horsePosition[1] = horse->actor.world.pos.y;
        message.horsePosition[2] = horse->actor.world.pos.z;
        message.horseRotation[0] = horse->actor.shape.rot.x;
        message.horseRotation[1] = horse->actor.shape.rot.y;
        message.horseRotation[2] = horse->actor.shape.rot.z;
        message.horseAnimation = static_cast<int8_t>(horse->animationIdx);
        message.horseAnimationFrame = horse->skin.skelAnime.curFrame;
        message.horseSpeed = horse->actor.speedXZ;
    }
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
    message.currentMask = automatedTestEnabled &&
                                  (automatedTestStage == TestAwaitingActors || automatedTestStage == TestAttacking)
                              ? static_cast<uint8_t>(PLAYER_MASK_BUNNY)
                              : player->currentMask;
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
    transport.Send(MessageType::SnapshotRequest,
                   EncodeSnapshotRequest({ sessionScope, gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num }));
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
    const bool isCurrentScene = gPlayState != nullptr && gPlayState->sceneNum == scene;
    const uint32_t tempSwitches = isCurrentScene
                                      ? gPlayState->actorCtx.flags.tempSwch
                                      : 0;
    const uint32_t tempClear = isCurrentScene ? gPlayState->actorCtx.flags.tempClear : 0;
    const uint32_t tempCollectible = isCurrentScene ? gPlayState->actorCtx.flags.tempCollect : 0;
    const SceneFlagsSnapshotMessage message{ sessionScope, sceneRevisions.Current(SceneStreamId(scene)), scene,
                                             flags.chest, flags.swch, tempSwitches, flags.clear, tempClear,
                                             flags.collect, tempCollectible };
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
    if (state.phase != BarrierPhase::WaitingForParticipants || !IsCurrentScope(state.scope) ||
        !BarrierParticipantLocationReady(state, gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num,
                                         IsBarrierTimelineReady(state))) {
        return;
    }
    transport.Send(MessageType::BarrierReady,
                   EncodeBarrierReady({ sessionScope, state.operationEpoch, playerId, gPlayState->sceneNum,
                                        gPlayState->roomCtx.curRoom.num }));
}

void Manager::SendAttackIntent(uint64_t entityId, int16_t scene, uint8_t attackKind, uint8_t damageEffect,
                               uint8_t damage, uint32_t damageFlags) {
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
    message.damageEffect = damageEffect;
    message.damage = std::max<uint8_t>(1, damage);
    message.damageFlags = damageFlags;
    if (transport.SendRepeatedRealtime(MessageType::AttackIntent, EncodeAttackIntent(message), message.entityId)) {
        pendingGuestAttackRequests[entityId] = message.requestId;
        if (automatedTestEnabled) {
            const TransportTelemetry telemetry = transport.GetTelemetry();
            ReportAutomatedTest("attack-intent-sent",
                                "kind=" + std::to_string(attackKind) +
                                    " request=" + std::to_string(message.requestId) +
                                    " realtime=" + std::to_string(telemetry.realtimeReady));
        }
    } else {
        ClearPendingGuestAttack(entityId);
    }
}

void Manager::ClearPendingGuestAttack(uint64_t entityId) {
    transport.CancelRepeatedRealtime(MessageType::AttackIntent, entityId);
    pendingGuestAttackRequests.erase(entityId);
    pendingGuestAttacks.erase(entityId);
}

void Manager::SendActorInteractionIntent(uint64_t entityId, int16_t scene, ActorInteractionKind kind, float value) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || entityId == 0 ||
        pendingActorInteractionRequests.contains(entityId)) {
        return;
    }
    ActorInteractionIntentMessage message;
    message.scope = sessionScope;
    message.participantId = playerId;
    message.requestId = nextRequestId++;
    message.entityId = entityId;
    message.playerTick = frameCounter;
    message.scene = scene;
    message.kind = kind;
    message.value = value;
    if (transport.SendRepeatedRealtime(MessageType::ActorInteractionIntent,
                                       EncodeActorInteractionIntent(message), message.entityId)) {
        pendingActorInteractionRequests[entityId] = message.requestId;
        lastActorInteractionTick[entityId] = frameCounter;
    }
}

void Manager::ClearPendingActorInteraction(uint64_t entityId) {
    transport.CancelRepeatedRealtime(MessageType::ActorInteractionIntent, entityId);
    pendingActorInteractionRequests.erase(entityId);
    lastActorInteractionTick.erase(entityId);
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
    const SharedProgressionState previousProgression = canonicalProgression;
    const bool hadCanonicalProgression = canonicalProgressionCaptured;
    CaptureCanonicalProgression();
    if (!hadCanonicalProgression || canonicalProgression != previousProgression) {
        ++progressionRevision;
    }

    ProgressionSnapshotMessage message;
    message.scope = sessionScope;
    message.revision = progressionRevision;
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

void Manager::SendStoryEventIntent(StoryEventKind kind) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded()) {
        return;
    }
    const TimelineScope timeline = GetLocalTimelineScope();
    StoryEventMessage message;
    message.scope = sessionScope;
    message.participantId = playerId;
    message.requestId = nextRequestId++;
    message.kind = kind;
    message.scene = gPlayState->sceneNum;
    message.linkAge = timeline.linkAge;
    message.sceneLayer = timeline.sceneLayer;
    transport.Send(MessageType::StoryEventIntent, EncodeStoryEvent(message));
}

uint64_t Manager::SendStoryEventCommand(StoryEventKind kind, uint64_t participantId, uint64_t requestId,
                                        uint64_t operationEpoch) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Host || !IsSaveLoaded() ||
        participantId == 0 || requestId == 0) {
        return 0;
    }
    const TimelineScope timeline = GetLocalTimelineScope();
    StoryEventMessage message;
    message.scope = sessionScope;
    message.operationEpoch = operationEpoch == 0 ? nextOperationEpoch++ : operationEpoch;
    message.participantId = participantId;
    message.requestId = requestId;
    message.kind = kind;
    message.scene = gPlayState->sceneNum;
    message.linkAge = timeline.linkAge;
    message.sceneLayer = timeline.sceneLayer;
    if (!transport.Send(MessageType::StoryEventCommand, EncodeStoryEvent(message))) {
        return 0;
    }
    return message.operationEpoch;
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
                              frameCounter - lastAttack->second >= kGuestDekuBabaAttackCooldownFrames;
        if (newSwing && !genericGuestTargetsHitThisSwing.contains(entityId) &&
            !pendingGuestAttacks.contains(entityId)) {
            genericGuestTargetsHitThisSwing.insert(entityId);
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
            genericGuestTargetsHitThisSwing.erase(entityId);
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
            genericGuestTargetsHitThisSwing.erase(iterator->first);
            iterator = localDekuBabas.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::SendJabuActorSnapshot(void* actorRef, bool alive) {
    if (transport.GetRole() != SessionRole::Host || !IsSaveLoaded() || actorRef == nullptr) {
        return;
    }
    Actor* canonical = CanonicalJabuActor(static_cast<Actor*>(actorRef));
    if (canonical == nullptr || (!alive &&
        std::none_of(localJabuActors.begin(), localJabuActors.end(),
                     [canonical](const auto& entry) { return entry.second == canonical; }))) {
        return;
    }

    ActorSnapshotMessage message =
        CaptureJabuActorSnapshot(canonical, gPlayState->sceneNum, frameCounter, sessionScope, alive);
    if (message.adapterWordCount != kJabuActorAdapterWordCount) {
        const auto lastKnown = actorSnapshots.find(message.entityId);
        if (alive || lastKnown == actorSnapshots.end()) {
            return;
        }
        message = lastKnown->second;
        message.hostTick = frameCounter;
        message.alive = false;
    }
    const auto previous = actorSnapshots.find(message.entityId);
    const bool importantTransition = previous == actorSnapshots.end() || previous->second.alive != message.alive ||
                                     previous->second.health != message.health ||
                                     previous->second.stateId != message.stateId;
    actorSnapshots[message.entityId] = message;
    if (alive) {
        localJabuActors[message.entityId] = canonical;
    }
    if (importantTransition) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    }
}

void Manager::UpdateJabuActor(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    Actor* canonical = CanonicalJabuActor(actor);
    if (canonical == nullptr || gPlayState == nullptr) {
        return;
    }
    const uint64_t entityId = GetJabuActorEntityId(canonical, gPlayState->sceneNum, sessionScope.worldGeneration);
    if (transport.GetRole() == SessionRole::Host) {
        localJabuActors[entityId] = canonical;
        if (actor == canonical && frameCounter % 2 == 0) {
            SendJabuActorSnapshot(canonical, true);
        }
        return;
    }
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsRemoteTimelineCompatible()) {
        return;
    }

    localJabuActors[entityId] = canonical;
    if (actor->colChkInfo.health > 0 && actor->colChkInfo.health < kGuestEnemyHealthSentinel &&
        !genericGuestTargetsHitThisSwing.contains(entityId) && !pendingGuestAttacks.contains(entityId)) {
        const uint8_t damage = static_cast<uint8_t>(
            std::clamp<int16_t>(kGuestEnemyHealthSentinel - actor->colChkInfo.health, 1, 8));
        genericGuestTargetsHitThisSwing.insert(entityId);
        pendingGuestAttacks.insert(entityId);
        lastGuestAttackTick[entityId] = frameCounter;
        SendAttackIntent(entityId, gPlayState->sceneNum, 4, actor->colChkInfo.damageEffect, damage);
    }

    if (actor != canonical) {
        return;
    }
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot == actorSnapshots.end()) {
        return;
    }
    if (!snapshot->second.alive) {
        ClearLocalTarget(canonical);
        Actor_Kill(canonical);
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
        localJabuActors.erase(entityId);
        return;
    }
    ApplyJabuActorSnapshot(canonical, snapshot->second);
}

void Manager::ApplyJabuActorAuthority(void* actorRef, bool* shouldUpdate) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded() ||
        actorRef == nullptr || shouldUpdate == nullptr || gPlayState == nullptr || !IsRemoteTimelineCompatible()) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorRef);
    Actor* canonical = CanonicalJabuActor(actor);
    if (canonical == nullptr) {
        return;
    }
    const uint64_t entityId = GetJabuActorEntityId(canonical, gPlayState->sceneNum, sessionScope.worldGeneration);
    localJabuActors[entityId] = canonical;
    uint8_t damageEffect = 0;
    uint8_t damage = 0;
    if (!genericGuestTargetsHitThisSwing.contains(entityId) && !pendingGuestAttacks.contains(entityId) &&
        PeekJabuActorDamage(actor, &damageEffect, &damage)) {
        genericGuestTargetsHitThisSwing.insert(entityId);
        pendingGuestAttacks.insert(entityId);
        lastGuestAttackTick[entityId] = frameCounter;
        SendAttackIntent(entityId, gPlayState->sceneNum, 4, damageEffect, damage);
    }
    // The first authoritative snapshot may arrive after this actor's first local collision. Keep that window
    // nonlethal while still allowing the native actor update to report guest hit intent.
    if (actor->colChkInfo.health > 0) {
        actor->colChkInfo.health = kGuestEnemyHealthSentinel;
    }
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot == actorSnapshots.end()) {
        return;
    }
    if (!snapshot->second.alive) {
        if (actor == canonical) {
            ClearLocalTarget(canonical);
            Actor_Kill(canonical);
            localJabuActors.erase(entityId);
        }
        *shouldUpdate = false;
        return;
    }
}

void Manager::ForgetJabuActor(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    Actor* canonical = CanonicalJabuActor(actor);
    for (auto iterator = localJabuActors.begin(); iterator != localJabuActors.end();) {
        if (iterator->second == actorRef || iterator->second == canonical) {
            ClearPendingGuestAttack(iterator->first);
            lastGuestAttackTick.erase(iterator->first);
            genericGuestTargetsHitThisSwing.erase(iterator->first);
            iterator = localJabuActors.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::SendBarinadeSnapshot(void* actorRef, bool alive) {
    if (transport.GetRole() != SessionRole::Host || !IsSaveLoaded() || actorRef == nullptr || gPlayState == nullptr) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorRef);
    const bool knownActor = std::any_of(localBarinadeActors.begin(), localBarinadeActors.end(),
                                        [actor](const auto& entry) { return entry.second == actor; });
    if (actor->id != ACTOR_BOSS_VA || (!alive && !knownActor)) {
        return;
    }

    ActorSnapshotMessage message =
        CaptureBarinadeSnapshot(actor, gPlayState->sceneNum, frameCounter, sessionScope, alive);
    if (alive && message.adapterWordCount != kBarinadeAdapterWordCount) {
        return;
    }
    const auto previous = actorSnapshots.find(message.entityId);
    bool importantTransition = previous == actorSnapshots.end() || previous->second.alive != message.alive ||
                               previous->second.health != message.health || previous->second.stateId != message.stateId;
    if (!importantTransition && previous->second.adapterWordCount == kBarinadeAdapterWordCount) {
        HyruleCoopBarinadeState previousState{};
        HyruleCoopBarinadeState currentState{};
        std::memcpy(&previousState, previous->second.adapterState.data(), sizeof(previousState));
        std::memcpy(&currentState, message.adapterState.data(), sizeof(currentState));
        importantTransition = previousState.phase != currentState.phase || previousState.isDead != currentState.isDead;
    }
    actorSnapshots[message.entityId] = message;
    if (alive) {
        localBarinadeActors[message.entityId] = actor;
    }
    if (importantTransition) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    }
}

void Manager::UpdateBarinade(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    if (actor == nullptr || actor->id != ACTOR_BOSS_VA || gPlayState == nullptr) {
        return;
    }
    const uint64_t entityId = GetBarinadeEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    if (transport.GetRole() == SessionRole::Host) {
        localBarinadeActors[entityId] = actor;
        const uint32_t snapshotInterval = HyruleCoop_BarinadeIsCanonicalRoot(actor) ? 3 : 6;
        if (frameCounter % snapshotInterval == 0) {
            SendBarinadeSnapshot(actor, true);
        }
        return;
    }
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsRemoteTimelineCompatible()) {
        return;
    }

    localBarinadeActors[entityId] = actor;
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot == actorSnapshots.end()) {
        return;
    }
    if (!snapshot->second.alive) {
        ClearLocalTarget(actor);
        Actor_Kill(actor);
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
        localBarinadeActors.erase(entityId);
        return;
    }
    ApplyBarinadeSnapshot(actor, snapshot->second);
}

void Manager::ApplyBarinadeAuthority(void* actorRef, bool* shouldUpdate) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded() ||
        actorRef == nullptr || shouldUpdate == nullptr || gPlayState == nullptr || !IsRemoteTimelineCompatible()) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorRef);
    if (actor->id != ACTOR_BOSS_VA) {
        return;
    }
    const uint64_t entityId = GetBarinadeEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    localBarinadeActors[entityId] = actor;
    uint8_t damageEffect = 0;
    uint8_t damage = 0;
    if (!genericGuestTargetsHitThisSwing.contains(entityId) && !pendingGuestAttacks.contains(entityId) &&
        HyruleCoop_BarinadeConsumeDamage(actor, &damageEffect, &damage)) {
        genericGuestTargetsHitThisSwing.insert(entityId);
        pendingGuestAttacks.insert(entityId);
        lastGuestAttackTick[entityId] = frameCounter;
        SendAttackIntent(entityId, gPlayState->sceneNum, 5, damageEffect, damage);
    }
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot != actorSnapshots.end() && !snapshot->second.alive) {
        ClearLocalTarget(actor);
        Actor_Kill(actor);
        localBarinadeActors.erase(entityId);
        *shouldUpdate = false;
    }
}

void Manager::ForgetBarinade(void* actorRef) {
    for (auto iterator = localBarinadeActors.begin(); iterator != localBarinadeActors.end();) {
        if (iterator->second == actorRef) {
            ClearPendingGuestAttack(iterator->first);
            lastGuestAttackTick.erase(iterator->first);
            genericGuestTargetsHitThisSwing.erase(iterator->first);
            iterator = localBarinadeActors.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::SendGenericEnemySnapshot(void* actorRef, bool alive) {
    if (transport.GetRole() != SessionRole::Host || !IsSaveLoaded() || actorRef == nullptr) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorRef);
    const bool knownActor = std::any_of(localGenericEnemies.begin(), localGenericEnemies.end(),
                                        [actor](const auto& entry) { return entry.second == actor; });
    if ((alive && !IsGenericEnemyActor(actor)) || (!alive && !knownActor)) {
        return;
    }

    ActorSnapshotMessage message =
        CaptureGenericEnemySnapshot(actor, gPlayState->sceneNum, frameCounter, sessionScope, alive);
    const auto previous = actorSnapshots.find(message.entityId);
    const bool importantTransition = previous == actorSnapshots.end() || previous->second.alive != message.alive ||
                                     previous->second.health != message.health;
    actorSnapshots[message.entityId] = message;
    if (alive) {
        localGenericEnemies[message.entityId] = actor;
    }
    if (importantTransition) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    }
}

void Manager::UpdateGenericEnemy(void* actorRef) {
    if (actorRef == nullptr) {
        return;
    }
    if (transport.GetRole() == SessionRole::Host) {
        if (frameCounter % 2 == 0) {
            SendGenericEnemySnapshot(actorRef, true);
        }
        return;
    }
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded()) {
        return;
    }
    if (!IsRemoteTimelineCompatible()) {
        return;
    }

    Actor* actor = static_cast<Actor*>(actorRef);
    const uint64_t entityId = GetGenericEnemyEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    if (!localGenericEnemies.contains(entityId)) {
        return;
    }
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot == actorSnapshots.end()) {
        return;
    }
    if (!snapshot->second.alive) {
        ClearLocalTarget(actor);
        Actor_Kill(actor);
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
        localGenericEnemies.erase(entityId);
        return;
    }

    // Let each client run the actor's own private action and animation state, then pull common fields back to the
    // host sample. Damage is consumed before the native actor update so a guest collision cannot start a private
    // fall/death action before the host validates it.
    ApplyGenericEnemySnapshot(actor, snapshot->second);
}

void Manager::ApplyGenericEnemyAuthority(void* actorRef, bool* shouldUpdate) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded() || actorRef == nullptr ||
        shouldUpdate == nullptr) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorRef);
    if (!IsGenericEnemyActor(actor)) {
        return;
    }
    const uint64_t entityId = GetGenericEnemyEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    localGenericEnemies[entityId] = actor;
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot == actorSnapshots.end()) {
        // Allow vanilla initialization to finish until the host's first authoritative sample arrives.
        return;
    }
    if (!snapshot->second.alive) {
        ClearLocalTarget(actor);
        Actor_Kill(actor);
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
        localGenericEnemies.erase(entityId);
        // Actor_Kill nulls actor->update. This hook runs inside Actor_UpdateAll, so returning with the old true
        // value would immediately invoke a null update callback on the destroyed actor.
        *shouldUpdate = false;
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    uint8_t damageEffect = 0;
    uint8_t damage = 0;
    const bool localMeleeHit = player != nullptr && player->meleeWeaponState > 0 &&
                               HyruleCoop_EnFireflyConsumeDamage(actor, &damageEffect, &damage);
    if (localMeleeHit && !genericGuestTargetsHitThisSwing.contains(entityId) &&
        !pendingGuestAttacks.contains(entityId)) {
        genericGuestTargetsHitThisSwing.insert(entityId);
        pendingGuestAttacks.insert(entityId);
        lastGuestAttackTick[entityId] = frameCounter;
        if (automatedTestEnabled && automatedTestStage == TestGenericCombat) {
            ++automatedTestPhysicalHits;
            ReportAutomatedTest("generic-enemy-client-physical-collision",
                                "hit=" + std::to_string(automatedTestPhysicalHits));
        }
        SendAttackIntent(entityId, gPlayState->sceneNum, 3, damageEffect, std::max<uint8_t>(damage, 1));
    }
    // Keep the actor's own update and animation running. Its health becomes a short-lived collision probe and is
    // reconciled from the host snapshot immediately after that update.
    actor->colChkInfo.health = kGuestEnemyHealthSentinel;
}

void Manager::ForgetGenericEnemy(void* actorRef) {
    for (auto iterator = localGenericEnemies.begin(); iterator != localGenericEnemies.end();) {
        if (iterator->second == actorRef) {
            ClearPendingGuestAttack(iterator->first);
            lastGuestAttackTick.erase(iterator->first);
            genericGuestTargetsHitThisSwing.erase(iterator->first);
            iterator = localGenericEnemies.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::SendSharedCombatEnemySnapshot(void* actorRef, bool alive, uint64_t acknowledgedRequestId) {
    if (transport.GetRole() != SessionRole::Host || !IsSaveLoaded() || actorRef == nullptr || gPlayState == nullptr) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorRef);
    const bool knownActor = std::any_of(localSharedCombatEnemies.begin(), localSharedCombatEnemies.end(),
                                        [actor](const auto& entry) { return entry.second == actor; });
    if ((alive && !IsSharedCombatEnemyActor(actor)) || (!alive && !knownActor)) {
        return;
    }
    const uint64_t entityId = GetGenericEnemyEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    const auto outcome = sharedCombatEnemyOutcomes.find(entityId);
    const SharedCombatEnemyOutcome state = outcome == sharedCombatEnemyOutcomes.end()
                                               ? SharedCombatEnemyOutcome{}
                                               : outcome->second;
    ActorSnapshotMessage message =
        CaptureSharedCombatEnemySnapshot(actor, gPlayState->sceneNum, frameCounter, sessionScope, alive,
                                         state.sequence, state.damageEffect, state.damage, state.damageFlags);
    message.acknowledgedRequestId = acknowledgedRequestId;
    const auto previous = actorSnapshots.find(entityId);
    const bool outcomeChanged = previous == actorSnapshots.end() ||
                                previous->second.adapterWordCount != kSharedCombatEnemyAdapterWordCount ||
                                previous->second.adapterState != message.adapterState;
    const bool importantTransition = acknowledgedRequestId != 0 || previous == actorSnapshots.end() ||
                                     previous->second.alive != message.alive ||
                                     previous->second.health != message.health || outcomeChanged;
    actorSnapshots[entityId] = message;
    if (alive) {
        localSharedCombatEnemies[entityId] = actor;
    }
    if (importantTransition) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message), entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), entityId);
    }
}

void Manager::UpdateSharedCombatEnemy(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    if (!IsSharedCombatEnemyActor(actor) || gPlayState == nullptr) {
        return;
    }
    const uint64_t entityId = GetGenericEnemyEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    if (transport.GetRole() == SessionRole::Host) {
        localSharedCombatEnemies[entityId] = actor;
        const auto previous = actorSnapshots.find(entityId);
        const auto outcome = sharedCombatEnemyOutcomes.find(entityId);
        const bool unsentOutcome = outcome != sharedCombatEnemyOutcomes.end() &&
                                   (previous == actorSnapshots.end() ||
                                    previous->second.adapterWordCount != kSharedCombatEnemyAdapterWordCount ||
                                    static_cast<uint16_t>(previous->second.adapterState[SharedCombatEnemyOutcomeSequenceLow]) !=
                                        static_cast<uint16_t>(outcome->second.sequence & 0xFFFF) ||
                                    static_cast<uint16_t>(previous->second.adapterState[SharedCombatEnemyOutcomeSequenceHigh]) !=
                                        static_cast<uint16_t>(outcome->second.sequence >> 16));
        if (unsentOutcome || frameCounter % 2 == 0) {
            SendSharedCombatEnemySnapshot(actor, true);
        }
        return;
    }
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsSaveLoaded() ||
        !IsRemoteTimelineCompatible()) {
        return;
    }
    localSharedCombatEnemies[entityId] = actor;
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot != actorSnapshots.end() && snapshot->second.alive) {
        // Health converges without moving the actor. Position, action, and animation stay native to avoid AI state
        // conflicts between the two local worlds.
        actor->colChkInfo.health = std::max<int16_t>(0, snapshot->second.health);
    }
}

void Manager::ApplySharedCombatEnemyAuthority(void* actorRef, bool* shouldUpdate) {
    Actor* actor = static_cast<Actor*>(actorRef);
    if (!IsSharedCombatEnemyActor(actor) || shouldUpdate == nullptr || gPlayState == nullptr || !IsSaveLoaded()) {
        return;
    }
    const uint64_t entityId = GetGenericEnemyEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    if (transport.GetRole() == SessionRole::Host) {
        localSharedCombatEnemies[entityId] = actor;
        if (!handshakeComplete) {
            return;
        }
        uint8_t damageEffect = 0;
        uint8_t damage = 0;
        uint32_t damageFlags = 0;
        if (PeekSharedCombatEnemyDamage(actor, &damageEffect, &damage, &damageFlags)) {
            SharedCombatEnemyOutcome& outcome = sharedCombatEnemyOutcomes[entityId];
            outcome.sequence += 1;
            if (outcome.sequence == 0) {
                outcome.sequence = 1;
            }
            outcome.damageEffect = damageEffect;
            outcome.damage = std::max<uint8_t>(damage, 1);
            outcome.damageFlags = damageFlags;
        }
        return;
    }
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsRemoteTimelineCompatible()) {
        return;
    }
    localSharedCombatEnemies[entityId] = actor;
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot == actorSnapshots.end()) {
        return;
    }
    if (!snapshot->second.alive) {
        ClearLocalTarget(actor);
        Actor_Kill(actor);
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
        localSharedCombatEnemies.erase(entityId);
        sharedCombatEnemyOutcomes.erase(entityId);
        appliedSharedCombatEnemyOutcomeSequences.erase(entityId);
        *shouldUpdate = false;
        return;
    }

    uint32_t outcomeSequence = 0;
    uint8_t damageEffect = 0;
    uint8_t damage = 0;
    uint32_t damageFlags = 0;
    if (DecodeSharedCombatEnemyOutcome(snapshot->second, &outcomeSequence, &damageEffect, &damage, &damageFlags) &&
        IsNewSharedCombatEnemyOutcome(appliedSharedCombatEnemyOutcomeSequences[entityId], outcomeSequence)) {
        if (ApplySharedCombatEnemyDamage(actor, damageEffect, damage, damageFlags, true)) {
            appliedSharedCombatEnemyOutcomeSequences[entityId] = outcomeSequence;
        }
    }

    Player* player = GET_PLAYER(gPlayState);
    if (player != nullptr && player->meleeWeaponState > 0 && !genericGuestTargetsHitThisSwing.contains(entityId) &&
        !pendingGuestAttacks.contains(entityId) &&
        ConsumeSharedCombatEnemyDamage(actor, &damageEffect, &damage, &damageFlags)) {
        genericGuestTargetsHitThisSwing.insert(entityId);
        pendingGuestAttacks.insert(entityId);
        lastGuestAttackTick[entityId] = frameCounter;
        SendAttackIntent(entityId, gPlayState->sceneNum, kSharedCombatAttackKind, damageEffect,
                         std::max<uint8_t>(damage, 1), damageFlags);
    }

    actor->colChkInfo.health = std::max<int16_t>(0, snapshot->second.health);
}

void Manager::ForgetSharedCombatEnemy(void* actorRef) {
    for (auto iterator = localSharedCombatEnemies.begin(); iterator != localSharedCombatEnemies.end();) {
        if (iterator->second == actorRef) {
            ClearPendingGuestAttack(iterator->first);
            lastGuestAttackTick.erase(iterator->first);
            genericGuestTargetsHitThisSwing.erase(iterator->first);
            sharedCombatEnemyOutcomes.erase(iterator->first);
            appliedSharedCombatEnemyOutcomeSequences.erase(iterator->first);
            iterator = localSharedCombatEnemies.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::SendPushBlockSnapshot(void* actorRef, uint64_t acknowledgedRequestId) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || actorRef == nullptr ||
        gPlayState == nullptr) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorRef);
    ActorSnapshotMessage message =
        CapturePushBlockSnapshot(actor, gPlayState->sceneNum, frameCounter, sessionScope);
    message.acknowledgedRequestId = acknowledgedRequestId;
    actorSnapshots[message.entityId] = message;
    localPushBlocks[message.entityId] = actor;
    if (acknowledgedRequestId != 0) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    }
}

void Manager::UpdatePushBlock(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    if (actor == nullptr || actor->id != ACTOR_OBJ_OSHIHIKI || gPlayState == nullptr) {
        return;
    }
    const uint64_t entityId = GetGenericEnemyEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    const bool firstSnapshot = !localPushBlocks.contains(entityId);
    localPushBlocks[entityId] = actor;
    if (transport.GetRole() == SessionRole::Host) {
        HyruleCoopPushBlockState state = {};
        const bool active = HyruleCoop_ObjOshihikiCaptureState(actor, &state) &&
                            state.phase != HYRULE_COOP_PUSH_BLOCK_ON_SCENE;
        const uint32_t interval = active ? 3 : 120;
        if (handshakeComplete && (firstSnapshot || frameCounter % interval == 0)) {
            SendPushBlockSnapshot(actor);
        }
        return;
    }
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsRemoteTimelineCompatible()) {
        return;
    }
    float direction = 0.0f;
    if (HyruleCoop_ObjOshihikiPeekPush(actor, &direction) && !pendingActorInteractionRequests.contains(entityId)) {
        SendActorInteractionIntent(entityId, gPlayState->sceneNum, ActorInteractionKind::PushBlockBegin, direction);
    }
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot != actorSnapshots.end() && !pendingActorInteractionRequests.contains(entityId)) {
        ApplyPushBlockSnapshot(actor, snapshot->second);
    }
}

void Manager::ForgetPushBlock(void* actorRef) {
    for (auto iterator = localPushBlocks.begin(); iterator != localPushBlocks.end();) {
        if (iterator->second == actorRef) {
            ClearPendingActorInteraction(iterator->first);
            iterator = localPushBlocks.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::SendDampeRaceSnapshot(void* actorRef, uint64_t acknowledgedRequestId) {
    if (transport.GetRole() != SessionRole::Host || !handshakeComplete || actorRef == nullptr ||
        gPlayState == nullptr) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorRef);
    ActorSnapshotMessage message =
        CaptureDampeRaceSnapshot(actor, gPlayState->sceneNum, frameCounter, sessionScope);
    message.acknowledgedRequestId = acknowledgedRequestId;
    actorSnapshots[message.entityId] = message;
    localDampeRaceActors[message.entityId] = actor;
    if (acknowledgedRequestId != 0) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), message.entityId);
    }
}

void Manager::UpdateDampeRaceActor(void* actorRef) {
    Actor* actor = static_cast<Actor*>(actorRef);
    if (actor == nullptr || !IsDampeRaceActorId(actor->id) || gPlayState == nullptr) {
        return;
    }
    const uint64_t entityId = GetGenericEnemyEntityId(actor, gPlayState->sceneNum, sessionScope.worldGeneration);
    localDampeRaceActors[entityId] = actor;
    if (transport.GetRole() == SessionRole::Host) {
        if (handshakeComplete && frameCounter % 3 == 0) {
            SendDampeRaceSnapshot(actor);
        }
        return;
    }
    if (transport.GetRole() != SessionRole::Client || !handshakeComplete || !IsRemoteTimelineCompatible()) {
        return;
    }
    if (actor->id == ACTOR_EN_PO_RELAY && HyruleCoop_DampeRaceConsumeStartRequest(actor) &&
        !pendingActorInteractionRequests.contains(entityId)) {
        SendActorInteractionIntent(entityId, gPlayState->sceneNum, ActorInteractionKind::DampeRaceStart);
    }
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot != actorSnapshots.end() && !pendingActorInteractionRequests.contains(entityId)) {
        ApplyDampeRaceSnapshot(actor, snapshot->second);
    }
}

void Manager::ForgetDampeRaceActor(void* actorRef) {
    for (auto iterator = localDampeRaceActors.begin(); iterator != localDampeRaceActors.end();) {
        if (iterator->second == actorRef) {
            ClearPendingActorInteraction(iterator->first);
            iterator = localDampeRaceActors.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::HandleStalchildInitialized(void* actorRef) {
    if (actorRef == nullptr || !handshakeComplete || transport.GetRole() != SessionRole::Client ||
        gPlayState == nullptr || gPlayState->sceneNum != SCENE_HYRULE_FIELD || spawningReplicatedStalchild) {
        return;
    }

    // Hyrule Field's encounter spawner chooses random positions independently on every peer. Guest-generated
    // Stalchildren therefore cannot be matched to host enemies and must not enter the shared world.
    Actor_Kill(static_cast<Actor*>(actorRef));
}

void Manager::ApplyStalchildSpawnerAuthority(void* actorRef, bool* shouldUpdate) {
    if (actorRef == nullptr || shouldUpdate == nullptr || transport.GetRole() != SessionRole::Client || gPlayState == nullptr ||
        !ShouldSuppressGuestStalchildSpawner(true, gPlayState->sceneNum == SCENE_HYRULE_FIELD,
                                             static_cast<uint16_t>(static_cast<Actor*>(actorRef)->params))) {
        return;
    }

    // Do not veto VB_ENCOUNT1_SPAWN_STALCHILD_OR_WOLFOS: its vanilla loop retries with unchanged counters. Stopping
    // this guest-only encounter manager before it enters that loop prevents native spawn/kill churn altogether.
    *shouldUpdate = false;
}

void Manager::SendStalchildSnapshot(void* actorRef, bool alive) {
    if (transport.GetRole() != SessionRole::Host || !IsSaveLoaded() || actorRef == nullptr || gPlayState == nullptr ||
        gPlayState->sceneNum != SCENE_HYRULE_FIELD) {
        return;
    }

    Actor* actor = static_cast<Actor*>(actorRef);
    if (actor->id != ACTOR_EN_SKB || actor->room != 0) {
        return;
    }

    EnSkb* stalchild = reinterpret_cast<EnSkb*>(actor);
    uint64_t entityId = stalchildIdentityRegistry.Find(actor);
    if (alive) {
        const bool nativeDeathTransition = actor->colChkInfo.health == 0 && stalchild->actionState == 1;
        if (actor->colChkInfo.health <= 0 && !nativeDeathTransition) {
            return;
        }
        entityId = stalchildIdentityRegistry.GetOrAssign(actor);
        if (entityId == 0) {
            SPDLOG_ERROR("[HyruleCoop] Stalchild identity space exhausted; refusing to replicate a new spawn");
            return;
        }
    } else if (entityId == 0) {
        // The actor died before its first host snapshot. It was never visible to the guest, so no terminal packet is
        // needed and, importantly, no acknowledged retry can be created for it.
        return;
    }

    if (!stalchildSnapshotLifecycle.ShouldPublish(entityId, alive)) {
        return;
    }

    ActorSnapshotMessage message =
        CaptureGenericEnemySnapshot(actor, gPlayState->sceneNum, frameCounter, sessionScope, alive);
    message.entityId = entityId;
    // A replicated Stalchild starts in its underground emergence animation. Its local action state is not stable
    // across peers, so carry the host's targetable transition explicitly instead of leaving the guest replica
    // permanently untargetable.
    const auto target = stalchildTargets.find(entityId);
    const StalchildTarget selectedTarget =
        target == stalchildTargets.end() ? StalchildTarget::Host : target->second;
    const auto previous = actorSnapshots.find(entityId);
    const bool attackActive = stalchild->setColliderAT != 0;
    const bool attackStarted = attackActive &&
                               (previous == actorSnapshots.end() ||
                                previous->second.adapterState[kStalchildAdapterAttackActive] == 0);
    uint16_t& attackSequence = stalchildAttackSequences[entityId];
    if (attackStarted) {
        attackSequence = static_cast<uint16_t>((attackSequence % kStalchildStateAttackSequenceMask) + 1);
    }
    message.stateId = EncodeStalchildState((actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0, selectedTarget,
                                           attackSequence);
    message.animationFrame = stalchild->skelAnime.curFrame;
    message.animationSpeed = stalchild->skelAnime.playSpeed;
    message.adapterWordCount = kStalchildAdapterWordCount;
    message.adapterState[kStalchildAdapterTarget] = static_cast<int16_t>(selectedTarget);
    message.adapterState[kStalchildAdapterBehavior] = stalchild->actionState;
    message.adapterState[kStalchildAdapterAttackActive] = stalchild->setColliderAT != 0;
    message.adapterState[kStalchildAdapterShapeYOffset] = static_cast<int16_t>(std::clamp<long>(
        std::lround(actor->shape.yOffset), kStalchildMinimumShapeYOffset, kStalchildMaximumShapeYOffset));
    message.adapterState[kStalchildAdapterShadowScale] = static_cast<int16_t>(std::clamp<long>(
        std::lround(actor->shape.shadowScale * kStalchildShadowScalePrecision),
        kStalchildMinimumShadowScale, kStalchildMaximumShadowScale));
    if (!alive && previous != actorSnapshots.end() && !previous->second.alive) {
        return;
    }

    const bool importantTransition = previous == actorSnapshots.end() || previous->second.alive != message.alive ||
                                     previous->second.health != message.health ||
                                     previous->second.stateId != message.stateId ||
                                     previous->second.adapterState[kStalchildAdapterBehavior] !=
                                         message.adapterState[kStalchildAdapterBehavior] ||
                                     previous->second.adapterState[kStalchildAdapterAttackActive] !=
                                         message.adapterState[kStalchildAdapterAttackActive];
    actorSnapshots[entityId] = message;
    if (automatedTestEnabled && automatedTestStage == TestStalchildCombat &&
        entityId == automatedTestTargetEntityId && importantTransition) {
        const int16_t facingDelta = actor->yawTowardsPlayer - actor->shape.rot.y;
        ReportAutomatedTest("stalchild-host-state",
                            "action=" + std::to_string(stalchild->actionState) +
                                " attack=" + std::to_string(stalchild->setColliderAT != 0) +
                                " target=" + std::to_string(static_cast<int>(selectedTarget)) +
                                " distance=" + std::to_string(actor->xzDistToPlayer) +
                                " facingDelta=" + std::to_string(facingDelta));
    }
    if (alive) {
        localStalchildren[entityId] = actor;
    }
    if (importantTransition) {
        transport.SendAcknowledgedRealtime(MessageType::ActorSnapshot, EncodeActorSnapshot(message), entityId);
    } else {
        transport.Send(MessageType::ActorSnapshot, EncodeActorSnapshot(message), entityId);
    }
}

void Manager::UpdateStalchild(void* actorRef) {
    if (actorRef == nullptr || gPlayState == nullptr || gPlayState->sceneNum != SCENE_HYRULE_FIELD) {
        return;
    }
    if (transport.GetRole() == SessionRole::Host) {
        if (frameCounter % kStalchildSnapshotIntervalFrames == 0) {
            SendStalchildSnapshot(actorRef, true);
        }
        return;
    }
    // Guests are advanced entirely by ApplyStalchildAuthority. Keeping a second post-update reconciliation path can
    // observe the same collision twice if another hook restores ShouldActorUpdate after this adapter suppresses it.
}

void Manager::ApplyStalchildAuthority(void* actorRef, bool* shouldUpdate) {
    if (!handshakeComplete || !IsSaveLoaded() || actorRef == nullptr || shouldUpdate == nullptr ||
        gPlayState == nullptr || gPlayState->sceneNum != SCENE_HYRULE_FIELD) {
        return;
    }

    Actor* actor = static_cast<Actor*>(actorRef);
    if (transport.GetRole() == SessionRole::Host) {
        const uint64_t entityId = stalchildIdentityRegistry.GetOrAssign(actor);
        if (entityId == 0) {
            return;
        }
        localStalchildren[entityId] = actor;

        const Player* localPlayer = GET_PLAYER(gPlayState);
        const bool guestAvailable = remotePlayerSnapshot.has_value() &&
                                    remotePlayerSnapshot->scene == SCENE_HYRULE_FIELD &&
                                    remotePlayerSnapshot->room == actor->room && IsRemoteTimelineCompatible();
        float hostPosition[3] = { localPlayer->actor.world.pos.x, localPlayer->actor.world.pos.y,
                                  localPlayer->actor.world.pos.z };
        const float actorPosition[3] = { actor->world.pos.x, actor->world.pos.y, actor->world.pos.z };
        const float hostDistanceSquared = DistanceSquared(actorPosition, hostPosition);
        const float guestDistanceSquared =
            guestAvailable ? DistanceSquared(actorPosition, remotePlayerSnapshot->position) : 0.0f;
        const auto previous = stalchildTargets.find(entityId);
        const StalchildTarget priorTarget =
            previous == stalchildTargets.end() ? StalchildTarget::Host : previous->second;
        const EnSkb* stalchild = reinterpret_cast<const EnSkb*>(actor);
        const bool targetLocked = stalchild->actionState == 3 || stalchild->actionState == 5;
        const StalchildTarget selected = SelectStalchildTarget(guestAvailable, hostDistanceSquared,
                                                               guestDistanceSquared, priorTarget, targetLocked);
        stalchildTargets[entityId] = selected;
        OverrideActorPlayerTracking(
            actor, selected == StalchildTarget::Guest ? remotePlayerSnapshot->position : hostPosition);
        if (selected == StalchildTarget::Guest && targetLocked) {
            // Vanilla accepts an attack while the Stalchild is still roughly 25 degrees off-axis. That works against
            // the local Link because both AI and collision use the same player, but a replicated swing must commit to
            // the shared target so every peer renders and collides with the same attack arc.
            actor->shape.rot.y = actor->yawTowardsPlayer;
            actor->world.rot.y = actor->yawTowardsPlayer;
        }
        return;
    }
    if (transport.GetRole() != SessionRole::Client) {
        return;
    }

    const uint64_t entityId = stalchildIdentityRegistry.Find(actor);
    const auto local = entityId == 0 ? localStalchildren.end() : localStalchildren.find(entityId);
    if (local == localStalchildren.end() || local->second != actor || retiredStalchildren.contains(entityId)) {
        // This is a locally generated encounter-spawner actor, or a local actor retired while waiting for the host's
        // dawn/despawn snapshot. Never let it run independently.
        Actor_Kill(actor);
        *shouldUpdate = false;
        return;
    }

    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot == actorSnapshots.end() || !snapshot->second.alive) {
        ClearLocalTarget(actor);
        Actor_Kill(actor);
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
        if (guestStalchildTargetThisSwing == entityId) {
            guestStalchildTargetThisSwing.reset();
        }
        localStalchildren.erase(local);
        *shouldUpdate = false;
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    EnSkb* stalchildReplica = reinterpret_cast<EnSkb*>(actor);
    const bool participatesInAutomatedCombat =
        !automatedTestEnabled || automatedTestStage != TestStalchildCombat || automatedTestTargetEntityId == 0 ||
        entityId == automatedTestTargetEntityId;
    const bool nativeSwordHit = player != nullptr && player->meleeWeaponState > 0 &&
                                (stalchildReplica->collider.base.acFlags & AC_HIT) != 0;
    if ((stalchildReplica->collider.base.acFlags & AC_HIT) != 0) {
        // Guest replicas do not run EnSkb_Update, so consume the collision edge here instead of leaving AC_HIT
        // latched across every frame of the same sword action.
        stalchildReplica->collider.base.acFlags &= ~AC_HIT;
    }
    const auto previousHit = lastGuestAttackTick.find(entityId);
    const bool outsideDuplicateWindow =
        previousHit == lastGuestAttackTick.end() || frameCounter - previousHit->second >= 12;
    std::optional<uint64_t> focusedStalchild;
    if (player != nullptr && player->focusActor != nullptr) {
        for (const auto& [candidateId, candidateActor] : localStalchildren) {
            if (candidateActor == player->focusActor) {
                focusedStalchild = candidateId;
                break;
            }
        }
    }
    const bool intendedStalchild = focusedStalchild.has_value() ? focusedStalchild.value() == entityId : true;
    const bool stalchildAvailableThisSwing = intendedStalchild &&
                                             (!guestStalchildTargetThisSwing.has_value() ||
                                              guestStalchildTargetThisSwing.value() == entityId);
    if (nativeSwordHit && stalchildAvailableThisSwing && !genericGuestTargetsHitThisSwing.contains(entityId) &&
        !pendingGuestAttacks.contains(entityId) && outsideDuplicateWindow) {
        guestStalchildTargetThisSwing = entityId;
        genericGuestTargetsHitThisSwing.insert(entityId);
        pendingGuestAttacks.insert(entityId);
        lastGuestAttackTick[entityId] = frameCounter;
        if (automatedTestEnabled && automatedTestStage == TestStalchildCombat) {
            if (automatedTestPhysicalHits == 0 && automatedTestCombatPhase == CombatFirstSwing &&
                !automatedTestSwingObserved) {
                automatedTestSwingObserved = true;
                ReportAutomatedTest("stalchild-client-sword-state-entered",
                                    "active sword collider reached the host replica");
            }
            if (automatedTestPhysicalHits == 1 && automatedTestCombatPhase == CombatSecondSwing &&
                automatedTestLastObservedHealth == 1) {
                // This hook observes the live sword collider before the later per-frame verifier. Record the accepted
                // melee state here so the proof orders the cause before its physical contact.
                automatedTestLastObservedHealth = -1;
                ReportAutomatedTest("stalchild-client-second-sword-state-entered",
                                    "active follow-up sword collider reached the host replica");
            }
            ++automatedTestPhysicalHits;
            ReportAutomatedTest("stalchild-client-physical-collision",
                                "hit=" + std::to_string(automatedTestPhysicalHits));
        }
        SendAttackIntent(entityId, gPlayState->sceneNum, 3);
    }

    ApplyStalchildSnapshot(actor, snapshot->second, false);
    EnSkb_AdvanceCoopAnimation(stalchildReplica);
    if (IsStalchildTargetable(snapshot->second.stateId)) {
        actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    } else {
        actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    }

    // The host advances private Stalchild AI. The guest is a render/collision replica whose sword AC remains a local
    // physical contact probe. Enemy damage is applied from the acknowledged host attack edge below.
    const bool attacksGuest = participatesInAutomatedCombat &&
                              DecodeStalchildTarget(snapshot->second.stateId) == StalchildTarget::Guest &&
                              snapshot->second.adapterState[kStalchildAdapterAttackActive] != 0;
    auto [appliedSequence, inserted] = appliedStalchildAttackSequences.try_emplace(
        entityId, DecodeStalchildAttackSequence(snapshot->second.stateId));
    const uint16_t incomingSequence = DecodeStalchildAttackSequence(snapshot->second.stateId);
    if (!inserted && IsNewerStalchildAttackSequence(incomingSequence, appliedSequence->second)) {
        if (attacksGuest && player != nullptr && player->invincibilityTimer <= 0) {
            const float actorPosition[3] = { actor->world.pos.x, actor->world.pos.y, actor->world.pos.z };
            const float playerPosition[3] = { player->actor.world.pos.x, player->actor.world.pos.y,
                                              player->actor.world.pos.z };
            if (HorizontalDistanceSquared(actorPosition, playerPosition) <= SQ(90.0f) &&
                std::abs(actor->world.pos.y - player->actor.world.pos.y) <= 80.0f) {
                // The host owns the attack phase and target. Commit damage on the deduplicated attack edge through
                // the same callback used by native enemies; queued knockback can be lost or strand a replica-driven
                // player in a damage action without ever applying health loss.
                if (automatedTestEnabled && automatedTestStage == TestStalchildCombat) {
                    player->stateFlags1 &= ~(PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING);
                    player->csAction = 0;
                    gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                }
                const int16_t healthBefore = gSaveContext.health;
                if (gPlayState->damagePlayer != nullptr) {
                    gPlayState->damagePlayer(gPlayState, -4);
                } else {
                    Health_ChangeBy(gPlayState, -4);
                }
                // Native damage callbacks are not consistent success predicates: some return zero after applying
                // damage. Deduplicate the authoritative attack from the health state they actually committed.
                const bool damageApplied = gSaveContext.health < healthBefore;
                if (damageApplied) {
                    appliedSequence->second = incomingSequence;
                }
                if (damageApplied && automatedTestEnabled && automatedTestStage == TestStalchildCombat) {
                    ReportAutomatedTest("stalchild-client-authoritative-attack-applied",
                                        "sequence=" + std::to_string(incomingSequence));
                }
            } else {
                appliedSequence->second = incomingSequence;
            }
        } else {
            appliedSequence->second = incomingSequence;
        }
    }
    EnSkb_RegisterCoopCollisions(stalchildReplica, gPlayState, false);
    *shouldUpdate = false;
}

void Manager::EnsureRemoteStalchild(const ActorSnapshotMessage& message) {
    if (gPlayState == nullptr || gPlayState->sceneNum != message.scene ||
        gPlayState->roomCtx.curRoom.num != message.room || retiredStalchildren.contains(message.entityId)) {
        return;
    }

    const auto existing = localStalchildren.find(message.entityId);
    if (existing != localStalchildren.end()) {
        // The per-frame authority hook interpolates toward the newest snapshot. Applying packets here as well makes
        // arrival jitter visible and advances replicas twice on packet frames.
        return;
    }

    spawningReplicatedStalchild = true;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_SKB, message.position[0], message.position[1],
                               message.position[2], message.worldRotation[0], message.worldRotation[1],
                               message.worldRotation[2], message.params);
    spawningReplicatedStalchild = false;
    if (actor == nullptr) {
        SPDLOG_WARN("[HyruleCoop] Unable to spawn host Stalchild entity {}", message.entityId);
        return;
    }

    actor->room = message.room;
    actor->home.pos = { message.homePosition[0], message.homePosition[1], message.homePosition[2] };
    localStalchildren.emplace(message.entityId, actor);
    // Associate the guest actor with the host-issued ID without deriving identity from its random local spawn data.
    if (!stalchildIdentityRegistry.Bind(actor, message.entityId)) {
        localStalchildren.erase(message.entityId);
        Actor_Kill(actor);
        return;
    }
    ApplyStalchildSnapshot(actor, message, true);
    if (IsStalchildTargetable(message.stateId)) {
        actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    } else {
        actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    }
}

void Manager::ForgetStalchild(void* actorRef) {
    uint64_t entityId = stalchildIdentityRegistry.Find(actorRef);
    if (entityId != 0) {
        stalchildIdentityRegistry.Forget(actorRef);
    }

    for (auto iterator = localStalchildren.begin(); iterator != localStalchildren.end();) {
        if (iterator->second == actorRef) {
            entityId = iterator->first;
            ClearPendingGuestAttack(entityId);
            lastGuestAttackTick.erase(entityId);
            genericGuestTargetsHitThisSwing.erase(entityId);
            if (guestStalchildTargetThisSwing == entityId) {
                guestStalchildTargetThisSwing.reset();
            }
            stalchildTargets.erase(entityId);
            stalchildAttackSequences.erase(entityId);
            appliedStalchildAttackSequences.erase(entityId);
            if (transport.GetRole() == SessionRole::Client && actorSnapshots.contains(entityId) &&
                actorSnapshots[entityId].alive) {
                // A locally simulated dawn/despawn must not race a delayed host death packet into a spawn/kill loop.
                // This ID is terminal locally; the host will use a fresh ID for the next night spawn.
                retiredStalchildren.insert(entityId);
            }
            stalchildSnapshotLifecycle.Forget(entityId);
            iterator = localStalchildren.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::ClearStalchildSceneState() {
    for (const auto& [entityId, actor] : localStalchildren) {
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
    }
    localStalchildren.clear();
    stalchildTargets.clear();
    stalchildAttackSequences.clear();
    appliedStalchildAttackSequences.clear();
    retiredStalchildren.clear();
    guestStalchildTargetThisSwing.reset();
    stalchildIdentityRegistry.Clear();
    stalchildSnapshotLifecycle.Clear();
    for (auto iterator = actorSnapshots.begin(); iterator != actorSnapshots.end();) {
        if (IsDynamicStalchildEntityId(iterator->first)) {
            iterator = actorSnapshots.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Manager::ClearStalchildSessionState() {
    ClearStalchildSceneState();
    stalchildIdentityRegistry.Reset();
    spawningReplicatedStalchild = false;
}

void Manager::UpdateGenericGuestAttack() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Client || !IsSaveLoaded() || gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || player->meleeWeaponState <= 0) {
        genericGuestTargetsHitThisSwing.clear();
        guestStalchildTargetThisSwing.reset();
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
    const auto snapshot = actorSnapshots.find(entityId);
    if (snapshot != actorSnapshots.end() && !snapshot->second.alive) {
        Actor_Kill(static_cast<Actor*>(actor));
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
        localGohmas.erase(entityId);
        localGohmaDeathPresentations.erase(entityId);
        *shouldUpdate = false;
        return;
    }
    if (snapshot != actorSnapshots.end() && snapshot->second.health <= 0 && snapshot->second.stateId == 2) {
        if (localGohmaDeathPresentations.insert(entityId).second) {
            if (StartGohmaDefeatPresentation(actor, gPlayState, snapshot->second)) {
                SPDLOG_INFO("[HyruleCoop] Starting local Gohma defeat presentation for entity {}", entityId);
            } else {
                localGohmaDeathPresentations.erase(entityId);
            }
        }
        ClearPendingGuestAttack(entityId);
        lastGuestAttackTick.erase(entityId);
        genericGuestTargetsHitThisSwing.erase(entityId);
        // Defeat is durable host-owned state, but the camera, audio, decay animation, heart, and warp are a
        // presentation each peer must advance locally. Reapplying host timer fields would freeze that sequence.
        *shouldUpdate = true;
        return;
    }
    if (ConsumeGohmaHit(actor)) {
        if (automatedTestEnabled && automatedTestStage == TestBossCombat &&
            automatedTestCombatPhase == CombatSecondSwing) {
            ReportAutomatedTest("gohma-second-collider-contact",
                                "guest Gohma collider observed Link's follow-up sword contact");
        }
        const auto lastAttack = lastGuestAttackTick.find(entityId);
        const bool newSwing = lastAttack == lastGuestAttackTick.end() ||
                              frameCounter - lastAttack->second >= kGuestAttackCooldownFrames;
        if (newSwing && !genericGuestTargetsHitThisSwing.contains(entityId) &&
            !pendingGuestAttacks.contains(entityId)) {
            genericGuestTargetsHitThisSwing.insert(entityId);
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
    if (snapshot != actorSnapshots.end()) {
        ApplyGohmaSnapshot(actor, snapshot->second);
        RegisterGohmaGuestCollision(actor, gPlayState);
    }
    *shouldUpdate = false;
}

void Manager::ForgetGohma(void* actor) {
    for (auto iterator = localGohmas.begin(); iterator != localGohmas.end();) {
        if (iterator->second == actor) {
            // Room or scene reconstruction can destroy the guest replica before the host acknowledges a physical
            // hit. Keep that bounded retry alive; an authoritative response, terminal snapshot, or peer reset owns
            // cancellation. Otherwise packet loss during reconstruction can silently eat the lethal swing.
            localGohmaDeathPresentations.erase(iterator->first);
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
                                     (automatedTestStage == TestAttacking || automatedTestStage == TestGenericCombat ||
                                      automatedTestStage == TestStalchildCombat ||
                                      automatedTestStage == TestBossCombat);
    const bool hostBossInput = !automatedTestClient && automatedTestStage == TestBossCombat;
    const bool hostGenericInput = !automatedTestClient && automatedTestStage == TestGenericCombat;
    if (!automatedTestEnabled || !IsSaveLoaded() || actorRef == nullptr || actorRef != GET_PLAYER(gPlayState)) {
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
    } else if (automatedTestStage == TestAttacking || automatedTestStage == TestGenericCombat ||
               automatedTestStage == TestStalchildCombat) {
        const uint32_t combatPhaseElapsed =
            static_cast<uint32_t>(automatedTestTick) - automatedTestCombatPhaseTick;
        if (automatedTestCombatPhase >= CombatAcquireTarget && automatedTestCombatPhase <= CombatAwaitDeath) {
            const bool pulseStalchildTarget = automatedTestStage == TestStalchildCombat;
            if (!pulseStalchildTarget) {
                buttons |= BTN_Z;
            }
        }
        const bool shouldSwing = (automatedTestStage == TestGenericCombat ||
                                  automatedTestStage == TestStalchildCombat)
                                     ? (automatedTestCombatPhase == CombatFirstSwing ||
                                        automatedTestCombatPhase == CombatSecondSwing)
                                     : (automatedTestCombatPhase == CombatFirstSwing ||
                                        automatedTestCombatPhase == CombatSecondSwing);
        const uint32_t swingOffset = 1;
        if (shouldSwing && combatPhaseElapsed >= swingOffset &&
            (combatPhaseElapsed - swingOffset) % 36 < 6) {
            buttons |= BTN_B;
            if (automatedTestStage == TestStalchildCombat && combatPhaseElapsed == swingOffset) {
                ReportAutomatedTest(automatedTestCombatPhase == CombatFirstSwing
                                        ? "stalchild-client-first-swing-input"
                                        : "stalchild-client-second-swing-input",
                                    "bounded B input retries no faster than every 36 updates");
            }
        }
    } else if (automatedTestStage == TestBossCombat) {
        if (automatedTestCombatPhase == CombatMove) {
            stickY = 55;
        }
        const uint32_t combatPhaseElapsed =
            static_cast<uint32_t>(automatedTestTick) - automatedTestCombatPhaseTick;
        if (automatedTestCombatPhase == CombatAcquireTarget) {
            if (combatPhaseElapsed >= 6 && (combatPhaseElapsed - 6) % 30 == 0) {
                buttons |= BTN_Z;
            }
        } else if (automatedTestCombatPhase >= CombatFirstSwing &&
                   automatedTestCombatPhase <= CombatAwaitDeath) {
            buttons |= BTN_Z;
        }
        if ((automatedTestCombatPhase == CombatFirstSwing || automatedTestCombatPhase == CombatSecondSwing) &&
            combatPhaseElapsed >= 1 && (combatPhaseElapsed - 1) % 36 < 6) {
            buttons |= BTN_B;
            if (automatedTestCombatPhase == CombatSecondSwing && combatPhaseElapsed == 1) {
                ReportAutomatedTest("gohma-second-swing-input",
                                    "bounded B input started after first synchronized damage");
            }
        }
        if (automatedTestCombatPhase == CombatVerifyCleanup &&
            automatedTestTick - automatedTestCombatPhaseTick == 1) {
            buttons |= BTN_Z | BTN_B;
        }
    }

    const bool shouldPrimeTarget = automatedTestTargetActor != nullptr &&
                                    (automatedTestCombatPhase == CombatAcquireTarget &&
                                     automatedTestStage != TestStalchildCombat);
    if (shouldPrimeTarget) {
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
    if (transport.GetRole() == SessionRole::Client && !storyPresentationBaselineApplied) {
        doorOfTimeOpeningPresented =
            HasSharedEventFlag(state, EVENTCHKINF_OPENED_THE_DOOR_OF_TIME);
        masterSwordEntrancePresented =
            HasSharedEventFlag(state, EVENTCHKINF_ENTERED_MASTER_SWORD_CHAMBER);
        masterSwordPullPresented =
            HasSharedEventFlag(state, EVENTCHKINF_PULLED_MASTER_SWORD_FROM_PEDESTAL);
        storyPresentationBaselineApplied = true;
    }
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

void Manager::ReconcileAuthoritativeSceneActor(void* actorRef) {
    // Either participant may perform the event. Reconcile both peers from the
    // host-owned flags so guest-originated songs also update the host's live
    // actor instead of waiting for an area reload.
    if (!handshakeComplete || !IsSaveLoaded() || actorRef == nullptr || gPlayState == nullptr) {
        return;
    }

    Actor* actor = static_cast<Actor*>(actorRef);
    switch (actor->id) {
        case ACTOR_OBJ_TIMEBLOCK: {
            ObjTimeblock* block = static_cast<ObjTimeblock*>(actorRef);
            if (block->unk_177 == 0 || block->demoEffectFirstPartTimer > 0) {
                return;
            }
            const uint8_t switchValue = Flags_GetSwitch(gPlayState, block->dyna.actor.params & 0x3F) ? 1 : 0;
            if (block->unk_174 == switchValue) {
                return;
            }
            block->unk_174 = switchValue;
            block->isVisible = ObjTimeblock_CalculateIsVisible(block);
            if ((block->dyna.actor.params >> 10) & 1) {
                if (block->isVisible) {
                    ObjTimeblock_SetupAltBehaviorVisible(block);
                } else {
                    ObjTimeblock_SetupAltBehaviourNotVisible(block);
                }
            } else {
                ObjTimeblock_SetupNormal(block);
            }
            break;
        }
        case ACTOR_EN_MD: {
            if (gPlayState->sceneNum != SCENE_LOST_WOODS ||
                !Flags_GetEventChkInf(EVENTCHKINF_PLAYED_SARIAS_SONG_FOR_MIDO_AS_ADULT)) {
                return;
            }
            EnMd* mido = static_cast<EnMd*>(actorRef);
            if (mido->actionFunc == EnMd_Idle) {
                return;
            }
            if (EnMd_SetMovedPos(mido, gPlayState)) {
                mido->actor.prevPos = mido->actor.world.pos;
                mido->actor.speedXZ = 0.0f;
                mido->interactInfo.talkState = NPC_TALK_STATE_IDLE;
                mido->actionFunc = EnMd_Idle;
            }
            break;
        }
        case ACTOR_BG_SPOT02_OBJECTS: {
            BgSpot02Objects* graveyardObject = static_cast<BgSpot02Objects*>(actorRef);
            if (gPlayState->sceneNum == SCENE_GRAVEYARD &&
                Flags_GetEventChkInf(EVENTCHKINF_DESTROYED_ROYAL_FAMILY_TOMB) &&
                (graveyardObject->dyna.actor.params == 2 || graveyardObject->dyna.actor.params == 3)) {
                Actor_Kill(&graveyardObject->dyna.actor);
            }
            break;
        }
        default:
            break;
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

void Manager::ReconcileDoorOfTime(void* actorRef) {
    if (!IsSaveLoaded() || actorRef == nullptr || gPlayState->sceneNum != SCENE_TEMPLE_OF_TIME ||
        !Flags_GetEventChkInf(EVENTCHKINF_OPENED_THE_DOOR_OF_TIME) || Play_InCsMode(gPlayState)) {
        return;
    }
    DemoKankyo* door = static_cast<DemoKankyo*>(actorRef);
    if (door->actor.params != DEMOKANKYO_DOOR_OF_TIME) {
        return;
    }
    if (handshakeComplete && !doorOfTimeOpeningPresented &&
        gSaveContext.sceneLayer < SCENE_LAYER_CUTSCENE_FIRST) {
        doorOfTimeOpeningPresented = true;
        applyingAuthoritativeState = true;
        EnOkarinaTag_PlayDoorOfTimeCutscene(nullptr, gPlayState);
        applyingAuthoritativeState = false;
        return;
    }
    gPlayState->roomCtx.unk_74[1] = 0xFF;
    if (door->actor.child != nullptr) {
        Actor_Kill(door->actor.child);
    }
    Actor_Kill(&door->actor);
}

void Manager::ReconcileMasterSwordChamber(void* actorRef) {
    if (!handshakeComplete || !IsSaveLoaded() || actorRef == nullptr || masterSwordEntrancePresented ||
        gPlayState->sceneNum != SCENE_TEMPLE_OF_TIME ||
        !Flags_GetEventChkInf(EVENTCHKINF_ENTERED_MASTER_SWORD_CHAMBER) ||
        gSaveContext.sceneLayer >= SCENE_LAYER_CUTSCENE_FIRST || Play_InCsMode(gPlayState)) {
        return;
    }
    masterSwordEntrancePresented = true;
    applyingAuthoritativeState = true;
    BgTokiSwd_PlayEntranceCutscene(gPlayState);
    applyingAuthoritativeState = false;
}

void Manager::RefreshRemotePlayer() {
    if (!IsSaveLoaded() || !handshakeComplete || !remotePlayerSnapshot.has_value() || remotePlayer != nullptr ||
        preparingRemotePlayer) {
        return;
    }

    ReclaimRemotePlayerActor();
    if (remotePlayer != nullptr) {
        return;
    }

    const PlayerSnapshotMessage& state = remotePlayerSnapshot.value();
    if (!IsRemotePlayerVisibleInRoom(gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num, state.scene,
                                     state.room, GetLocalTimelineScope(), SnapshotTimelineScope(state))) {
        return;
    }

    preparingRemotePlayer = true;
    remotePlayer = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, state.position[0], state.position[1],
                               state.position[2], state.rotation[0], state.rotation[1], state.rotation[2], 0);
    preparingRemotePlayer = false;
}

void Manager::ReclaimRemotePlayerActor() {
    if (gPlayState == nullptr) {
        return;
    }

    const int16_t activeRoom = gPlayState->roomCtx.curRoom.num;
    Actor* survivor = nullptr;
    uint32_t retired = 0;
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; actor != nullptr;) {
        Actor* next = actor->next;
        if (actor->update == HyruleCoopRemotePlayer_Update) {
            if (survivor == nullptr && IsRemotePlayerActorInActiveRoom(actor->room, activeRoom)) {
                survivor = actor;
            } else {
                Actor_Kill(actor);
                ++retired;
            }
        }
        actor = next;
    }

    remotePlayer = survivor;
    if (retired != 0) {
        SPDLOG_WARN("[HyruleCoop] Retired {} stale remote player actor(s) in scene {} room {}", retired,
                    gPlayState->sceneNum, activeRoom);
    }
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

void Manager::RefreshRemoteHorse() {
    if (!IsSaveLoaded() || !handshakeComplete || !remotePlayerSnapshot.has_value() ||
        !remotePlayerSnapshot->mounted) {
        DestroyRemoteHorse();
        return;
    }
    if (remoteHorse != nullptr || preparingRemoteHorse || gPlayState == nullptr) {
        return;
    }

    ReclaimRemoteHorseActor();
    if (remoteHorse != nullptr) {
        return;
    }

    const PlayerSnapshotMessage& state = remotePlayerSnapshot.value();
    if (!IsRemotePlayerVisibleInRoom(gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num, state.scene,
                                     state.room, GetLocalTimelineScope(), SnapshotTimelineScope(state))) {
        return;
    }

    preparingRemoteHorse = true;
    remoteHorse = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_HORSE, state.horsePosition[0],
                              state.horsePosition[1], state.horsePosition[2], state.horseRotation[0],
                              state.horseRotation[1], state.horseRotation[2], 1);
    preparingRemoteHorse = false;
}

void Manager::ReclaimRemoteHorseActor() {
    if (gPlayState == nullptr) {
        return;
    }

    const int16_t activeRoom = gPlayState->roomCtx.curRoom.num;
    Actor* survivor = nullptr;
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; actor != nullptr;) {
        Actor* next = actor->next;
        if (actor->update == HyruleCoopRemoteHorse_Update) {
            if (survivor == nullptr && IsRemotePlayerActorInActiveRoom(actor->room, activeRoom)) {
                survivor = actor;
            } else {
                Actor_Kill(actor);
            }
        }
        actor = next;
    }
    remoteHorse = survivor;
}

void Manager::DestroyRemoteHorse() {
    if (remoteHorse == nullptr || gPlayState == nullptr) {
        remoteHorse = nullptr;
        return;
    }
    Actor* actor = static_cast<Actor*>(remoteHorse);
    if (actor->update == HyruleCoopRemoteHorse_Update) {
        Actor_Kill(actor);
    }
    remoteHorse = nullptr;
}

bool Manager::IsRemoteTimelineCompatible() const {
    return IsSaveLoaded() && remotePlayerSnapshot.has_value() &&
           IsSameTimeline(GetLocalTimelineScope(), SnapshotTimelineScope(*remotePlayerSnapshot));
}

bool Manager::PrepareBarrierTimeline(const BarrierState& state) {
    const TimelineScope target{ state.targetLinkAge, state.targetSceneLayer };
    if (!IsValidTimelineScope(target) || state.targetNight > 1 || gPlayState == nullptr) {
        return false;
    }

    // A barrier is an explicit travel operation. It is the only place where a participant adopts another
    // timeline, before the destination scene is loaded. Normal shared-progression snapshots preserve age.
    gSaveContext.dayTime = state.targetDayTime;
    gSaveContext.skyboxTime = state.targetSkyboxTime;
    gSaveContext.nightFlag = state.targetNight;
    gTimeSpeed = state.targetTimeSpeed;
    if (gPlayState->linkAgeOnLoad != target.linkAge) {
        // SwitchAge deliberately toggles the loaded age while the save still identifies the departing age. Scene
        // teardown uses that mismatch to archive the old equipment and restore this player's destination-age set.
        gSaveContext.linkAge = gPlayState->linkAgeOnLoad;
        SwitchAge();
        if (state.targetEntrance >= 0) {
            gPlayState->nextEntranceIndex = state.targetEntrance;
        }
        if (state.targetCutsceneIndex >= 0) {
            gSaveContext.nextCutsceneIndex = static_cast<uint16_t>(state.targetCutsceneIndex);
        }
        return false;
    }
    gSaveContext.linkAge = target.linkAge;
    return true;
}

bool Manager::IsBarrierTimelineReady(const BarrierState& state) const {
    if (gPlayState == nullptr || gPlayState->linkAgeOnLoad != state.targetLinkAge ||
        gSaveContext.linkAge != state.targetLinkAge) {
        return false;
    }

    // Explicit travel barriers are created before the destination loads, so targetSceneLayer describes the
    // departing scene. Once the requested destination is loaded, its scene layer has already been derived from
    // the synchronized age and campaign state and must not be compared with that stale source value.
    return gPlayState->sceneNum == state.targetScene || gSaveContext.sceneLayer == state.targetSceneLayer;
}

void Manager::PopulateBarrierTimeline(BarrierState& state) const {
    const TimelineScope timeline = GetLocalTimelineScope();
    state.targetLinkAge = timeline.linkAge;
    state.targetSceneLayer = timeline.sceneLayer;
    state.targetDayTime = gSaveContext.dayTime;
    state.targetSkyboxTime = gSaveContext.skyboxTime;
    state.targetTimeSpeed = gTimeSpeed;
    state.targetNight = gSaveContext.nightFlag != 0;
}

void Manager::NotifyLocalStoryEvent(StoryEventKind kind) {
    if (!ShouldCoordinateTempleStory(kind, false)) {
        return;
    }
    if (transport.GetRole() == SessionRole::Host) {
        SendStoryEventCommand(kind, playerId, nextRequestId++);
    } else if (transport.GetRole() == SessionRole::Client) {
        SendStoryEventIntent(kind);
    }
}

void Manager::NotifyMasterSwordPullStarted() {
    if (applyingAuthoritativeState) {
        return;
    }
    masterSwordPullPresented = true;
    coordinatedMasterSwordPullActive =
        handshakeComplete && ShouldCoordinateTempleStory(StoryEventKind::MasterSwordPull, false);
    if (coordinatedMasterSwordPullActive) {
        NotifyLocalStoryEvent(StoryEventKind::MasterSwordPull);
    }
}

bool Manager::ShouldCoordinateTempleStory(StoryEventKind kind, bool remoteInitiated) const {
    (void)remoteInitiated;
    if (!handshakeComplete || !IsSaveLoaded() || !remotePlayerSnapshot.has_value() ||
        gPlayState->sceneNum != SCENE_TEMPLE_OF_TIME ||
        (kind != StoryEventKind::DoorOfTimeOpening &&
         kind != StoryEventKind::MasterSwordChamberEntrance &&
         kind != StoryEventKind::MasterSwordPull)) {
        return false;
    }
    const PlayerSnapshotMessage& remote = remotePlayerSnapshot.value();
    return remote.scene == SCENE_TEMPLE_OF_TIME &&
           IsSameTimeline(GetLocalTimelineScope(), SnapshotTimelineScope(remote));
}

bool Manager::ShouldReplayTempleStoryPresentation(StoryEventKind kind) const {
    if (!IsSaveLoaded() || gPlayState->sceneNum != SCENE_TEMPLE_OF_TIME || Play_InCsMode(gPlayState)) {
        return false;
    }
    if (kind == StoryEventKind::MasterSwordChamberEntrance) {
        return !masterSwordEntrancePresented && gSaveContext.sceneLayer < SCENE_LAYER_CUTSCENE_FIRST;
    }
    if (kind == StoryEventKind::MasterSwordPull) {
        return !masterSwordPullPresented && gPlayState->linkAgeOnLoad == LINK_AGE_CHILD &&
               gSaveContext.sceneLayer < SCENE_LAYER_CUTSCENE_FIRST;
    }
    return kind == StoryEventKind::DoorOfTimeOpening && !doorOfTimeOpeningPresented;
}

void Manager::ReplayTempleStoryPresentation(StoryEventKind kind) {
    if (!ShouldReplayTempleStoryPresentation(kind)) {
        return;
    }
    if (kind == StoryEventKind::DoorOfTimeOpening) {
        doorOfTimeOpeningPresented = true;
        EnOkarinaTag_PlayDoorOfTimeCutscene(nullptr, gPlayState);
    } else if (kind == StoryEventKind::MasterSwordChamberEntrance) {
        masterSwordEntrancePresented = true;
        BgTokiSwd_PlayEntranceCutscene(gPlayState);
    } else if (kind == StoryEventKind::MasterSwordPull) {
        masterSwordPullPresented = true;
        coordinatedMasterSwordPullActive = true;
        BgTokiSwd_PlayPullCutscene(gPlayState);
    }
}

void Manager::BeginStoryBarrier(StoryEventKind storyEvent) {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }
    if (storyEvent != StoryEventKind::CastleEscape) {
        pendingStoryEvent = StoryEventKind::None;
        return;
    }
    pendingStoryEvent = StoryEventKind::None;
    activeStoryEvent = storyEvent;
    storyCutsceneObserved = false;

    BarrierState state;
    state.operationEpoch = nextOperationEpoch++;
    state.scope = sessionScope;
    state.kind = BarrierKind::StoryEvent;
    state.phase = BarrierPhase::Prepare;
    state.manifestHash = HashCapabilities(negotiatedCapabilities);
    state.targetScene = SCENE_HYRULE_FIELD;
    state.targetRoom = -1;
    state.targetEntrance = ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN;
    state.targetCutsceneIndex = 0xFFF1;
    PopulateBarrierTimeline(state);
    state.deadlineTick = static_cast<uint64_t>(frameCounter) + 1200;
    state.participants = { 1, 2 };
    if (!barrierCoordinator.Begin(state) || !barrierCoordinator.WaitForParticipants()) {
        activeStoryEvent = StoryEventKind::None;
        protocolError = "Could not coordinate Zelda's escape cutscene";
        return;
    }

    gSaveContext.nextCutsceneIndex = 0xFFF1;
    if (gPlayState->sceneNum == state.targetScene && IsBarrierTimelineReady(state) &&
        gSaveContext.entranceIndex == state.targetEntrance) {
        barrierCoordinator.MarkReady(1);
    }
    SendBarrierSnapshot();
    if (!barrierCoordinator.GetState().readyParticipants.empty()) {
        CompleteBarrierIfReady();
    } else if (gPlayState->transitionTrigger != TRANS_TRIGGER_START) {
        GameInteractor::RawAction::TeleportPlayerSilent(state.targetEntrance);
    }
}

void Manager::UpdateStoryEvent() {
    if (pendingStoryEvent != StoryEventKind::None && handshakeComplete &&
        transport.GetRole() == SessionRole::Host) {
        BeginStoryBarrier(pendingStoryEvent);
    }
    if (activeStoryEvent == StoryEventKind::None || gPlayState == nullptr) {
        return;
    }
    const bool cutsceneRunning = gSaveContext.cutsceneIndex >= 0xFFF0 || gPlayState->csCtx.state != CS_STATE_IDLE;
    storyCutsceneObserved |= cutsceneRunning;
    if (storyCutsceneObserved && !cutsceneRunning) {
        activeStoryEvent = StoryEventKind::None;
        storyCutsceneObserved = false;
    }
}

void Manager::BeginHandshakeBarrier() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }

    // A Hello is accepted only after the previous peer transport has gone away. Any nonterminal barrier still
    // references that departed participant and cannot be resumed by the replacement socket. Retire it before the
    // reconnect snapshot barrier is created, otherwise Begin() rejects the new operation and immediately drops a
    // successfully authenticated guest.
    const BarrierPhase priorPhase = barrierCoordinator.GetState().phase;
    if (priorPhase == BarrierPhase::Commit || priorPhase == BarrierPhase::Active) {
        barrierCoordinator.Complete();
    } else if (priorPhase != BarrierPhase::Idle && priorPhase != BarrierPhase::Complete &&
               priorPhase != BarrierPhase::Aborted) {
        barrierCoordinator.Abort();
    }

    BarrierState state;
    state.operationEpoch = nextOperationEpoch++;
    state.scope = sessionScope;
    state.kind = pendingHandshakeBarrierKind;
    state.phase = BarrierPhase::Prepare;
    state.manifestHash = HashCapabilities(negotiatedCapabilities);
    state.targetScene = gPlayState->sceneNum;
    state.targetRoom = -1;
    state.targetEntrance = gSaveContext.entranceIndex;
    if (activeStoryEvent != StoryEventKind::None && gSaveContext.cutsceneIndex >= 0xFFF0) {
        state.targetCutsceneIndex = gSaveContext.cutsceneIndex;
    }
    PopulateBarrierTimeline(state);
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

    const BarrierPhase priorPhase = barrierCoordinator.GetState().phase;
    if (priorPhase == BarrierPhase::Commit || priorPhase == BarrierPhase::Active) {
        barrierCoordinator.Complete();
    } else if (priorPhase != BarrierPhase::Idle && priorPhase != BarrierPhase::Complete &&
               priorPhase != BarrierPhase::Aborted) {
        // The preceding automated scene proof has already verified both peers and dawn cleanup. Do not let a
        // delayed final ready packet from that synthetic barrier prevent the independent boss proof from starting.
        barrierCoordinator.Abort();
    }

    BarrierState state;
    state.operationEpoch = nextOperationEpoch++;
    state.scope = sessionScope;
    state.kind = BarrierKind::BossEncounter;
    state.phase = BarrierPhase::Prepare;
    state.manifestHash = HashCapabilities(negotiatedCapabilities);
    state.targetScene = SCENE_DEKU_TREE_BOSS;
    state.targetRoom = -1;
    state.targetEntrance = ENTR_DEKU_TREE_BOSS_ENTRANCE;
    PopulateBarrierTimeline(state);
    state.deadlineTick = static_cast<uint64_t>(frameCounter) + 1200;
    state.participants = { 1, 2 };
    if (!barrierCoordinator.Begin(state) || !barrierCoordinator.WaitForParticipants()) {
        FailAutomatedTest("could not prepare the Gohma encounter barrier");
        return;
    }
    SendBarrierSnapshot();
    GameInteractor::RawAction::TeleportPlayerSilent(ENTR_DEKU_TREE_BOSS_ENTRANCE);
}

void Manager::BeginAutomatedStalchildBarrier() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }

    gSaveContext.dayTime = 0x0000;
    gSaveContext.skyboxTime = 0x0000;
    gSaveContext.nightFlag = 1;
    gTimeSpeed = 0;
    SendClockSnapshot();

    BarrierState state;
    state.operationEpoch = nextOperationEpoch++;
    state.scope = sessionScope;
    state.kind = BarrierKind::SceneTransition;
    state.phase = BarrierPhase::Prepare;
    state.manifestHash = HashCapabilities(negotiatedCapabilities);
    state.targetScene = SCENE_HYRULE_FIELD;
    state.targetRoom = -1;
    state.targetEntrance = ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN;
    PopulateBarrierTimeline(state);
    state.deadlineTick = static_cast<uint64_t>(frameCounter) + 1200;
    state.participants = { 1, 2 };
    if (!barrierCoordinator.Begin(state) || !barrierCoordinator.WaitForParticipants()) {
        FailAutomatedTest("could not prepare the Hyrule Field Stalchild barrier");
        return;
    }
    SendBarrierSnapshot();
    GameInteractor::RawAction::TeleportPlayerSilent(ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN);
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

void Manager::SetAutomatedTestStage(uint8_t stage, const std::string& event, const std::string& detail) {
    automatedTestStage = stage;
    automatedTestStageTick = automatedTestTick;
    ReportAutomatedTest(event, detail);
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

void Manager::BeginAutomatedForestBarrier() {
    if (!handshakeComplete || transport.GetRole() != SessionRole::Host || !IsSaveLoaded()) {
        return;
    }

    automatedTestActorSpawned = false;

    BarrierState state;
    state.operationEpoch = nextOperationEpoch++;
    state.scope = sessionScope;
    state.kind = BarrierKind::SceneTransition;
    state.phase = BarrierPhase::Prepare;
    state.manifestHash = HashCapabilities(negotiatedCapabilities);
    state.targetScene = SCENE_KOKIRI_FOREST;
    state.targetRoom = -1;
    // The generic Kokiri entrance can resolve to different streaming rooms while a participant is changing age.
    // The Deku Tree approach is an unambiguous shared-room rendezvous for the actor/collision proof.
    state.targetEntrance = ENTR_KOKIRI_FOREST_OUTSIDE_DEKU_TREE;
    PopulateBarrierTimeline(state);
    state.deadlineTick = static_cast<uint64_t>(frameCounter) + 1200;
    state.participants = { 1, 2 };
    if (!barrierCoordinator.Begin(state) || !barrierCoordinator.WaitForParticipants()) {
        FailAutomatedTest("could not prepare the Kokiri Forest test barrier");
        return;
    }
    SendBarrierSnapshot();
    GameInteractor::RawAction::TeleportPlayerSilent(ENTR_KOKIRI_FOREST_OUTSIDE_DEKU_TREE);
}

bool Manager::SpawnAutomatedTestDekuBaba() {
    if (!IsSaveLoaded() || gPlayState->sceneNum != SCENE_KOKIRI_FOREST) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    const float anchorX = automatedTestClient && remotePlayerSnapshot.has_value()
                              ? remotePlayerSnapshot->position[0]
                              : player->actor.world.pos.x;
    const float anchorY = automatedTestClient && remotePlayerSnapshot.has_value()
                              ? remotePlayerSnapshot->position[1]
                              : player->actor.world.pos.y;
    const float anchorZ = automatedTestClient && remotePlayerSnapshot.has_value()
                              ? remotePlayerSnapshot->position[2]
                              : player->actor.world.pos.z;
    float x = std::round(anchorX / 10.0f) * 10.0f;
    float y = std::round(anchorY / 10.0f) * 10.0f;
    float z = std::round(anchorZ / 10.0f) * 10.0f + 120.0f;
    int16_t params = 0;
    if (automatedTestClient) {
        const auto authoritative =
            std::find_if(actorSnapshots.begin(), actorSnapshots.end(), [x, y, z](const auto& entry) {
                return entry.second.scene == SCENE_KOKIRI_FOREST && entry.second.actorId == ACTOR_EN_DEKUBABA &&
                       entry.second.alive && std::abs(entry.second.homePosition[0] - x) < 1.0f &&
                       std::abs(entry.second.homePosition[1] - y) < 1.0f &&
                       std::abs(entry.second.homePosition[2] - z) < 1.0f;
            });
        if (authoritative == actorSnapshots.end()) {
            return false;
        }
        x = authoritative->second.homePosition[0];
        y = authoritative->second.homePosition[1];
        z = authoritative->second.homePosition[2];
        params = authoritative->second.params;
    }
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_DEKUBABA, x, y, z, 0, 0, 0, params);
    if (actor == nullptr) {
        return false;
    }
    actor->colChkInfo.health = 2;
    automatedTestTargetEntityId =
        GetDekuBabaEntityId(actor, SCENE_KOKIRI_FOREST, sessionScope.worldGeneration);
    automatedTestTargetActor = actor;
    if (automatedTestClient) {
        // Stage from the authoritative actor's exact home transform. Interpolated player coordinates can differ by
        // one identity unit, which would accidentally test two independent enemies instead of the guest collider.
        PositionAutomatedTestPlayer(player, actor, 55.0f);
    }
    automatedTestActorSpawned = true;
    return true;
}

bool Manager::SpawnAutomatedTestKeese(const ActorSnapshotMessage& anchor) {
    if (!IsSaveLoaded() || gPlayState->sceneNum != SCENE_KOKIRI_FOREST) {
        return false;
    }
    if (Object_GetIndex(&gPlayState->objectCtx, OBJECT_FIREFLY) < 0 &&
        Object_Spawn(&gPlayState->objectCtx, OBJECT_FIREFLY) < 0) {
        return false;
    }

    const float x = anchor.homePosition[0] + 180.0f;
    const float y = anchor.homePosition[1] + 40.0f;
    const float z = anchor.homePosition[2];
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_FIREFLY, x, y, z, 0, 0, 0, 2);
    if (actor == nullptr) {
        return false;
    }
    actor->colChkInfo.health = 2;
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
    const uint32_t tempSwitchMask = 1u << (kAutomatedTestTempSwitchFlag - 0x20);
    const uint32_t tempCollectibleMask = 1u << (kAutomatedTestTempCollectibleFlag - 0x20);
    const uint32_t tempClearMask = 1u << kAutomatedTestTempClearFlag;

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
            if (!automatedTestClient) {
                BeginAutomatedForestBarrier();
            }
            SetAutomatedTestStage(TestAwaitingScene, "warp-requested");
            return;
        case TestAwaitingScene: {
            if (!IsSaveLoaded() || gPlayState->sceneNum != SCENE_KOKIRI_FOREST ||
                automatedTestTick - automatedTestStageTick < 30) {
                return;
            }
            if (automatedTestClient &&
                (!remotePlayerSnapshot.has_value() || remotePlayerSnapshot->scene != SCENE_KOKIRI_FOREST)) {
                return;
            }
            if (automatedTestClient) {
                const auto authoritative =
                    std::find_if(actorSnapshots.begin(), actorSnapshots.end(), [](const auto& entry) {
                        return entry.second.scene == SCENE_KOKIRI_FOREST &&
                               entry.second.actorId == ACTOR_EN_DEKUBABA && entry.second.alive;
                    });
                if (authoritative == actorSnapshots.end()) {
                    return;
                }
            }
            GET_PLAYER(gPlayState)->currentMask = PLAYER_MASK_BUNNY;
            automatedTestRemotePresentationRendered = false;
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].collect &= ~collectibleMask;
            gPlayState->actorCtx.flags.collect &= ~collectibleMask;
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch &= ~switchMask;
            gPlayState->actorCtx.flags.swch &= ~switchMask;
            gPlayState->actorCtx.flags.tempSwch &= ~tempSwitchMask;
            gPlayState->actorCtx.flags.tempCollect &= ~tempCollectibleMask;
            gPlayState->actorCtx.flags.tempClear &= ~tempClearMask;
            const auto clearEventFlag = [](int16_t flag) {
                gSaveContext.eventChkInf[flag >> 4] &=
                    static_cast<uint16_t>(~(1u << (flag & 0xF)));
            };
            clearEventFlag(EVENTCHKINF_OBTAINED_RUTOS_LETTER);
            clearEventFlag(EVENTCHKINF_KING_ZORA_MOVED);
            clearEventFlag(EVENTCHKINF_OBTAINED_SILVER_SCALE);
            clearEventFlag(EVENTCHKINF_LEARNED_SONG_OF_TIME);
            clearEventFlag(EVENTCHKINF_OPENED_ZORAS_DOMAIN);
            gSaveContext.inventory.questItems &= ~(1u << QUEST_SONG_TIME);
            gSaveContext.itemGetInf[ITEMGETINF_0C >> 4] &=
                static_cast<uint16_t>(~(1u << (ITEMGETINF_0C & 0xF)));
            for (int index = 0; index < 4; ++index) {
                gSaveContext.inventory.items[SLOT_BOTTLE_1 + index] = ITEM_NONE;
            }
            gSaveContext.inventory.upgrades &= gUpgradeNegMasks[UPG_SCALE];
            gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_NONE;
            gSaveContext.inventory.items[SLOT_FARORES_WIND] = ITEM_NONE;
            gSaveContext.itemGetInf[ITEMGETINF_18_19_1A_INDEX] &= ~ITEMGETINF_18_MASK;
            gSaveContext.inventory.dungeonItems[kAutomatedTestJabuDungeonIndex] &=
                static_cast<uint8_t>(~kAutomatedTestJabuRewardMask);
            gSaveContext.sceneFlags[SCENE_JABU_JABU].chest &=
                ~(kAutomatedTestJabuMapChestMask | kAutomatedTestJabuCompassChestMask);
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
                // Reproduce the pre-updater corruption observed in a real campaign: both reward flags survived,
                // but the Ruto bottle, scale upgrade, and King Zora consequence did not.
                gSaveContext.itemGetInf[ITEMGETINF_0C >> 4] |=
                    static_cast<uint16_t>(1u << (ITEMGETINF_0C & 0xF));
                gSaveContext.eventChkInf[EVENTCHKINF_OBTAINED_RUTOS_LETTER >> 4] |=
                    static_cast<uint16_t>(1u << (EVENTCHKINF_OBTAINED_RUTOS_LETTER & 0xF));
                gSaveContext.eventChkInf[EVENTCHKINF_OBTAINED_SILVER_SCALE >> 4] |=
                    static_cast<uint16_t>(1u << (EVENTCHKINF_OBTAINED_SILVER_SCALE & 0xF));
                gSaveContext.eventChkInf[EVENTCHKINF_LEARNED_SONG_OF_TIME >> 4] |=
                    static_cast<uint16_t>(1u << (EVENTCHKINF_LEARNED_SONG_OF_TIME & 0xF));
                gSaveContext.inventory.items[SLOT_BOTTLE_1] = ITEM_BOTTLE;
                CaptureCanonicalProgression();
                ++progressionRevision;
                SendProgressionSnapshot();
            }
            if (!SpawnAutomatedTestDekuBaba()) {
                if (automatedTestClient) {
                    // The host snapshot is authoritative and may arrive after the scene barrier under intentional
                    // UDP loss/reordering. A guest replica waits for that snapshot; it never creates a competing
                    // canonical actor or treats ordinary network delay as a spawn failure.
                    return;
                }
                FailAutomatedTest("could not spawn deterministic Deku Baba");
                return;
            }
            SetAutomatedTestStage(TestAwaitingActors, "forest-loaded");
            return;
        }
        case TestAwaitingActors: {
            // Keep the presentation stable long enough for both peers to observe it before the guest equips a sword,
            // which legitimately clears masks and would otherwise make this bidirectional proof timing-dependent.
            if (automatedTestTick - automatedTestStageTick < 60) {
                return;
            }
            if ((automatedTestTick - automatedTestStageTick) % 300 == 0) {
                const TimelineScope localTimeline = GetLocalTimelineScope();
                const int remoteAge = remotePlayerSnapshot.has_value() ? remotePlayerSnapshot->linkAge : -1;
                const int remoteLayer = remotePlayerSnapshot.has_value() ? remotePlayerSnapshot->sceneLayer : -1;
                const int remoteRoom = remotePlayerSnapshot.has_value() ? remotePlayerSnapshot->room : -99;
                const int remoteMask = remotePlayer == nullptr
                                           ? -1
                                           : static_cast<int>(reinterpret_cast<Player*>(remotePlayer)->currentMask);
                ReportAutomatedTest(
                    "awaiting-actors-state",
                    "spawned=" + std::to_string(automatedTestActorSpawned) +
                        " babas=" + std::to_string(localDekuBabas.size()) +
                        " actorSnapshots=" + std::to_string(actorSnapshots.size()) +
                        " remoteActor=" + std::to_string(remotePlayer != nullptr) +
                        " remoteSnapshot=" + std::to_string(remotePlayerSnapshot.has_value()) +
                        " localRoom=" + std::to_string(gPlayState->roomCtx.curRoom.num) +
                        " remoteRoom=" + std::to_string(remoteRoom) +
                        " localAge=" + std::to_string(localTimeline.linkAge) +
                        " remoteAge=" + std::to_string(remoteAge) +
                        " localLayer=" + std::to_string(localTimeline.sceneLayer) +
                        " remoteLayer=" + std::to_string(remoteLayer) +
                        " remoteMask=" + std::to_string(remoteMask) +
                        " draw=" + std::to_string(automatedTestRemotePresentationRendered));
            }
            const auto liveDekuBaba = actorSnapshots.find(automatedTestTargetEntityId);
            if (!automatedTestActorSpawned || liveDekuBaba == actorSnapshots.end() ||
                liveDekuBaba->second.scene != SCENE_KOKIRI_FOREST ||
                liveDekuBaba->second.actorId != ACTOR_EN_DEKUBABA || !liveDekuBaba->second.alive ||
                !localDekuBabas.contains(liveDekuBaba->first) || remotePlayer == nullptr ||
                !remotePlayerSnapshot.has_value() || remotePlayerSnapshot->scene != SCENE_KOKIRI_FOREST) {
                return;
            }
            if (reinterpret_cast<Player*>(remotePlayer)->currentMask != PLAYER_MASK_BUNNY ||
                (automatedTestRequireDraw && !automatedTestRemotePresentationRendered)) {
                return;
            }
            ReportAutomatedTest(automatedTestClient ? "bunny-hood-host-applied" : "bunny-hood-client-applied",
                                "remote Player state carries Bunny Hood");
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
            automatedTestRemoteSwingDrawn = false;
            SetAutomatedTestStage(TestAttacking, "combat-started");
            return;
        }
        case TestAttacking: {
            const auto fixture = actorSnapshots.find(automatedTestTargetEntityId);
            const bool fixtureDead = fixture != actorSnapshots.end() && !fixture->second.alive;
            if ((automatedTestTick - automatedTestStageTick) % 300 == 0) {
                const auto live = actorSnapshots.find(automatedTestTargetEntityId);
                ReportAutomatedTest(
                    "deku-baba-stage-state",
                    "phase=" + std::to_string(static_cast<int>(automatedTestCombatPhase)) +
                        " snapshots=" + std::to_string(actorSnapshots.size()) +
                        " local=" + std::to_string(localDekuBabas.size()) +
                        " live=" + std::to_string(live != actorSnapshots.end()) +
                        " localMatch=" +
                        std::to_string(live != actorSnapshots.end() && live->second.alive &&
                                       localDekuBabas.contains(live->first)) +
                        " focus=" + std::to_string(GET_PLAYER(gPlayState)->focusActor != nullptr) +
                        " target=" + std::to_string(automatedTestTargetActor != nullptr));
            }
            if (automatedTestClient && automatedTestPhysicalHits > 0 && automatedTestTick % 60 == 0) {
                const auto live = actorSnapshots.find(automatedTestTargetEntityId);
                ReportAutomatedTest("deku-baba-combat-state",
                                    "phase=" + std::to_string(static_cast<int>(automatedTestCombatPhase)) +
                                        " hits=" + std::to_string(automatedTestPhysicalHits) +
                                        " melee=" + std::to_string(GET_PLAYER(gPlayState)->meleeWeaponState) +
                                        " pending=" +
                                        std::to_string(live != actorSnapshots.end() &&
                                                       pendingGuestAttacks.contains(live->first)) +
                                        " health=" +
                                        std::to_string(live != actorSnapshots.end() ? live->second.health : -1));
            }
            if (fixtureDead) {
                if (automatedTestClient) {
                    Player* player = GET_PLAYER(gPlayState);
                    if (localDekuBabas.contains(fixture->first) || player->focusActor == automatedTestTargetActor) {
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
                           !automatedTestRemoteSwingRendered ||
                           (automatedTestRequireDraw && !automatedTestRemoteSwingDrawn)) {
                    return;
                }
                ReportAutomatedTest("deku-baba-dead-synchronized", std::to_string(fixture->first));
                if (!SpawnAutomatedTestKeese(fixture->second)) {
                    FailAutomatedTest("could not spawn deterministic ordinary Keese");
                    return;
                }
                SetAutomatedTestStage(TestAwaitingGenericEnemy, "ordinary-enemy-test-started");
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
            const auto target = actorSnapshots.find(automatedTestTargetEntityId);
            if (target == actorSnapshots.end()) {
                return;
            }
            Player* player = GET_PLAYER(gPlayState);
            const auto local = localDekuBabas.find(target->first);
            if (local == localDekuBabas.end()) {
                return;
            }
            if (automatedTestCombatPhase == CombatSecondSwing) {
                PositionAutomatedTestPlayer(player, static_cast<Actor*>(local->second), 55.0f);
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
                const uint32_t acquireElapsed =
                    static_cast<uint32_t>(automatedTestTick) - automatedTestCombatPhaseTick;
                if (player->focusActor != automatedTestTargetActor && acquireElapsed % 30 == 5) {
                    player->stateFlags1 &= ~(PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING);
                    Player_SetCsAction(gPlayState, &player->actor, 0);
                    gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                    func_80853080(player, gPlayState);
                    EquipAutomatedTestSword(player);
                }
                if (player->focusActor != automatedTestTargetActor) {
                    if (acquireElapsed > 240) {
                        FailAutomatedTest("guest Z input did not acquire the live Gohma replica");
                    }
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
            if ((automatedTestCombatPhase == CombatFirstSwing && automatedTestPhysicalHits >= 1) ||
                (automatedTestCombatPhase == CombatSecondSwing && automatedTestPhysicalHits >= 2)) {
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
            if (automatedTestCombatPhase == CombatAwaitFirstDamage && automatedTestTick % 60 == 0) {
                ReportAutomatedTest("deku-baba-first-hit-wait",
                                    "pending=" + std::to_string(pendingGuestAttacks.contains(target->first)) +
                                        " melee=" + std::to_string(player->meleeWeaponState) +
                                        " health=" + std::to_string(target->second.health));
            }
            return;
        }
        case TestAwaitingGenericEnemy: {
            const auto target = std::find_if(actorSnapshots.begin(), actorSnapshots.end(), [](const auto& entry) {
                return entry.second.scene == SCENE_KOKIRI_FOREST && entry.second.actorId == ACTOR_EN_FIREFLY &&
                       entry.second.alive && entry.second.health == 2;
            });
            if (target == actorSnapshots.end()) {
                return;
            }
            const auto local = localGenericEnemies.find(target->first);
            if (local == localGenericEnemies.end() || remotePlayer == nullptr || !remotePlayerSnapshot.has_value() ||
                remotePlayerSnapshot->scene != SCENE_KOKIRI_FOREST) {
                return;
            }
            automatedTestTargetEntityId = target->first;
            automatedTestTargetActor = local->second;
            automatedTestLastObservedHealth = target->second.health;
            automatedTestCombatPhase = CombatSetup;
            automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
            automatedTestPhysicalHits = 0;
            automatedTestTargetObserved = false;
            automatedTestRemoteTargetObserved = false;
            automatedTestSwingObserved = false;
            automatedTestRemoteSwingObserved = false;
            automatedTestRemoteSwingRendered = false;
            automatedTestRemoteSwingDrawn = false;
            automatedTestFirstDamageObserved = false;
            ReportAutomatedTest("generic-enemy-ready", std::to_string(target->first));
            SetAutomatedTestStage(TestGenericCombat, "ordinary-enemy-combat-started");
            return;
        }
        case TestGenericCombat: {
            const auto target = actorSnapshots.find(automatedTestTargetEntityId);
            if (target == actorSnapshots.end() || target->second.actorId != ACTOR_EN_FIREFLY ||
                target->second.scene != SCENE_KOKIRI_FOREST) {
                return;
            }
            Player* player = GET_PLAYER(gPlayState);
            const auto local = localGenericEnemies.find(target->first);

            if (!target->second.alive) {
                if (local != localGenericEnemies.end() || player->focusActor == automatedTestTargetActor) {
                    return;
                }
                if (automatedTestPhysicalHits == 0 || !automatedTestTargetObserved ||
                    !automatedTestSwingObserved || !automatedTestFirstDamageObserved ||
                    !automatedTestRemoteTargetObserved || !automatedTestRemoteSwingObserved ||
                    !automatedTestRemoteSwingRendered ||
                    (automatedTestRequireDraw && !automatedTestRemoteSwingDrawn)) {
                    return;
                }
                ReportAutomatedTest("generic-enemy-target-released", "dead ordinary actor cannot remain targeted");
                ReportAutomatedTest("generic-enemy-dead-synchronized", std::to_string(target->first));
                if (automatedTestClient) {
                    gSaveContext.equips.buttonItems[0] = ITEM_NONE;
                    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_NONE);
                    gSaveContext.inventory.equipment &=
                        ~OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI);
                    Player_UseItem(gPlayState, player, ITEM_NONE);
                    Flags_SetCollectible(gPlayState, kAutomatedTestCollectibleFlag);
                    ReportAutomatedTest("collectible-picked-up", std::to_string(kAutomatedTestCollectibleFlag));
                }
                SetAutomatedTestStage(TestAwaitingCollection, "collectible-test-started");
                return;
            }

            if (remotePlayerSnapshot.has_value() && remotePlayerSnapshot->focusActorId == ACTOR_EN_FIREFLY &&
                !automatedTestRemoteTargetObserved) {
                automatedTestRemoteTargetObserved = true;
                ReportAutomatedTest(automatedTestClient ? "generic-enemy-host-target-visible"
                                                        : "generic-enemy-client-target-visible",
                                    automatedTestClient ? "host lock-on reached the guest player stream"
                                                        : "client lock-on reached the host player stream");
            }

            if (automatedTestClient) {
                if (local == localGenericEnemies.end()) {
                    return;
                }
                Actor* actor = static_cast<Actor*>(local->second);
                if (automatedTestCombatPhase == CombatSetup) {
                    EquipAutomatedTestSword(player);
                    PositionAutomatedTestPlayer(player, actor, 35.0f);
                    Player_ClearZTargeting(player);
                    player->zTargetActiveTimer = 0;
                    automatedTestCombatPhase = CombatAcquireTarget;
                    automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    ReportAutomatedTest("generic-enemy-client-physical-setup", std::to_string(target->first));
                    return;
                }
                if (automatedTestCombatPhase == CombatAcquireTarget) {
                    PositionAutomatedTestPlayer(player, actor, 35.0f);
                    if (player->focusActor != automatedTestTargetActor) {
                        return;
                    }
                    automatedTestTargetObserved = true;
                    automatedTestCombatPhase = CombatFirstSwing;
                    automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    ReportAutomatedTest("generic-enemy-client-target-acquired", "Z input selected the live actor");
                    return;
                }
                if (automatedTestCombatPhase == CombatFirstSwing) {
                    PositionAutomatedTestPlayer(player, actor, 35.0f);
                    if (player->meleeWeaponState > 0 && !automatedTestSwingObserved) {
                        automatedTestSwingObserved = true;
                        ReportAutomatedTest("generic-enemy-client-sword-state-entered",
                                            "Player_Update entered a melee weapon state from B input");
                    }
                    if (automatedTestPhysicalHits > 0) {
                        automatedTestCombatPhase = CombatAwaitFirstDamage;
                        automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    } else if (automatedTestTick - automatedTestCombatPhaseTick > 360) {
                        const float deltaX = player->actor.world.pos.x - actor->world.pos.x;
                        const float deltaZ = player->actor.world.pos.z - actor->world.pos.z;
                        FailAutomatedTest(
                            "guest sword action produced no ordinary-enemy collider contact: meleeState=" +
                            std::to_string(player->meleeWeaponState) + " meleeAnimation=" +
                            std::to_string(player->meleeWeaponAnimation) + " focused=" +
                            std::to_string(player->focusActor == automatedTestTargetActor) + " distance=" +
                            std::to_string(std::sqrt(deltaX * deltaX + deltaZ * deltaZ)));
                    }
                    return;
                }
                if (automatedTestCombatPhase == CombatAwaitFirstDamage && target->second.health == 1 &&
                    !pendingGuestAttacks.contains(target->first)) {
                    automatedTestFirstDamageObserved = true;
                    automatedTestLastObservedHealth = 1;
                    automatedTestCombatPhase = CombatAwaitDeath;
                    automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    ReportAutomatedTest("generic-enemy-client-damage-synchronized", "health=2 -> health=1");
                }
                return;
            }

            if (!automatedTestFirstDamageObserved || target->second.health != 1 ||
                local == localGenericEnemies.end()) {
                return;
            }
            Actor* actor = static_cast<Actor*>(local->second);
            if (automatedTestCombatPhase == CombatSetup) {
                EquipAutomatedTestSword(player);
                PositionAutomatedTestPlayer(player, actor, 35.0f);
                Player_ClearZTargeting(player);
                player->zTargetActiveTimer = 0;
                automatedTestCombatPhase = CombatAcquireTarget;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                ReportAutomatedTest("generic-enemy-host-physical-setup", std::to_string(target->first));
                return;
            }
            if (automatedTestCombatPhase == CombatAcquireTarget) {
                PositionAutomatedTestPlayer(player, actor, 35.0f);
                if (player->focusActor != automatedTestTargetActor) {
                    return;
                }
                automatedTestTargetObserved = true;
                automatedTestCombatPhase = CombatFirstSwing;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                ReportAutomatedTest("generic-enemy-host-target-acquired", "Z input selected the live actor");
                return;
            }
            if (automatedTestCombatPhase == CombatFirstSwing) {
                PositionAutomatedTestPlayer(player, actor, 35.0f);
                if (player->meleeWeaponState > 0 && !automatedTestSwingObserved) {
                    automatedTestSwingObserved = true;
                    ReportAutomatedTest("generic-enemy-host-sword-state-entered",
                                        "Player_Update entered a melee weapon state from B input");
                }
                if (automatedTestPhysicalHits == 0 && automatedTestTick - automatedTestCombatPhaseTick > 360) {
                    const float deltaX = player->actor.world.pos.x - actor->world.pos.x;
                    const float deltaZ = player->actor.world.pos.z - actor->world.pos.z;
                    FailAutomatedTest(
                        "host sword action produced no ordinary-enemy collider contact: meleeState=" +
                        std::to_string(player->meleeWeaponState) + " meleeAnimation=" +
                        std::to_string(player->meleeWeaponAnimation) + " focused=" +
                        std::to_string(player->focusActor == automatedTestTargetActor) + " distance=" +
                        std::to_string(std::sqrt(deltaX * deltaX + deltaZ * deltaZ)));
                }
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
            automatedTestProgressionRupees = gSaveContext.rupees;
            automatedTestProgressionArrows = gSaveContext.inventory.ammo[SLOT_BOW];
            automatedTestProgressionMagic = gSaveContext.magic;
            automatedTestProgressionResourcesCaptured = true;
            SetAutomatedTestStage(TestAwaitingProgression, "shared-progression-test-started");
            return;
        }
        case TestAwaitingProgression: {
            if (!automatedTestProgressionResourcesCaptured) {
                FailAutomatedTest("personal resource baseline was not captured before shared progression");
                return;
            }
            const int16_t expectedRupees = automatedTestProgressionRupees;
            const int8_t expectedArrows = automatedTestProgressionArrows;
            const int8_t expectedMagic = automatedTestProgressionMagic;
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
            const size_t bottleCount = std::count_if(
                &gSaveContext.inventory.items[SLOT_BOTTLE_1],
                &gSaveContext.inventory.items[SLOT_BOTTLE_4 + 1],
                [](uint8_t item) { return item != ITEM_NONE; });
            const bool durableRewardsRepaired = bottleCount >= 2 && CUR_UPG_VALUE(UPG_SCALE) >= 1 &&
                                                Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_RUTOS_LETTER) &&
                                                Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED) &&
                                                Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_SILVER_SCALE) &&
                                                Flags_GetEventChkInf(EVENTCHKINF_LEARNED_SONG_OF_TIME) &&
                                                CHECK_QUEST_ITEM(QUEST_SONG_TIME) &&
                                                !Inventory_HasSpecificBottle(ITEM_LETTER_RUTO);
            if (!durableRewardsRepaired) {
                FailAutomatedTest(
                    "derived bottle, Ruto hand-in, Silver Scale, and Song of Time progression was not repaired");
                return;
            }
            if (!remotePlayerSnapshot.has_value() ||
                gPlayState->linkAgeOnLoad != remotePlayerSnapshot->linkAge) {
                FailAutomatedTest("remote Link snapshot did not match its live skeleton age");
                return;
            }
            if (gSaveContext.rupees != expectedRupees ||
                gSaveContext.inventory.ammo[SLOT_BOW] != expectedArrows || gSaveContext.magic != expectedMagic ||
                gSaveContext.magicLevel != 1 || gSaveContext.magicCapacity != MAGIC_NORMAL_METER ||
                !gSaveContext.isMagicAcquired || gSaveContext.isDoubleMagicAcquired) {
                FailAutomatedTest(
                    "shared progression overwrote a local resource or corrupted the magic meter: rupees=" +
                    std::to_string(gSaveContext.rupees) + "/" + std::to_string(expectedRupees) +
                    " arrows=" + std::to_string(gSaveContext.inventory.ammo[SLOT_BOW]) + "/" +
                    std::to_string(expectedArrows) + " magic=" + std::to_string(gSaveContext.magic) + "/" +
                    std::to_string(expectedMagic) + " level=" + std::to_string(gSaveContext.magicLevel) +
                    " capacity=" + std::to_string(gSaveContext.magicCapacity) + " acquired=" +
                    std::to_string(gSaveContext.isMagicAcquired) + " double=" +
                    std::to_string(gSaveContext.isDoubleMagicAcquired));
                return;
            }
            ReportAutomatedTest("shared-progression-synchronized",
                                "Hookshot, Kokiri Sword, and Farore's Wind shared; rupees, arrows, and current magic remained local");
            ReportAutomatedTest("derived-progression-repaired",
                                "Cucco and Ruto bottles, King Zora hand-in, Silver Scale, and Song of Time recovered from durable flags");
            SetAutomatedTestStage(TestAwaitingDungeonRewards, "dungeon-reward-test-started");
            return;
        }
        case TestAwaitingDungeonRewards: {
            uint8_t& dungeonItems = gSaveContext.inventory.dungeonItems[kAutomatedTestJabuDungeonIndex];
            if (!automatedTestClient && !automatedTestDungeonMapChestTriggered) {
                automatedTestDungeonMapChestTriggered = true;
                gSaveContext.sceneFlags[SCENE_JABU_JABU].chest |= kAutomatedTestJabuMapChestMask;
                CaptureCanonicalProgression();
                if ((dungeonItems & kDungeonMapItemBit) == 0) {
                    FailAutomatedTest("Jabu fixed-layout map chest did not restore the canonical map bit");
                    return;
                }
                ++progressionRevision;
                SendProgressionSnapshot();
                ReportAutomatedTest("dungeon-rewards-map-reconciled",
                                    "Jabu fixed-layout map chest restored canonical map ownership");
                return;
            }

            if (automatedTestClient && !automatedTestDungeonCompassIntentTriggered) {
                if ((dungeonItems & kDungeonMapItemBit) == 0) {
                    return;
                }
                automatedTestDungeonCompassIntentTriggered = true;
                automatedTestDungeonCompassIntentSnapshotRevision = lastAppliedProgressionRevision;
                const uint16_t previousMapIndex = gSaveContext.mapIndex;
                gSaveContext.mapIndex = kAutomatedTestJabuDungeonIndex;
                Item_Give(gPlayState, ITEM_COMPASS);
                gSaveContext.mapIndex = previousMapIndex;
                if ((dungeonItems & kDungeonCompassItemBit) == 0) {
                    FailAutomatedTest("guest Jabu compass Item_Give did not update its local dungeon item bit");
                    return;
                }
                ReportAutomatedTest("dungeon-rewards-compass-intent-sent",
                                    "guest Item_Give(ITEM_COMPASS) used mapIndex=Jabu");
                return;
            }

            if ((dungeonItems & kAutomatedTestJabuRewardMask) != kAutomatedTestJabuRewardMask) {
                return;
            }
            if (automatedTestClient &&
                lastAppliedProgressionRevision <= automatedTestDungeonCompassIntentSnapshotRevision) {
                return;
            }
            ReportAutomatedTest("dungeon-rewards-synchronized",
                                "both Jabu map and compass bits converged through the host snapshot");
            SetAutomatedTestStage(TestAwaitingWorldState, "global-world-state-test-started");
            return;
        }
        case TestAwaitingWorldState: {
            if (!automatedTestWorldStateTriggered) {
                automatedTestWorldStateTriggered = true;
                if (automatedTestClient) {
                    Flags_SetEventChkInf(EVENTCHKINF_OPENED_ZORAS_DOMAIN);
                    applyingAuthoritativeState = true;
                    GameInteractor::RawAction::UnsetFlag(FLAG_EVENT_CHECK_INF, EVENTCHKINF_OPENED_ZORAS_DOMAIN);
                    applyingAuthoritativeState = false;
                    Flags_SetSwitch(gPlayState, kAutomatedTestTempSwitchFlag);
                    Flags_SetCollectible(gPlayState, kAutomatedTestTempCollectibleFlag);
                    Flags_SetTempClear(gPlayState, kAutomatedTestTempClearFlag);
                    applyingAuthoritativeState = true;
                    GameInteractor::RawAction::UnsetSceneFlag(SCENE_KOKIRI_FOREST, FLAG_SCENE_SWITCH,
                                                              kAutomatedTestTempSwitchFlag);
                    GameInteractor::RawAction::UnsetSceneFlag(SCENE_KOKIRI_FOREST, FLAG_SCENE_COLLECTIBLE,
                                                              kAutomatedTestTempCollectibleFlag);
                    GameInteractor::RawAction::UnsetSceneFlag(SCENE_KOKIRI_FOREST, FLAG_SCENE_TEMP_CLEAR,
                                                              kAutomatedTestTempClearFlag);
                    applyingAuthoritativeState = false;
                    ReportAutomatedTest("guest-world-state-intent-sent", "Zora's Domain opened flag 0x39");
                    ReportAutomatedTest("guest-temporary-scene-state-intents-sent",
                                        "temporary switch 0x3C, collectible 0x3D, and room clear 0x1D; local prediction rolled back");
                } else {
                    Flags_SetSwitch(gPlayState, kAutomatedTestSwitchFlag);
                    ReportAutomatedTest("host-scene-switch-set", "live Kokiri Forest switch 0x1F");
                }
                return;
            }
            if (!Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED) ||
                !Flags_GetEventChkInf(EVENTCHKINF_OPENED_ZORAS_DOMAIN) ||
                (gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch & switchMask) == 0 ||
                (gPlayState->actorCtx.flags.tempSwch & tempSwitchMask) == 0 ||
                (gPlayState->actorCtx.flags.tempCollect & tempCollectibleMask) == 0 ||
                (gPlayState->actorCtx.flags.tempClear & tempClearMask) == 0) {
                return;
            }
            ReportAutomatedTest("global-world-state-synchronized",
                                "derived King Zora state, Zora's Domain opening, and host live scene switch replayed across peers");
            ReportAutomatedTest("temporary-scene-state-synchronized",
                                "temporary switch, collectible, and room clear replayed across peers");
            if (!automatedTestClient) {
                BeginAutomatedStalchildBarrier();
            }
            SetAutomatedTestStage(TestAwaitingStalchildScene, "stalchild-barrier-started");
            return;
        }
        case TestAwaitingStalchildScene:
            if (!IsSaveLoaded() || gPlayState->sceneNum != SCENE_HYRULE_FIELD ||
                phase != ConnectionPhase::Ready || gSaveContext.nightFlag == 0) {
                return;
            }
            if (!automatedTestClient) {
                gTimeSpeed = 0;
                SendClockSnapshot();
            }
            SetAutomatedTestStage(TestAwaitingStalchild, "stalchild-field-ready");
            return;
        case TestAwaitingStalchild: {
            // The encounter manager can keep multiple Stalchildren alive. Select the host's first issued identity on
            // both peers instead of depending on unordered-map iteration or packet arrival order.
            const uint64_t targetEntityId = kDynamicStalchildEntityPrefix | 1ULL;
            const auto target = actorSnapshots.find(targetEntityId);
            if (!automatedTestClient && target == actorSnapshots.end() &&
                automatedTestTick - automatedTestStageTick == 30) {
                Player* player = GET_PLAYER(gPlayState);
                Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_SKB,
                                             player->actor.world.pos.x, player->actor.world.pos.y,
                                             player->actor.world.pos.z + 120.0f, 0, 0, 0, 0);
                if (spawned == nullptr) {
                    FailAutomatedTest("could not spawn deterministic host-owned Stalchild");
                } else {
                    ReportAutomatedTest("stalchild-fixture-spawned",
                                        "explicit fixture avoids ambient encounter timing and controller input");
                }
                return;
            }
            if (target == actorSnapshots.end() || target->second.scene != SCENE_HYRULE_FIELD ||
                target->second.actorId != ACTOR_EN_SKB || !target->second.alive || target->second.health != 2 ||
                !IsStalchildTargetable(target->second.stateId)) {
                return;
            }
            const auto local = localStalchildren.find(target->first);
            if (local == localStalchildren.end() ||
                (static_cast<Actor*>(local->second)->flags & ACTOR_FLAG_ATTENTION_ENABLED) == 0 ||
                remotePlayer == nullptr || !remotePlayerSnapshot.has_value() ||
                remotePlayerSnapshot->scene != SCENE_HYRULE_FIELD) {
                return;
            }

            Actor* actor = static_cast<Actor*>(local->second);
            const uint64_t bystanderEntityId = kDynamicStalchildEntityPrefix | 2ULL;
            auto bystander = actorSnapshots.find(bystanderEntityId);
            if (!automatedTestClient && bystander == actorSnapshots.end()) {
                Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_SKB,
                                             actor->world.pos.x + 250.0f, actor->world.pos.y, actor->world.pos.z,
                                             actor->world.rot.x, actor->world.rot.y, actor->world.rot.z, actor->params);
                if (spawned == nullptr) {
                    FailAutomatedTest("could not spawn the Stalchild bystander used by the isolation proof");
                    return;
                }
                SendStalchildSnapshot(spawned, true);
                return;
            }
            bystander = actorSnapshots.find(bystanderEntityId);
            const auto localBystander = localStalchildren.find(bystanderEntityId);
            if (bystander == actorSnapshots.end() || !bystander->second.alive || bystander->second.health != 2 ||
                !IsStalchildTargetable(bystander->second.stateId) || localBystander == localStalchildren.end()) {
                return;
            }
            if (!automatedTestClient) {
                // Keep the host outside the retarget hysteresis while delayed guest pose packets settle. The combat
                // proof specifically exercises a host-owned Stalchild attacking the guest.
                PositionAutomatedTestPlayer(GET_PLAYER(gPlayState), actor, 500.0f);
                if (DecodeStalchildTarget(target->second.stateId) != StalchildTarget::Guest) {
                    return;
                }
            }
            if (actor->shape.yOffset < -1.0f || actor->shape.shadowScale < 24.0f) {
                return;
            }
            if (automatedTestClient) {
                ReportAutomatedTest("stalchild-client-visual-emerged",
                                    "host emergence offset and shadow scale applied to the guest replica");
            }

            automatedTestTargetEntityId = target->first;
            automatedTestTargetActor = local->second;
            automatedTestStalchildBystanderEntityId = bystanderEntityId;
            static_cast<Actor*>(local->second)->scale = { 0.016f, 0.016f, 0.016f };
            automatedTestLastObservedHealth = target->second.health;
            automatedTestCombatPhase = CombatSetup;
            automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
            automatedTestPhysicalHits = 0;
            automatedTestTargetObserved = false;
            automatedTestRemoteTargetObserved = false;
            automatedTestSwingObserved = false;
            automatedTestRemoteSwingObserved = false;
            automatedTestRemoteSwingRendered = false;
            automatedTestRemoteSwingDrawn = false;
            automatedTestFirstDamageObserved = false;
            automatedTestStalchildDawnTriggered = false;
            automatedTestStalchildTargetAgreementObserved = false;
            automatedTestStalchildDeathTransitionObserved = false;
            automatedTestStalchildDropObserved = false;
            automatedTestStalchildBystanderObserved = false;
            automatedTestMissingTargetTick = 0;
            automatedTestNonTargetHealth = -1;
            ReportAutomatedTest("stalchild-host-identity-ready", std::to_string(target->first));
            ReportAutomatedTest("stalchild-bystander-identity-ready", std::to_string(bystanderEntityId));
            SetAutomatedTestStage(TestStalchildCombat, "stalchild-combat-started");
            return;
        }
        case TestStalchildCombat: {
            const auto target = actorSnapshots.find(automatedTestTargetEntityId);
            if (target == actorSnapshots.end() || target->second.actorId != ACTOR_EN_SKB ||
                target->second.scene != SCENE_HYRULE_FIELD) {
                return;
            }
            Player* player = GET_PLAYER(gPlayState);
            const auto local = localStalchildren.find(target->first);
            const auto bystander = actorSnapshots.find(automatedTestStalchildBystanderEntityId);
            const auto localBystander = localStalchildren.find(automatedTestStalchildBystanderEntityId);
            if (!automatedTestClient &&
                (bystander == actorSnapshots.end() || !bystander->second.alive || bystander->second.health != 2)) {
                FailAutomatedTest("Stalchild bystander was damaged or removed: snapshot=" +
                                  std::to_string(bystander != actorSnapshots.end()) + " alive=" +
                                  std::to_string(bystander != actorSnapshots.end() && bystander->second.alive) +
                                  " health=" +
                                  std::to_string(bystander != actorSnapshots.end() ? bystander->second.health : -1) +
                                  " local=" + std::to_string(localBystander != localStalchildren.end()));
                return;
            }
            if (target->second.alive && local == localStalchildren.end()) {
                if (automatedTestMissingTargetTick == 0) {
                    automatedTestMissingTargetTick = automatedTestTick;
                } else if (automatedTestTick - automatedTestMissingTargetTick > 60) {
                    FailAutomatedTest("live Stalchild replica disappeared during synchronized combat");
                }
                return;
            }
            automatedTestMissingTargetTick = 0;
            if (target->second.alive && target->second.health == 0 &&
                target->second.adapterState[kStalchildAdapterBehavior] == 1 &&
                !automatedTestStalchildDeathTransitionObserved) {
                automatedTestStalchildDeathTransitionObserved = true;
                ReportAutomatedTest("stalchild-native-death-visible",
                                    "zero-health replica remained for the native dying animation");
            }
            if (!automatedTestClient) {
                const StalchildTarget selected = DecodeStalchildTarget(target->second.stateId);
                if (selected == StalchildTarget::Guest && !automatedTestStalchildTargetAgreementObserved) {
                    gSaveContext.health = gSaveContext.healthCapacity;
                    automatedTestNonTargetHealth = gSaveContext.health;
                    automatedTestStalchildTargetAgreementObserved = true;
                    ReportAutomatedTest("stalchild-host-target-agreed",
                                        "host selected the guest and disabled its own attack collider");
                } else if (automatedTestStalchildTargetAgreementObserved &&
                           gSaveContext.health < automatedTestNonTargetHealth) {
                    FailAutomatedTest("non-target host took damage from a guest-targeted Stalchild");
                    return;
                }
                if (automatedTestPhysicalHits < 2 && local != localStalchildren.end()) {
                    // Native host simulation remains active during this proof. Hold the actor at the health implied
                    // by accepted guest contacts so only the second distinct guest collider hit can kill it.
                    static_cast<Actor*>(local->second)->colChkInfo.health =
                        static_cast<int16_t>(2 - automatedTestPhysicalHits);
                }
            }

            if (!target->second.alive) {
                if (local != localStalchildren.end() || player->focusActor == automatedTestTargetActor) {
                    return;
                }
                const bool combatObserved =
                    automatedTestClient
                        ? automatedTestStalchildTargetAgreementObserved && automatedTestTargetObserved &&
                              automatedTestSwingObserved && automatedTestPhysicalHits == 2
                        : automatedTestStalchildTargetAgreementObserved && automatedTestRemoteSwingObserved &&
                              automatedTestRemoteSwingRendered &&
                              (!automatedTestRequireDraw || automatedTestRemoteSwingDrawn) &&
                              automatedTestPhysicalHits == 2;
                if (!combatObserved || !automatedTestFirstDamageObserved) {
                    return;
                }
                if (!automatedTestStalchildDeathTransitionObserved ||
                    (!automatedTestClient && !automatedTestStalchildDropObserved)) {
                    FailAutomatedTest("Stalchild terminal snapshot bypassed its native death or collectible path");
                    return;
                }
                automatedTestStalchildBystanderObserved = true;
                ReportAutomatedTest("stalchild-bystander-survived",
                                    std::to_string(automatedTestStalchildBystanderEntityId));
                ReportAutomatedTest("stalchild-dead-synchronized", std::to_string(target->first));
                automatedTestTargetActor = nullptr;
                SetAutomatedTestStage(TestAwaitingStalchildDawn, "stalchild-dawn-test-started");
                return;
            }

            if (remotePlayerSnapshot.has_value() && remotePlayerSnapshot->focusActorId == ACTOR_EN_SKB &&
                !automatedTestRemoteTargetObserved) {
                automatedTestRemoteTargetObserved = true;
                ReportAutomatedTest(automatedTestClient ? "stalchild-host-target-visible"
                                                        : "stalchild-client-target-visible",
                                    automatedTestClient ? "host lock-on reached the guest player stream"
                                                        : "client lock-on reached the host player stream");
            }
            if (remotePlayerSnapshot.has_value() && remotePlayerSnapshot->meleeWeaponState > 0 &&
                !automatedTestRemoteSwingObserved) {
                automatedTestRemoteSwingObserved = true;
                ReportAutomatedTest(automatedTestClient ? "stalchild-host-swing-visible"
                                                        : "stalchild-client-swing-visible",
                                    automatedTestClient ? "host melee state reached the guest player stream"
                                                        : "client melee state reached the host player stream");
            }

            if (automatedTestClient) {
                if (local == localStalchildren.end()) {
                    return;
                }
                Actor* actor = static_cast<Actor*>(local->second);
                if (automatedTestCombatPhase == CombatSetup) {
                    EquipAutomatedTestSword(player);
                    player->stateFlags1 &= ~(PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING);
                    Player_SetCsAction(gPlayState, &player->actor, 0);
                    gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                    func_80853080(player, gPlayState);
                    gSaveContext.health = gSaveContext.healthCapacity;
                    player->invincibilityTimer = 0;
                    PositionAutomatedTestPlayer(player, actor, 40.0f);
                    automatedTestLastObservedHealth = gSaveContext.health;
                    automatedTestCombatPhase = CombatAwaitEnemyAttack;
                    automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    ReportAutomatedTest("stalchild-client-enemy-attack-setup", std::to_string(target->first));
                    return;
                }
                if (automatedTestCombatPhase == CombatAwaitEnemyAttack) {
                    PositionAutomatedTestPlayer(player, actor, 40.0f);
                    const StalchildTarget selected = DecodeStalchildTarget(target->second.stateId);
                    if (selected != StalchildTarget::Guest) {
                        if (automatedTestStalchildTargetAgreementObserved) {
                            FailAutomatedTest("guest observed the shared Stalchild retarget away during combat");
                        }
                        return;
                    }
                    if (!automatedTestStalchildTargetAgreementObserved) {
                        automatedTestStalchildTargetAgreementObserved = true;
                        ReportAutomatedTest("stalchild-client-target-agreed",
                                            "guest received the host-selected guest target before enemy contact");
                    }
                    if (gSaveContext.health < automatedTestLastObservedHealth) {
                        ReportAutomatedTest("stalchild-client-damaged-by-enemy",
                                            "health=" + std::to_string(automatedTestLastObservedHealth) + " -> " +
                                                std::to_string(gSaveContext.health));
                        gSaveContext.health = gSaveContext.healthCapacity;
                        player->invincibilityTimer = 2;
                        Player_ClearZTargeting(player);
                        player->zTargetActiveTimer = 0;
                        automatedTestCombatPhase = CombatAcquireTarget;
                        automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                        return;
                    }
                    if (automatedTestTick - automatedTestCombatPhaseTick > 240) {
                        const int16_t behavior = target->second.adapterState[kStalchildAdapterBehavior];
                        const int16_t attackActive = target->second.adapterState[kStalchildAdapterAttackActive];
                        FailAutomatedTest("guest Stalchild replica did not attack or damage the local guest: behavior=" +
                                          std::to_string(behavior) + " attack=" +
                                          std::to_string(attackActive) + " distance=" +
                                          std::to_string(actor->xzDistToPlayer));
                    }
                    return;
                }
                if (automatedTestCombatPhase == CombatAcquireTarget) {
                    player->invincibilityTimer = 2;
                    PositionAutomatedTestPlayer(player, actor, 55.0f);
                    if ((actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) == 0) {
                        FailAutomatedTest("synchronized Stalchild was not targetable after emergence");
                        return;
                    }
                    if (!automatedTestTargetObserved) {
                        // The headless warp can leave Player_Action_CsAction installed after csAction and its state
                        // flag have already cleared. Action 7 is the engine's normal request to return that handler
                        // to regular player control.
                        player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
                        Player_SetCsAction(gPlayState, &player->actor, 0);
                        func_80853080(player, gPlayState);
                        automatedTestTargetObserved = true;
                        automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                        return;
                    }
                    const uint32_t acquireElapsed =
                        static_cast<uint32_t>(automatedTestTick) - automatedTestCombatPhaseTick;
                    if (acquireElapsed <= kAutomatedStalchildTargetRecoveryFrames) {
                        return;
                    }
                    player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
                    if (Player_InBlockingCsMode(gPlayState, player)) {
                        if (acquireElapsed > 240) {
                            FailAutomatedTest("guest Link did not leave the synthetic entrance action: csAction=" +
                                              std::to_string(player->csAction) + " stateFlags1=" +
                                              std::to_string(player->stateFlags1) + " transition=" +
                                              std::to_string(gPlayState->transitionTrigger));
                        }
                        return;
                    }
                    Player_SetCsAction(gPlayState, &player->actor, 0);
                    func_80853080(player, gPlayState);
                    EquipAutomatedTestSword(player);
                    automatedTestCombatPhase = CombatFirstSwing;
                    automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    ReportAutomatedTest("stalchild-client-target-acquired",
                                        "targetable host replica entered the physical sword-contact proof");
                    return;
                }
                if (automatedTestCombatPhase == CombatFirstSwing) {
                    // Silent headless warps can restore the entrance cutscene bit after setup. It is test harness
                    // residue, not part of the native sword-action contract this phase validates.
                    player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
                    const uint32_t swingElapsed =
                        static_cast<uint32_t>(automatedTestTick) - automatedTestCombatPhaseTick;
                    if (player->meleeWeaponState <= 0 && swingElapsed % 36 == 0) {
                        player->stateFlags1 &= ~PLAYER_STATE1_LOADING;
                        Player_SetCsAction(gPlayState, &player->actor, 0);
                        gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                        func_80853080(player, gPlayState);
                        EquipAutomatedTestSword(player);
                    }
                    player->invincibilityTimer = 2;
                    PositionAutomatedTestPlayer(player, actor, 55.0f);
                    if (player->meleeWeaponState > 0 && !automatedTestSwingObserved) {
                        automatedTestSwingObserved = true;
                        ReportAutomatedTest("stalchild-client-sword-state-entered",
                                            "Player_Update entered a melee weapon state from B input");
                    }
                    if (automatedTestPhysicalHits > 0) {
                        automatedTestCombatPhase = CombatAwaitFirstDamage;
                        automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    }
                    return;
                }
                if (automatedTestCombatPhase == CombatAwaitFirstDamage && target->second.health == 1 &&
                    !pendingGuestAttacks.contains(target->first) && player->meleeWeaponState <= 0) {
                    automatedTestFirstDamageObserved = true;
                    automatedTestLastObservedHealth = 1;
                    automatedTestCombatPhase = CombatAwaitRecovery;
                    automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    automatedTestRecoveryStartedTick = static_cast<uint32_t>(automatedTestTick);
                    ReportAutomatedTest("stalchild-client-damage-synchronized", "health=2 -> health=1");
                    return;
                }
                if (automatedTestCombatPhase == CombatAwaitFirstDamage &&
                    automatedTestTick - automatedTestCombatPhaseTick > 240) {
                    FailAutomatedTest("guest did not settle after the first Stalchild hit: health=" +
                                      std::to_string(target->second.health) + " pending=" +
                                      std::to_string(pendingGuestAttacks.contains(target->first)) + " meleeState=" +
                                      std::to_string(player->meleeWeaponState));
                    return;
                }
                if (automatedTestCombatPhase == CombatAwaitRecovery) {
                    player->invincibilityTimer = 2;
                    PositionAutomatedTestPlayer(player, actor, 55.0f);
                    if (automatedTestTick - automatedTestRecoveryStartedTick > 240) {
                        FailAutomatedTest("guest Link did not recover from the first Stalchild sword action: "
                                          "meleeState=" + std::to_string(player->meleeWeaponState) +
                                          " meleeAnimation=" + std::to_string(player->meleeWeaponAnimation) +
                                          " stateFlags1=" + std::to_string(player->stateFlags1) +
                                          " input=" + std::to_string(gPlayState->state.input[0].cur.button));
                        return;
                    }
                    if (player->meleeWeaponState > 0) {
                        automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                        return;
                    }
                    if (automatedTestTick - automatedTestCombatPhaseTick >= 18) {
                        // Silent headless warps can leave the entrance movement's cutscene bit latched when the test
                        // pins Link's transform. Clear only that harness residue before requiring the second native
                        // sword action and collider contact.
                        player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
                        Player_SetCsAction(gPlayState, &player->actor, 0);
                        func_80853080(player, gPlayState);
                        EquipAutomatedTestSword(player);
                        automatedTestCombatPhase = CombatSecondSwing;
                        automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                        ReportAutomatedTest("stalchild-client-first-sword-recovered",
                                            "Link remained outside the melee collider window for 18 updates");
                    }
                    return;
                }
                if (automatedTestCombatPhase == CombatSecondSwing) {
                    // The silent test warp can restore this entrance-state bit after the recovery transition. Keep
                    // removing only that harness residue while the second native sword action is being exercised.
                    player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
                    const uint32_t swingElapsed =
                        static_cast<uint32_t>(automatedTestTick) - automatedTestCombatPhaseTick;
                    if (player->meleeWeaponState <= 0 && swingElapsed % 36 == 0) {
                        player->stateFlags1 &= ~PLAYER_STATE1_LOADING;
                        Player_SetCsAction(gPlayState, &player->actor, 0);
                        gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                        func_80853080(player, gPlayState);
                        EquipAutomatedTestSword(player);
                    }
                    player->invincibilityTimer = 2;
                    PositionAutomatedTestPlayer(player, actor, 55.0f);
                    if (player->meleeWeaponState > 0 && automatedTestLastObservedHealth == 1) {
                        automatedTestLastObservedHealth = -1;
                        ReportAutomatedTest("stalchild-client-second-sword-state-entered",
                                            "Player_Update accepted the follow-up B input");
                    }
                    if (automatedTestPhysicalHits >= 2) {
                        automatedTestCombatPhase = CombatAwaitDeath;
                        automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                    } else if (automatedTestTick - automatedTestCombatPhaseTick > 360) {
                        FailAutomatedTest("guest follow-up sword action produced no second Stalchild collider contact: "
                                          "meleeState=" + std::to_string(player->meleeWeaponState) +
                                          " meleeAnimation=" + std::to_string(player->meleeWeaponAnimation) +
                                          " stateFlags1=" + std::to_string(player->stateFlags1) +
                                          " focused=" +
                                          std::to_string(player->focusActor == automatedTestTargetActor));
                    }
                }
                return;
            }

            return;
        }
        case TestAwaitingStalchildDawn: {
            const auto live = std::find_if(actorSnapshots.begin(), actorSnapshots.end(), [this](const auto& entry) {
                return entry.first != automatedTestTargetEntityId && IsDynamicStalchildEntityId(entry.first) &&
                       entry.second.scene == SCENE_HYRULE_FIELD && entry.second.alive;
            });
            if (!automatedTestStalchildDawnTriggered) {
                if (!automatedTestClient) {
                    gSaveContext.dayTime = 0x4555;
                    gSaveContext.skyboxTime = 0x4555;
                    gSaveContext.nightFlag = 0;
                    gTimeSpeed = 0;
                    SendClockSnapshot();
                } else if (gSaveContext.nightFlag != 0) {
                    return;
                }
                automatedTestStalchildDawnTriggered = true;
                ReportAutomatedTest("stalchild-dawn-triggered",
                                    live == actorSnapshots.end() ? "no additional live spawn remained"
                                                                 : std::to_string(live->first));
                return;
            }

            const bool liveSnapshotRemaining =
                std::any_of(actorSnapshots.begin(), actorSnapshots.end(), [](const auto& entry) {
                    return IsDynamicStalchildEntityId(entry.first) && entry.second.scene == SCENE_HYRULE_FIELD &&
                           entry.second.alive;
                });
            if (gSaveContext.nightFlag != 0 || liveSnapshotRemaining || !localStalchildren.empty()) {
                return;
            }
            ReportAutomatedTest("stalchild-dawn-retired", "host population removed on both peers");
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
            automatedTestRemoteSwingDrawn = false;
            automatedTestFirstDamageObserved = false;
            automatedTestBossDeathPresentationObserved = false;
            automatedTestPostDeathCleanupObserved = false;
            automatedTestTargetEntityId = 0;
            automatedTestTargetActor = automatedTestClient ? nullptr : localGohmas.begin()->second;
            automatedTestRemotePreviousTick = 0;
            SetAutomatedTestStage(TestBossCombat, "gohma-combat-started");
            return;
        }
        case TestBossCombat: {
            const bool bossClear = (gSaveContext.sceneFlags[SCENE_DEKU_TREE_BOSS].clear & kGohmaRoomMask) != 0;
            if (!automatedTestBossDeathPresentationObserved && Play_InCsMode(gPlayState)) {
                const bool defeatedLocally = std::any_of(localGohmas.begin(), localGohmas.end(), [](const auto& entry) {
                    return entry.second != nullptr && static_cast<Actor*>(entry.second)->colChkInfo.health == 0;
                });
                if (defeatedLocally) {
                    automatedTestBossDeathPresentationObserved = true;
                    ReportAutomatedTest("gohma-death-presentation-started",
                                        "native local boss cutscene entered after authoritative defeat");
                }
            }
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
                if (automatedTestAcceptedBossHits != 2 || !automatedTestBossDeathPresentationObserved ||
                    !automatedTestTargetObserved ||
                    !automatedTestRemoteMovementObserved ||
                    !automatedTestRemoteTargetObserved || !automatedTestRemoteSwingObserved ||
                    !automatedTestRemoteSwingRendered ||
                    (automatedTestRequireDraw && !automatedTestRemoteSwingDrawn)) {
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
                const uint32_t movementElapsed =
                    static_cast<uint32_t>(automatedTestTick) - automatedTestCombatPhaseTick;
                const float position[] = { player->actor.world.pos.x, player->actor.world.pos.y,
                                           player->actor.world.pos.z };
                if (HorizontalDistanceSquared(position, automatedTestMovementOrigin) <= 25.0f * 25.0f ||
                    std::abs(player->linearVelocity) <= 0.5f || movementElapsed < 90) {
                    return;
                }
                automatedTestMovementObserved = true;
                ReportAutomatedTest("gohma-local-movement-verified",
                                    "real Player_Update moved Link more than 25 units");
                if (local == localGohmas.end()) {
                    return;
                }
                PositionAutomatedTestPlayer(player, static_cast<Actor*>(local->second), 75.0f);
                player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
                Player_SetCsAction(gPlayState, &player->actor, 0);
                func_80853080(player, gPlayState);
                EquipAutomatedTestSword(player);
                automatedTestCombatPhase = CombatAcquireTarget;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                return;
            }
            if (automatedTestCombatPhase == CombatAcquireTarget) {
                if (player->focusActor != automatedTestTargetActor) {
                    return;
                }
                automatedTestTargetObserved = true;
                // Silent headless warps can leave the entrance action installed even after lock-on succeeds. Return
                // only the synthetic player to its normal action before requiring a real B-driven sword collider.
                player->stateFlags1 &= ~(PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING);
                Player_SetCsAction(gPlayState, &player->actor, 0);
                gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                func_80853080(player, gPlayState);
                EquipAutomatedTestSword(player);
                automatedTestCombatPhase = CombatFirstSwing;
                automatedTestCombatPhaseTick = static_cast<uint32_t>(automatedTestTick);
                ReportAutomatedTest("gohma-target-acquired", "Z input selected the live guest replica");
                return;
            }
            if (automatedTestCombatPhase == CombatFirstSwing || automatedTestCombatPhase == CombatSecondSwing) {
                const uint32_t swingElapsed =
                    static_cast<uint32_t>(automatedTestTick) - automatedTestCombatPhaseTick;
                player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
                if (player->meleeWeaponState <= 0 && swingElapsed % 36 == 0) {
                    player->stateFlags1 &= ~PLAYER_STATE1_LOADING;
                    Player_SetCsAction(gPlayState, &player->actor, 0);
                    gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                    func_80853080(player, gPlayState);
                    EquipAutomatedTestSword(player);
                }
                if (local != localGohmas.end()) {
                    PositionAutomatedTestPlayer(player, static_cast<Actor*>(local->second), 75.0f);
                }
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
                player->stateFlags1 &= ~(PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_LOADING);
                Player_SetCsAction(gPlayState, &player->actor, 0);
                gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
                func_80853080(player, gPlayState);
                EquipAutomatedTestSword(player);
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
                const auto liveBoss = localGohmas.find(target->first);
                const bool bossStillDamageable = liveBoss != localGohmas.end() && liveBoss->second != nullptr &&
                                                 (static_cast<Actor*>(liveBoss->second)->colChkInfo.health > 0 ||
                                                  CanDamageGohma(liveBoss->second));
                if (automatedTestPhysicalHits != 2 || pendingGuestAttacks.contains(target->first) ||
                    bossStillDamageable) {
                    FailAutomatedTest("post-death sword input reached the defeated Gohma collider");
                    return;
                }
                automatedTestPostDeathCleanupObserved = true;
                automatedTestCombatPhase = CombatDone;
                ReportAutomatedTest("gohma-post-death-attack-rejected",
                                    "B input produced no target, collision, or additional hit");
            }
            if (bossClear && automatedTestCombatPhase == CombatDone && automatedTestMovementObserved &&
                automatedTestTargetObserved && automatedTestSwingObserved && automatedTestFirstDamageObserved &&
                automatedTestBossDeathPresentationObserved && automatedTestPostDeathCleanupObserved &&
                automatedTestPhysicalHits == 2) {
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
                automatedTestHostReconnectRupees = gSaveContext.rupees;
                automatedTestHostReconnectArrows = gSaveContext.inventory.ammo[SLOT_BOW];
                automatedTestHostReconnectMagic = gSaveContext.magic;
                automatedTestHostReconnectResourcesCaptured = true;
                SetAutomatedTestStage(
                    TestAwaitingReconnect, "host-awaiting-client-reconnect",
                    "rupees=" + std::to_string(automatedTestHostReconnectRupees) +
                        " arrows=" + std::to_string(automatedTestHostReconnectArrows) +
                        " magic=" + std::to_string(automatedTestHostReconnectMagic));
                return;
            }
            automatedTestSaveRequested = true;
            automatedTestSaveCompleted.store(false);
            SaveManager::Instance->SaveFile(gSaveContext.fileNum);
            ReportAutomatedTest("guest-save-requested", "personal save protection active");
            SetAutomatedTestStage(TestAwaitingGuestProtection, "guest-save-protection-started");
            return;
        }
        case TestAwaitingGuestProtection: {
            if (!automatedTestSaveCompleted.load()) {
                return;
            }
            ReportAutomatedTest("guest-save-protection-complete", "pre-join save written unchanged");
            Disconnect();
            // Simulate a stale guest only after disconnect cleanup has restored its local save overlay.
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].collect &= ~collectibleMask;
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch &= ~switchMask;
            gSaveContext.sceneFlags[SCENE_DEKU_TREE_BOSS].clear &= ~kGohmaRoomMask;
            GameInteractor::RawAction::UnsetFlag(FLAG_EVENT_CHECK_INF, EVENTCHKINF_KING_ZORA_MOVED);
            GameInteractor::RawAction::UnsetFlag(FLAG_EVENT_CHECK_INF, EVENTCHKINF_LEARNED_SONG_OF_TIME);
            gSaveContext.inventory.questItems &= ~(1u << QUEST_SONG_TIME);
            gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_NONE;
            gSaveContext.inventory.items[SLOT_FARORES_WIND] = ITEM_NONE;
            gSaveContext.itemGetInf[ITEMGETINF_18_19_1A_INDEX] &= ~ITEMGETINF_18_MASK;
            gSaveContext.inventory.equipment &=
                ~OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI);
            automatedTestReconnectStarted = false;
            // Disconnect() removes the frame hook that advances this automated
            // stage. Keep only the test manager alive while the stopped
            // transport settles; Join() will perform its normal hook reset.
            RegisterHooks(true);
            SetAutomatedTestStage(TestAwaitingReconnect, "client-reconnect-paused",
                                  "transport stopped before restart");
            return;
        }
        case TestAwaitingReconnect: {
            if (automatedTestClient && !automatedTestReconnectStarted) {
                // Starting a new client in the same update as Stop() can leave the
                // replacement connection accepted without its handshake worker.
                // Exercise the normal Join path after teardown has settled.
                // Let the host finish its independent native death/cutscene assertions and process the old TCP
                // disconnect before a replacement socket is accepted. Eight updates was shorter than the host's
                // post-death verification window and allowed the old disconnect to tear down the new peer.
                if (automatedTestTick - automatedTestStageTick < 90) {
                    return;
                }
                if (!Join(automatedTestAddress, automatedTestPort, "Local Guest")) {
                    FailAutomatedTest("client reconnect could not start");
                    return;
                }
                automatedTestReconnectStarted = true;
                ReportAutomatedTest("client-reconnect-started");
                return;
            }
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
            const size_t bottleCount = std::count_if(
                &gSaveContext.inventory.items[SLOT_BOTTLE_1],
                &gSaveContext.inventory.items[SLOT_BOTTLE_4 + 1],
                [](uint8_t item) { return item != ITEM_NONE; });
            const bool worldState = Flags_GetEventChkInf(EVENTCHKINF_KING_ZORA_MOVED) &&
                                    Flags_GetEventChkInf(EVENTCHKINF_OPENED_ZORAS_DOMAIN) &&
                                    Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_RUTOS_LETTER) &&
                                    Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_SILVER_SCALE) &&
                                    Flags_GetEventChkInf(EVENTCHKINF_LEARNED_SONG_OF_TIME) &&
                                    CHECK_QUEST_ITEM(QUEST_SONG_TIME) &&
                                    bottleCount >= 2 && CUR_UPG_VALUE(UPG_SCALE) >= 1;
            int16_t expectedRupees = automatedTestHostReconnectRupees;
            int8_t expectedArrows = automatedTestHostReconnectArrows;
            int8_t expectedMagic = automatedTestHostReconnectMagic;
            if (automatedTestClient && originalSaveContext.size() == sizeof(SaveContext)) {
                SaveContext protectedGuestSave{};
                std::memcpy(&protectedGuestSave, originalSaveContext.data(), sizeof(SaveContext));
                expectedRupees = protectedGuestSave.rupees;
                expectedArrows = protectedGuestSave.inventory.ammo[SLOT_BOW];
                expectedMagic = protectedGuestSave.magic;
            }
            if (!automatedTestClient && !automatedTestHostReconnectResourcesCaptured) {
                return;
            }
            if (!collected || !dead || !hookshotShared || !faroresWindShared || !swordShared || !bossClear ||
                !worldState || remotePlayer == nullptr) {
                const uint64_t ticksInStage = automatedTestTick - automatedTestStageTick;
                if (ticksInStage >= 60 && ticksInStage % 300 == 0) {
                    ReportAutomatedTest(
                        "reconnect-state-pending",
                        "collected=" + std::to_string(collected) + " dead=" + std::to_string(dead) +
                            " hookshot=" + std::to_string(hookshotShared) +
                            " farores=" + std::to_string(faroresWindShared) +
                            " sword=" + std::to_string(swordShared) + " boss=" + std::to_string(bossClear) +
                            " world=" + std::to_string(worldState) +
                            " remote=" + std::to_string(remotePlayer != nullptr) +
                            " localScene=" + std::to_string(gPlayState != nullptr ? gPlayState->sceneNum : -1) +
                            " remoteScene=" +
                            std::to_string(remotePlayerSnapshot.has_value() ? remotePlayerSnapshot->scene : -1) +
                            " localRoom=" +
                            std::to_string(gPlayState != nullptr ? gPlayState->roomCtx.curRoom.num : -99) +
                            " remoteRoom=" +
                            std::to_string(remotePlayerSnapshot.has_value() ? remotePlayerSnapshot->room : -99) +
                            " localAge=" + std::to_string(GetLocalTimelineScope().linkAge) +
                            " remoteAge=" +
                            std::to_string(remotePlayerSnapshot.has_value() ? remotePlayerSnapshot->linkAge : -1) +
                            " localLayer=" + std::to_string(GetLocalTimelineScope().sceneLayer) +
                            " remoteLayer=" +
                            std::to_string(remotePlayerSnapshot.has_value() ? remotePlayerSnapshot->sceneLayer : -1) +
                            " preparing=" + std::to_string(preparingRemotePlayer) +
                            " locations=" + std::to_string(collectedLocations.size()) +
                            " revision=" + std::to_string(lastAppliedProgressionRevision));
                }
                return;
            }
            if (gSaveContext.rupees != expectedRupees ||
                gSaveContext.inventory.ammo[SLOT_BOW] != expectedArrows || gSaveContext.magic != expectedMagic ||
                gSaveContext.magicLevel != 1 || gSaveContext.magicCapacity != MAGIC_NORMAL_METER ||
                !gSaveContext.isMagicAcquired || gSaveContext.isDoubleMagicAcquired) {
                FailAutomatedTest(
                    "reconnect resource mismatch: rupees=" + std::to_string(gSaveContext.rupees) + "/" +
                    std::to_string(expectedRupees) + " arrows=" +
                    std::to_string(gSaveContext.inventory.ammo[SLOT_BOW]) + "/" + std::to_string(expectedArrows) +
                    " magic=" + std::to_string(gSaveContext.magic) + "/" + std::to_string(expectedMagic) +
                    " level=" + std::to_string(gSaveContext.magicLevel) + " capacity=" +
                    std::to_string(gSaveContext.magicCapacity) + " acquired=" +
                    std::to_string(gSaveContext.isMagicAcquired) + " double=" +
                    std::to_string(gSaveContext.isDoubleMagicAcquired));
                return;
            }
            ReportAutomatedTest(
                "reconnect-state-restored",
                automatedTestClient
                    ? "pickup, spell, equipment, world event, boss completion, and remote Link replayed to stale guest"
                    : "enemy, pickup, spell, equipment, scene switch, world event, boss completion, and remote Link retained by host");
            if (!automatedTestClient && !automatedTestSaveRequested) {
                automatedTestSaveRequested = true;
                automatedTestSaveCompleted.store(false);
                SaveManager::Instance->SaveFile(gSaveContext.fileNum);
                ReportAutomatedTest("host-campaign-save-requested", "canonical campaign state ready for persistence");
                SetAutomatedTestStage(TestAwaitingPersistence, "campaign-persistence-save-started");
                return;
            }
            if (automatedTestClient) {
                automatedTestStage = TestComplete;
                ReportAutomatedTest("PASS", "full two-instance OoT vertical proof completed");
            }
            return;
        }
        case TestAwaitingPersistence:
            if (!automatedTestSaveCompleted.load()) {
                return;
            }
            ReportAutomatedTest("host-campaign-save-complete", "canonical host campaign written");
            automatedTestStage = TestComplete;
            ReportAutomatedTest("PASS", "full two-instance OoT vertical proof completed");
            return;
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
    originalSaveContext.resize(sizeof(SaveContext));
    std::memcpy(originalSaveContext.data(), &gSaveContext, sizeof(SaveContext));
    originalSceneFlags.reserve(SCENE_ID_MAX);
    for (int16_t scene = 0; scene < SCENE_ID_MAX; ++scene) {
        const SavedSceneFlags& flags = gSaveContext.sceneFlags[scene];
        originalSceneFlags.emplace(scene,
                                   SceneFlagState{ flags.chest, flags.swch, flags.clear, flags.collect });
    }
    originalProgression = CaptureSharedProgression(&gSaveContext);
    if (transport.GetRole() == SessionRole::Host) {
        doorOfTimeOpeningPresented =
            HasSharedEventFlag(originalProgression, EVENTCHKINF_OPENED_THE_DOOR_OF_TIME);
        masterSwordEntrancePresented =
            HasSharedEventFlag(originalProgression, EVENTCHKINF_ENTERED_MASTER_SWORD_CHAMBER);
        masterSwordPullPresented =
            HasSharedEventFlag(originalProgression, EVENTCHKINF_PULLED_MASTER_SWORD_FROM_PEDESTAL);
        storyPresentationBaselineApplied = true;
    }
    coordinatedMasterSwordPullActive = false;
    if (transport.GetRole() == SessionRole::Host && !canonicalProgressionCaptured) {
        canonicalProgression = originalProgression;
        canonicalProgressionCaptured = true;
    }
    saveOverlayCaptured = true;
}

void Manager::RestoreSaveOverlay() {
    if (!saveOverlayCaptured || transport.GetRole() != SessionRole::Client ||
        originalSaveContext.size() != sizeof(SaveContext)) {
        return;
    }
    std::memcpy(&gSaveContext, originalSaveContext.data(), sizeof(SaveContext));
    const int8_t durableMagicLevel =
        gSaveContext.isDoubleMagicAcquired ? 2 : (gSaveContext.isMagicAcquired ? 1 : 0);
    const int16_t durableMagicCapacity = durableMagicLevel * MAGIC_NORMAL_METER;
    gSaveContext.magicLevel = durableMagicLevel;
    gSaveContext.magicCapacity = durableMagicCapacity;
    gSaveContext.magic = std::clamp<int16_t>(gSaveContext.magic, 0, durableMagicCapacity);
    gSaveContext.magicFillTarget = gSaveContext.magic;
    gSaveContext.magicTarget = gSaveContext.magic;
    gSaveContext.magicState = MAGIC_STATE_IDLE;
    gSaveContext.prevMagicState = MAGIC_STATE_IDLE;
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

extern "C" void HyruleCoop_NotifyMasterSwordPullStarted(void) {
    if (HyruleCoop::Manager::Instance != nullptr) {
        HyruleCoop::Manager::Instance->NotifyMasterSwordPullStarted();
    }
}

extern "C" int HyruleCoop_ShouldRegisterStalchildAttack(void* actor, int nativeAttackActive) {
    if (HyruleCoop::Manager::Instance == nullptr) {
        return nativeAttackActive != 0;
    }
    return HyruleCoop::Manager::Instance->ShouldRegisterStalchildAttack(actor, nativeAttackActive != 0);
}

extern "C" int HyruleCoop_ShouldProcessStalchildHit(void* actor, void* attacker) {
    if (HyruleCoop::Manager::Instance == nullptr) {
        return 1;
    }
    return HyruleCoop::Manager::Instance->ShouldProcessStalchildHit(actor, attacker);
}

extern "C" int HyruleCoop_ShouldSuppressSharedEnemyLocalReward(void* actor) {
    return HyruleCoop::Manager::Instance != nullptr &&
           HyruleCoop::Manager::Instance->ShouldSuppressSharedEnemyLocalReward(actor);
}
