#include "RemotePlayer.h"

#include "HyruleCoop.h"
#include "RemotePlayerRoomPolicy.h"
#include "soh/Enhancements/nametag.h"

#include <algorithm>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "objects/object_horse/object_horse.h"
#include "src/overlays/actors/ovl_En_Horse/z_en_horse.h"
#include "variables.h"

extern PlayState* gPlayState;

void Player_UseItem(PlayState* play, Player* player, s32 item);
void Player_Draw(Actor* actor, PlayState* play);
void EnHorse_Draw(Actor* actor, PlayState* play);
}

namespace {

void CopyVec3s(Vec3s& destination, const int16_t source[3]) {
    destination.x = source[0];
    destination.y = source[1];
    destination.z = source[2];
}

bool GetState(const HyruleCoop::PlayerSnapshotMessage*& state) {
    if (HyruleCoop::Manager::Instance == nullptr) {
        return false;
    }
    state = HyruleCoop::Manager::Instance->GetRemotePlayerSnapshot();
    return state != nullptr;
}

bool IsVisibleInCurrentRoom(const HyruleCoop::PlayerSnapshotMessage& state) {
    return gPlayState != nullptr &&
           HyruleCoop::IsRemotePlayerVisibleInRoom(
               gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num, state.scene, state.room,
               HyruleCoop::Manager::Instance != nullptr
                   ? HyruleCoop::Manager::Instance->GetLocalTimelineScope()
                   : HyruleCoop::TimelineScope{ gPlayState->linkAgeOnLoad,
                                                 static_cast<int16_t>(gSaveContext.sceneLayer) },
               { state.linkAge, state.sceneLayer });
}

AnimationHeader* RemoteHorseAnimation(int8_t index) {
    static const char* const animations[] = {
        gEponaIdleAnim,      gEponaWhinnyAnim,   gEponaRefuseAnim,
        gEponaRearingAnim,   gEponaWalkingAnim,  gEponaTrottingAnim,
        gEponaGallopingAnim, gEponaJumpingAnim,  gEponaJumpingHighAnim,
    };
    constexpr int8_t kAnimationCount = static_cast<int8_t>(sizeof(animations) / sizeof(animations[0]));
    const int8_t normalized = std::clamp(index, static_cast<int8_t>(0), static_cast<int8_t>(kAnimationCount - 1));
    return reinterpret_cast<AnimationHeader*>(const_cast<char*>(animations[normalized]));
}

SkeletonHeader* RemoteHorseSkeleton() {
    return reinterpret_cast<SkeletonHeader*>(const_cast<char*>(gEponaSkel));
}

void ApplyRemoteHorsePose(EnHorse* horse, const HyruleCoop::PlayerSnapshotMessage& state) {
    Actor* actor = &horse->actor;
    actor->world.pos = { state.horsePosition[0], state.horsePosition[1], state.horsePosition[2] };
    actor->prevPos = actor->world.pos;
    CopyVec3s(actor->world.rot, state.horseRotation);
    CopyVec3s(actor->shape.rot, state.horseRotation);
    actor->speedXZ = state.horseSpeed;
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 70.0f;

    const int8_t animation = std::clamp(state.horseAnimation, static_cast<int8_t>(0), static_cast<int8_t>(8));
    AnimationHeader* animationHeader = RemoteHorseAnimation(animation);
    if (horse->animationIdx != animation) {
        Animation_Change(&horse->skin.skelAnime, animationHeader, 1.0f, 0.0f,
                         Animation_GetLastFrame(animationHeader), ANIMMODE_LOOP, 0.0f);
        horse->animationIdx = animation;
    }
    const float lastFrame = static_cast<float>(Animation_GetLastFrame(animationHeader));
    horse->skin.skelAnime.playSpeed = 0.0f;
    horse->skin.skelAnime.curFrame = std::clamp(state.horseAnimationFrame, 0.0f, lastFrame);
    horse->curFrame = horse->skin.skelAnime.curFrame;
    horse->stateFlags &= ~ENHORSE_INACTIVE;
}

} // namespace

extern "C" void HyruleCoopRemotePlayer_Init(Actor* actor, PlayState* play) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state)) {
        Actor_Kill(actor);
        return;
    }

    Player* player = reinterpret_cast<Player*>(actor);
    const s32 originalAge = gSaveContext.linkAge;
    const u8 originalButtonItem = gSaveContext.equips.buttonItems[0];
    gSaveContext.linkAge = state->linkAge;

    player->itemAction = player->heldItemAction = -1;
    player->heldItemId = ITEM_NONE;
    Player_UseItem(play, player, ITEM_NONE);
    Player_SetModelGroup(player, Player_ActionToModelGroup(player, player->heldItemAction));
    play->playerInit(player, play, gPlayerSkelHeaders[state->linkAge]);
    play->func_11D54(player, play);

    // playerInit deliberately makes the local player roomless. A remote player must instead participate in normal
    // room cleanup so every room load retires the previous remote actor.
    actor->room = play->roomCtx.curRoom.num;

    // playerInit establishes local defaults. Reapply the replicated presentation before the first draw so a
    // reconnect cannot briefly render the guest without an equipped mask or weapon.
    player->currentBoots = state->boots;
    player->currentShield = state->shield;
    player->currentTunic = state->tunic;
    player->currentMask = state->currentMask;
    player->itemAction = state->itemAction;
    player->heldItemAction = state->heldItemAction;
    player->meleeWeaponState = state->meleeWeaponState;
    player->meleeWeaponAnimation = state->meleeWeaponAnimation;
    HyruleCoop::Manager::Instance->NotifyRemotePlayerPoseApplied(state->meleeWeaponState > 0);
    gSaveContext.equips.buttonItems[0] = state->buttonItem;
    Player_SetModelGroup(player, state->modelGroup);
    gSaveContext.equips.buttonItems[0] = originalButtonItem;

    actor->flags |=
        ACTOR_FLAG_LOCK_ON_DISABLED | ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    actor->shape.shadowAlpha = 255;
    gSaveContext.linkAge = originalAge;

    const std::string& remoteName = HyruleCoop::Manager::Instance->GetRemotePlayerName();
    if (!remoteName.empty()) {
        NameTagOptions options{};
        options.tag = "hyrule-coop-player";
        options.yOffset = 24;
        NameTag_RegisterForActorWithOptions(actor, remoteName.c_str(), options);
    }
}

extern "C" void HyruleCoopRemotePlayer_Update(Actor* actor, PlayState*) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state) || gPlayState == nullptr) {
        // Timeline mismatch is a hard visibility boundary, not just a draw rule. Retire the actor so Link's
        // collider, targeting state, and update hooks cannot leak between child/adult or day/night worlds.
        if (HyruleCoop::Manager::Instance != nullptr) {
            HyruleCoop::Manager::Instance->NotifyRemotePlayerDestroyed(actor);
        }
        Actor_Kill(actor);
        return;
    }

    const int16_t activeRoom = gPlayState->roomCtx.curRoom.num;
    if (!HyruleCoop::IsRemotePlayerActorInActiveRoom(actor->room, activeRoom)) {
        HyruleCoop::Manager::Instance->NotifyRemotePlayerDestroyed(actor);
        Actor_Kill(actor);
        return;
    }
    if (!IsVisibleInCurrentRoom(*state)) {
        actor->shape.shadowAlpha = 0;
        actor->world.pos = { -9999.0f, -9999.0f, -9999.0f };
        return;
    }

    Player* player = reinterpret_cast<Player*>(actor);
    actor->shape.shadowAlpha = 255;
    actor->world.pos = { state->position[0], state->position[1], state->position[2] };
    CopyVec3s(actor->shape.rot, state->rotation);
    CopyVec3s(player->upperLimbRot, state->upperLimbRotation);
    CopyVec3s(player->skelAnime.prevTransl, state->previousTranslation);
    for (size_t i = 0; i < 24; ++i) {
        player->skelAnime.jointTable[i].x = state->joints[i * 3];
        player->skelAnime.jointTable[i].y = state->joints[i * 3 + 1];
        player->skelAnime.jointTable[i].z = state->joints[i * 3 + 2];
    }
    player->skelAnime.movementFlags = state->movementFlags;
    player->currentBoots = state->boots;
    player->currentShield = state->shield;
    player->currentTunic = state->tunic;
    player->currentMask = state->currentMask;
    player->stateFlags1 = state->stateFlags1;
    player->stateFlags2 = state->stateFlags2;
    player->itemAction = state->itemAction;
    player->heldItemAction = state->heldItemAction;
    player->invincibilityTimer = state->invincibilityTimer;
    player->unk_862 = state->modelState > static_cast<int16_t>(GID_MAXIMUM)
                            ? static_cast<int16_t>(GID_STONE_OF_AGONY)
                            : state->modelState;
    player->unk_85C = state->modelBlend;
    player->av1.actionVar1 = state->actionVariable;
    player->linearVelocity = state->linearVelocity;
    // Player_Draw uses these independently of the skeleton pose. Without them the remote Link can play a sword
    // swing animation while the weapon model remains hidden.
    player->meleeWeaponState = state->meleeWeaponState;
    player->meleeWeaponAnimation = state->meleeWeaponAnimation;

    if (player->modelGroup != state->modelGroup) {
        const s32 originalAge = gSaveContext.linkAge;
        const u8 originalButtonItem = gSaveContext.equips.buttonItems[0];
        gSaveContext.linkAge = state->linkAge;
        gSaveContext.equips.buttonItems[0] = state->buttonItem;
        Player_SetModelGroup(player, state->modelGroup);
        gSaveContext.linkAge = originalAge;
        gSaveContext.equips.buttonItems[0] = originalButtonItem;
    }
    HyruleCoop::Manager::Instance->NotifyRemotePlayerPoseApplied(state->meleeWeaponState > 0);
}

extern "C" void HyruleCoopRemotePlayer_Draw(Actor* actor, PlayState* play) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state) || !IsVisibleInCurrentRoom(*state)) {
        return;
    }

    const s32 originalAge = gSaveContext.linkAge;
    const u8 originalButtonItem = gSaveContext.equips.buttonItems[0];
    auto originalUpdate = actor->update;
    gSaveContext.linkAge = state->linkAge;
    gSaveContext.equips.buttonItems[0] = state->buttonItem;

    // Player_Draw normally builds and registers Link's sword and shield colliders. A remote Link is a visual
    // projection: its attacks are validated and committed through network intents, so allowing these draw-time
    // colliders to participate would damage the same enemy once natively and once authoritatively. The collision
    // API already rejects actors without an update callback, which lets us preserve the complete weapon render
    // without leaking gameplay collision from this presentation actor.
    actor->update = nullptr;
    Player_Draw(actor, play);
    actor->update = originalUpdate;
    HyruleCoop::Manager::Instance->NotifyRemotePlayerDrawApplied(state->meleeWeaponState > 0, state->currentMask);
    gSaveContext.linkAge = originalAge;
    gSaveContext.equips.buttonItems[0] = originalButtonItem;
}

extern "C" void HyruleCoopRemotePlayer_Destroy(Actor* actor, PlayState* play) {
    Player* player = reinterpret_cast<Player*>(actor);
    if (play != nullptr && player->meleeWeaponEffectIndex < TOTAL_EFFECT_COUNT) {
        Effect_Delete(play, player->meleeWeaponEffectIndex);
        player->meleeWeaponEffectIndex = TOTAL_EFFECT_COUNT;
    }
    if (HyruleCoop::Manager::Instance != nullptr) {
        HyruleCoop::Manager::Instance->NotifyRemotePlayerDestroyed(actor);
    }
    actor->id = ACTOR_PLAYER;
}

extern "C" void HyruleCoopRemoteHorse_Init(Actor* actor, PlayState* play) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state) || !state->mounted) {
        Actor_Kill(actor);
        return;
    }

    EnHorse* horse = reinterpret_cast<EnHorse*>(actor);
    // Do not call EnHorse_Init. Its gameplay initializer intentionally removes Epona in scenes where the viewer's
    // local save, age, or time of day does not allow a horse. This actor is only a remote render projection.
    Actor_SetScale(actor, 0.01f);
    actor->gravity = -3.5f;
    ActorShape_Init(&actor->shape, 0.0f, ActorShadow_DrawHorse, 20.0f);
    horse->type = HORSE_EPONA;
    horse->animationIdx = ENHORSE_ANIM_IDLE;
    horse->stateFlags = ENHORSE_FLAG_7;
    horse->postDrawFunc = nullptr;
    Skin_Init(play, &horse->skin, RemoteHorseSkeleton(), RemoteHorseAnimation(ENHORSE_ANIM_IDLE));
    horse->skin.skelAnime.playSpeed = 0.0f;
    actor->room = play->roomCtx.curRoom.num;
    actor->flags |= ACTOR_FLAG_LOCK_ON_DISABLED | ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    horse->rider = nullptr;
    actor->child = nullptr;
    actor->update = HyruleCoopRemoteHorse_Update;
    actor->draw = HyruleCoopRemoteHorse_Draw;
    actor->destroy = HyruleCoopRemoteHorse_Destroy;
    ApplyRemoteHorsePose(horse, *state);
}

extern "C" void HyruleCoopRemoteHorse_Update(Actor* actor, PlayState*) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state) || !state->mounted || !IsVisibleInCurrentRoom(*state)) {
        if (HyruleCoop::Manager::Instance != nullptr) {
            HyruleCoop::Manager::Instance->NotifyRemoteHorseDestroyed(actor);
        }
        Actor_Kill(actor);
        return;
    }

    ApplyRemoteHorsePose(reinterpret_cast<EnHorse*>(actor), *state);
}

extern "C" void HyruleCoopRemoteHorse_Draw(Actor* actor, PlayState* play) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state) || !state->mounted || !IsVisibleInCurrentRoom(*state)) {
        return;
    }

    EnHorse_Draw(actor, play);
}

extern "C" void HyruleCoopRemoteHorse_Destroy(Actor* actor, PlayState* play) {
    Skin_Free(play, &reinterpret_cast<EnHorse*>(actor)->skin);
    if (HyruleCoop::Manager::Instance != nullptr) {
        HyruleCoop::Manager::Instance->NotifyRemoteHorseDestroyed(actor);
    }
}
