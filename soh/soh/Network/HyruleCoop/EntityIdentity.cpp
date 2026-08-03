#include "EntityIdentity.h"

#include <bit>
#include <cstddef>
#include <type_traits>

namespace HyruleCoop {
namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

template <typename T>
void HashValue(uint64_t& hash, T value) {
    using Unsigned = std::make_unsigned_t<T>;
    const Unsigned bits = std::bit_cast<Unsigned>(value);
    for (size_t i = 0; i < sizeof(T); ++i) {
        hash ^= static_cast<uint8_t>(bits >> (i * 8));
        hash *= kFnvPrime;
    }
}

} // namespace

uint64_t BuildStaticEntityId(const StaticEntitySignature& signature) {
    uint64_t hash = kFnvOffset;
    HashValue(hash, signature.worldGeneration);
    HashValue(hash, signature.scene);
    HashValue(hash, signature.room);
    HashValue(hash, signature.actorId);
    HashValue(hash, signature.params);
    for (int32_t value : signature.homePosition) {
        HashValue(hash, value);
    }
    for (int16_t value : signature.homeRotation) {
        HashValue(hash, value);
    }
    hash &= ~(1ULL << 63);
    return hash == 0 ? 1 : hash;
}

} // namespace HyruleCoop
