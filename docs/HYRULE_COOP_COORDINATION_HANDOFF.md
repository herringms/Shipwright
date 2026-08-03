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

### Live Remote Findings

- A direct herri/Tillya session connected after router port `7777` was forwarded for both TCP and UDP.
- A pot held over herri's head remained invisible to Tillya. This is a carried-world-actor replication gap, not an
  inventory synchronization failure: the pot needs canonical ownership and lifecycle state plus attachment to the
  carrying player's hand/held-above-head pose.
- The next actor pass should prove pickup from either peer, source-world removal, remote hand attachment, carried
  transform updates, drop, throw, break, and cleanup after scene change or disconnect. Add this sequence to the
  two-instance localhost harness before producing another remote package.
- Tillya also could not see a Cucco carried by herri. Cuccos used for traversal should be participant-local rather than
  one canonical shared actor, because both players may need to carry one simultaneously in areas such as Zora's Domain.
  Each client keeps its own interactive Cucco while rendering a non-colliding visual proxy attached to the remote
  player's hands. Releasing a Cucco removes only that player's remote proxy; it must not create another interactive
  Cucco on the observing client.
- A host side-slash left Deku Babas alive on the guest. The existing harness proves only its synthetic permanent-death
  path; it does not cover the vanilla pruned, temporary-death, and regrowth lifecycle. Replicate those canonical action
  phases and test vertical and horizontal cuts from both peers, including synchronized regrowth and item drops.
- Tillya could not see herri's equipped Bunny Hood. `PlayerSnapshotMessage` carries boots, shield, tunic, item action,
  and model state but omits `Player.currentMask`. Add the worn mask to remote presentation state and verify equip,
  unequip, scene transition, and reconnect independently from durable mask ownership.
- Tillya's client froze at the frog log in the Zora area while the host remained responsive and its TCP connection on
  port `7777` remained established. A client dump and log are still needed before attributing this to the frog actor or
  scene logic.
- That session used mismatched executables. The live host loaded the 3:02 PM build with SHA-256
  `84DFCFD38D4BA14B4B413DCC938E9E359AEFA329638500432C9FB9CB260EFD29`; Tillya's verified baseline contains the
  7:32 PM build with SHA-256 `9FFB6E6F5D4178EEC81DC9064932E4D95747496CB85F6E0B5C4E3D8D7E218C9B`.
  The compatibility handshake incorrectly accepted both because uncommitted builds share the same upstream commit and
  static PoC ID. Treat the freeze and any non-obvious actor result from this session as provisional until reproduced
  with identical builds. Generate the advertised compatibility fingerprint from protocol-relevant source inputs and
  make the handshake reject differing fingerprints before the next remote test.
- Zelda's Lullaby opened the Zora's Domain waterfall only for herri; Tillya had to play it independently. The gate uses
  `EVENTCHKINF_OPENED_ZORAS_DOMAIN`, while Hyrule Co-op currently hooks only scene flags and does not synchronize
  durable global event-check flags. Add host-owned world-event intents and snapshots with echo suppression. The first
  acceptance case must open the already-loaded waterfall for both players after either player performs the song, then
  remain open through scene changes and guest reconnect without replaying the song.
- Remote Links need their configured player names rendered overhead and their positions shown on the minimap. Reuse
  Shipwright's existing actor name-tag and Anchor compass-icon rendering rather than introducing another HUD system.
  Preserve both peer names during the handshake, enable both features by default, and expose separate Direct Co-op
  toggles. A marker appears only in the same scene and, in dungeons, the currently displayed room; the remote marker
  must remain visually distinct from the local player's marker. Off-scene pause-map locations can be a later extension.
- Consumable pickups need host-committed, idempotent pickup operations rather than shared resource counters. A recovery
  heart collected by either player is consumed once and applies its recovery amount to both players' local health,
  clamped independently. An ammunition pickup similarly applies its normal delta and eligibility rules to both local
  ammo pools; subsequent ammunition use remains entirely local. Rupees remain local unless a separate policy is chosen.
- Unique durable pickups already reconcile through progression snapshots, but the observing player receives no live
  notification. Add a one-time commit notification using Shipwright's notification UI, such as `Tillya found a Gold
  Skulltula Token`, for tokens, Pieces of Heart, capacity upgrades such as the Silver Scale, and other non-junk durable
  items. Notifications carry a commit ID and finder name, fire exactly once on live commit, and never replay when a
  reconnect snapshot reconstructs state. Heart Pieces and Containers should also apply their intended recovery to both
  local health pools while max-health progression remains shared.

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
