#include "GohmaAdapter.h"
#include "EntityIdentity.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>

extern "C" {
#include "functions.h"
#include "src/overlays/actors/ovl_Boss_Goma/z_boss_goma.h"

void BossGoma_Encounter(BossGoma* actor, PlayState* play);
void BossGoma_Defeated(BossGoma* actor, PlayState* play);
void BossGoma_FloorAttackPosture(BossGoma* actor, PlayState* play);
void BossGoma_FloorPrepareAttack(BossGoma* actor, PlayState* play);
void BossGoma_FloorAttack(BossGoma* actor, PlayState* play);
void BossGoma_FloorDamaged(BossGoma* actor, PlayState* play);
void BossGoma_FloorLandStruckDown(BossGoma* actor, PlayState* play);
void BossGoma_FloorLand(BossGoma* actor, PlayState* play);
void BossGoma_FloorStunned(BossGoma* actor, PlayState* play);
void BossGoma_FallJump(BossGoma* actor, PlayState* play);
void BossGoma_FallStruckDown(BossGoma* actor, PlayState* play);
void BossGoma_CeilingSpawnGohmas(BossGoma* actor, PlayState* play);
void BossGoma_CeilingPrepareSpawnGohmas(BossGoma* actor, PlayState* play);
void BossGoma_FloorIdle(BossGoma* actor, PlayState* play);
void BossGoma_CeilingIdle(BossGoma* actor, PlayState* play);
void BossGoma_FloorMain(BossGoma* actor, PlayState* play);
void BossGoma_WallClimb(BossGoma* actor, PlayState* play);
void BossGoma_CeilingMoveToCenter(BossGoma* actor, PlayState* play);
void BossGoma_SetupDefeated(BossGoma* actor, PlayState* play);
}

namespace HyruleCoop {
namespace {

constexpr uint8_t kAdapterWordCount = 55;

const BossGomaActionFunc kActions[] = {
    nullptr,
    BossGoma_Encounter,
    BossGoma_Defeated,
    BossGoma_FloorAttackPosture,
    BossGoma_FloorPrepareAttack,
    BossGoma_FloorAttack,
    BossGoma_FloorDamaged,
    BossGoma_FloorLandStruckDown,
    BossGoma_FloorLand,
    BossGoma_FloorStunned,
    BossGoma_FallJump,
    BossGoma_FallStruckDown,
    BossGoma_CeilingSpawnGohmas,
    BossGoma_CeilingPrepareSpawnGohmas,
    BossGoma_FloorIdle,
    BossGoma_CeilingIdle,
    BossGoma_FloorMain,
    BossGoma_WallClimb,
    BossGoma_CeilingMoveToCenter,
};

uint16_t GetActionState(const BossGoma* actor) {
    for (uint16_t index = 1; index < std::size(kActions); ++index) {
        if (actor->actionFunc == kActions[index]) {
            return index;
        }
    }
    return 0;
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

uint64_t GetGohmaEntityId(void* actorRef, int16_t scene, uint32_t worldGeneration) {
    const BossGoma* actor = static_cast<const BossGoma*>(actorRef);
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

ActorSnapshotMessage CaptureGohmaSnapshot(void* actorRef, int16_t scene, uint32_t hostTick,
                                         SessionScope scope, bool alive) {
    const BossGoma* actor = static_cast<const BossGoma*>(actorRef);
    ActorSnapshotMessage message;
    message.scope = scope;
    message.hostTick = hostTick;
    message.entityId = GetGohmaEntityId(actorRef, scene, scope.worldGeneration);
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
    message.animationFrame = actor->skelanime.curFrame;
    message.animationSpeed = actor->skelanime.playSpeed;
    message.alive = alive;
    message.adapterWordCount = kAdapterWordCount;

    const int16_t shorts[] = {
        actor->frameCount, actor->patienceTimer, actor->eyeLidBottomRotX, actor->eyeLidTopRotX,
        actor->eyeClosedTimer, actor->eyeIrisRotX, actor->eyeIrisRotY, actor->unusedTimer,
        actor->childrenGohmaState[0], actor->childrenGohmaState[1], actor->childrenGohmaState[2],
        actor->spawnGohmasActionTimer, actor->eyeState, actor->doNotMoveThisFrame, actor->visualState,
        actor->invincibilityFrames, actor->disableGameplayLogic, actor->decayingProgress,
        actor->noBackfaceCulling, actor->blinkTimer, actor->lookedAtFrames, actor->actionState,
        actor->framesUntilNextAction, actor->timer, actor->sfxFaintTimer,
    };
    for (size_t index = 0; index < std::size(shorts); ++index) {
        message.adapterState[index] = shorts[index];
    }
    size_t offset = std::size(shorts);
    for (float scale : actor->tailLimbsScale) {
        StoreFloat(message.adapterState, offset, scale);
        offset += 2;
    }
    StoreFloat(message.adapterState, offset, actor->eyeIrisScaleX);
    offset += 2;
    StoreFloat(message.adapterState, offset, actor->eyeIrisScaleY);
    offset += 2;
    for (float color : actor->mainEnvColor) {
        StoreFloat(message.adapterState, offset, color);
        offset += 2;
    }
    for (float color : actor->eyeEnvColor) {
        StoreFloat(message.adapterState, offset, color);
        offset += 2;
    }
    return message;
}

bool ApplyGohmaSnapshot(void* actorRef, const ActorSnapshotMessage& message) {
    BossGoma* actor = static_cast<BossGoma*>(actorRef);
    if (message.actorId != ACTOR_BOSS_GOMA || message.adapterWordCount < kAdapterWordCount) {
        return false;
    }
    actor->actor.world.pos = { message.position[0], message.position[1], message.position[2] };
    actor->actor.velocity = { message.velocity[0], message.velocity[1], message.velocity[2] };
    actor->actor.scale = { message.scale[0], message.scale[1], message.scale[2] };
    actor->actor.world.rot = { message.worldRotation[0], message.worldRotation[1], message.worldRotation[2] };
    actor->actor.shape.rot = { message.shapeRotation[0], message.shapeRotation[1], message.shapeRotation[2] };
    actor->actor.speedXZ = message.speed;
    actor->actor.gravity = message.gravity;
    actor->actor.colChkInfo.health = static_cast<uint8_t>(std::max<int16_t>(0, message.health));
    if (message.health <= 0) {
        actor->actor.flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
        actor->collider.elements[0].info.bumperFlags &= ~BUMP_HIT;
    }
    actor->skelanime.curFrame = message.animationFrame;
    actor->skelanime.playSpeed = message.animationSpeed;
    if (message.stateId > 0 && message.stateId < std::size(kActions)) {
        actor->actionFunc = kActions[message.stateId];
    }

    int16_t* shorts[] = {
        &actor->frameCount, &actor->patienceTimer, &actor->eyeLidBottomRotX, &actor->eyeLidTopRotX,
        &actor->eyeClosedTimer, &actor->eyeIrisRotX, &actor->eyeIrisRotY, &actor->unusedTimer,
        &actor->childrenGohmaState[0], &actor->childrenGohmaState[1], &actor->childrenGohmaState[2],
        &actor->spawnGohmasActionTimer, &actor->eyeState, &actor->doNotMoveThisFrame, &actor->visualState,
        &actor->invincibilityFrames, &actor->disableGameplayLogic, &actor->decayingProgress,
        &actor->noBackfaceCulling, &actor->blinkTimer, &actor->lookedAtFrames, &actor->actionState,
        &actor->framesUntilNextAction, &actor->timer, &actor->sfxFaintTimer,
    };
    for (size_t index = 0; index < std::size(shorts); ++index) {
        *shorts[index] = message.adapterState[index];
    }
    size_t offset = std::size(shorts);
    for (float& scale : actor->tailLimbsScale) {
        scale = LoadFloat(message.adapterState, offset);
        offset += 2;
    }
    actor->eyeIrisScaleX = LoadFloat(message.adapterState, offset);
    offset += 2;
    actor->eyeIrisScaleY = LoadFloat(message.adapterState, offset);
    offset += 2;
    for (float& color : actor->mainEnvColor) {
        color = LoadFloat(message.adapterState, offset);
        offset += 2;
    }
    for (float& color : actor->eyeEnvColor) {
        color = LoadFloat(message.adapterState, offset);
        offset += 2;
    }
    return true;
}

bool StartGohmaDefeatPresentation(void* actorRef, void* playStateRef, const ActorSnapshotMessage& message) {
    BossGoma* actor = static_cast<BossGoma*>(actorRef);
    PlayState* play = static_cast<PlayState*>(playStateRef);
    if (actor == nullptr || play == nullptr || message.health > 0 || message.stateId != 2 ||
        !ApplyGohmaSnapshot(actor, message)) {
        return false;
    }

    // A delayed snapshot may contain an already-advanced host cutscene state. Each peer must initialize its own
    // camera, animation, and timers from the beginning while retaining the authoritative terminal transform.
    BossGoma_SetupDefeated(actor, play);
    actor->actor.colChkInfo.health = 0;
    return true;
}

bool ConsumeGohmaHit(void* actorRef) {
    BossGoma* actor = static_cast<BossGoma*>(actorRef);
    if ((actor->collider.elements[0].info.bumperFlags & BUMP_HIT) == 0) {
        return false;
    }
    actor->collider.elements[0].info.bumperFlags &= ~BUMP_HIT;
    return true;
}

bool CanDamageGohma(void* actorRef) {
    const BossGoma* actor = static_cast<const BossGoma*>(actorRef);
    return actor->actor.colChkInfo.health > 0 && actor->actionFunc == BossGoma_FloorStunned &&
           actor->invincibilityFrames == 0;
}

bool DamageGohma(void* actorRef, void* playStateRef, int16_t damage) {
    BossGoma* actor = static_cast<BossGoma*>(actorRef);
    PlayState* play = static_cast<PlayState*>(playStateRef);
    if (play == nullptr || damage <= 0 || !CanDamageGohma(actor)) {
        return false;
    }
    actor->actor.colChkInfo.health = static_cast<uint8_t>(std::max<int16_t>(0, actor->actor.colChkInfo.health - damage));
    if (actor->actor.colChkInfo.health == 0) {
        BossGoma_SetupDefeated(actor, play);
        Enemy_StartFinishingBlow(play, &actor->actor);
        GameInteractor_ExecuteOnBossDefeat(&actor->actor);
        Player* player = GET_PLAYER(play);
        if (player->focusActor == &actor->actor) {
            Player_ClearZTargeting(player);
        }
    } else {
        actor->invincibilityFrames = 10;
    }
    return true;
}

void RegisterGohmaGuestCollision(void* actorRef, void* playStateRef) {
    BossGoma* actor = static_cast<BossGoma*>(actorRef);
    PlayState* play = static_cast<PlayState*>(playStateRef);
    if (play == nullptr || actor->actor.colChkInfo.health == 0) {
        return;
    }
    CollisionCheck_SetAC(play, &play->colChkCtx, &actor->collider.base);
    CollisionCheck_SetOC(play, &play->colChkCtx, &actor->collider.base);
}

void PrepareGohmaForAutomatedTest(void* actorRef) {
    BossGoma* actor = static_cast<BossGoma*>(actorRef);
    actor->actionFunc = BossGoma_FloorStunned;
    actor->actor.colChkInfo.health = 2;
    actor->disableGameplayLogic = false;
    actor->invincibilityFrames = 0;
    actor->eyeClosedTimer = 0;
    actor->framesUntilNextAction = 600;
}

} // namespace HyruleCoop
