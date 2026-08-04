#include "soh/Network/HyruleCoop/PlayerInterpolation.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace HyruleCoop;

namespace {

PlayerSnapshotMessage Snapshot(uint32_t tick, float x, int16_t yaw = 0, int16_t scene = 1) {
    PlayerSnapshotMessage result;
    result.scope = { 100, 2 };
    result.tick = tick;
    result.scene = scene;
    result.room = 0;
    result.entrance = 1;
    result.linkAge = 0;
    result.position[0] = x;
    result.rotation[1] = yaw;
    result.joints[0] = yaw;
    return result;
}

void AssertNear(float actual, float expected) {
    assert(std::abs(actual - expected) < 0.01f);
}

} // namespace

int main() {
    PlayerSnapshotInterpolator interpolator;
    PlayerSnapshotMessage sampled;
    assert(!interpolator.Sample(1000, sampled));

    interpolator.Push(Snapshot(1, 0.0f, 32760), 1000);
    assert(interpolator.Sample(1000, sampled));
    AssertNear(sampled.position[0], 0.0f);

    interpolator.Push(Snapshot(2, 100.0f, -32760), 1050);
    assert(interpolator.Size() == 2);
    assert(interpolator.Sample(1125, sampled));
    AssertNear(sampled.position[0], 50.0f);
    assert(std::abs(static_cast<int>(sampled.rotation[1])) > 32000);
    assert(std::abs(static_cast<int>(sampled.joints[0])) > 32000);

    assert(interpolator.Sample(1200, sampled));
    AssertNear(sampled.position[0], 200.0f);
    assert(interpolator.Sample(1400, sampled));
    AssertNear(sampled.position[0], 300.0f);

    interpolator.Push(Snapshot(3, 1000.0f), 1100);
    assert(interpolator.Size() == 1);
    assert(interpolator.Sample(1200, sampled));
    AssertNear(sampled.position[0], 1000.0f);

    interpolator.Push(Snapshot(4, 1010.0f, 0, 2), 1150);
    assert(interpolator.Size() == 1);
    assert(interpolator.Sample(1250, sampled));
    assert(sampled.scene == 2);

    interpolator.Push(Snapshot(5, 1020.0f, 0, 2), 1200);
    interpolator.Push(Snapshot(5, 9000.0f, 0, 2), 1250);
    assert(interpolator.Size() == 2);

    interpolator.Reset();
    assert(interpolator.Size() == 0);
    assert(!interpolator.Sample(1300, sampled));

    std::cout << "HyruleCoop player interpolation tests passed\n";
    return 0;
}
