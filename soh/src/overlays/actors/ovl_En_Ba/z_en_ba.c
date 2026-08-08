/*
 * File: z_en_ba.c
 * Overlay: ovl_En_Ba
 * Description: Tentacle from inside Lord Jabu-Jabu
 */

#include "z_en_ba.h"
#include "objects/object_bxa/object_bxa.h"
#include "soh/frame_interpolation.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Network/HyruleCoop/JabuActorBridge.h"

#define FLAGS (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE | ACTOR_FLAG_UPDATE_CULLING_DISABLED)

void EnBa_Init(Actor* thisx, PlayState* play);
void EnBa_Destroy(Actor* thisx, PlayState* play);
void EnBa_Update(Actor* thisx, PlayState* play);
void EnBa_Draw(Actor* thisx, PlayState* play);

void EnBa_SetupIdle(EnBa* this);
void EnBa_SetupFallAsBlob(EnBa* this);
void EnBa_Idle(EnBa* this, PlayState* play);
void EnBa_FallAsBlob(EnBa* this, PlayState* play);
void EnBa_SwingAtPlayer(EnBa* this, PlayState* play);
void EnBa_RecoilFromDamage(EnBa* this, PlayState* play);
void EnBa_Die(EnBa* this, PlayState* play);
void EnBa_SetupSwingAtPlayer(EnBa* this);

const ActorInit En_Ba_InitVars = {
    ACTOR_EN_BA,
    ACTORCAT_ENEMY,
    FLAGS,
    OBJECT_BXA,
    sizeof(EnBa),
    (ActorFunc)EnBa_Init,
    (ActorFunc)EnBa_Destroy,
    (ActorFunc)EnBa_Update,
    (ActorFunc)EnBa_Draw,
    NULL,
};

static Vec3f D_809B8080 = { 0.0f, 0.0f, 32.0f };

static ColliderJntSphElementInit sJntSphElementInit[2] = {
    {
        {
            ELEMTYPE_UNK0,
            { 0x00000000, 0x00, 0x00 },
            { 0x00000010, 0x00, 0x00 },
            TOUCH_NONE,
            BUMP_ON,
            OCELEM_NONE,
        },
        { 8, { { 0, 0, 0 }, 20 }, 100 },
    },
    {
        {
            ELEMTYPE_UNK0,
            { 0x20000000, 0x00, 0x04 },
            { 0x00000000, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_NONE,
            OCELEM_NONE,
        },
        { 13, { { 0, 0, 0 }, 25 }, 100 },
    },
};

static ColliderJntSphInit sJntSphInit = {
    {
        COLTYPE_HIT0,
        AT_ON | AT_TYPE_ENEMY,
        AC_ON | AC_TYPE_PLAYER,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_JNTSPH,
    },
    2,
    sJntSphElementInit,
};

void EnBa_SetupAction(EnBa* this, EnBaActionFunc actionFunc) {
    this->actionFunc = actionFunc;
}

static Vec3f D_809B80E4 = { 0.01f, 0.01f, 0.01f };

static InitChainEntry sInitChain[] = {
    ICHAIN_S8(naviEnemyId, 0x15, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneScale, 1500, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneDownward, 2500, ICHAIN_CONTINUE),
    ICHAIN_F32(targetArrowOffset, 0, ICHAIN_STOP),
};

void EnBa_Init(Actor* thisx, PlayState* play) {
    EnBa* this = (EnBa*)thisx;
    Vec3f sp38 = D_809B80E4;
    s32 pad;
    s16 i;

    Actor_ProcessInitChain(&this->actor, sInitChain);
    this->actor.world.pos.y = this->actor.home.pos.y + 100.0f;
    for (i = 13; i >= 0; i--) {
        this->unk_200[i] = sp38;
        this->unk_2A8[i].x = -0x4000;
        this->unk_158[i] = this->actor.world.pos;
        this->unk_158[i].y = this->actor.world.pos.y - (i + 1) * 32.0f;
    }

    this->actor.targetMode = 4;
    this->upperParams = (thisx->params >> 8) & 0xFF;
    thisx->params &= 0xFF;
    this->epoch++;

    if (this->actor.params < EN_BA_DEAD_BLOB) {
        if (Flags_GetSwitch(play, this->upperParams)) {
            Actor_Kill(&this->actor);
            return;
        }
        ActorShape_Init(&this->actor.shape, 0.0f, ActorShadow_DrawCircle, 48.0f);
        Actor_SetScale(&this->actor, 0.01f);
        EnBa_SetupIdle(this);
        this->actor.colChkInfo.health = 4;
        this->actor.colChkInfo.mass = MASS_HEAVY;
        Collider_InitJntSph(play, &this->collider);
        Collider_SetJntSph(play, &this->collider, &this->actor, &sJntSphInit, this->colliderItems);
    } else {
        Actor_SetScale(&this->actor, 0.021f);
        EnBa_SetupFallAsBlob(this);
    }
}

void EnBa_Destroy(Actor* thisx, PlayState* play) {
    EnBa* this = (EnBa*)thisx;
    Collider_DestroyJntSph(play, &this->collider);
}

void EnBa_SetupIdle(EnBa* this) {
    this->unk_14C = 2;
    this->unk_31C = 1500;
    this->actor.speedXZ = 10.0f;
    EnBa_SetupAction(this, EnBa_Idle);
}

void EnBa_Idle(EnBa* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 i;
    s32 pad;
    Vec3s sp5C;

    if ((this->actor.colChkInfo.mass == MASS_IMMOVABLE) && (this->actor.xzDistToPlayer > 175.0f)) {
        Math_SmoothStepToF(&this->actor.world.pos.y, this->actor.home.pos.y + 330.0f, 1.0f, 7.0f, 0.0f);
    } else {
        this->actor.flags |= ACTOR_FLAG_ATTENTION_ENABLED;
        Math_SmoothStepToF(&this->actor.world.pos.y, this->actor.home.pos.y + 100.0f, 1.0f, 10.0f, 0.0f);
    }
    this->unk_2FC = this->actor.world.pos;
    if (play->gameplayFrames % 16 == 0) {
        this->unk_308.z += Rand_CenteredFloat(180.0f);
        this->unk_314 += Rand_CenteredFloat(180.0f);
        this->unk_308.x = Math_SinF(this->unk_308.z) * 80.0f;
        this->unk_308.y = Math_CosF(this->unk_314) * 80.0f;
    }
    this->unk_2FC.y -= 448.0f;
    this->unk_2FC.x += this->unk_308.x;
    this->unk_2FC.z += this->unk_308.y;
    func_80033AEC(&this->unk_2FC, &this->unk_158[13], 1.0f, this->actor.speedXZ, 0.0f, 0.0f);
    for (i = 12; i >= 0; i--) {
        func_80035844(&this->unk_158[i + 1], &this->unk_158[i], &sp5C, 0);
        Matrix_Translate(this->unk_158[i + 1].x, this->unk_158[i + 1].y, this->unk_158[i + 1].z, MTXMODE_NEW);
        Matrix_RotateZYX(sp5C.x, sp5C.y, 0, MTXMODE_APPLY);
        Matrix_MultVec3f(&D_809B8080, &this->unk_158[i]);
    }
    func_80035844(&this->unk_158[0], &this->unk_2FC, &sp5C, 0);
    Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
    Math_SmoothStepToS(&this->actor.shape.rot.y, this->unk_2A8[0].y, 3, this->unk_31C, 182);
    Math_SmoothStepToS(&this->actor.shape.rot.x, this->unk_2A8[0].x, 3, this->unk_31C, 182);
    Matrix_RotateZYX(this->actor.shape.rot.x - 0x8000, this->actor.shape.rot.y, 0, MTXMODE_APPLY);
    Matrix_MultVec3f(&D_809B8080, &this->unk_158[0]);
    this->unk_2A8[13].y = sp5C.y;
    this->unk_2A8[13].x = sp5C.x + 0x8000;

    for (i = 0; i < 13; i++) {
        Matrix_Translate(this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, MTXMODE_NEW);
        Math_SmoothStepToS(&this->unk_2A8[i].y, this->unk_2A8[i + 1].y, 3, this->unk_31C, 182);
        Math_SmoothStepToS(&this->unk_2A8[i].x, this->unk_2A8[i + 1].x, 3, this->unk_31C, 182);
        Matrix_RotateZYX(this->unk_2A8[i].x - 0x8000, this->unk_2A8[i].y, 0, MTXMODE_APPLY);
        Matrix_MultVec3f(&D_809B8080, &this->unk_158[i + 1]);
    }
    this->unk_2A8[13].x = this->unk_2A8[12].x;
    this->unk_2A8[13].y = this->unk_2A8[12].y;
    if (!(player->stateFlags1 & PLAYER_STATE1_DAMAGED) && (this->actor.xzDistToPlayer <= 175.0f) &&
        (this->actor.world.pos.y == this->actor.home.pos.y + 100.0f)) {
        EnBa_SetupSwingAtPlayer(this);
    }
}

void EnBa_SetupFallAsBlob(EnBa* this) {
    this->unk_14C = 0;
    this->actor.speedXZ = Rand_CenteredFloat(8.0f);
    this->actor.world.rot.y = Rand_CenteredFloat(65535.0f);
    this->unk_318 = 20;
    this->actor.gravity = -2.0f;
    EnBa_SetupAction(this, EnBa_FallAsBlob);
}

/**
 * Action function of the pink fleshy blobs that spawn and fall to the floor when a tentacle dies
 */
void EnBa_FallAsBlob(EnBa* this, PlayState* play) {
    if (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
        this->actor.scale.y -= 0.001f;
        this->actor.scale.x += 0.0005f;
        this->actor.scale.z += 0.0005f;
        this->unk_318--;
        if (this->unk_318 == 0) {
            Actor_Kill(&this->actor);
        }
    } else {
        Actor_MoveXZGravity(&this->actor);
        Actor_UpdateBgCheckInfo(play, &this->actor, 30.0f, 28.0f, 80.0f, 5);
    }
}

void EnBa_SetupSwingAtPlayer(EnBa* this) {
    this->unk_14C = 3;
    this->unk_318 = 20;
    this->unk_31A = 0;
    this->unk_31C = 1500;
    this->actor.colChkInfo.mass = MASS_IMMOVABLE;
    this->actor.speedXZ = 20.0f;
    EnBa_SetupAction(this, EnBa_SwingAtPlayer);
}

void EnBa_SwingAtPlayer(EnBa* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s16 temp;
    s16 i;
    Vec3s sp58;
    s16 phi_fp;

    Math_SmoothStepToF(&this->actor.world.pos.y, this->actor.home.pos.y + 60.0f, 1.0f, 10.0f, 0.0f);
    if ((this->actor.xzDistToPlayer <= 175.0f) || (this->unk_31A != 0)) {
        if (this->unk_318 == 20) {
            Audio_PlayActorSound2(&this->actor, NA_SE_EN_BALINADE_HAND_UP);
            this->unk_31C = 1500;
        }
        if (this->unk_318 != 0) {
            this->unk_31A = 10;
            this->unk_318--;
            if (this->unk_318 >= 11) {
                this->unk_2FC = player->actor.world.pos;
                this->unk_2FC.y += 30.0f;
                phi_fp = this->actor.yawTowardsPlayer;
            } else {
                phi_fp = Math_Vec3f_Yaw(&this->actor.world.pos, &this->unk_2FC);
            }
            Math_SmoothStepToS(&this->unk_31C, 1500, 1, 30, 0);
            func_80035844(&this->actor.world.pos, &this->unk_158[0], &sp58, 0);
            Math_SmoothStepToS(&this->actor.shape.rot.y, sp58.y, 1, this->unk_31C, 0);
            Math_SmoothStepToS(&this->actor.shape.rot.x, (sp58.x + 0x8000), 1, this->unk_31C, 0);
            Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
            Matrix_RotateZYX((this->actor.shape.rot.x - 0x8000), this->actor.shape.rot.y, 0, MTXMODE_APPLY);
            Matrix_MultVec3f(&D_809B8080, &this->unk_158[0]);

            for (i = 0; i < 13; i++) {
                Math_SmoothStepToS(&this->unk_2A8[i].x, (i * 1200) - 0x4000, 1, this->unk_31C, 0);
                Math_SmoothStepToS(&this->unk_2A8[i].y, phi_fp, 1, this->unk_31C, 0);
                Matrix_Translate(this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, MTXMODE_NEW);
                Matrix_RotateZYX((this->unk_2A8[i].x - 0x8000), this->unk_2A8[i].y, 0, MTXMODE_APPLY);
                Matrix_MultVec3f(&D_809B8080, &this->unk_158[i + 1]);
            }
        } else {
            if (this->unk_31A == 10) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_BALINADE_HAND_DOWN);
            }
            if (this->unk_31A != 0) {
                this->unk_31C = 8000;
                this->actor.speedXZ = 30.0f;
                phi_fp = Math_Vec3f_Yaw(&this->actor.world.pos, &this->unk_2FC);
                temp = Math_Vec3f_Pitch(&this->actor.world.pos, &this->unk_158[0]) + 0x8000;
                Math_SmoothStepToS(&this->actor.shape.rot.y, phi_fp, 1, this->unk_31C, 0);
                Math_SmoothStepToS(&this->actor.shape.rot.x, temp, 1, this->unk_31C, 0);
                Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z,
                                 MTXMODE_NEW);
                Matrix_RotateZYX(this->actor.shape.rot.x - 0x8000, this->actor.shape.rot.y, 0, MTXMODE_APPLY);
                Matrix_MultVec3f(&D_809B8080, this->unk_158);

                for (i = 0; i < 13; i++) {
                    temp = -Math_CosS(this->unk_31A * 0xCCC) * (i * 1200);
                    Math_SmoothStepToS(&this->unk_2A8[i].x, temp - 0x4000, 1, this->unk_31C, 0);
                    Math_SmoothStepToS(&this->unk_2A8[i].y, phi_fp, 1, this->unk_31C, 0);
                    Matrix_Translate(this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, MTXMODE_NEW);
                    Matrix_RotateZYX(this->unk_2A8[i].x - 0x8000, this->unk_2A8[i].y, 0, MTXMODE_APPLY);
                    Matrix_MultVec3f(&D_809B8080, &this->unk_158[i + 1]);
                }
                this->unk_31A--;
            } else if ((this->actor.xzDistToPlayer > 175.0f) || (player->stateFlags1 & PLAYER_STATE1_DAMAGED)) {
                EnBa_SetupIdle(this);
            } else {
                EnBa_SetupSwingAtPlayer(this);
                this->unk_318 = 27;
                this->unk_31C = 750;
            }
        }
        this->unk_2A8[13].x = this->unk_2A8[12].x;
        this->unk_2A8[13].y = this->unk_2A8[12].y;
        if (this->collider.base.atFlags & 2) {
            this->collider.base.atFlags &= ~2;
            if (this->collider.base.at == &player->actor) {
                Actor_SetPlayerKnockbackLargeNoDamage(play, &this->actor, 8.0f, this->actor.yawTowardsPlayer, 8.0f);
            }
        }
        CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);
        return;
    }
    if ((this->actor.xzDistToPlayer > 175.0f) || (player->stateFlags1 & PLAYER_STATE1_DAMAGED)) {
        EnBa_SetupIdle(this);
    } else {
        EnBa_SetupSwingAtPlayer(this);
        this->unk_318 = 27;
        this->unk_31C = 750;
    }
}

void func_809B7174(EnBa* this) {
    this->unk_14C = 1;
    this->unk_31C = 1500;
    this->unk_318 = 20;
    this->actor.colChkInfo.mass = MASS_IMMOVABLE;
    this->actor.speedXZ = 10.0f;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_BALINADE_HAND_DAMAGE);
    Actor_SetColorFilter(&this->actor, 0x4000, 255, 0, 12);
    EnBa_SetupAction(this, EnBa_RecoilFromDamage);
}

void EnBa_RecoilFromDamage(EnBa* this, PlayState* play) {
    s32 i;
    Vec3s sp6C;

    Math_SmoothStepToF(&this->actor.world.pos.y, this->actor.home.pos.y + 330.0f, 1.0f, 30.0f, 0.0f);
    this->unk_2FC = this->actor.world.pos;
    if (play->gameplayFrames % 16 == 0) {
        this->unk_308.z += Rand_CenteredFloat(180.0f);
        this->unk_314 += Rand_CenteredFloat(180.0f);
        this->unk_308.x = Math_SinF(this->unk_308.z) * 80.0f;
        this->unk_308.y = Math_CosF(this->unk_314) * 80.0f;
    }
    this->unk_2FC.y -= 448.0f;
    this->unk_2FC.x += this->unk_308.x;
    this->unk_2FC.z += this->unk_308.y;
    func_80033AEC(&this->unk_2FC, &this->unk_158[13], 1.0f, this->actor.speedXZ, 0.0f, 0.0f);
    for (i = 12; i >= 0; i--) {
        func_80035844(&this->unk_158[i + 1], &this->unk_158[i], &sp6C, 0);
        Matrix_Translate(this->unk_158[i + 1].x, this->unk_158[i + 1].y, this->unk_158[i + 1].z, MTXMODE_NEW);
        Matrix_RotateZYX(sp6C.x, sp6C.y, 0, MTXMODE_APPLY);
        Matrix_MultVec3f(&D_809B8080, &this->unk_158[i]);
    }
    func_80035844(&this->actor.world.pos, &this->unk_158[0], &sp6C, 0);
    Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
    Math_SmoothStepToS(&this->actor.shape.rot.y, sp6C.y, 3, this->unk_31C, 182);
    Math_SmoothStepToS(&this->actor.shape.rot.x, sp6C.x + 0x8000, 3, this->unk_31C, 182);
    Matrix_RotateZYX(this->actor.shape.rot.x - 0x8000, this->actor.shape.rot.y, 0, MTXMODE_APPLY);
    Matrix_MultVec3f(&D_809B8080, &this->unk_158[0]);

    for (i = 0; i < 13; i++) {
        func_80035844(&this->unk_158[i], &this->unk_158[i + 1], &sp6C, 0);
        Matrix_Translate(this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, MTXMODE_NEW);
        Math_SmoothStepToS(&this->unk_2A8[i].y, sp6C.y, 3, this->unk_31C, 182);
        Math_SmoothStepToS(&this->unk_2A8[i].x, sp6C.x + 0x8000, 3, this->unk_31C, 182);
        Matrix_RotateZYX(this->unk_2A8[i].x - 0x8000, this->unk_2A8[i].y, 0, MTXMODE_APPLY);
        Matrix_MultVec3f(&D_809B8080, &this->unk_158[i + 1]);
    }

    this->unk_2A8[13].x = this->unk_2A8[12].x;
    this->unk_2A8[13].y = this->unk_2A8[12].y;
    this->unk_318--;
    if (this->unk_318 == 0) {
        EnBa_SetupIdle(this);
    }
}

void func_809B75A0(EnBa* this, PlayState* play2) {
    s16 unk_temp;
    s32 i;
    Vec3f sp74 = { 0.0f, 0.0f, 0.0f };
    PlayState* play = play2;

    this->unk_31C = 2500;
    EffectSsDeadSound_SpawnStationary(play, &this->actor.projectedPos, NA_SE_EN_BALINADE_HAND_DEAD, 1, 1, 40);
    this->unk_14C = 0;

    for (i = 7; i < 14; i++) {
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BA, this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, 0,
                    0, 0, EN_BA_DEAD_BLOB);
    }
    unk_temp = Math_Vec3f_Pitch(&this->actor.world.pos, &this->unk_158[0]) + 0x8000;
    Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 1, this->unk_31C, 0);
    Math_SmoothStepToS(&this->actor.shape.rot.x, unk_temp, 1, this->unk_31C, 0);
    Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
    Matrix_RotateZYX(this->actor.shape.rot.x - 0x8000, this->actor.shape.rot.y, 0, MTXMODE_APPLY);
    Matrix_MultVec3f(&D_809B8080, &this->unk_158[0]);
    this->actor.flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    for (i = 5; i < 13; i++) {
        Math_SmoothStepToS(&this->unk_2A8[i].x, this->unk_2A8[5].x, 1, this->unk_31C, 0);
        Math_SmoothStepToS(&this->unk_2A8[i].y, this->unk_2A8[5].y, 1, this->unk_31C, 0);
        Matrix_Translate(this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, MTXMODE_NEW);
        Matrix_RotateZYX(this->unk_2A8[i].x - 0x8000, this->unk_2A8[i].y, 0, MTXMODE_APPLY);
        Matrix_MultVec3f(&sp74, &this->unk_158[i + 1]);
    }
    this->unk_31A = 15;
    EnBa_SetupAction(this, EnBa_Die);
}

void EnBa_Die(EnBa* this, PlayState* play) {
    Vec3f sp6C = { 0.0f, 0.0f, 0.0f };
    s16 temp;
    s32 i;

    if (this->unk_31A != 0) {
        this->actor.speedXZ = 30.0f;
        this->unk_31C = 8000;
        this->actor.world.pos.y += 8.0f;
        temp = Math_Vec3f_Pitch(&this->actor.world.pos, &this->unk_158[0]) + 0x8000;
        Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 1, this->unk_31C, 0);
        Math_SmoothStepToS(&this->actor.shape.rot.x, temp, 1, this->unk_31C, 0);
        Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
        Matrix_RotateZYX(this->actor.shape.rot.x - 0x8000, this->actor.shape.rot.y, 0, MTXMODE_APPLY);
        Matrix_MultVec3f(&D_809B8080, &this->unk_158[0]);
        for (i = 0; i < 5; i++) {
            temp = -Math_CosS(this->unk_31A * 0x444) * (i * 400);
            Math_SmoothStepToS(&this->unk_2A8[i].x, temp - 0x4000, 1, this->unk_31C, 0);
            Math_SmoothStepToS(&this->unk_2A8[i].y, this->actor.yawTowardsPlayer, 1, this->unk_31C, 0);
            Matrix_Translate(this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, MTXMODE_NEW);
            Matrix_RotateZYX(this->unk_2A8[i].x - 0x8000, this->unk_2A8[i].y, 0, MTXMODE_APPLY);
            Matrix_MultVec3f(&D_809B8080, &this->unk_158[i + 1]);
        }
        for (i = 5; i < 13; i++) {
            Math_SmoothStepToS(&this->unk_2A8[i].x, this->unk_2A8[5].x, 1, this->unk_31C, 0);
            Math_SmoothStepToS(&this->unk_2A8[i].y, this->unk_2A8[5].y, 1, this->unk_31C, 0);
            Matrix_Translate(this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, MTXMODE_NEW);
            Matrix_RotateZYX(this->unk_2A8[i].x - 0x8000, this->unk_2A8[i].y, 0, MTXMODE_APPLY);
            Matrix_MultVec3f(&sp6C, &this->unk_158[i + 1]);
        }
        this->unk_31A--;
    } else {
        Flags_SetSwitch(play, this->upperParams);
        Actor_Kill(&this->actor);
    }
}

static uint8_t EnBa_GetCoopAction(const EnBa* this) {
    if (this->actionFunc == EnBa_Idle) return HYRULE_COOP_TENTACLE_IDLE;
    if (this->actionFunc == EnBa_SwingAtPlayer) return HYRULE_COOP_TENTACLE_SWING;
    if (this->actionFunc == EnBa_RecoilFromDamage) return HYRULE_COOP_TENTACLE_RECOIL;
    if (this->actionFunc == EnBa_Die) return HYRULE_COOP_TENTACLE_DIE;
    if (this->actionFunc == EnBa_FallAsBlob) return HYRULE_COOP_TENTACLE_BLOB;
    return UINT8_MAX;
}

static int EnBa_SetCoopAction(EnBa* this, uint8_t action) {
    switch (action) {
        case HYRULE_COOP_TENTACLE_IDLE:
            EnBa_SetupIdle(this);
            break;
        case HYRULE_COOP_TENTACLE_SWING:
            EnBa_SetupSwingAtPlayer(this);
            break;
        case HYRULE_COOP_TENTACLE_RECOIL:
            func_809B7174(this);
            break;
        case HYRULE_COOP_TENTACLE_DIE:
            return 0;
        case HYRULE_COOP_TENTACLE_BLOB:
            EnBa_SetupFallAsBlob(this);
            break;
        default:
            return 0;
    }
    return 1;
}

static int EnBa_ApplyDamage(EnBa* this, PlayState* play, uint8_t damage) {
    if (this == NULL || play == NULL || this->actor.params >= EN_BA_DEAD_BLOB || this->actor.colChkInfo.health == 0 ||
        damage == 0) {
        return 0;
    }
    if (damage >= this->actor.colChkInfo.health) {
        this->actor.colChkInfo.health = 0;
        func_809B75A0(this, play);
        GameInteractor_ExecuteOnEnemyDefeat(&this->actor);
    } else {
        this->actor.colChkInfo.health -= damage;
        func_809B7174(this);
    }
    return 1;
}

void* HyruleCoop_EnBaCanonicalActor(void* actorRef) {
    EnBa* this = actorRef;

    if (this == NULL || this->actor.id != ACTOR_EN_BA || this->actor.params >= EN_BA_DEAD_BLOB) {
        return NULL;
    }
    return this;
}

int HyruleCoop_EnBaPeekDamage(const void* actorRef, uint8_t* damageEffect, uint8_t* damage) {
    const EnBa* this = actorRef;

    if (this == NULL || damageEffect == NULL || damage == NULL || this->actor.id != ACTOR_EN_BA ||
        this->actor.params >= EN_BA_DEAD_BLOB || !(this->collider.base.acFlags & AC_HIT)) {
        return 0;
    }
    *damageEffect = this->actor.colChkInfo.damageEffect;
    // Vanilla appendages always take one damage for a valid collider hit.
    *damage = 1;
    return 1;
}

void EnBa_Update(Actor* thisx, PlayState* play) {
    EnBa* this = (EnBa*)thisx;

    if ((this->actor.params < EN_BA_DEAD_BLOB) && (this->collider.base.acFlags & 2)) {
        this->collider.base.acFlags &= ~2;
        EnBa_ApplyDamage(this, play, 1);
    }
    this->actionFunc(this, play);
    if (this->actor.params < EN_BA_DEAD_BLOB) {
        this->actor.focus.pos = this->unk_158[6];
    }
    if (this->unk_14C >= 2) {
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider.base);
    }
}

int HyruleCoop_EnBaCaptureState(const void* actorRef, HyruleCoopJabuActorState* state) {
    const EnBa* this = actorRef;
    uint8_t action;

    if (this == NULL || state == NULL || this->actor.id != ACTOR_EN_BA) {
        return 0;
    }
    action = EnBa_GetCoopAction(this);
    if (action == UINT8_MAX) {
        return 0;
    }
    state->action = action;
    state->health = this->actor.colChkInfo.health;
    state->flags = this->upperParams;
    state->variant = this->actor.params;
    state->timer = this->unk_318;
    state->auxTimer = this->unk_31A;
    state->auxState = this->unk_31C;
    state->alpha = this->actor.colorFilterTimer;
    state->animationFrame = this->unk_314;
    state->animationSpeed = this->actor.speedXZ;
    return 1;
}

int HyruleCoop_EnBaApplyState(void* actorRef, void* playRef, const HyruleCoopJabuActorState* state) {
    EnBa* this = actorRef;
    PlayState* play = playRef;

    if (this == NULL || play == NULL || state == NULL || this->actor.id != ACTOR_EN_BA) {
        return 0;
    }
    if (state->action == HYRULE_COOP_TENTACLE_DIE) {
        if (this->actionFunc != EnBa_Die) {
            if (!EnBa_ApplyDamage(this, play, MAX(1, this->actor.colChkInfo.health))) {
                return 0;
            }
        }
    } else if ((EnBa_GetCoopAction(this) != state->action) && !EnBa_SetCoopAction(this, state->action)) {
        return 0;
    }
    this->actor.colChkInfo.health = state->health;
    this->upperParams = state->flags;
    this->actor.params = state->variant;
    this->unk_318 = state->timer;
    this->unk_31A = state->auxTimer;
    this->unk_31C = state->auxState;
    this->actor.colorFilterTimer = state->alpha;
    this->unk_314 = state->animationFrame;
    this->actor.speedXZ = state->animationSpeed;
    return 1;
}

int HyruleCoop_EnBaApplyDamage(void* actorRef, void* playRef, uint8_t damage) {
    EnBa* this = actorRef;

    if (this == NULL || this->actor.id != ACTOR_EN_BA) {
        return 0;
    }
    return EnBa_ApplyDamage(this, playRef, damage);
}

int HyruleCoop_EnBaApplyDeath(void* actorRef, void* playRef) {
    EnBa* this = actorRef;

    if (this == NULL || this->actor.id != ACTOR_EN_BA) {
        return 0;
    }
    return EnBa_ApplyDamage(this, playRef, MAX(1, this->actor.colChkInfo.health));
}

static void* D_809B8118[] = {
    object_bxa_Tex_0024F0,
    object_bxa_Tex_0027F0,
    object_bxa_Tex_0029F0,
};

void EnBa_Draw(Actor* thisx, PlayState* play) {
    EnBa* this = (EnBa*)thisx;
    s32 pad;
    s16 i;
    Mtx* mtx = Graph_Alloc(play->state.gfxCtx, sizeof(Mtx) * 14);
    Vec3f unused = { 0.0f, 0.0f, 448.0f };

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    if (this->actor.params < EN_BA_DEAD_BLOB) {
        Matrix_Push();
        gSPSegment(POLY_OPA_DISP++, 0x0C, mtx);
        gSPSegment(POLY_OPA_DISP++, 0x08, SEGMENTED_TO_VIRTUAL(D_809B8118[this->actor.params]));
        gSPSegment(POLY_OPA_DISP++, 0x09,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 16, 16, 1, 0, (play->gameplayFrames * -10) % 128, 32,
                                      32, 0, 0, 0, -10));
        for (i = 0; i < 14; i++, mtx++) {
            FrameInterpolation_RecordOpenChild(this, this->epoch + i * 25);

            Matrix_Translate(this->unk_158[i].x, this->unk_158[i].y, this->unk_158[i].z, MTXMODE_NEW);
            Matrix_RotateZYX(this->unk_2A8[i].x, this->unk_2A8[i].y, this->unk_2A8[i].z, MTXMODE_APPLY);
            Matrix_Scale(this->unk_200[i].x, this->unk_200[i].y, this->unk_200[i].z, MTXMODE_APPLY);
            if ((i == 6) || (i == 13)) {
                switch (i) {
                    case 13:
                        Collider_UpdateSpheres(i, &this->collider);
                        break;
                    default:
                        Matrix_Scale(0.5f, 0.5f, 1.0f, MTXMODE_APPLY);
                        Collider_UpdateSpheres(8, &this->collider);
                        break;
                }
            }
            MATRIX_TOMTX(mtx);

            FrameInterpolation_RecordCloseChild();
        }
        Matrix_Pop();
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, object_bxa_DL_000890);
    } else {
        gSPSegment(POLY_OPA_DISP++, 0x08,
                   Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, (play->gameplayFrames * 2) % 128,
                                      (play->gameplayFrames * 2) % 128, 32, 32, 1, (play->gameplayFrames * -5) % 128,
                                      (play->gameplayFrames * -5) % 128, 32, 32, 2, 2, -5, -5));
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 125, 100, 255);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, object_bxa_DL_001D80);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}
