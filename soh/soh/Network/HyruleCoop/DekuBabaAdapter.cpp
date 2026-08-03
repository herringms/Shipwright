#include "DekuBabaAdapter.h"
#include "EntityIdentity.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include "src/overlays/actors/ovl_En_Dekubaba/z_en_dekubaba.h"

extern "C" {
void EnDekubaba_Wait(EnDekubaba* actor, PlayState* play);
void EnDekubaba_Grow(EnDekubaba* actor, PlayState* play);
void EnDekubaba_Retract(EnDekubaba* actor, PlayState* play);
void EnDekubaba_DecideLunge(EnDekubaba* actor, PlayState* play);
void EnDekubaba_Lunge(EnDekubaba* actor, PlayState* play);
void EnDekubaba_PrepareLunge(EnDekubaba* actor, PlayState* play);
void EnDekubaba_PullBack(EnDekubaba* actor, PlayState* play);
void EnDekubaba_Recover(EnDekubaba* actor, PlayState* play);
void EnDekubaba_Hit(EnDekubaba* actor, PlayState* play);
void EnDekubaba_StunnedVertical(EnDekubaba* actor, PlayState* play);
void EnDekubaba_Sway(EnDekubaba* actor, PlayState* play);
void EnDekubaba_PrunedSomersault(EnDekubaba* actor, PlayState* play);
void EnDekubaba_ShrinkDie(EnDekubaba* actor, PlayState* play);
void EnDekubaba_DeadStickDrop(EnDekubaba* actor, PlayState* play);
}

namespace HyruleCoop {
namespace {

constexpr uint8_t kAdapterWordCount = 55;

uint16_t GetActionState(const EnDekubaba* actor) {
    const EnDekubabaActionFunc actions[] = {
        nullptr,
        EnDekubaba_Wait,
        EnDekubaba_Grow,
        EnDekubaba_Retract,
        EnDekubaba_DecideLunge,
        EnDekubaba_Lunge,
        EnDekubaba_PrepareLunge,
        EnDekubaba_PullBack,
        EnDekubaba_Recover,
        EnDekubaba_Hit,
        EnDekubaba_StunnedVertical,
        EnDekubaba_Sway,
        EnDekubaba_PrunedSomersault,
        EnDekubaba_ShrinkDie,
        EnDekubaba_DeadStickDrop,
    };
    for (uint16_t i = 1; i < std::size(actions); ++i) {
        if (actor->actionFunc == actions[i]) {
            return i;
        }
    }
    return 0;
}

EnDekubabaActionFunc GetActionFunction(uint16_t state) {
    const EnDekubabaActionFunc actions[] = {
        nullptr,
        EnDekubaba_Wait,
        EnDekubaba_Grow,
        EnDekubaba_Retract,
        EnDekubaba_DecideLunge,
        EnDekubaba_Lunge,
        EnDekubaba_PrepareLunge,
        EnDekubaba_PullBack,
        EnDekubaba_Recover,
        EnDekubaba_Hit,
        EnDekubaba_StunnedVertical,
        EnDekubaba_Sway,
        EnDekubaba_PrunedSomersault,
        EnDekubaba_ShrinkDie,
        EnDekubaba_DeadStickDrop,
    };
    return state < std::size(actions) ? actions[state] : nullptr;
}

void StoreFloat(std::array<int16_t, kMaximumActorAdapterWords>& state, size_t offset, float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    state[offset] = static_cast<int16_t>(bits >> 16);
    state[offset + 1] = static_cast<int16_t>(bits);
}

float LoadFloat(const std::array<int16_t, kMaximumActorAdapterWords>& state, size_t offset) {
    const uint32_t bits = (static_cast<uint32_t>(static_cast<uint16_t>(state[offset])) << 16) |
                          static_cast<uint16_t>(state[offset + 1]);
    return std::bit_cast<float>(bits);
}

} // namespace

uint64_t GetDekuBabaEntityId(void* actorRef, int16_t scene, uint32_t worldGeneration) {
    const EnDekubaba* actor = static_cast<const EnDekubaba*>(actorRef);
    StaticEntitySignature signature;
    signature.worldGeneration = worldGeneration;
    signature.scene = scene;
    signature.room = actor->actor.room;
    signature.actorId = actor->actor.id;
    signature.params = actor->actor.params;
    signature.homePosition[0] = std::lround(actor->actor.home.pos.x);
    signature.homePosition[1] = std::lround(actor->actor.home.pos.y);
    signature.homePosition[2] = std::lround(actor->actor.home.pos.z);
    signature.homeRotation[0] = actor->actor.home.rot.x;
    signature.homeRotation[1] = actor->actor.home.rot.y;
    signature.homeRotation[2] = actor->actor.home.rot.z;
    return BuildStaticEntityId(signature);
}

ActorSnapshotMessage CaptureDekuBabaSnapshot(void* actorRef, int16_t scene, uint32_t hostTick,
                                             SessionScope scope, bool alive) {
    const EnDekubaba* actor = static_cast<const EnDekubaba*>(actorRef);
    ActorSnapshotMessage message;
    message.scope = scope;
    message.hostTick = hostTick;
    message.entityId = GetDekuBabaEntityId(actorRef, scene, scope.worldGeneration);
    message.scene = scene;
    message.room = actor->actor.room;
    message.actorId = actor->actor.id;
    message.params = actor->actor.params;
    message.homePosition[0] = actor->actor.home.pos.x;
    message.homePosition[1] = actor->actor.home.pos.y;
    message.homePosition[2] = actor->actor.home.pos.z;
    message.position[0] = actor->actor.world.pos.x;
    message.position[1] = actor->actor.world.pos.y;
    message.position[2] = actor->actor.world.pos.z;
    message.velocity[0] = actor->actor.velocity.x;
    message.velocity[1] = actor->actor.velocity.y;
    message.velocity[2] = actor->actor.velocity.z;
    message.scale[0] = actor->actor.scale.x;
    message.scale[1] = actor->actor.scale.y;
    message.scale[2] = actor->actor.scale.z;
    message.worldRotation[0] = actor->actor.world.rot.x;
    message.worldRotation[1] = actor->actor.world.rot.y;
    message.worldRotation[2] = actor->actor.world.rot.z;
    message.shapeRotation[0] = actor->actor.shape.rot.x;
    message.shapeRotation[1] = actor->actor.shape.rot.y;
    message.shapeRotation[2] = actor->actor.shape.rot.z;
    message.speed = actor->actor.speedXZ;
    message.gravity = actor->actor.gravity;
    message.health = actor->actor.colChkInfo.health;
    message.stateId = GetActionState(actor);
    message.animationFrame = actor->skelAnime.curFrame;
    message.animationSpeed = actor->skelAnime.playSpeed;
    message.alive = alive;
    message.adapterWordCount = kAdapterWordCount;
    message.adapterState[0] = actor->timer;
    message.adapterState[1] = actor->targetSwayAngle;
    for (size_t i = 0; i < 3; ++i) {
        message.adapterState[2 + i] = actor->stemSectionAngle[i];
    }
    StoreFloat(message.adapterState, 5, actor->size);
    for (size_t i = 0; i < 8; ++i) {
        message.adapterState[7 + i * 3] = actor->jointTable[i].x;
        message.adapterState[8 + i * 3] = actor->jointTable[i].y;
        message.adapterState[9 + i * 3] = actor->jointTable[i].z;
        message.adapterState[31 + i * 3] = actor->morphTable[i].x;
        message.adapterState[32 + i * 3] = actor->morphTable[i].y;
        message.adapterState[33 + i * 3] = actor->morphTable[i].z;
    }
    return message;
}

bool ApplyDekuBabaSnapshot(void* actorRef, const ActorSnapshotMessage& message) {
    EnDekubaba* actor = static_cast<EnDekubaba*>(actorRef);
    if (message.actorId != ACTOR_EN_DEKUBABA || message.adapterWordCount < kAdapterWordCount) {
        return false;
    }
    actor->actor.world.pos = { message.position[0], message.position[1], message.position[2] };
    actor->actor.velocity = { message.velocity[0], message.velocity[1], message.velocity[2] };
    actor->actor.scale = { message.scale[0], message.scale[1], message.scale[2] };
    actor->actor.world.rot = { message.worldRotation[0], message.worldRotation[1], message.worldRotation[2] };
    actor->actor.shape.rot = { message.shapeRotation[0], message.shapeRotation[1], message.shapeRotation[2] };
    actor->actor.speedXZ = message.speed;
    actor->actor.gravity = message.gravity;
    actor->actor.colChkInfo.health = static_cast<uint8_t>(message.health);
    actor->skelAnime.curFrame = message.animationFrame;
    actor->skelAnime.playSpeed = message.animationSpeed;
    if (EnDekubabaActionFunc action = GetActionFunction(message.stateId); action != nullptr) {
        actor->actionFunc = action;
    }
    actor->timer = message.adapterState[0];
    actor->targetSwayAngle = message.adapterState[1];
    for (size_t i = 0; i < 3; ++i) {
        actor->stemSectionAngle[i] = message.adapterState[2 + i];
    }
    actor->size = LoadFloat(message.adapterState, 5);
    for (size_t i = 0; i < 8; ++i) {
        actor->jointTable[i] = { message.adapterState[7 + i * 3], message.adapterState[8 + i * 3],
                                 message.adapterState[9 + i * 3] };
        actor->morphTable[i] = { message.adapterState[31 + i * 3], message.adapterState[32 + i * 3],
                                 message.adapterState[33 + i * 3] };
    }
    return true;
}

bool ConsumeDekuBabaHit(void* actorRef) {
    EnDekubaba* actor = static_cast<EnDekubaba*>(actorRef);
    if ((actor->collider.base.acFlags & AC_HIT) == 0) {
        return false;
    }
    actor->collider.base.acFlags &= ~AC_HIT;
    return true;
}

bool DamageDekuBaba(void* actorRef, int16_t damage) {
    EnDekubaba* actor = static_cast<EnDekubaba*>(actorRef);
    if (damage <= 0 || actor->actor.colChkInfo.health <= 0) {
        return false;
    }
    actor->actor.colChkInfo.health = std::max<int16_t>(0, actor->actor.colChkInfo.health - damage);
    if (actor->actor.colChkInfo.health == 0) {
        Actor_Kill(&actor->actor);
        return false;
    }
    return true;
}

} // namespace HyruleCoop
