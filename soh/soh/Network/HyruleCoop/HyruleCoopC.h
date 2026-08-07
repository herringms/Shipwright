#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int HyruleCoop_ShouldRegisterStalchildAttack(void* actor, int nativeAttackActive);
int HyruleCoop_ShouldProcessStalchildHit(void* actor, void* attacker);

#ifdef __cplusplus
}
#endif
