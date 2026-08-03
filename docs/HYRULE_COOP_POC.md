# Hyrule Co-op Proof of Concept

## Product rule

The player chooses **Host Game** or **Join Game**. Version checks, authority, snapshots, replay handling, and state
ownership belong to the implementation rather than setup instructions.

## Architectural proof

The current proof implements the complete first vertical path for two Ocarina of Time peers:

1. Negotiate an exact game/build and namespaced capability manifest.
2. Assign a stable participant identity and reconnect token within a host-generated session epoch.
3. Coordinate scene entry through a host-owned prepare/ready/commit/active/complete barrier.
4. Render the remote player and one host-owned Deku Baba.
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
- The host owns time, enemy and boss AI, enemy and boss health, durable scene flags, and progression commits.
- Guests send intents; they do not publish committed gameplay state.
- Request identity is `(sessionEpoch, worldGeneration, participantId, requestId)`.
- Scene flags, progression, and actors have independent revisions or keyed streams.
- Coordinated transitions use a barrier operation epoch and an explicit target scene, optional room, and entrance.
- TCP remains transport only. Ordered bytes do not replace authority, idempotency, or readiness.

## Session save overlay

Both peers capture their original durable scene flags and shared progression when direct co-op starts. The game uses
canonical host state in memory during the session, but normal save copies are sanitized back to each player's original
values. Disconnect also restores those original values in memory. Exporting shared progress is intentionally not
implicit.

Shared progression includes durable inventory slots, child and adult trade items, bottle ownership, equipment,
upgrades, quest items, dungeon items and keys, health capacity, magic ownership, double defense, Biggoron's Sword
ownership, and Gold Skulltula tokens. Rupees, ammunition counts, current health and magic, bottle contents, and local
button/equipment choices remain player-owned.

## Protocol

`DirectSession` is a persistent one-host/one-guest framed TCP transport. It supports keyed replacement of realtime
snapshots, malformed-peer rejection, explicit rejection delivery, and reconnecting a replacement guest without
destroying canonical host state.

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
isolated save copies, connects them over TCP, coordinates two scene transitions, renders both Links, kills a host-owned
Deku Baba from guest attack intents, commits a durable collectible, shares a guest-originated Hookshot and Kokiri
Sword without sharing rupees or arrows, defeats host-owned Gohma from two guest attacks, and reconnects a deliberately
stale guest. Both instances verify canonical reconstruction and report `PASS`. The harness is dormant unless
`HYRULE_COOP_TEST_ROLE` is explicitly set.

The verified host and client executables have identical SHA-256 hashes and negotiate the explicit
`hyrule-coop-poc.2` compatibility ID with protocol version 3 in addition to Shipwright's upstream commit. The verified
Windows executable SHA-256 is `362364A1216A21AC432B51CEC42DA2F9822DC985006CE369D4BEA9672E3F971B`.
The PoC build pins its own OneDrive directory for offline availability instead of rejecting the path by name. No
installed Ship of Harkinian or 2Ship files are modified by this branch.

## Deliberately outside this proof

- More than two players and host migration
- Automatic reconnect UI
- A generated compatibility fingerprint that rejects different locally built protocol/runtime revisions even when
  they share the same upstream Git commit
- Authenticated pairing, encryption, NAT traversal, and invite services
- Generic synchronization for every actor, puzzle, cutscene, and boss beyond Deku Baba and Gohma
- Carried world actors and their player attachments, including pots and Cuccos held overhead, dropped, thrown, or
  released without duplicate world and attachment visuals
- Complete Deku Baba hit reactions, temporary pruning, drops, and regrowth beyond the synthetic permanent-death proof
- Remote presentation of currently worn masks independently from shared ownership of those masks
- An explicit shared-progress export policy
- MM player, enemy, quest, and Song of Time behavior adapters
- Player-count difficulty scaling
