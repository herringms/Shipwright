# Hyrule Co-op Coordination Handoff

## Purpose

This is an architectural handoff from the Sneaky-Pug multiplayer work. It is advisory and should not interrupt or
replace an in-progress protocol, transport, or scene-state change. Finish and verify the current slice first, then use
this document when choosing the next synchronization boundary.

Sneaky-Pug and Hide and Seek DX exposed a distinction that also applies to OoT and MM:

- Transport delivers messages.
- An authority publishes what the shared game must mean.
- Local reconcilers make each engine instance satisfy that declared state.

Ordered TCP solves byte delivery. It does not solve unsafe application timing, reconnect reconstruction, duplicate
side effects, or deciding which peer owns a transition.

## Reusable Model

Use a host-owned barrier coordinator above `DirectSession` for transitions that genuinely require participants to
prepare together:

```text
IDLE
  -> PREPARE
  -> WAITING_FOR_CLIENTS
  -> COMMIT
  -> ACTIVE
  -> COMPLETE / ABORT
```

An operation should be declarative and safe to apply repeatedly:

```text
operationEpoch
kind
phase
sessionEpoch
worldGeneration
manifestHash
targetScene
targetRoom
targetEntrance
participants
readyPlayers
deadline
```

Candidate barrier kinds include:

- session join and shared-save preparation
- scene or dungeon travel
- boss encounter start and completion
- reconnect snapshot application
- MM day rollover, Song of Time reset, and moon crash

Routine combat and collectible requests do not pass through this barrier. They use independent host-owned domain
ledgers and revisions, so an unrelated slow participant cannot serialize every hit or pickup. The critical rule is
that replaying any current snapshot must not grant an item twice, restart a cutscene,
respawn a defeated enemy, advance a cycle twice, or trigger another warp.

## Identity And Revisions

Keep these concepts separate:

- `sessionEpoch`: changes when the host creates a new session.
- `worldGeneration`: changes when the canonical world is reset, including an MM cycle reset.
- `operationEpoch`: changes for each coordinated transition or commit.
- Domain revisions: independently order clock, progression, scene flags, actors, and inventory.
- `streamId`: identifies a replaceable realtime stream, such as one player or one replicated actor.

Side-effect requests use `(sessionEpoch, worldGeneration, stableParticipantId, requestId)`. A reconnect token restores
the same participant identity inside the same session; a new session epoch invalidates the old token and request
ledger.

An old packet can then be rejected even when it arrives on a valid TCP connection.

## Capability Manifest

Sneaky-Pug's world-provider registry uses stable IDs, versions, namespaces, and capabilities. The equivalent here is a
game-adapter manifest negotiated before a client reports ready:

```text
oot.clock.v1
oot.sceneFlags.v1
oot.actor.dekuBaba.v1
oot.boss.gohma.v1
oot.sharedProgression.v1
mm.cycle.v1
mm.songOfTimeReset.v1
```

Build hashes remain useful, but capabilities state which synchronization contracts the build actually implements.
The host must not commit an operation until every participant supports its required capabilities.

## Domain Ownership

- Players own their movement and animation streams.
- The host owns time, enemy AI, enemy health, drops, random choices, bosses, and durable progression.
- Guests send interaction intents. Intents are requests, not committed state.
- The host validates an intent, mutates canonical state once, increments the relevant revision, and publishes a
  snapshot.
- Clients reconcile snapshots without generating another intent or gameplay side effect.

For example, the eventual item flow should be:

```text
guest pickup intent(locationId, requestId)
  -> host validates location is uncollected
  -> host commits item and scene flag once
  -> host increments progression revision
  -> host publishes canonical progression snapshot
  -> both clients reconcile it idempotently
```

A guest may need a temporary local prediction for responsiveness, but the host snapshot must be able to correct it.

## Save Boundary

Do not silently merge the guest's personal save into the host's save. Prefer a session overlay:

- The host save is canonical for the active session.
- The guest receives shared inventory and progression in memory.
- The guest's original local save remains recoverable.
- Persisting or exporting shared progress is a separate, explicit policy.

This is analogous to Sneaky-Pug's provider save namespaces: world identity and persistence ownership must be explicit.

## Applying This To Current Work

`DirectSession` should remain a transport concern. Packet framing, keyed snapshot coalescing, compatibility checks, and
socket lifecycle do not belong in the coordinator.

The current scene-flag work is a valid foundation if treated as canonical snapshots. Its eventual authority flow
should distinguish:

- guest `SceneFlagIntent`
- host validation and commit
- host `SceneFlagsSnapshot`
- client reconciliation guarded against hook echo
- reconnect or scene-entry `SnapshotRequest`

MM cycle transitions need operations, not only periodic clock assignments. The tuple of day, time, speed, weather,
generation, and pending transition describes state; an operation epoch ensures a reset or transition executes once.

Actor replication should use stable entity IDs and per-entity streams. A client must not independently run
authoritative AI and then merely have its transform corrected. Actor-specific adapters should freeze guest AI, apply
host snapshots, and convert guest attacks into validated intents.

## Current Implementation

The OoT proof now includes the coordinator, join and reconnect barriers, host-validated scene and collectible commits,
stable actor identities, one regular-enemy adapter, shared durable progression with local resource isolation, and one
boss adapter. The real-engine two-instance harness exercises those domains together and verifies stale-client replay.

The next expansion should add actor and puzzle adapters one domain at a time, retain the same authority and replay
contracts, and add each domain to the localhost proof before a remote build is packaged.

## First Architectural Proof

The architecture is demonstrated when two OoT instances can:

1. Negotiate a compatible capability manifest.
2. Prepare and acknowledge entry into the same scene.
3. Commit the scene operation once.
4. Render one host-owned Deku Baba on both clients.
5. Accept a guest attack intent and commit host-owned damage and death.
6. Commit one collectible once.
7. Share guest-originated durable inventory and equipment without sharing currency or ammunition.
8. Coordinate a Gohma encounter and commit two guest attacks and boss completion on the host.
9. Reconnect a deliberately stale guest and reconstruct progression and boss completion without replaying events.

This proof passes in two real local Shipwright instances. MM's three-day cycle and additional OoT actors, puzzles, and
bosses remain larger adapters over the same coordination model rather than separate networking systems.
