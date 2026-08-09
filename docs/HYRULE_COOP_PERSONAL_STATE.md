# Hyrule Co-op Personal State

## Ownership boundary

The host save owns the campaign:

- story, quest, and world flags
- dungeon state, bosses, enemies, and puzzles
- shared equipment and major progression
- Epona unlock, race, and summon eligibility
- unique world-item leases

Each stable player identity owns campaign-specific personal state:

- rupees and current health
- ammunition and other consumable counts
- bottle contents, independently from shared bottle ownership
- equipped items, mask, boots, shield, and tunic
- personal horse placement used only when that player is present

Shipwright preferences are profile-wide rather than campaign-specific.

## Storage

```text
%LOCALAPPDATA%\HyruleCoop\
  identity.json
  Profiles\<player-id>\
    preferences-v1.json
    Campaigns\<campaign-id>\personal-v1.json
  Campaigns\<campaign-id>\
    campaign-v1.json
```

`identity.json` contains a random 128-bit identifier. It survives updates and
reconnects but is not an authentication credential.

Every sidecar is versioned, written through a temporary file, atomically
replaced, and backed up. A malformed sidecar is quarantined instead of blocking
startup.

## Session reconciliation

The join handshake exchanges player ID, campaign ID, schema version, personal
revision, and content hash. Bootstrap order is:

1. Host campaign snapshot.
2. The host's accepted personal snapshot for that player.
3. Active world-item leases.
4. Scene and actor state.
5. Ready barrier completion.

Personal mutations are reliable, revisioned, idempotent messages. During a
reconnect, the host's live session copy wins over a stale local sidecar. A guest
Shipwright save remains protected from host campaign data.

## Migration

On first use of a campaign, import rupees, ammo, bottle contents, health, and
loadout from the selected local save. Record its hash and import timestamp so a
later launch cannot repeatedly restore stale values such as an old rupee count.

Preferences such as autosave, owl-message defaults, D-pad C buttons, graphics,
and audio are migrated once into `preferences-v1.json` and retained across
runtime updates.

## Unique temporary items

Ruto's Letter is a host-owned lease:

```text
world -> held by player -> presented to King Zora -> consumed
                   |
                   +-> holder absent -> returned to world
```

The host ledger determines existence. A bottle slot is only the current visual
container. Once King Zora has moved, the letter is consumed and must not return.

## Actor distinctions

- Traversal Cuccos are participant-local interactive actors with remote visual
  proxies, allowing both players to carry one.
- Epona's unlock and race state are host-owned campaign progression. Each
  participant gets a local interactive riding instance; peers render a
  non-interactive proxy from reliable mount/spawn edges and a realtime pose
  stream. Remote Link is attached to that proxy rather than rendered at the
  rider position without a horse.

## Required verification

- Different personal resources remain distinct while campaign progress agrees.
- Personal state survives disconnect, reconnect, and full application restart.
- Guest Shipwright save data remains unchanged by a host campaign.
- Ruto's Letter returns when its holder is absent and remains consumed after
  King Zora moves.
- Both players can carry traversal Cuccos.
- Both players can see Epona, mount and dismount, transition scenes, and reconnect
  without rider-height or off-map placement errors.
