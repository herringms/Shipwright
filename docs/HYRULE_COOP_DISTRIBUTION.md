# Hyrule Co-op Distribution

## Permanent installs and saves

Hyrule Co-op stores Windows saves under `%LOCALAPPDATA%\HyruleCoop\Save`, independently of the extracted runtime.
Save slots are `file*.sav`; `global.sav` contains global metadata. The first compatible launch can copy saves from a
nearby legacy portable installation into that location without deleting the originals.

The Hyrule Co-op bootstrap package is installed once. Each player supplies a supported ROM or imports an existing
locally generated `oot.o2r`. ROMs, generated O2R archives, mods, logs, and `shipofharkinian.json` live under
`%LOCALAPPDATA%\HyruleCoop\UserData`; saves live under `%LOCALAPPDATA%\HyruleCoop\Save`. They are never part of a
published runtime. Do not distribute a ROM, generated O2R archive, or another player's saves.

## Updates

Routine Hyrule Co-op releases are published as immutable GitHub Release assets. Players start `HyruleCoop.exe`; the
launcher checks the fixed release manifest, downloads the complete runtime, verifies its ZIP and every managed file,
installs it into a new AppData version directory, and switches versions only after validation. Git and a GitHub
account are not required.

Updates carry the complete managed runtime as one tested set: the executable, port archive, extractor archive,
runtime DLLs, controller database, and package instructions. This prevents a new executable from being combined with
stale support files. The updater never replaces the player's initial or expanded configuration.

The updater verifies every managed payload file and preserves these installation-local personal paths:

- `Save`
- `oot.o2r` and `oot-mq.o2r`
- `.z64`, `.n64`, and `.v64` ROMs
- `mods`
- `logs`
- `shipofharkinian.json`

The previous versioned runtime remains available for rollback. The older manual update ZIP remains a development
recovery path, not the normal player workflow.

The first launch may create or expand `shipofharkinian.json`, create `logs` and `mods`, generate `oot.o2r` from the
player's ROM, and initialize or migrate `%LOCALAPPDATA%\HyruleCoop\Save`. Players retain the original
`HyruleCoop.exe`; later releases are installed automatically into new version directories.

Player preferences are migrated independently from saves. On first setup, the launcher inspects only recognized
immediate sibling installations and selects the newest `shipofharkinian.json`. It imports that file when the AppData
configuration is missing or still byte-for-byte equal to the shipped bootstrap default. A customized AppData
configuration always wins. The launcher records the decision in `preferences-migration-v1.json`, and subsequent
runtime updates continue to preserve the AppData configuration. This retains enhancement, autosave, message, input,
controller, audio, and display choices without recursively searching unrelated folders or overwriting newer choices.

## Release gate

`scripts/windows/New-HyruleCoopRelease.ps1` constructs the GitHub runtime assets and one-time bootstrap from an
explicit allowlist. It requires an explicit private test save, runs both matching-build and mismatched-build
localhost gates, and rejects ROMs, generated O2R archives, saves, logs, mods, or personal configuration in the
resulting ZIPs.

`scripts/windows/Test-HyruleCoopLauncher.ps1` proves bootstrap delegation, conservative preference and player-data
import, runtime installation, player-data preservation, rollback, and offline launch.

`scripts/windows/New-HyruleCoopUpdate.ps1` runs the complete two-instance localhost proof before producing a patch.
It then applies the staged update to a disposable installation, verifies the complete managed runtime, and proves
that saves, ROM/O2R data, mods, logs, and personal config are unchanged. Use `-SkipLocalhostTest` only for local
packaging diagnostics, never for a player-facing build.
