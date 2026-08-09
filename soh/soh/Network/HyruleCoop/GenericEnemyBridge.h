#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int HyruleCoop_EnFireflyConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage);

/*
 * These adapters deliberately expose only collision-derived damage and native
 * damage application. Their private movement, targeting, and animation loops
 * remain local, so different player targets cannot fight an imported transform.
 */
int HyruleCoop_EnRdPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnRdConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnRdApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);

int HyruleCoop_EnMbPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnMbConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnMbApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);

int HyruleCoop_EnBbPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnBbConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnBbApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);

int HyruleCoop_EnTestPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnTestConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnTestApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);

int HyruleCoop_EnSwPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnSwConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnSwApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);

int HyruleCoop_EnStPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnStConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnStApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);

int HyruleCoop_EnSshPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnSshConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnSshApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);

int HyruleCoop_EnPohPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnPohConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnPohApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);

int HyruleCoop_EnPoSistersSupportsSharedCombat(const void* actor);
int HyruleCoop_EnPoSistersPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage,
                                     uint32_t* damageFlags);
int HyruleCoop_EnPoSistersConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage,
                                        uint32_t* damageFlags);
int HyruleCoop_EnPoSistersApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage,
                                      uint32_t damageFlags);

int HyruleCoop_EnWfPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnWfConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage, uint32_t* damageFlags);
int HyruleCoop_EnWfApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage, uint32_t damageFlags);
int HyruleCoop_EnWfReplayDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage,
                                uint32_t damageFlags);

int HyruleCoop_BossGanondrofSupportsSharedCombat(const void* actor);
int HyruleCoop_BossGanondrofPeekDamage(const void* actor, uint8_t* damageEffect, uint8_t* damage,
                                       uint32_t* damageFlags);
int HyruleCoop_BossGanondrofConsumeDamage(void* actor, uint8_t* damageEffect, uint8_t* damage,
                                          uint32_t* damageFlags);
int HyruleCoop_BossGanondrofApplyDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage,
                                        uint32_t damageFlags);
int HyruleCoop_BossGanondrofReplayDamage(void* actor, void* play, uint8_t damageEffect, uint8_t damage,
                                         uint32_t damageFlags);

/* Guest replicas retain death effects but must not generate an independent drop. */
int HyruleCoop_ShouldSuppressSharedEnemyLocalReward(void* actor);

#ifdef __cplusplus
}
#endif
