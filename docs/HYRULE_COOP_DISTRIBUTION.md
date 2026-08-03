# Hyrule Co-op Distribution

## Permanent installs and saves

Hyrule Co-op stores Windows saves under `%LOCALAPPDATA%\HyruleCoop\Save`, independently of the extracted runtime.
Save slots are `file*.sav`; `global.sav` contains global metadata. The first compatible launch can copy saves from a
nearby legacy portable installation into that location without deleting the originals.

The baseline Hyrule Co-op package is installed once. Each player supplies a supported ROM and generates `oot.o2r`
locally. ROMs, generated O2R archives, mods, logs, and `shipofharkinian.json` remain installation-local in this proof
of concept. Do not distribute a ROM, generated O2R archive, or another player's saves.

## Updates

Routine Hyrule Co-op releases use a small update ZIP instead of replacing the installation. Extract the update ZIP
into the existing Hyrule Co-op folder, close Shipwright, and run `Apply Hyrule Co-op Update.cmd`.

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

It stores replaced managed files under `_hyrule_backup/<patch-id>` for rollback.

The first launch may create or expand `shipofharkinian.json`, create `logs` and `mods`, generate `oot.o2r` from the
player's ROM, and initialize or migrate `%LOCALAPPDATA%\HyruleCoop\Save`. A newer baseline must not be extracted over
that installation; apply an update ZIP so installation-local files remain intact.

## Release gate

`scripts/windows/New-HyruleCoopBaseline.ps1` constructs the full package from an explicit allowlist in a clean staging
directory. It runs localhost, proves first-run O2R extraction using a locally supplied ROM, and rejects ROMs, O2R
archives, saves, logs, mods, or personal configuration in the resulting ZIP.

`scripts/windows/New-HyruleCoopUpdate.ps1` runs the complete two-instance localhost proof before producing a patch.
It then applies the staged update to a disposable installation, verifies the complete managed runtime, and proves
that saves, ROM/O2R data, mods, logs, and personal config are unchanged. Use `-SkipLocalhostTest` only for local
packaging diagnostics, never for a player-facing build.
