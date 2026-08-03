#pragma once

#include "z64.h"

extern "C" {
void HyruleCoopRemotePlayer_Init(Actor* actor, PlayState* play);
void HyruleCoopRemotePlayer_Update(Actor* actor, PlayState* play);
void HyruleCoopRemotePlayer_Draw(Actor* actor, PlayState* play);
void HyruleCoopRemotePlayer_Destroy(Actor* actor, PlayState* play);
}
