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
    activeMeleeSnapshot.reset();
}

void PlayerSnapshotInterpolator::Push(const PlayerSnapshotMessage& snapshot, uint64_t receivedAtMs) {
    if (!snapshots.empty()) {
        const TimedSnapshot& previous = snapshots.back();
        if (!IsNewerTick(snapshot.tick, previous.snapshot.tick)) {
            return;
        }
        if (IsDiscontinuity(previous, snapshot, receivedAtMs)) {
            snapshots.clear();
            activeMeleeSnapshot.reset();
        }
    }

    snapshots.push_back({ snapshot, receivedAtMs });
    if (snapshot.meleeWeaponState > 0) {
        activeMeleeSnapshot = TimedSnapshot{ snapshot, receivedAtMs };
    }
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
        ApplyLatchedMeleeAction(nowMs > kInterpolationDelayMs ? nowMs - kInterpolationDelayMs : 0, result);
        return true;
    }

    const uint64_t targetMs = nowMs > kInterpolationDelayMs ? nowMs - kInterpolationDelayMs : 0;
    if (targetMs <= snapshots.front().receivedAtMs) {
        result = snapshots.front().snapshot;
        ApplyLatchedMeleeAction(targetMs, result);
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
        ApplyLatchedMeleeAction(targetMs, result);
        return true;
    }

    const TimedSnapshot& latest = snapshots.back();
    const TimedSnapshot& previous = snapshots[snapshots.size() - 2];
    result = latest.snapshot;
    const uint64_t durationMs = latest.receivedAtMs - previous.receivedAtMs;
    const uint64_t extrapolationMs =
        std::min(targetMs - latest.receivedAtMs, kMaximumExtrapolationMs);
    if (durationMs == 0 || durationMs > kDiscontinuityGapMs || extrapolationMs == 0) {
        ApplyLatchedMeleeAction(targetMs, result);
        return true;
    }

    for (size_t axis = 0; axis < 3; ++axis) {
        float speedPerMs = (latest.snapshot.position[axis] - previous.snapshot.position[axis]) /
                           static_cast<float>(durationMs);
        speedPerMs = std::clamp(speedPerMs, -kMaximumExtrapolationSpeedPerMs,
                                kMaximumExtrapolationSpeedPerMs);
        result.position[axis] += speedPerMs * static_cast<float>(extrapolationMs);
    }
    ApplyLatchedMeleeAction(targetMs, result);
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
    if (from.mounted && to.mounted) {
        for (size_t axis = 0; axis < 3; ++axis) {
            result.horsePosition[axis] = Lerp(from.horsePosition[axis], to.horsePosition[axis], amount);
            result.horseRotation[axis] = LerpAngle(from.horseRotation[axis], to.horseRotation[axis], amount);
        }
        result.horseAnimationFrame = Lerp(from.horseAnimationFrame, to.horseAnimationFrame, amount);
        result.horseSpeed = Lerp(from.horseSpeed, to.horseSpeed, amount);
    }
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

void PlayerSnapshotInterpolator::ApplyLatchedMeleeAction(uint64_t targetMs,
                                                         PlayerSnapshotMessage& result) const {
    if (!activeMeleeSnapshot.has_value()) {
        return;
    }

    const TimedSnapshot& action = *activeMeleeSnapshot;
    if (targetMs < action.receivedAtMs || targetMs - action.receivedAtMs > kMeleeActionHoldMs ||
        result.scope.sessionEpoch != action.snapshot.scope.sessionEpoch ||
        result.scope.worldGeneration != action.snapshot.scope.worldGeneration ||
        result.scene != action.snapshot.scene) {
        return;
    }

    // Discrete attack state can be shorter than the interpolation delay. Preserve the edge while position and
    // joints continue to interpolate so an immediately following idle packet cannot hide the remote weapon.
    result.buttonItem = action.snapshot.buttonItem;
    result.itemAction = action.snapshot.itemAction;
    result.heldItemAction = action.snapshot.heldItemAction;
    result.modelGroup = action.snapshot.modelGroup;
    result.modelState = action.snapshot.modelState;
    result.actionVariable = action.snapshot.actionVariable;
    result.meleeWeaponState = action.snapshot.meleeWeaponState;
    result.meleeWeaponAnimation = action.snapshot.meleeWeaponAnimation;
}

} // namespace HyruleCoop
