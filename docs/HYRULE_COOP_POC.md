# Hyrule Co-op Proof of Concept

## Product rule

The player chooses **Host Game** or **Join Game**. Version checks, authority, snapshots, replay handling, and state
ownership belong to the implementation rather than setup instructions.

## Architectural proof

The current proof implements the complete first vertical path for two Ocarina of Time peers:

1. Negotiate an exact game/build and namespaced capability manifest.
2. Assign a stable participant identity and reconnect token within a host-generated session epoch.
3. Coordinate scene entry through a host-owned prepare/ready/commit/active/complete barrier.
4. Render the named remote player, including reliable equipment and worn-mask presentation, and one host-owned Deku
   Baba.
5. Convert a guest hit into an idempotent attack intent validated by the host for session, scene, entity, and range.
6. Commit Deku Baba damage and death on the host and publish the canonical actor snapshot.
7. Convert one durable collectible flag into a location-keyed, idempotent host commit.
8. Share durable inventory and equipment from guest intents while keeping rupees, ammunition, current health, current
   magic, and bottle contents local.
9. Coordinate entry into Gohma's arena, run Gohma only on the host, accept two guest attacks, and publish boss death
   and the room-clear progression flag.
10. Reconstruct actor death, collected locations, shared progression, boss completion, player state, and clock after
    reconnect without replaying any side effect.

An engine-independent architectural test executes that entire sequence, including reconnect reconstruction.

## Authority and ordering

- Players own their movement and animation streams.
- The host owns time, enemy and boss AI, enemy and boss health, durable and live temporary scene flags, and progression
  commits.
- Guests send intents; they do not publish committed gameplay state.
- Request identity is `(sessionEpoch, worldGeneration, participantId, requestId)`.
- Scene flags, progression, and actors have independent revisions or keyed streams.
- Coordinated transitions use a barrier operation epoch and an explicit target scene, optional room, and entrance.
- TCP is the reliable control and durability lane. Authenticated UDP carries replaceable realtime snapshots,
  bounded idempotent attack-intent retries, and acknowledged realtime outcomes. Important UDP outcomes are retried,
  deduplicated, and fall back to TCP after a bounded deadline. Transport ordering does not replace authority,
  idempotency, or readiness.

## Host-owned campaign save

The save loaded by the host is the canonical campaign. Normal manual saves, autosaves, and exit saves persist the
host-authoritative scene flags and shared progression to the host's ordinary save slot. A guest may join from any
compatible save; joining applies the host's age, durable progression, world state, and coordinated location in memory.
The guest's complete pre-join save is retained, guest save writes are sanitized back to that snapshot, and disconnect
restores it in memory. If the pre-join snapshot is unavailable, the write is suppressed instead of allowing host session
state into the guest's personal save. A guest therefore participates in the host campaign without merging or
overwriting their own.

Shared progression includes durable inventory slots, child and adult trade items, bottle ownership, equipment,
upgrades, quest items, dungeon items and keys, health capacity, magic ownership, double defense, Biggoron's Sword
ownership, and Gold Skulltula tokens. Rupees, ammunition counts, current health and magic, bottle contents, and local
button/equipment choices remain player-owned.

## Protocol

`DirectSession` is a persistent one-host/one-guest hybrid transport. Framed TCP carries reliable control and durable
state. Authenticated UDP on the same numeric port carries high-frequency player, clock, and actor snapshots, bounded
attack-intent retries, and acknowledged realtime outcomes, with TCP fallback when realtime delivery is unavailable or
an acknowledgement deadline expires. It supports keyed replacement, freshness rejection, duplicate suppression,
malformed-peer rejection, explicit rejection delivery, and reconnecting a replacement guest without destroying
canonical host state. Remote Link presentation uses a 100 ms interpolation buffer with bounded extrapolation so WAN
jitter does not directly become visible position and joint stutter. The Direct Co-op menu exposes RTT, arrival jitter,
queue depth, application delay, retransmissions, duplicates, and fallback counts for live diagnosis.

The shared OoT/MM protocol includes:

- `Hello` / `HelloAck` with build, capability, session, participant, and resume identity
- scoped clock and MM cycle snapshots
- player and actor snapshots
- scene-flag and collectible intents plus canonical snapshots
- guest attack intents
- shared-progression item/key intents plus canonical snapshots
- barrier state and readiness messages

OoT and MM compile the same byte-identical protocol and coordination sources.

## Majora's Mask foundation

The MM branch now uses the same capability, session, resume, scope, and barrier contracts. Its host publishes the full
cycle tuple:

```text
day + time + time speed + weather + world/cycle generation + pending transition
```

Reconnect and host cycle resets use coordinated barriers with explicit entrances. MM remote-player rendering, combat,
progression adapters, and Song of Time policy are not represented as completed features.

## Verification

Portable tests cover packet framing/codecs, capabilities, request replay, independent revisions, barrier transitions,
the full architectural proof, entity identity, malformed peers, rejection delivery, and reconnect transport behavior.
Both game managers pass standalone syntax checks against their port headers.

The OoT branch produces a complete MinGW Windows application. An environment-gated localhost harness boots two
isolated save copies, connects them over TCP and UDP, coordinates scene transitions, renders both Links, kills a
host-owned Deku Baba, Keese, and Stalchild from physical guest collisions, synchronizes temporary dungeon state,
commits a durable collectible, shares guest-originated progression without sharing local resources, defeats host-owned
Gohma from two physical guest attacks, and reconnects a deliberately stale guest. The same proof can deterministically
inject recurring UDP loss, bounded data delay, and
pair reordering. The commit gate repeats the full proof while dropping every fifth UDP data packet, delaying surviving
packets by 25 ms, and reversing each surviving packet pair. Both instances verify canonical reconstruction and report
`PASS`. The harness is dormant unless `HYRULE_COOP_TEST_ROLE` is explicitly set.

Verified host and client executables must have identical generated fingerprints and negotiate the explicit
`hyrule-coop-poc.3` compatibility ID with protocol version 9 in addition to Shipwright's upstream commit. Release
tooling records the exact executable and package hashes for each published build.
The PoC build pins its own OneDrive directory for offline availability instead of rejecting the path by name. No
installed Ship of Harkinian or 2Ship files are modified by this branch.

## Deliberately outside this proof

- More than two players and host migration
- Automatic reconnect UI
- A generated compatibility fingerprint that rejects different locally built protocol/runtime revisions even when
  they share the same upstream Git commit
- Authenticated pairing, encryption, NAT traversal, and invite services
- Generic synchronization for every actor, puzzle, cutscene, and boss beyond the specialized Deku Baba, Stalchild,
  and Gohma adapters plus the explicitly allowlisted Keese baseline
- Carried world actors and their player attachments, including canonical shared pots and participant-local traversal
  Cuccos whose remote carry proxies do not replace either player's interactive Cucco
- Complete Deku Baba hit reactions, temporary pruning, drops, and regrowth beyond the synthetic permanent-death proof
- Host-owned durable world-event flags, beginning with `EVENTCHKINF_OPENED_ZORAS_DOMAIN`, with immediate loaded-actor
  reconciliation and reconnect replay
- Same-scene minimap markers using existing Shipwright rendering hooks
- Host-committed shared recovery and ammunition pickup effects while current health and ammunition balances remain
  participant-local
- One-time finder notifications for unique durable pickups, without replay during snapshot or reconnect reconciliation
- Bottle ownership independent of peer-local slot positions, with local ordinary contents and notified host-owned quest
  content transitions such as Ruto's Letter
- Broader host-save coverage for progression domains not yet represented by a co-op adapter
- MM player, enemy, quest, and Song of Time behavior adapters
- Player-count difficulty scaling
