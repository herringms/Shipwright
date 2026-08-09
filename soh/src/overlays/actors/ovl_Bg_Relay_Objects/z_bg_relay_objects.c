/*
 * File: z_bg_relay_objects.c
 * Overlay: ovl_Bg_Relay_Objects
 * Description: Windmill Setpieces
 */

#include "z_bg_relay_objects.h"
#include "objects/object_relay_objects/object_relay_objects.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/savestate_serialize.h"
#include "soh/Network/HyruleCoop/DampeRaceBridge.h"

#define FLAGS ACTOR_FLAG_UPDATE_CULLING_DISABLED

typedef enum {
    /* 0 */ WINDMILL_ROTATING_GEAR,
    /* 1 */ WINDMILL_DAMPE_STONE_DOOR
} WindmillSetpiecesMode;

void BgRelayObjects_Init(Actor* thisx, PlayState* play);
void BgRelayObjects_Destroy(Actor* thisx, PlayState* play);
void BgRelayObjects_Update(Actor* thisx, PlayState* play);
void BgRelayObjects_Draw(Actor* thisx, PlayState* play);
void BgRelayObjects_Reset(void);

static int BgRelayObjects_IsDampeRaceDoor(const BgRelayObjects* this);
static u8 BgRelayObjects_GetCoopAction(const BgRelayObjects* this);
static void BgRelayObjects_SetCoopAction(BgRelayObjects* this, u8 action);
static void BgRelayObjects_ApplyCoopSwitch(PlayState* play, u8 switchFlag, u8 switchIsSet);

void func_808A90F4(BgRelayObjects* this, PlayState* play);
void func_808A91AC(BgRelayObjects* this, PlayState* play);
void func_808A9234(BgRelayObjects* this, PlayState* play);
void BgRelayObjects_DoNothing(BgRelayObjects* this, PlayState* play);
void func_808A932C(BgRelayObjects* this, PlayState* play);
void func_808A939C(BgRelayObjects* this, PlayState* play);

const ActorInit Bg_Relay_Objects_InitVars = {
    ACTOR_BG_RELAY_OBJECTS,
    ACTORCAT_BG,
    FLAGS,
    OBJECT_RELAY_OBJECTS,
    sizeof(BgRelayObjects),
    (ActorFunc)BgRelayObjects_Init,
    (ActorFunc)BgRelayObjects_Destroy,
    (ActorFunc)BgRelayObjects_Update,
    (ActorFunc)BgRelayObjects_Draw,
    BgRelayObjects_Reset,
};

static InitChainEntry sInitChain[] = {
    ICHAIN_F32(gravity, 5, ICHAIN_CONTINUE),
    ICHAIN_VEC3F_DIV1000(scale, 100, ICHAIN_STOP),
};

static u32 D_808A9508 = 0;

#define BG_RELAY_OBJECTS_SHIP_SAVESTATE_FIELDS(F) F(D_808A9508)

SHIP_SAVESTATE_DEFINE(BgRelayObjects, BG_RELAY_OBJECTS_SHIP_SAVESTATE_FIELDS)

void BgRelayObjects_Init(Actor* thisx, PlayState* play) {
    BgRelayObjects* this = (BgRelayObjects*)thisx;
    s32 pad;
    CollisionHeader* colHeader = NULL;

    Actor_ProcessInitChain(thisx, sInitChain);
    this->switchFlag = thisx->params & 0x3F;
    thisx->params = (thisx->params >> 8) & 0xFF;
    DynaPolyActor_Init(&this->dyna, 3);
    if (thisx->params == WINDMILL_ROTATING_GEAR) {
        CollisionHeader_GetVirtual(&gWindmillRotatingPlatformCol, &colHeader);
        if (Flags_GetEventChkInf(EVENTCHKINF_PLAYED_SONG_OF_STORMS_IN_WINDMILL)) {
            thisx->world.rot.y = 0x400;
        } else {
            thisx->world.rot.y = 0x80;
        }
        Audio_PlayWindmillBgm();
        thisx->room = -1;
        thisx->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
        if (D_808A9508 & 2) {
            thisx->params = 0xFF;
            Actor_Kill(thisx);
        } else {
            D_808A9508 |= 2;
            this->actionFunc = func_808A939C;
        }
    } else {
        CollisionHeader_GetVirtual(&gDampeRaceDoorCol, &colHeader);
        if (thisx->room == 0) {
            this->unk_169 = this->switchFlag - 0x33;
        } else {
            this->unk_169 = thisx->room + 1;
        }
        thisx->room = -1;
        this->timer = 1;
        if (this->unk_169 >= 6) {
            if (D_808A9508 & 1) {
                Actor_Kill(thisx);
            } else {
                D_808A9508 |= 1;
                this->actionFunc = BgRelayObjects_DoNothing;
            }
        } else if (this->unk_169 != 5) {
            if (HyruleCoop_DampeRaceIsLocalAuthority()) {
                Flags_UnsetSwitch(play, this->switchFlag);
            }
            if (D_808A9508 & (1 << this->unk_169)) {
                Actor_Kill(thisx);
            } else {
                D_808A9508 |= (1 << this->unk_169);
                this->actionFunc = func_808A90F4;
            }
        } else {
            if (HyruleCoop_DampeRaceIsLocalAuthority()) {
                Flags_SetSwitch(play, this->switchFlag);
            }
            this->actionFunc = func_808A91AC;
            thisx->world.pos.y += 120.0f;
            D_808A9508 |= 1;
        }
    }
    this->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, thisx, colHeader);
}

void BgRelayObjects_Destroy(Actor* thisx, PlayState* play) {
    BgRelayObjects* this = (BgRelayObjects*)thisx;

    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, this->dyna.bgId);
    if ((this->dyna.actor.params == WINDMILL_ROTATING_GEAR) && (gSaveContext.cutsceneIndex < 0xFFF0)) {
        Flags_UnsetEventChkInf(EVENTCHKINF_PLAYED_SONG_OF_STORMS_IN_WINDMILL);
    }
}

void func_808A90F4(BgRelayObjects* this, PlayState* play) {
    if (Flags_GetSwitch(play, this->switchFlag)) {
        if (this->timer != 0) {
            Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_SLIDE_DOOR_OPEN);
            if (INV_CONTENT(ITEM_HOOKSHOT) != ITEM_NONE) {
                this->timer = 120;
            } else {
                this->timer = 160;
            }
        }
        if (Math_StepToF(&this->dyna.actor.world.pos.y, this->dyna.actor.home.pos.y + 120.0f, 12.0f)) {
            this->actionFunc = func_808A91AC;
        }
    }
}

void func_808A91AC(BgRelayObjects* this, PlayState* play) {
    if (this->unk_169 != 5) {
        if (GameInteractor_Should(VB_SWITCH_TIMER_TICK, this->timer != 0, this, &this->timer)) {
            this->timer--;
        }
        Actor_PlaySfx_FlaggedTimer(&this->dyna.actor, this->timer);
    }
    if ((this->timer == 0) || (this->unk_169 == play->roomCtx.curRoom.num)) {
        Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_SLIDE_DOOR_CLOSE);
        this->actionFunc = func_808A9234;
    }
}

void func_808A9234(BgRelayObjects* this, PlayState* play) {
    this->dyna.actor.velocity.y += this->dyna.actor.gravity;
    if (Math_StepToF(&this->dyna.actor.world.pos.y, this->dyna.actor.home.pos.y, this->dyna.actor.velocity.y)) {
        Rumble_Request(this->dyna.actor.xyzDistToPlayerSq, 180, 20, 100);
        Audio_PlayActorSound2(&this->dyna.actor, NA_SE_EV_STONE_BOUND);
        if (this->unk_169 != play->roomCtx.curRoom.num) {
            Sfx_PlaySfxCentered2(NA_SE_EN_PO_LAUGH);
            this->timer = 5;
            this->actionFunc = func_808A932C;
            return;
        }
        Flags_UnsetSwitch(play, this->switchFlag);
        this->dyna.actor.flags &= ~ACTOR_FLAG_UPDATE_CULLING_DISABLED;
        if (play->roomCtx.curRoom.num == 4) {
            gSaveContext.timerState = TIMER_STATE_UP_FREEZE;
        }
        this->actionFunc = BgRelayObjects_DoNothing;
    }
}

void BgRelayObjects_DoNothing(BgRelayObjects* this, PlayState* play) {
}

void func_808A932C(BgRelayObjects* this, PlayState* play) {
    if (GameInteractor_Should(VB_SWITCH_TIMER_TICK, this->timer != 0, this, &this->timer)) {
        this->timer--;
    }
    if (this->timer == 0) {
        if (!Player_InCsMode(play)) {
            Sfx_PlaySfxCentered(NA_SE_OC_ABYSS);
            Play_TriggerRespawn(play);
            this->actionFunc = BgRelayObjects_DoNothing;
        }
    }
}

void func_808A939C(BgRelayObjects* this, PlayState* play) {
    if (Flags_GetEnv(play, 5)) {
        Flags_SetEventChkInf(EVENTCHKINF_PLAYED_SONG_OF_STORMS_IN_WINDMILL);
    }
    if (Flags_GetEventChkInf(EVENTCHKINF_PLAYED_SONG_OF_STORMS_IN_WINDMILL)) {
        Math_ScaledStepToS(&this->dyna.actor.world.rot.y, 0x400, 8);
    } else {
        Math_ScaledStepToS(&this->dyna.actor.world.rot.y, 0x80, 8);
    }
    this->dyna.actor.shape.rot.y += this->dyna.actor.world.rot.y;
    func_800F436C(&this->dyna.actor.projectedPos, NA_SE_EV_WOOD_GEAR - SFX_FLAG,
                  ((this->dyna.actor.world.rot.y - 0x80) * (1.0f / 0x380)) + 1.0f);
}

void BgRelayObjects_Update(Actor* thisx, PlayState* play) {
    BgRelayObjects* this = (BgRelayObjects*)thisx;

    if (BgRelayObjects_IsDampeRaceDoor(this) && !HyruleCoop_DampeRaceIsLocalAuthority()) {
        return;
    }
    this->actionFunc(this, play);
}

static int BgRelayObjects_IsDampeRaceDoor(const BgRelayObjects* this) {
    return this != NULL && this->dyna.actor.id == ACTOR_BG_RELAY_OBJECTS &&
           this->dyna.actor.params == WINDMILL_DAMPE_STONE_DOOR;
}

static u8 BgRelayObjects_GetCoopAction(const BgRelayObjects* this) {
    if (this->actionFunc == func_808A90F4) {
        return HYRULE_COOP_DAMPE_DOOR_OPENING;
    }
    if (this->actionFunc == func_808A91AC) {
        return HYRULE_COOP_DAMPE_DOOR_OPEN;
    }
    if (this->actionFunc == func_808A9234) {
        return HYRULE_COOP_DAMPE_DOOR_CLOSING;
    }
    if (this->actionFunc == func_808A932C) {
        return HYRULE_COOP_DAMPE_DOOR_RESPAWN;
    }
    return HYRULE_COOP_DAMPE_DOOR_CLOSED;
}

static void BgRelayObjects_SetCoopAction(BgRelayObjects* this, u8 action) {
    switch (action) {
        case HYRULE_COOP_DAMPE_DOOR_OPENING:
            this->actionFunc = func_808A90F4;
            break;
        case HYRULE_COOP_DAMPE_DOOR_OPEN:
            this->actionFunc = func_808A91AC;
            break;
        case HYRULE_COOP_DAMPE_DOOR_CLOSING:
            this->actionFunc = func_808A9234;
            break;
        case HYRULE_COOP_DAMPE_DOOR_RESPAWN:
            this->actionFunc = func_808A932C;
            break;
        default:
            this->actionFunc = BgRelayObjects_DoNothing;
            break;
    }
}

static void BgRelayObjects_ApplyCoopSwitch(PlayState* play, u8 switchFlag, u8 switchIsSet) {
    u32 mask = 1U << (switchFlag & 0x1F);

    /* Remote snapshots must not fire local scene-flag hooks or create a new intent. */
    if (switchFlag < 0x20) {
        if (switchIsSet) {
            play->actorCtx.flags.swch |= mask;
        } else {
            play->actorCtx.flags.swch &= ~mask;
        }
    } else if (switchIsSet) {
        play->actorCtx.flags.tempSwch |= mask;
    } else {
        play->actorCtx.flags.tempSwch &= ~mask;
    }
}

int HyruleCoop_DampeRaceCaptureDoorState(const void* actorRef, HyruleCoopDampeRaceDoorState* state) {
    const BgRelayObjects* this = actorRef;

    if (!BgRelayObjects_IsDampeRaceDoor(this) || state == NULL) {
        return 0;
    }
    state->action = BgRelayObjects_GetCoopAction(this);
    state->switchFlag = this->switchFlag;
    state->room = this->unk_169;
    state->switchIsSet = Flags_GetSwitch(gPlayState, this->switchFlag) != 0;
    state->timer = this->timer;
    state->worldRotY = this->dyna.actor.world.rot.y;
    state->worldPosX = this->dyna.actor.world.pos.x;
    state->worldPosY = this->dyna.actor.world.pos.y;
    state->worldPosZ = this->dyna.actor.world.pos.z;
    state->velocityY = this->dyna.actor.velocity.y;
    return 1;
}

int HyruleCoop_DampeRaceApplyDoorState(void* actorRef, void* playRef, const HyruleCoopDampeRaceDoorState* state) {
    BgRelayObjects* this = actorRef;
    PlayState* play = playRef;

    if (!BgRelayObjects_IsDampeRaceDoor(this) || play == NULL || state == NULL || state->switchFlag >= 0x40 ||
        state->action >= HYRULE_COOP_DAMPE_DOOR_ACTION_COUNT) {
        return 0;
    }
    this->switchFlag = state->switchFlag;
    this->unk_169 = state->room;
    this->timer = state->timer;
    this->dyna.actor.world.rot.y = state->worldRotY;
    this->dyna.actor.world.pos.x = state->worldPosX;
    this->dyna.actor.world.pos.y = state->worldPosY;
    this->dyna.actor.world.pos.z = state->worldPosZ;
    this->dyna.actor.velocity.y = state->velocityY;
    BgRelayObjects_ApplyCoopSwitch(play, state->switchFlag, state->switchIsSet);
    BgRelayObjects_SetCoopAction(this, state->action);
    return 1;
}

void BgRelayObjects_Draw(Actor* thisx, PlayState* play) {
    BgRelayObjects* this = (BgRelayObjects*)thisx;

    if (this->dyna.actor.params == WINDMILL_ROTATING_GEAR) {
        Gfx_DrawDListOpa(play, gWindmillRotatingPlatformDL);
    } else {
        Gfx_DrawDListOpa(play, gDampeRaceDoorDL);
    }
}

void BgRelayObjects_Reset(void) {
    D_808A9508 = 0;
}
