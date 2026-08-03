#include "soh/Network/HyruleCoop/Coordination.h"

#include <cassert>
#include <iostream>
#include <unordered_set>

using namespace HyruleCoop;

namespace {

struct SimulatedPlayerState {
    bool hookshot = false;
    bool kokiriSword = false;
    int rupees = 0;
    int arrows = 0;
};

void ApplyCanonicalProgression(SimulatedPlayerState& player, const SimulatedPlayerState& canonical) {
    player.hookshot = canonical.hookshot;
    player.kokiriSword = canonical.kokiriSword;
}

} // namespace

int main() {
    const CapabilityList hostCapabilities = NormalizeCapabilities(
        { Capability::Coordination, Capability::RequestLedger, Capability::OotClock, Capability::OotPlayer,
          Capability::OotSceneFlags, Capability::OotDekuBaba, Capability::OotGuestAttack,
          Capability::OotCollectible, Capability::OotSharedProgression, Capability::OotGohma })
                                                    .value();
    const CapabilityList guestCapabilities = hostCapabilities;
    assert(SupportsCapabilities(guestCapabilities, hostCapabilities));
    const CapabilityList negotiated = IntersectCapabilities(hostCapabilities, guestCapabilities);
    assert(negotiated == hostCapabilities);

    const SessionScope scope{ 0x12345678, 4 };
    BarrierState sceneEntry;
    sceneEntry.operationEpoch = 1;
    sceneEntry.scope = scope;
    sceneEntry.kind = BarrierKind::SceneTransition;
    sceneEntry.phase = BarrierPhase::Prepare;
    sceneEntry.manifestHash = HashCapabilities(negotiated);
    sceneEntry.targetScene = 1;
    sceneEntry.targetRoom = 0;
    sceneEntry.targetEntrance = 0x0201;
    sceneEntry.deadlineTick = 600;
    sceneEntry.participants = { 1, 2 };

    BarrierCoordinator hostBarrier;
    BarrierCoordinator guestBarrier;
    assert(hostBarrier.Begin(sceneEntry));
    assert(hostBarrier.WaitForParticipants());
    assert(hostBarrier.MarkReady(1));
    assert(guestBarrier.Reconcile(hostBarrier.GetState()));
    assert(hostBarrier.MarkReady(2));
    assert(hostBarrier.Commit());
    assert(hostBarrier.Activate());
    assert(hostBarrier.Complete());
    assert(guestBarrier.Reconcile(hostBarrier.GetState()));

    RequestLedger ledger;
    ledger.BeginScope(scope);
    int enemyHealth = 1;
    bool enemyAlive = true;
    const RequestKey attack{ scope, 2, 1 };
    assert(ledger.Lookup(attack) == RequestLookup::New);
    --enemyHealth;
    enemyAlive = enemyHealth > 0;
    assert(ledger.Record(attack, { true, 0xBABA, 1 }));
    assert(!enemyAlive);
    assert(ledger.Lookup(attack) == RequestLookup::Replay);
    assert(enemyHealth == 0);

    std::unordered_set<uint64_t> collectedLocations;
    uint64_t progressionRevision = 0;
    const uint64_t locationId = 0x0103000C;
    const RequestKey pickup{ scope, 2, 2 };
    assert(ledger.Lookup(pickup) == RequestLookup::New);
    assert(collectedLocations.insert(locationId).second);
    ++progressionRevision;
    assert(ledger.Record(pickup, { true, locationId, progressionRevision }));
    assert(ledger.Lookup(pickup) == RequestLookup::Replay);
    assert(collectedLocations.size() == 1);
    assert(progressionRevision == 1);

    SimulatedPlayerState hostPlayer{ false, false, 111, 7 };
    SimulatedPlayerState guestPlayer{ false, false, 222, 23 };
    const RequestKey sharedItem{ scope, 2, 3 };
    assert(ledger.Lookup(sharedItem) == RequestLookup::New);
    hostPlayer.hookshot = true;
    hostPlayer.kokiriSword = true;
    ++progressionRevision;
    assert(ledger.Record(sharedItem, { true, 2, progressionRevision }));
    ApplyCanonicalProgression(guestPlayer, hostPlayer);
    assert(hostPlayer.hookshot && hostPlayer.kokiriSword);
    assert(guestPlayer.hookshot && guestPlayer.kokiriSword);
    assert(hostPlayer.rupees == 111 && hostPlayer.arrows == 7);
    assert(guestPlayer.rupees == 222 && guestPlayer.arrows == 23);
    assert(ledger.Lookup(sharedItem) == RequestLookup::Replay);

    BarrierState bossEntry = sceneEntry;
    bossEntry.operationEpoch = 2;
    bossEntry.kind = BarrierKind::BossEncounter;
    bossEntry.targetScene = 17;
    bossEntry.targetRoom = 1;
    bossEntry.targetEntrance = 0x040F;
    BarrierCoordinator bossBarrier;
    assert(bossBarrier.Begin(bossEntry));
    assert(bossBarrier.WaitForParticipants());
    assert(bossBarrier.MarkReady(1));
    assert(bossBarrier.MarkReady(2));
    assert(bossBarrier.Commit());
    assert(bossBarrier.Activate());
    assert(bossBarrier.Complete());

    int gohmaHealth = 2;
    bool gohmaClear = false;
    for (uint64_t requestId : { 4ULL, 5ULL }) {
        const RequestKey attackGohma{ scope, 2, requestId };
        assert(ledger.Lookup(attackGohma) == RequestLookup::New);
        --gohmaHealth;
        gohmaClear = gohmaHealth == 0;
        assert(ledger.Record(attackGohma, { true, static_cast<uint64_t>(gohmaHealth), gohmaClear }));
        assert(ledger.Lookup(attackGohma) == RequestLookup::Replay);
    }
    assert(gohmaHealth == 0);
    assert(gohmaClear);

    BarrierState reconnect = sceneEntry;
    reconnect.operationEpoch = 3;
    reconnect.kind = BarrierKind::ReconnectSnapshot;
    reconnect.phase = BarrierPhase::Prepare;
    BarrierCoordinator reconnectBarrier;
    assert(reconnectBarrier.Begin(reconnect));
    assert(reconnectBarrier.WaitForParticipants());
    assert(reconnectBarrier.MarkReady(1));
    assert(reconnectBarrier.MarkReady(2));
    assert(reconnectBarrier.Commit());

    // Reconstructing canonical snapshots changes no authoritative domain revision.
    const bool reconstructedEnemyAlive = enemyAlive;
    const std::unordered_set<uint64_t> reconstructedLocations = collectedLocations;
    guestPlayer.hookshot = false;
    guestPlayer.kokiriSword = false;
    ApplyCanonicalProgression(guestPlayer, hostPlayer);
    assert(!reconstructedEnemyAlive);
    assert(reconstructedLocations.contains(locationId));
    assert(enemyHealth == 0);
    assert(progressionRevision == 2);
    assert(guestPlayer.hookshot && guestPlayer.kokiriSword);
    assert(guestPlayer.rupees == 222 && guestPlayer.arrows == 23);
    assert(gohmaClear);

    assert(reconnectBarrier.Activate());
    assert(reconnectBarrier.Complete());
    std::cout << "HyruleCoop architectural proof tests passed\n";
    return 0;
}
