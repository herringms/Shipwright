#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Actor-private state for one Boss_Va component. The normal actor snapshot
 * carries transform data. This state carries the native phase/action data that
 * turns the Barinade actor family into one host-owned encounter.
 */
typedef struct HyruleCoopBarinadeState {
    int16_t component;
    int16_t phase;
    int16_t cutsceneState;
    int16_t doorState;
    uint16_t phase2Timer;
    int16_t phase4Health;
    int16_t timer2;
    int16_t visualRotation;
    int32_t timer;
    uint32_t actorFlags;
    uint8_t action;
    uint8_t health;
    uint8_t bodyState;
    uint8_t phase3StopMoving;
    uint8_t onCeiling;
    uint8_t burst;
    uint8_t isDead;
    int8_t invincibilityTimer;
    int16_t bodyGlow;
    int16_t colorFilterTimer;
    int16_t visualAngles[6];
    float animationFrame;
    float animationSpeed;
    float verticalOffset;
    float visualScaleX;
    float visualScaleY;
    float visualScaleZ;
} HyruleCoopBarinadeState;

enum {
    HYRULE_COOP_BARINADE_ACTION_BODY_INTRO,
    HYRULE_COOP_BARINADE_ACTION_BODY_PHASE_1,
    HYRULE_COOP_BARINADE_ACTION_BODY_PHASE_2,
    HYRULE_COOP_BARINADE_ACTION_BODY_PHASE_3,
    HYRULE_COOP_BARINADE_ACTION_BODY_PHASE_4,
    HYRULE_COOP_BARINADE_ACTION_BODY_DEATH,
    HYRULE_COOP_BARINADE_ACTION_SUPPORT_INTRO,
    HYRULE_COOP_BARINADE_ACTION_SUPPORT_ATTACHED,
    HYRULE_COOP_BARINADE_ACTION_SUPPORT_CUT,
    HYRULE_COOP_BARINADE_ACTION_ZAPPER_INTRO,
    HYRULE_COOP_BARINADE_ACTION_ZAPPER_ATTACK,
    HYRULE_COOP_BARINADE_ACTION_ZAPPER_ENRAGED,
    HYRULE_COOP_BARINADE_ACTION_ZAPPER_DAMAGED,
    HYRULE_COOP_BARINADE_ACTION_ZAPPER_HOLD,
    HYRULE_COOP_BARINADE_ACTION_ZAPPER_DEATH,
    HYRULE_COOP_BARINADE_ACTION_STUMP,
    HYRULE_COOP_BARINADE_ACTION_BARI_INTRO,
    HYRULE_COOP_BARINADE_ACTION_BARI_PHASE_2,
    HYRULE_COOP_BARINADE_ACTION_BARI_PHASE_3,
    HYRULE_COOP_BARINADE_ACTION_BARI_STUNNED,
    HYRULE_COOP_BARINADE_ACTION_BARI_DEATH,
    HYRULE_COOP_BARINADE_ACTION_DOOR,
};

/*
 * The root body is the canonical actor for every child component with a
 * Boss_Va parent. Detached stumps have no parent and intentionally remain
 * their own visual-only component.
 */
void* HyruleCoop_BarinadeCanonicalActor(void* actor);
int HyruleCoop_BarinadeIsCanonicalRoot(const void* actor);

int HyruleCoop_BarinadeCaptureState(const void* actor, HyruleCoopBarinadeState* state);

/* Guest-only observation: reports and consumes a native collider edge before local encounter logic sees it. */
int HyruleCoop_BarinadeConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage);

/* Safe on guests: applies state without setup, flag, reward, or spawn effects. */
int HyruleCoop_BarinadeApplyState(void* actor, void* play, const HyruleCoopBarinadeState* state);

/* Host-only: applies a validated guest hit through the encounter's native paths. */
int HyruleCoop_BarinadeApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage);

/* Host-only: runs the native terminal path. Guests should receive a state snapshot instead. */
int HyruleCoop_BarinadeApplyDeath(void* actor, void* play);

#ifdef __cplusplus
}
#endif
