#include "soh/Network/HyruleCoop/HyruleCoopProtocol.h"
#include "soh/Network/HyruleCoop/RemotePlayerRoomPolicy.h"
#include "soh/Network/HyruleCoop/StalchildPolicy.h"

#include <array>
#include <cassert>
#include <iostream>

using namespace HyruleCoop;

static void TestRoundTrip() {
    HelloMessage hello;
    hello.gameId = GameId::OcarinaOfTime;
    hello.buildId = "build-123";
    hello.playerName = "Tillya";
    hello.requestedSessionEpoch = 99;
    hello.requestedParticipantId = 2;
    hello.resumeTokenHigh = 3;
    hello.resumeTokenLow = 4;
    hello.capabilities = { Capability::Coordination, Capability::OotClock };
    Packet source{ MessageType::Hello, 42, EncodeHello(hello), 0x1122334455667788ULL };
    const std::vector<uint8_t> encoded = EncodePacket(source);

    Packet decoded;
    size_t consumed = 0;
    std::string error;
    assert(TryDecodePacket(encoded, decoded, consumed, error) == DecodeResult::Decoded);
    assert(consumed == encoded.size());
    assert(decoded.type == MessageType::Hello);
    assert(decoded.sequence == 42);
    assert(decoded.streamId == source.streamId);

    const auto decodedHello = DecodeHello(decoded.payload);
    assert(decodedHello.has_value());
    assert(decodedHello->gameId == GameId::OcarinaOfTime);
    assert(decodedHello->buildId == "build-123");
    assert(decodedHello->playerName == "Tillya");
    assert(decodedHello->requestedSessionEpoch == 99);
    assert(decodedHello->requestedParticipantId == 2);
    assert(decodedHello->capabilities == hello.capabilities);
}

static void TestFragmentedPacket() {
    Packet source{ MessageType::Heartbeat, 7, { 1, 2, 3, 4 } };
    const std::vector<uint8_t> encoded = EncodePacket(source);
    std::vector<uint8_t> fragment(encoded.begin(), encoded.begin() + kHeaderSize - 1);

    Packet decoded;
    size_t consumed = 99;
    std::string error;
    assert(TryDecodePacket(fragment, decoded, consumed, error) == DecodeResult::NeedMoreData);
    assert(consumed == 0);

    fragment = encoded;
    fragment.pop_back();
    assert(TryDecodePacket(fragment, decoded, consumed, error) == DecodeResult::NeedMoreData);
    assert(consumed == 0);
}

static void TestClockAndAckMessages() {
    ClockSnapshotMessage clock{ { 90, 2 }, 1234, 0x8000, 0x8010, 3, true };
    const auto decodedClock = DecodeClockSnapshot(EncodeClockSnapshot(clock));
    assert(decodedClock.has_value());
    assert(decodedClock->hostTick == 1234);
    assert(decodedClock->dayTime == 0x8000);
    assert(decodedClock->skyboxTime == 0x8010);
    assert(decodedClock->timeSpeed == 3);
    assert(decodedClock->night);

    HelloAckMessage rejection;
    rejection.playerName = "Host Link";
    rejection.reason = "different builds";
    const auto decodedAck = DecodeHelloAck(EncodeHelloAck(rejection));
    assert(decodedAck.has_value());
    assert(!decodedAck->accepted);
    assert(decodedAck->playerName == "Host Link");
    assert(decodedAck->reason == "different builds");
}

static void TestPlayerSnapshot() {
    PlayerSnapshotMessage player;
    player.scope = { 90, 2 };
    player.tick = 88;
    player.scene = 7;
    player.room = 2;
    player.entrance = 0x1234;
    player.linkAge = 1;
    player.sceneLayer = 3;
    player.position[0] = 10.25f;
    player.position[1] = -3.5f;
    player.position[2] = 999.0f;
    player.rotation[1] = -1234;
    player.joints[17] = -30000;
    player.previousTranslation[2] = 44;
    player.movementFlags = 5;
    player.upperLimbRotation[0] = 321;
    player.boots = -1;
    player.currentMask = 4;
    player.stateFlags1 = 0xDEADBEEF;
    player.buttonItem = 7;
    player.itemAction = -4;
    player.modelState = -55;
    player.modelBlend = 0.75f;
    player.actionVariable = -8;
    player.linearVelocity = 4.5f;
    player.focusActorId = 27;
    player.meleeWeaponState = 1;
    player.meleeWeaponAnimation = 12;

    const auto decoded = DecodePlayerSnapshot(EncodePlayerSnapshot(player));
    assert(decoded.has_value());
    assert(decoded->tick == player.tick);
    assert(decoded->scene == player.scene);
    assert(decoded->room == player.room);
    assert(decoded->entrance == player.entrance);
    assert(decoded->linkAge == player.linkAge);
    assert(decoded->sceneLayer == player.sceneLayer);
    assert(decoded->position[0] == player.position[0]);
    assert(decoded->position[1] == player.position[1]);
    assert(decoded->rotation[1] == player.rotation[1]);
    assert(decoded->joints[17] == player.joints[17]);
    assert(decoded->boots == player.boots);
    assert(decoded->currentMask == player.currentMask);
    assert(decoded->stateFlags1 == player.stateFlags1);
    assert(decoded->itemAction == player.itemAction);
    assert(decoded->modelBlend == player.modelBlend);
    assert(decoded->actionVariable == player.actionVariable);
    assert(decoded->linearVelocity == player.linearVelocity);
    assert(decoded->focusActorId == player.focusActorId);
    assert(decoded->meleeWeaponState == player.meleeWeaponState);
    assert(decoded->meleeWeaponAnimation == player.meleeWeaponAnimation);

    std::vector<uint8_t> truncated = EncodePlayerSnapshot(player);
    truncated.pop_back();
    assert(!DecodePlayerSnapshot(truncated).has_value());
}

static void TestRemotePlayerRoomLifecycle() {
    struct RemoteActor {
        int16_t room = -1;
        bool live = false;
    };

    std::array<RemoteActor, 9> actors{};
    size_t actorCount = 0;
    const std::array<int16_t, 9> visitedRooms{ 1, 14, 3, 13, 3, 2, 3, 14, 1 };

    for (const int16_t activeRoom : visitedRooms) {
        // OnSceneSpawnActors drops the cached pointer because the engine may have deleted the prior actor. The
        // lifecycle reconciliation must retain only a same-room remote actor before deciding whether to spawn one.
        RemoteActor* survivor = nullptr;
        for (size_t index = 0; index < actorCount; ++index) {
            RemoteActor& actor = actors[index];
            if (!actor.live) {
                continue;
            }
            if (survivor == nullptr && IsRemotePlayerActorInActiveRoom(actor.room, activeRoom)) {
                survivor = &actor;
            } else {
                actor.live = false;
            }
        }
        if (survivor == nullptr) {
            actors[actorCount++] = { activeRoom, true };
        }

        size_t liveActors = 0;
        for (size_t index = 0; index < actorCount; ++index) {
            liveActors += actors[index].live ? 1 : 0;
        }
        assert(liveActors == 1);
    }

    assert(!IsRemotePlayerActorInActiveRoom(-1, visitedRooms.back()));
    const TimelineScope childDay{ 0, 0 };
    const TimelineScope childNight{ 0, 1 };
    const TimelineScope adultDay{ 1, 2 };
    assert(IsRemotePlayerVisibleInRoom(2, 14, 2, 14, childDay, childDay));
    assert(!IsRemotePlayerVisibleInRoom(2, 14, 2, 3, childDay, childDay));
    assert(!IsRemotePlayerVisibleInRoom(2, 14, 3, 14, childDay, childDay));
    assert(!IsRemotePlayerVisibleInRoom(2, 14, 2, 14, childDay, childNight));
    assert(!IsRemotePlayerVisibleInRoom(2, 14, 2, 14, childDay, adultDay));
    assert(!IsSameTimeline(childDay, TimelineScope{ -1, 0 }));
}

static void TestPlayerPresentation() {
    PlayerPresentationMessage presentation;
    presentation.scope = { 90, 2 };
    presentation.revision = 8;
    presentation.boots = 2;
    presentation.shield = 3;
    presentation.tunic = 1;
    presentation.currentMask = 4;
    presentation.buttonItem = 5;
    presentation.itemAction = -7;
    presentation.heldItemAction = 9;
    presentation.modelGroup = 6;

    const auto decoded = DecodePlayerPresentation(EncodePlayerPresentation(presentation));
    assert(decoded.has_value());
    assert(decoded->scope == presentation.scope);
    assert(decoded->revision == presentation.revision);
    assert(decoded->boots == presentation.boots);
    assert(decoded->shield == presentation.shield);
    assert(decoded->tunic == presentation.tunic);
    assert(decoded->currentMask == presentation.currentMask);
    assert(decoded->buttonItem == presentation.buttonItem);
    assert(decoded->itemAction == presentation.itemAction);
    assert(decoded->heldItemAction == presentation.heldItemAction);
    assert(decoded->modelGroup == presentation.modelGroup);

    std::vector<uint8_t> truncated = EncodePlayerPresentation(presentation);
    truncated.pop_back();
    assert(!DecodePlayerPresentation(truncated).has_value());
}

static void TestCycleSnapshot() {
    CycleSnapshotMessage cycle{ { 90, 3 }, 900, 3, 2, 0x8123, 0x8110, -2, 3, 1, 4, 1 };
    const auto decoded = DecodeCycleSnapshot(EncodeCycleSnapshot(cycle));
    assert(decoded.has_value());
    assert(decoded->hostTick == cycle.hostTick);
    assert(decoded->cycleGeneration == cycle.cycleGeneration);
    assert(decoded->day == cycle.day);
    assert(decoded->time == cycle.time);
    assert(decoded->timeSpeedOffset == cycle.timeSpeedOffset);
    assert(decoded->sceneTimeSpeed == cycle.sceneTimeSpeed);
    assert(decoded->weatherMode == cycle.weatherMode);
    assert(decoded->pendingTransition == cycle.pendingTransition);
}

static void TestWorldStateMessages() {
    SnapshotRequestMessage request{ { 90, 9 }, 12, 3 };
    const auto decodedRequest = DecodeSnapshotRequest(EncodeSnapshotRequest(request));
    assert(decodedRequest.has_value());
    assert(decodedRequest->scene == 12);
    assert(decodedRequest->room == 3);
    assert(decodedRequest->scope == request.scope);

    SceneFlagIntentMessage intent{ { 90, 9 }, 2, 77, 4, -2, 31, false };
    const auto decodedIntent = DecodeSceneFlagIntent(EncodeSceneFlagIntent(intent));
    assert(decodedIntent.has_value());
    assert(decodedIntent->requestId == 77);
    assert(decodedIntent->scene == 4);
    assert(decodedIntent->flagType == -2);
    assert(decodedIntent->flag == 31);
    assert(!decodedIntent->set);

    // Jabu-Jabu's switch doors use temporary scene-switch flags 0x39/0x3C. FLAG_SCENE_SWITCH is zero in the
    // engine, but keep this protocol test independent of gameplay-only flag headers.
    SceneFlagIntentMessage temporarySwitch{ { 90, 9 }, 2, 78, 4, 0, 0x39, true };
    const auto decodedTemporarySwitch = DecodeSceneFlagIntent(EncodeSceneFlagIntent(temporarySwitch));
    assert(decodedTemporarySwitch.has_value());
    assert(decodedTemporarySwitch->flagType == 0);
    assert(decodedTemporarySwitch->flag == 0x39);
    assert(decodedTemporarySwitch->set);

    SceneFlagsSnapshotMessage flags{ { 90, 9 }, 19, 8, 0x80000001, 0x01020304, 0x12000000, 0x10203040,
                                     0x55667788, 0xA5A5A5A5, 0xCC33CC33 };
    const auto decodedFlags = DecodeSceneFlagsSnapshot(EncodeSceneFlagsSnapshot(flags));
    assert(decodedFlags.has_value());
    assert(decodedFlags->revision == 19);
    assert(decodedFlags->scene == 8);
    assert(decodedFlags->chest == 0x80000001);
    assert(decodedFlags->switches == 0x01020304);
    assert(decodedFlags->tempSwitches == 0x12000000);
    assert(decodedFlags->clear == 0x10203040);
    assert(decodedFlags->tempClear == 0x55667788);
    assert(decodedFlags->collectible == 0xA5A5A5A5);
    assert(decodedFlags->tempCollectible == 0xCC33CC33);
}

static void TestActorSnapshot() {
    ActorSnapshotMessage actor;
    actor.scope = { 90, 4 };
    actor.hostTick = 123;
    actor.entityId = 0x8877665544332211ULL;
    actor.acknowledgedRequestId = 0x123456789ABCDEF0ULL;
    actor.scene = 6;
    actor.room = -1;
    actor.actorId = 0x55;
    actor.params = -7;
    actor.homePosition[1] = -20.5f;
    actor.position[0] = 321.25f;
    actor.velocity[2] = -4.0f;
    actor.scale[1] = 1.5f;
    actor.worldRotation[2] = -12345;
    actor.shapeRotation[1] = 23456;
    actor.speed = 8.25f;
    actor.gravity = -1.0f;
    actor.health = 3;
    actor.stateId = 12;
    actor.animationFrame = 7.5f;
    actor.animationSpeed = -0.5f;
    actor.alive = true;
    actor.adapterWordCount = 3;
    actor.adapterState[0] = -1;
    actor.adapterState[1] = 200;
    actor.adapterState[2] = -30000;

    const auto decoded = DecodeActorSnapshot(EncodeActorSnapshot(actor));
    assert(decoded.has_value());
    assert(decoded->entityId == actor.entityId);
    assert(decoded->acknowledgedRequestId == actor.acknowledgedRequestId);
    assert(decoded->room == -1);
    assert(decoded->params == -7);
    assert(decoded->homePosition[1] == -20.5f);
    assert(decoded->position[0] == 321.25f);
    assert(decoded->velocity[2] == -4.0f);
    assert(decoded->worldRotation[2] == -12345);
    assert(decoded->shapeRotation[1] == 23456);
    assert(decoded->animationSpeed == -0.5f);
    assert(decoded->adapterWordCount == 3);
    assert(decoded->adapterState[2] == -30000);

    actor.adapterWordCount = static_cast<uint8_t>(kMaximumActorAdapterWords);
    for (size_t index = 0; index < actor.adapterState.size(); ++index) {
        actor.adapterState[index] = static_cast<int16_t>((index * 977) - 30000);
    }
    const auto decodedMaximumAdapter = DecodeActorSnapshot(EncodeActorSnapshot(actor));
    assert(decodedMaximumAdapter.has_value());
    assert(decodedMaximumAdapter->adapterWordCount == kMaximumActorAdapterWords);
    assert(decodedMaximumAdapter->adapterState == actor.adapterState);

    std::vector<uint8_t> truncated = EncodeActorSnapshot(actor);
    truncated.pop_back();
    assert(!DecodeActorSnapshot(truncated).has_value());
}

static void TestCoordinationMessages() {
    BarrierState state;
    state.operationEpoch = 20;
    state.scope = { 90, 4 };
    state.kind = BarrierKind::BossEncounter;
    state.phase = BarrierPhase::WaitingForParticipants;
    state.manifestHash = "manifest";
    state.targetScene = 17;
    state.targetRoom = 1;
    state.targetEntrance = 0x1234;
    state.targetCutsceneIndex = 0xFFF1;
    state.targetLinkAge = 1;
    state.targetSceneLayer = 3;
    state.targetDayTime = 0x8000;
    state.targetSkyboxTime = 0x8010;
    state.targetTimeSpeed = 3;
    state.targetNight = 1;
    state.deadlineTick = 500;
    state.participants = { 1, 2 };
    state.readyParticipants = { 1 };
    const auto barrier = DecodeBarrierSnapshot(EncodeBarrierSnapshot({ state }));
    assert(barrier.has_value());
    assert(barrier->state.operationEpoch == 20);
    assert(barrier->state.scope == state.scope);
    assert(barrier->state.targetEntrance == state.targetEntrance);
    assert(barrier->state.targetCutsceneIndex == state.targetCutsceneIndex);
    assert(barrier->state.targetLinkAge == state.targetLinkAge);
    assert(barrier->state.targetSceneLayer == state.targetSceneLayer);
    assert(barrier->state.targetDayTime == state.targetDayTime);
    assert(barrier->state.targetSkyboxTime == state.targetSkyboxTime);
    assert(barrier->state.targetTimeSpeed == state.targetTimeSpeed);
    assert(barrier->state.targetNight == state.targetNight);
    assert(barrier->state.participants == state.participants);

    BarrierReadyMessage ready{ { 90, 4 }, 20, 2, 17, 1 };
    const auto decodedReady = DecodeBarrierReady(EncodeBarrierReady(ready));
    assert(decodedReady.has_value());
    assert(decodedReady->participantId == 2);

    AttackIntentMessage attack{ { 90, 4 }, 2, 88, 0x12345678, 300, 17, 1, 15, 2 };
    const auto decodedAttack = DecodeAttackIntent(EncodeAttackIntent(attack));
    assert(decodedAttack.has_value());
    assert(decodedAttack->requestId == 88);
    assert(decodedAttack->entityId == 0x12345678);
    assert(decodedAttack->damageEffect == 15);
    assert(decodedAttack->damage == 2);

    CollectibleIntentMessage collectible{ { 90, 4 }, 2, 89, 0x9988, 17, 3, 12 };
    const auto decodedCollectible = DecodeCollectibleIntent(EncodeCollectibleIntent(collectible));
    assert(decodedCollectible.has_value());
    assert(decodedCollectible->locationId == 0x9988);

    ProgressionSnapshotMessage progression;
    progression.scope = { 90, 4 };
    progression.revision = 7;
    progression.locations = { { 0x9988, 17, 3, 12 }, { 0x7766, 2, 0, 4 } };
    progression.shared.durableItems[0] = 0x0A;
    progression.shared.durableItems[17] = 0x5A;
    progression.shared.tradeItems = { 0x2D, 0x37 };
    progression.shared.bottleOwnershipMask = 0x0B;
    progression.shared.equipment = 0x1234;
    progression.shared.upgrades = 0x89ABCDEF;
    progression.shared.questItems = 0x10203040;
    progression.shared.dungeonItems[19] = 7;
    progression.shared.dungeonKeys[18] = -1;
    progression.shared.healthCapacity = 0xA0;
    progression.shared.linkAge = 1;
    progression.shared.magicLevel = 2;
    progression.shared.isMagicAcquired = 1;
    progression.shared.isDoubleMagicAcquired = 1;
    progression.shared.isDoubleDefenseAcquired = 1;
    progression.shared.bgsFlag = 1;
    progression.shared.gsTokens = 77;
    progression.shared.eventChkInf[3] = 0x0208;
    progression.shared.eventChkInf[13] = 0x0040;
    const auto decodedProgression = DecodeProgressionSnapshot(EncodeProgressionSnapshot(progression));
    assert(decodedProgression.has_value());
    assert(decodedProgression->revision == 7);
    assert(decodedProgression->locations.size() == 2);
    assert(decodedProgression->locations[1].flag == 4);
    assert(decodedProgression->shared == progression.shared);

    ProgressionIntentMessage progressionIntent;
    progressionIntent.scope = { 90, 4 };
    progressionIntent.participantId = 2;
    progressionIntent.requestId = 90;
    progressionIntent.kind = ProgressionIntentKind::ItemReceived;
    progressionIntent.itemId = 0x0A;
    progressionIntent.modIndex = 0;
    progressionIntent.mapIndex = 3;
    progressionIntent.remainingDungeonKeys = 4;
    const auto decodedProgressionIntent =
        DecodeProgressionIntent(EncodeProgressionIntent(progressionIntent));
    assert(decodedProgressionIntent.has_value());
    assert(decodedProgressionIntent->scope == progressionIntent.scope);
    assert(decodedProgressionIntent->participantId == 2);
    assert(decodedProgressionIntent->requestId == 90);
    assert(decodedProgressionIntent->kind == ProgressionIntentKind::ItemReceived);
    assert(decodedProgressionIntent->itemId == 0x0A);
    assert(decodedProgressionIntent->mapIndex == 3);
    assert(decodedProgressionIntent->remainingDungeonKeys == 4);

    progressionIntent.kind = ProgressionIntentKind::GlobalFlagChanged;
    progressionIntent.flagType = 5;
    progressionIntent.flag = 0x33;
    progressionIntent.set = true;
    const auto decodedGlobalFlagIntent =
        DecodeProgressionIntent(EncodeProgressionIntent(progressionIntent));
    assert(decodedGlobalFlagIntent.has_value());
    assert(decodedGlobalFlagIntent->kind == ProgressionIntentKind::GlobalFlagChanged);
    assert(decodedGlobalFlagIntent->flagType == 5);
    assert(decodedGlobalFlagIntent->flag == 0x33);
    assert(decodedGlobalFlagIntent->set);
}

static void TestInvalidPacket() {
    Packet source{ MessageType::Heartbeat, 1, {} };
    std::vector<uint8_t> encoded = EncodePacket(source);
    encoded[0] = 0;

    Packet decoded;
    size_t consumed = 0;
    std::string error;
    assert(TryDecodePacket(encoded, decoded, consumed, error) == DecodeResult::Invalid);
    assert(error == "invalid packet magic");

    encoded = EncodePacket(source);
    encoded[5] = 1;
    assert(TryDecodePacket(encoded, decoded, consumed, error) == DecodeResult::Invalid);
    assert(error == "unsupported protocol version");
}

static void TestDynamicStalchildLifecycle() {
    DynamicStalchildIdentityRegistry identities;
    const int hostActorOne = 1;
    const int hostActorTwo = 2;
    const uint64_t firstId = identities.GetOrAssign(&hostActorOne);
    const uint64_t secondId = identities.GetOrAssign(&hostActorTwo);
    assert(IsDynamicStalchildEntityId(firstId));
    assert(IsDynamicStalchildEntityId(secondId));
    assert(firstId != secondId);
    assert(identities.Find(&hostActorOne) == firstId);
    identities.Forget(&hostActorOne);
    assert(identities.Find(&hostActorOne) == 0);

    // Guest field spawners must be stopped before their native while-loop runs. Other encounter types remain native.
    const uint16_t stalchildParams = static_cast<uint16_t>(kStalchildSpawnerType << kEncounterSpawnerTypeShift);
    assert(ShouldSuppressGuestStalchildSpawner(true, true, stalchildParams));
    assert(!ShouldSuppressGuestStalchildSpawner(false, true, stalchildParams));
    assert(!ShouldSuppressGuestStalchildSpawner(true, false, stalchildParams));
    assert(!ShouldSuppressGuestStalchildSpawner(true, true, 0));

    const uint16_t guestTargetState = EncodeStalchildState(true, StalchildTarget::Guest, 37);
    assert(IsStalchildTargetable(guestTargetState));
    assert(DecodeStalchildTarget(guestTargetState) == StalchildTarget::Guest);
    assert(DecodeStalchildAttackSequence(guestTargetState) == 37);
    assert(DecodeStalchildAttackSequence(
               EncodeStalchildState(false, StalchildTarget::Host, kStalchildStateAttackSequenceMask + 1)) ==
           0);
    assert(IsNewerStalchildAttackSequence(1, 0));
    assert(IsNewerStalchildAttackSequence(38, 37));
    assert(IsNewerStalchildAttackSequence(1, kStalchildStateAttackSequenceMask));
    assert(!IsNewerStalchildAttackSequence(37, 37));
    assert(!IsNewerStalchildAttackSequence(36, 37));
    assert(!IsNewerStalchildAttackSequence(kStalchildStateAttackSequenceMask, 1));
    assert(!IsStalchildTargetable(EncodeStalchildState(false, StalchildTarget::Host)));
    assert(SelectStalchildTarget(true, 40000.0f, 100.0f, StalchildTarget::Host, false) ==
           StalchildTarget::Guest);
    assert(SelectStalchildTarget(true, 100.0f, 40000.0f, StalchildTarget::Guest, false) ==
           StalchildTarget::Host);
    assert(SelectStalchildTarget(true, 40000.0f, 100.0f, StalchildTarget::Host, true) ==
           StalchildTarget::Host);
    assert(SelectStalchildTarget(false, 40000.0f, 100.0f, StalchildTarget::Guest, true) ==
           StalchildTarget::Host);
    std::array<int16_t, kMaximumActorAdapterWords> stalchildAdapter{};
    stalchildAdapter[kStalchildAdapterTarget] = static_cast<int16_t>(StalchildTarget::Guest);
    stalchildAdapter[kStalchildAdapterBehavior] = 3;
    stalchildAdapter[kStalchildAdapterAttackActive] = 1;
    stalchildAdapter[kStalchildAdapterShapeYOffset] = -2400;
    stalchildAdapter[kStalchildAdapterShadowScale] = 1750;
    assert(IsValidStalchildAdapterState(kStalchildAdapterWordCount, stalchildAdapter.data()));
    stalchildAdapter[kStalchildAdapterBehavior] = 7;
    assert(!IsValidStalchildAdapterState(kStalchildAdapterWordCount, stalchildAdapter.data()));
    stalchildAdapter[kStalchildAdapterBehavior] = 3;
    stalchildAdapter[kStalchildAdapterShapeYOffset] = -8001;
    assert(!IsValidStalchildAdapterState(kStalchildAdapterWordCount, stalchildAdapter.data()));
    stalchildAdapter[kStalchildAdapterShapeYOffset] = 0;
    stalchildAdapter[kStalchildAdapterShadowScale] = 2501;
    assert(!IsValidStalchildAdapterState(kStalchildAdapterWordCount, stalchildAdapter.data()));

    DynamicStalchildSnapshotLifecycle lifecycle;
    size_t publishedAliveSnapshots = 0;
    for (size_t frame = 0; frame < 600; ++frame) {
        if (lifecycle.ShouldPublish(secondId, true)) {
            ++publishedAliveSnapshots;
        }
    }
    assert(publishedAliveSnapshots == 600);
    assert(lifecycle.Size() == 1);

    // Dawn/destruction has one durable terminal transition, regardless of duplicate engine lifecycle callbacks.
    assert(lifecycle.ShouldPublish(secondId, false));
    for (size_t callback = 0; callback < 32; ++callback) {
        assert(!lifecycle.ShouldPublish(secondId, false));
    }
    lifecycle.Forget(secondId);
    assert(lifecycle.Size() == 0);
}

int main() {
    TestRoundTrip();
    TestFragmentedPacket();
    TestClockAndAckMessages();
    TestPlayerSnapshot();
    TestRemotePlayerRoomLifecycle();
    TestPlayerPresentation();
    TestCycleSnapshot();
    TestWorldStateMessages();
    TestActorSnapshot();
    TestCoordinationMessages();
    TestInvalidPacket();
    TestDynamicStalchildLifecycle();
    std::cout << "HyruleCoop protocol tests passed\n";
    return 0;
}
