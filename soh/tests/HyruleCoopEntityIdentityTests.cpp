#include "soh/Network/HyruleCoop/EntityIdentity.h"

#include <cassert>
#include <iostream>

using namespace HyruleCoop;

int main() {
    StaticEntitySignature first{ 2, 4, 1, 0x55, -3, { 100, -20, 300 }, { 0, 0x4000, 0 } };
    StaticEntitySignature same = first;
    assert(BuildStaticEntityId(first) == BuildStaticEntityId(same));
    assert(BuildStaticEntityId(first) != 0);
    assert((BuildStaticEntityId(first) & (1ULL << 63)) == 0);

    same.room = 2;
    assert(BuildStaticEntityId(first) != BuildStaticEntityId(same));
    same = first;
    same.homePosition[2]++;
    assert(BuildStaticEntityId(first) != BuildStaticEntityId(same));
    same = first;
    same.worldGeneration++;
    assert(BuildStaticEntityId(first) != BuildStaticEntityId(same));

    std::cout << "HyruleCoop entity identity tests passed\n";
    return 0;
}
