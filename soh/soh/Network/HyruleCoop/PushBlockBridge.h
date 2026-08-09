#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HyruleCoopPushBlockPhase {
    HYRULE_COOP_PUSH_BLOCK_ON_SCENE = 0,
    HYRULE_COOP_PUSH_BLOCK_ON_ACTOR = 1,
    HYRULE_COOP_PUSH_BLOCK_PUSHING = 2,
    HYRULE_COOP_PUSH_BLOCK_FALLING = 3,
} HyruleCoopPushBlockPhase;

typedef struct HyruleCoopPushBlockState {
    float homePosition[3];
    float position[3];
    float velocity[3];
    float pushSpeed;
    float pushDistance;
    float direction;
    int16_t timer;
    int16_t worldYaw;
    uint8_t phase;
} HyruleCoopPushBlockState;

int HyruleCoop_ObjOshihikiCaptureState(const void* actor, HyruleCoopPushBlockState* state);
int HyruleCoop_ObjOshihikiApplyState(void* actor, void* play, const HyruleCoopPushBlockState* state);
int HyruleCoop_ObjOshihikiPeekPush(const void* actor, float* direction);
int HyruleCoop_ObjOshihikiBeginPush(void* actor, void* play, float direction);

#ifdef __cplusplus
}
#endif
