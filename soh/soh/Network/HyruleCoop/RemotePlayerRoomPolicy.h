#pragma once

#include <cstdint>

namespace HyruleCoop {

struct TimelineScope {
    int32_t linkAge = -1;
    int16_t sceneLayer = -1;
};

constexpr bool IsValidTimelineScope(const TimelineScope& scope) {
    return (scope.linkAge == 0 || scope.linkAge == 1) && scope.sceneLayer >= 0;
}

constexpr bool IsSameTimeline(const TimelineScope& local, const TimelineScope& remote) {
    return IsValidTimelineScope(local) && IsValidTimelineScope(remote) && local.linkAge == remote.linkAge &&
           local.sceneLayer == remote.sceneLayer;
}

constexpr bool IsRemotePlayerActorInActiveRoom(int16_t actorRoom, int16_t activeRoom) {
    return actorRoom == activeRoom;
}

constexpr bool IsRemotePlayerVisibleInRoom(int16_t localScene, int16_t activeRoom, int16_t remoteScene,
                                           int16_t remoteRoom, const TimelineScope& localTimeline,
                                           const TimelineScope& remoteTimeline) {
    return localScene == remoteScene && activeRoom == remoteRoom && IsSameTimeline(localTimeline, remoteTimeline);
}

} // namespace HyruleCoop
