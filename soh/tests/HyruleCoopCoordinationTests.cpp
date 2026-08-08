#include "soh/Network/HyruleCoop/Coordination.h"
#include "soh/Network/HyruleCoop/DungeonRewardPolicy.h"

#include <cassert>
#include <iostream>

using namespace HyruleCoop;

static void TestCapabilities() {
    const CapabilityList unordered{ Capability::OotClock, Capability::Coordination, Capability::OotClock };
    const auto normalized = NormalizeCapabilities(unordered);
    assert(normalized.has_value());
    assert((normalized.value() == CapabilityList{ Capability::Coordination, Capability::OotClock }));
    assert(SupportsCapabilities(unordered, { Capability::Coordination }));
    assert(!SupportsCapabilities(unordered, { Capability::OotDekuBaba }));
    assert((IntersectCapabilities(unordered, { Capability::OotDekuBaba, Capability::OotClock }) ==
            CapabilityList{ Capability::OotClock }));
    assert(HashCapabilities(unordered) == HashCapabilities(normalized.value()));
    assert(!HashCapabilities(unordered).empty());
    assert(!NormalizeCapabilities({ std::string(97, 'x') }).has_value());
}

static void TestRequestLedger() {
    RequestLedger ledger;
    const SessionScope scope{ 1001, 4 };
    ledger.BeginScope(scope);

    const RequestKey request{ scope, 22, 7 };
    assert(ledger.Lookup(request) == RequestLookup::New);
    const RequestOutcome outcome{ true, 90, 11 };
    assert(ledger.Record(request, outcome));
    assert(!ledger.Record(request, { false, 91, 12 }));

    RequestOutcome replay;
    assert(ledger.Lookup(request, &replay) == RequestLookup::Replay);
    assert(replay == outcome);
    assert(ledger.Size() == 1);
    assert(ledger.Lookup({ { 1001, 5 }, 22, 8 }) == RequestLookup::StaleScope);
    assert(ledger.Lookup({ scope, 0, 8 }) == RequestLookup::Invalid);

    ledger.BeginScope({ 1002, 0 });
    assert(ledger.Size() == 0);
    assert(ledger.Lookup(request) == RequestLookup::StaleScope);
}

static void TestDomainRevisions() {
    DomainRevisionTracker revisions;
    assert(revisions.Advance(10) == 1);
    assert(revisions.Advance(10) == 2);
    assert(revisions.Advance(20) == 1);
    assert(revisions.Current(10) == 2);
    assert(revisions.Current(20) == 1);

    DomainRevisionTracker receiver;
    assert(receiver.Observe(10, 5) == RevisionDecision::Apply);
    assert(receiver.Observe(10, 5) == RevisionDecision::Replay);
    assert(receiver.Observe(10, 4) == RevisionDecision::Stale);
    assert(receiver.Observe(20, 1) == RevisionDecision::Apply);
    assert(receiver.Observe(10, 6) == RevisionDecision::Apply);
}

static void TestBarrierCoordinator() {
    BarrierCoordinator coordinator;
    BarrierState operation;
    operation.operationEpoch = 33;
    operation.scope = { 1001, 4 };
    operation.kind = BarrierKind::BossEncounter;
    operation.phase = BarrierPhase::Prepare;
    operation.manifestHash = "oot.boss.gohma.v1";
    operation.targetScene = 17;
    operation.targetRoom = 0;
    operation.deadlineTick = 500;
    operation.participants = { 2, 1 };

    assert(coordinator.Begin(operation));
    assert(!coordinator.Commit());
    assert(coordinator.WaitForParticipants());
    assert(!coordinator.MarkReady(3));
    assert(coordinator.MarkReady(2));
    assert(coordinator.MarkReady(2));
    assert(!coordinator.CanCommit());
    assert(coordinator.MarkReady(1));
    assert(coordinator.CanCommit());
    assert((coordinator.GetState().readyParticipants == std::vector<uint64_t>{ 1, 2 }));
    assert(!coordinator.IsExpired(499));
    assert(coordinator.IsExpired(500));
    assert(coordinator.Commit());
    assert(coordinator.Activate());
    assert(coordinator.Complete());
    assert(!coordinator.Abort());
    assert(coordinator.Reconcile(coordinator.GetState()));

    BarrierState stale = coordinator.GetState();
    stale.phase = BarrierPhase::Commit;
    assert(!coordinator.Reconcile(stale));

    operation.operationEpoch = 34;
    operation.participants = { 1, 1 };
    assert(!coordinator.Begin(operation));
    operation.participants = { 1, 2 };
    assert(coordinator.Begin(operation));
    assert(coordinator.Abort());
}

static void TestDungeonRewardRepair() {
    assert(ReconcileDungeonRewardFromChest(0, 1u << 0x02, 2, false) == kDungeonMapItemBit);
    assert(ReconcileDungeonRewardFromChest(0, 1u << 0x04, 2, false) == kDungeonCompassItemBit);
    assert(ReconcileDungeonRewardFromChest(kDungeonMapItemBit, 1u << 0x04, 2, false) ==
           (kDungeonMapItemBit | kDungeonCompassItemBit));
    assert(ReconcileDungeonRewardFromChest(0, 1u << 0x03, 2, true) == kDungeonMapItemBit);
    assert(ReconcileDungeonRewardFromChest(0, 1u << 0x00, 2, true) == kDungeonCompassItemBit);
    assert(ReconcileDungeonRewardFromChest(0x80, UINT32_MAX, kDungeonRewardDungeonCount, false) == 0x80);
}

int main() {
    TestCapabilities();
    TestRequestLedger();
    TestDomainRevisions();
    TestBarrierCoordinator();
    TestDungeonRewardRepair();
    assert(GenerateNonce64() != 0);
    std::cout << "HyruleCoop coordination tests passed\n";
    return 0;
}
