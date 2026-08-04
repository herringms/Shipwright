#include "RemotePlayer.h"

#include "HyruleCoop.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"

extern PlayState* gPlayState;

void Player_UseItem(PlayState* play, Player* player, s32 item);
void Player_Draw(Actor* actor, PlayState* play);
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

} // namespace

extern "C" void HyruleCoopRemotePlayer_Init(Actor* actor, PlayState* play) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state)) {
        Actor_Kill(actor);
        return;
    }

    Player* player = reinterpret_cast<Player*>(actor);
    const s32 originalAge = gSaveContext.linkAge;
    gSaveContext.linkAge = state->linkAge;

    actor->room = -1;
    player->itemAction = player->heldItemAction = -1;
    player->heldItemId = ITEM_NONE;
    Player_UseItem(play, player, ITEM_NONE);
    Player_SetModelGroup(player, Player_ActionToModelGroup(player, player->heldItemAction));
    play->playerInit(player, play, gPlayerSkelHeaders[state->linkAge]);
    Effect_Delete(play, player->meleeWeaponEffectIndex);
    player->meleeWeaponEffectIndex = TOTAL_EFFECT_COUNT;
    play->func_11D54(player, play);

    actor->flags |= ACTOR_FLAG_LOCK_ON_DISABLED;
    actor->shape.shadowAlpha = 255;
    gSaveContext.linkAge = originalAge;
}

extern "C" void HyruleCoopRemotePlayer_Update(Actor* actor, PlayState*) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state) || gPlayState == nullptr || state->scene != gPlayState->sceneNum) {
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
    HyruleCoop::Manager::Instance->NotifyRemotePlayerPoseApplied(state->meleeWeaponState > 0);

    if (player->modelGroup != state->modelGroup) {
        const s32 originalAge = gSaveContext.linkAge;
        const u8 originalButtonItem = gSaveContext.equips.buttonItems[0];
        gSaveContext.linkAge = state->linkAge;
        gSaveContext.equips.buttonItems[0] = state->buttonItem;
        Player_SetModelGroup(player, state->modelGroup);
        gSaveContext.linkAge = originalAge;
        gSaveContext.equips.buttonItems[0] = originalButtonItem;
    }
}

extern "C" void HyruleCoopRemotePlayer_Draw(Actor* actor, PlayState* play) {
    const HyruleCoop::PlayerSnapshotMessage* state = nullptr;
    if (!GetState(state) || gPlayState == nullptr || state->scene != gPlayState->sceneNum) {
        return;
    }

    const s32 originalAge = gSaveContext.linkAge;
    const u8 originalButtonItem = gSaveContext.equips.buttonItems[0];
    gSaveContext.linkAge = state->linkAge;
    gSaveContext.equips.buttonItems[0] = state->buttonItem;
    Player_Draw(actor, play);
    gSaveContext.linkAge = originalAge;
    gSaveContext.equips.buttonItems[0] = originalButtonItem;
}

extern "C" void HyruleCoopRemotePlayer_Destroy(Actor* actor, PlayState*) {
    if (HyruleCoop::Manager::Instance != nullptr) {
        HyruleCoop::Manager::Instance->NotifyRemotePlayerDestroyed(actor);
    }
    actor->id = ACTOR_PLAYER;
}
