#include "soh/Network/HyruleCoop/HyruleCoop.h"

extern "C" bool HyruleCoop_GetRemotePlayerMapPosition(int16_t scene, float* x, float* z, int16_t* yaw) {
    if (HyruleCoop::Manager::Instance == nullptr || !HyruleCoop::Manager::Instance->IsReady() || x == nullptr ||
        z == nullptr || yaw == nullptr) {
        return false;
    }

    const HyruleCoop::PlayerSnapshotMessage* snapshot =
        HyruleCoop::Manager::Instance->GetRemotePlayerSnapshot();
    if (snapshot == nullptr || snapshot->scene != scene) {
        return false;
    }

    *x = snapshot->position[0];
    *z = snapshot->position[2];
    *yaw = snapshot->rotation[1];
    HyruleCoop::Manager::Instance->NotifyRemotePlayerMapPositionRead(scene);
    return true;
}
