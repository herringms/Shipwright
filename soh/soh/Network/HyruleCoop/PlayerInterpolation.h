#pragma once

#include "HyruleCoopProtocol.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace HyruleCoop {

class PlayerSnapshotInterpolator {
  public:
    static constexpr uint64_t kInterpolationDelayMs = 100;
    static constexpr uint64_t kMaximumExtrapolationMs = 100;

    void Reset();
    void Push(const PlayerSnapshotMessage& snapshot, uint64_t receivedAtMs);
    bool Sample(uint64_t nowMs, PlayerSnapshotMessage& result) const;
    size_t Size() const;

  private:
    struct TimedSnapshot {
        PlayerSnapshotMessage snapshot;
        uint64_t receivedAtMs = 0;
    };

    static PlayerSnapshotMessage Interpolate(const PlayerSnapshotMessage& from,
                                             const PlayerSnapshotMessage& to, float amount);
    static bool IsDiscontinuity(const TimedSnapshot& previous, const PlayerSnapshotMessage& next,
                                uint64_t receivedAtMs);

    std::deque<TimedSnapshot> snapshots;
};

} // namespace HyruleCoop
