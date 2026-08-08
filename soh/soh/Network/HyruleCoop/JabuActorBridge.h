#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compact actor-private state for Jabu-Jabu enemies. The common actor transform
 * remains owned by the normal actor snapshot; these fields only select native
 * action paths and preserve the timers that make those paths deterministic.
 */
typedef struct HyruleCoopJabuActorState {
    uint8_t action;
    uint8_t health;
    uint8_t flags;
    int16_t variant;
    int16_t timer;
    int16_t auxTimer;
    int16_t auxState;
    int16_t alpha;
    float animationFrame;
    float animationSpeed;
} HyruleCoopJabuActorState;

enum {
    HYRULE_COOP_BILI_FLOAT_IDLE,
    HYRULE_COOP_BILI_SPAWNED_FLY_APART,
    HYRULE_COOP_BILI_DISCHARGE,
    HYRULE_COOP_BILI_CLIMB,
    HYRULE_COOP_BILI_APPROACH,
    HYRULE_COOP_BILI_SET_HOME_HEIGHT,
    HYRULE_COOP_BILI_RECOIL,
    HYRULE_COOP_BILI_BURNT,
    HYRULE_COOP_BILI_DIE,
    HYRULE_COOP_BILI_STUNNED,
    HYRULE_COOP_BILI_FROZEN,
};

enum {
    HYRULE_COOP_BARI_LURK,
    HYRULE_COOP_BARI_DROP_APPEAR,
    HYRULE_COOP_BARI_FLOAT_IDLE,
    HYRULE_COOP_BARI_ATTACKED,
    HYRULE_COOP_BARI_RETALIATE,
    HYRULE_COOP_BARI_MOVE_ARMS_DOWN,
    HYRULE_COOP_BARI_BURNT,
    HYRULE_COOP_BARI_DIVIDE_AND_DIE,
    HYRULE_COOP_BARI_STUNNED,
    HYRULE_COOP_BARI_FROZEN,
    HYRULE_COOP_BARI_RETURN_TO_LURK,
};

enum {
    HYRULE_COOP_TAILPASARAN_FRAGMENT_FADE,
    HYRULE_COOP_TAILPASARAN_DIE,
    HYRULE_COOP_TAILPASARAN_TAIL_FOLLOW,
    HYRULE_COOP_TAILPASARAN_HEAD_WAIT,
    HYRULE_COOP_TAILPASARAN_HEAD_APPROACH,
    HYRULE_COOP_TAILPASARAN_HEAD_TAKEOFF,
    HYRULE_COOP_TAILPASARAN_HEAD_BURROW,
};

enum {
    HYRULE_COOP_TENTACLE_IDLE,
    HYRULE_COOP_TENTACLE_SWING,
    HYRULE_COOP_TENTACLE_RECOIL,
    HYRULE_COOP_TENTACLE_DIE,
    HYRULE_COOP_TENTACLE_BLOB,
};

enum {
    HYRULE_COOP_BIGOKUTA_PROP_WAIT,
    HYRULE_COOP_BIGOKUTA_PROP_JUMP,
    HYRULE_COOP_BIGOKUTA_PROP_TURN,
    HYRULE_COOP_BIGOKUTA_WAIT,
    HYRULE_COOP_BIGOKUTA_BOMB_STUN,
    HYRULE_COOP_BIGOKUTA_ORBIT,
    HYRULE_COOP_BIGOKUTA_STUNNED,
    HYRULE_COOP_BIGOKUTA_RECOIL,
    HYRULE_COOP_BIGOKUTA_DEATH,
    HYRULE_COOP_BIGOKUTA_RECOVER,
    HYRULE_COOP_BIGOKUTA_SINK,
    HYRULE_COOP_BIGOKUTA_RISE,
};

int HyruleCoop_EnBiliCaptureState(const void* actor, HyruleCoopJabuActorState* state);
int HyruleCoop_EnBiliPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage);
int HyruleCoop_EnBiliApplyState(void* actor, void* play, const HyruleCoopJabuActorState* state);
int HyruleCoop_EnBiliApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage);
int HyruleCoop_EnBiliApplyDeath(void* actor, void* play);

int HyruleCoop_EnValiCaptureState(const void* actor, HyruleCoopJabuActorState* state);
int HyruleCoop_EnValiPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage);
int HyruleCoop_EnValiApplyState(void* actor, void* play, const HyruleCoopJabuActorState* state);
int HyruleCoop_EnValiApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage);
int HyruleCoop_EnValiApplyDeath(void* actor, void* play);

/* Tailpasaran helpers operate on the head when a tail segment is supplied. */
void* HyruleCoop_EnTpCanonicalActor(void* actor);
int HyruleCoop_EnTpCaptureState(const void* actor, HyruleCoopJabuActorState* state);
int HyruleCoop_EnTpPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage);
int HyruleCoop_EnTpApplyState(void* actor, void* play, const HyruleCoopJabuActorState* state);
int HyruleCoop_EnTpApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage);
int HyruleCoop_EnTpApplyDeath(void* actor, void* play);

int HyruleCoop_EnBaCaptureState(const void* actor, HyruleCoopJabuActorState* state);
void* HyruleCoop_EnBaCanonicalActor(void* actor);
int HyruleCoop_EnBaPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage);
int HyruleCoop_EnBaApplyState(void* actor, void* play, const HyruleCoopJabuActorState* state);
int HyruleCoop_EnBaApplyDamage(void* actor, void* play, uint8_t damage);
int HyruleCoop_EnBaApplyDeath(void* actor, void* play);

int HyruleCoop_EnBigokutaCaptureState(const void* actor, HyruleCoopJabuActorState* state);
int HyruleCoop_EnBigokutaPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage);
int HyruleCoop_EnBigokutaApplyState(void* actor, void* play, const HyruleCoopJabuActorState* state);
int HyruleCoop_EnBigokutaApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage);
int HyruleCoop_EnBigokutaApplyDeath(void* actor, void* play);

#ifdef __cplusplus
}
#endif
