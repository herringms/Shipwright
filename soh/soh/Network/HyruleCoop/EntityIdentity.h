#pragma once

#include <cstdint>

namespace HyruleCoop {

struct StaticEntitySignature {
    uint32_t worldGeneration = 0;
    int16_t scene = -1;
    int16_t room = -1;
    int16_t actorId = -1;
    int16_t params = 0;
    int32_t homePosition[3] = {};
    int16_t homeRotation[3] = {};
};

uint64_t BuildStaticEntityId(const StaticEntitySignature& signature);

} // namespace HyruleCoop
