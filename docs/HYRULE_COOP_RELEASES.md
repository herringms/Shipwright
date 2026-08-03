# Hyrule Co-op Releases

## Client model

`HyruleCoop.exe` is the immutable bootstrap players retain. It delegates to the current launcher under
`%LOCALAPPDATA%\HyruleCoop\Launcher`. The launcher downloads only published GitHub Release assets, validates the
runtime ZIP and each managed file, stages a new version under `Runtime`, and atomically switches
`current-runtime.txt` after validation. A failed update retains the current runtime; a later integrity failure on the
selected runtime automatically restores the previous verified version.

Shipwright runs from the selected immutable runtime with `%LOCALAPPDATA%\HyruleCoop\UserData` as its working
directory. This keeps generated O2Rs, `shipofharkinian.json`, mods, and logs stable across releases. Saves remain in
`%LOCALAPPDATA%\HyruleCoop\Save`.

The launcher uses this fixed stable-channel URL by default:

```text
https://github.com/herringms/Shipwright/releases/latest/download/hyrule-coop-release.json
```

Release manifests and binaries must be published as immutable GitHub Release assets. Do not point clients at a
branch, raw repository file, workflow artifact, or mutable development build.

## Build assets

Create a release after the game build and localhost verification pass:

```powershell
.\scripts\windows\New-HyruleCoopRelease.ps1 `
  -ReleaseId hyrule-coop-v0.3.0 `
  -Version 0.3.0 `
  -SeedSavePath C:\path\to\private-test-save\file1.sav
```

The output directory contains:

- `Hyrule-Coop-Runtime-Windows-x64-<version>.zip`
- `HyruleCoopLauncher.exe`
- `hyrule-coop-release.json`
- `Hyrule-Coop-Bootstrap-<version>-Windows.zip`
- SHA-256 sidecars for every published asset

Upload the runtime, launcher, and fixed-name manifest to the GitHub Release whose tag matches `ReleaseId`. Give a
new player only the bootstrap ZIP. Existing players receive the same runtime through the launcher. The stable
channel release must not be marked as a draft or prerelease because GitHub's `/releases/latest` endpoint excludes
both.

## Release gate

Before publishing:

1. Build from committed Shipwright, libultraship, and Torch revisions.
2. Run the full game build, matching-build localhost proof, and exact-build rejection proof.
3. Run `scripts/windows/Test-HyruleCoopLauncher.ps1`.
4. Build the release without `-SkipLocalhostTest`.
5. Confirm the manifest URLs match the target GitHub tag.
6. Confirm the ZIP audits contain no ROM, generated O2R, save, personal config, mods, or logs.
7. Test the bootstrap in a clean folder and test an update from the previous published release.
8. Publish only after both the runtime and launcher SHA-256 values match the generated sidecars.

The manual PowerShell update package remains a recovery path. It is not the normal player workflow.

## Asset metadata

`UserData\asset-metadata.json` records the generated O2R hash, optional source-ROM hash, extractor archive hash,
release ID, runtime `assetSchema`, and the schema associated with the observed O2R hash. Increment `assetSchema`
only when a runtime actually requires O2R regeneration. An unchanged O2R then retains its earlier generated schema
and produces a warning. The launcher never copies a ROM without confirmation and never deletes an O2R silently.

## Upstream maintenance

The fork keeps `upstream` pointed at `HarbourMasters/Shipwright` and develops Hyrule Co-op on its own branch. Before
starting a new release cycle, fetch `upstream/develop`, integrate it into the co-op branch, update the pinned
libultraship and Torch forks deliberately, and repeat the complete release gate. Never move a submodule pointer to a
commit that is not already reachable from its corresponding `herringms` fork.

Do not merge upstream during a live player test or between building the runtime and publishing its manifest. A
release manifest identifies one exact executable and port archive; upstream integration starts the next release.
