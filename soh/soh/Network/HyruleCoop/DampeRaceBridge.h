#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HYRULE_COOP_DAMPE_GHOST_IDLE,
    HYRULE_COOP_DAMPE_GHOST_TALK,
    HYRULE_COOP_DAMPE_GHOST_RACE,
    HYRULE_COOP_DAMPE_GHOST_END_RACE,
    HYRULE_COOP_DAMPE_GHOST_TALK2,
    HYRULE_COOP_DAMPE_GHOST_DISAPPEAR,
    HYRULE_COOP_DAMPE_GHOST_ACTION_COUNT,
};

enum {
    HYRULE_COOP_DAMPE_DOOR_OPENING,
    HYRULE_COOP_DAMPE_DOOR_OPEN,
    HYRULE_COOP_DAMPE_DOOR_CLOSING,
    HYRULE_COOP_DAMPE_DOOR_RESPAWN,
    HYRULE_COOP_DAMPE_DOOR_CLOSED,
    HYRULE_COOP_DAMPE_DOOR_ACTION_COUNT,
};

/*
 * The relay ghost uses actor-private path and timer state. These snapshots are
 * intentionally independent of the generic actor transform packet.
 */
typedef struct HyruleCoopDampeRaceGhostState {
    uint8_t action;
    uint8_t hookshotSlotFull;
    uint8_t bobTimer;
    uint8_t eyeTextureIdx;
    int16_t actionTimer;
    int16_t pathIndex;
    int16_t yawTowardsPathPoint;
    int16_t worldRotY;
    int16_t shapeRotY;
    uint16_t textId;
    int16_t timerState;
    int16_t timerSeconds;
    float worldPosX;
    float worldPosY;
    float worldPosZ;
    float homePosY;
    float speedXZ;
    float scale;
    float animationFrame;
    float animationSpeed;
} HyruleCoopDampeRaceGhostState;

typedef struct HyruleCoopDampeRaceDoorState {
    uint8_t action;
    uint8_t switchFlag;
    int8_t room;
    uint8_t switchIsSet;
    int16_t timer;
    int16_t worldRotY;
    float worldPosX;
    float worldPosY;
    float worldPosZ;
    float velocityY;
} HyruleCoopDampeRaceDoorState;

/* Called by the session owner whenever local host/guest authority changes. */
void HyruleCoop_DampeRaceSetLocalAuthority(int authoritative);
int HyruleCoop_DampeRaceIsLocalAuthority(void);

/* Guest talk completion queues one request; networking consumes and forwards it. */
int HyruleCoop_DampeRaceConsumeStartRequest(const void* actor);
int HyruleCoop_DampeRaceBeginHostRace(void* actor, void* play);

int HyruleCoop_DampeRaceCaptureGhostState(const void* actor, HyruleCoopDampeRaceGhostState* state);
int HyruleCoop_DampeRaceApplyGhostState(void* actor, void* play, const HyruleCoopDampeRaceGhostState* state);

int HyruleCoop_DampeRaceCaptureDoorState(const void* actor, HyruleCoopDampeRaceDoorState* state);
int HyruleCoop_DampeRaceApplyDoorState(void* actor, void* play, const HyruleCoopDampeRaceDoorState* state);

#ifdef __cplusplus
}
#endif
