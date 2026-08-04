#include "PlayerInterpolation.h"

#include <algorithm>
#include <cmath>

namespace HyruleCoop {
namespace {

constexpr size_t kMaximumSnapshots = 32;
constexpr uint64_t kMaximumHistoryMs = 1500;
constexpr uint64_t kDiscontinuityGapMs = 1000;
constexpr float kDiscontinuityDistance = 600.0f;
constexpr float kMaximumExtrapolationSpeedPerMs = 2.0f;

float Lerp(float from, float to, float amount) {
    return from + (to - from) * amount;
}

int16_t LerpAngle(int16_t from, int16_t to, float amount) {
    const int16_t delta = static_cast<int16_t>(static_cast<uint16_t>(to) - static_cast<uint16_t>(from));
    return static_cast<int16_t>(from + static_cast<int32_t>(std::lround(static_cast<float>(delta) * amount)));
}

bool IsNewerTick(uint32_t tick, uint32_t previous) {
    return static_cast<int32_t>(tick - previous) > 0;
}

} // namespace

void PlayerSnapshotInterpolator::Reset() {
    snapshots.clear();
}

void PlayerSnapshotInterpolator::Push(const PlayerSnapshotMessage& snapshot, uint64_t receivedAtMs) {
    if (!snapshots.empty()) {
        const TimedSnapshot& previous = snapshots.back();
        if (!IsNewerTick(snapshot.tick, previous.snapshot.tick)) {
            return;
        }
        if (IsDiscontinuity(previous, snapshot, receivedAtMs)) {
            snapshots.clear();
        }
    }

    snapshots.push_back({ snapshot, receivedAtMs });
    while (snapshots.size() > kMaximumSnapshots ||
           (snapshots.size() > 1 && receivedAtMs - snapshots.front().receivedAtMs > kMaximumHistoryMs)) {
        snapshots.pop_front();
    }
}

bool PlayerSnapshotInterpolator::Sample(uint64_t nowMs, PlayerSnapshotMessage& result) const {
    if (snapshots.empty()) {
        return false;
    }
    if (snapshots.size() == 1) {
        result = snapshots.back().snapshot;
        return true;
    }

    const uint64_t targetMs = nowMs > kInterpolationDelayMs ? nowMs - kInterpolationDelayMs : 0;
    if (targetMs <= snapshots.front().receivedAtMs) {
        result = snapshots.front().snapshot;
        return true;
    }

    for (size_t index = 1; index < snapshots.size(); ++index) {
        const TimedSnapshot& to = snapshots[index];
        if (targetMs > to.receivedAtMs) {
            continue;
        }
        const TimedSnapshot& from = snapshots[index - 1];
        const uint64_t durationMs = to.receivedAtMs - from.receivedAtMs;
        const float amount = durationMs == 0
                                 ? 1.0f
                                 : static_cast<float>(targetMs - from.receivedAtMs) /
                                       static_cast<float>(durationMs);
        result = Interpolate(from.snapshot, to.snapshot, std::clamp(amount, 0.0f, 1.0f));
        return true;
    }

    const TimedSnapshot& latest = snapshots.back();
    const TimedSnapshot& previous = snapshots[snapshots.size() - 2];
    result = latest.snapshot;
    const uint64_t durationMs = latest.receivedAtMs - previous.receivedAtMs;
    const uint64_t extrapolationMs =
        std::min(targetMs - latest.receivedAtMs, kMaximumExtrapolationMs);
    if (durationMs == 0 || durationMs > kDiscontinuityGapMs || extrapolationMs == 0) {
        return true;
    }

    for (size_t axis = 0; axis < 3; ++axis) {
        float speedPerMs = (latest.snapshot.position[axis] - previous.snapshot.position[axis]) /
                           static_cast<float>(durationMs);
        speedPerMs = std::clamp(speedPerMs, -kMaximumExtrapolationSpeedPerMs,
                                kMaximumExtrapolationSpeedPerMs);
        result.position[axis] += speedPerMs * static_cast<float>(extrapolationMs);
    }
    return true;
}

size_t PlayerSnapshotInterpolator::Size() const {
    return snapshots.size();
}

PlayerSnapshotMessage PlayerSnapshotInterpolator::Interpolate(const PlayerSnapshotMessage& from,
                                                               const PlayerSnapshotMessage& to,
                                                               float amount) {
    PlayerSnapshotMessage result = to;
    for (size_t axis = 0; axis < 3; ++axis) {
        result.position[axis] = Lerp(from.position[axis], to.position[axis], amount);
        result.rotation[axis] = LerpAngle(from.rotation[axis], to.rotation[axis], amount);
        result.previousTranslation[axis] =
            static_cast<int16_t>(std::lround(Lerp(static_cast<float>(from.previousTranslation[axis]),
                                                  static_cast<float>(to.previousTranslation[axis]), amount)));
        result.upperLimbRotation[axis] =
            LerpAngle(from.upperLimbRotation[axis], to.upperLimbRotation[axis], amount);
    }
    for (size_t index = 0; index < result.joints.size(); ++index) {
        result.joints[index] = LerpAngle(from.joints[index], to.joints[index], amount);
    }
    result.modelBlend = Lerp(from.modelBlend, to.modelBlend, amount);
    result.linearVelocity = Lerp(from.linearVelocity, to.linearVelocity, amount);
    return result;
}

bool PlayerSnapshotInterpolator::IsDiscontinuity(const TimedSnapshot& previous,
                                                  const PlayerSnapshotMessage& next,
                                                  uint64_t receivedAtMs) {
    if (previous.snapshot.scope != next.scope || previous.snapshot.scene != next.scene ||
        previous.snapshot.room != next.room || previous.snapshot.entrance != next.entrance ||
        previous.snapshot.linkAge != next.linkAge || receivedAtMs < previous.receivedAtMs ||
        receivedAtMs - previous.receivedAtMs > kDiscontinuityGapMs) {
        return true;
    }

    float distanceSquared = 0.0f;
    for (size_t axis = 0; axis < 3; ++axis) {
        const float delta = next.position[axis] - previous.snapshot.position[axis];
        distanceSquared += delta * delta;
    }
    return distanceSquared > kDiscontinuityDistance * kDiscontinuityDistance;
}

} // namespace HyruleCoop
