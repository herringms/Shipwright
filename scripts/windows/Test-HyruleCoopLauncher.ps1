param(
    [string]$LauncherBuildPath
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($LauncherBuildPath)) {
    $LauncherBuildPath = Join-Path $repoRoot "build-poc-mingw\launcher"
    & (Join-Path $PSScriptRoot "Build-HyruleCoopLauncher.ps1") -OutputDirectory $LauncherBuildPath
}
$LauncherBuildPath = [IO.Path]::GetFullPath($LauncherBuildPath)
$bootstrapExe = Join-Path $LauncherBuildPath "HyruleCoop.exe"
$launcherExe = Join-Path $LauncherBuildPath "HyruleCoopLauncher.exe"

$testRoot = Join-Path $repoRoot "build-poc-mingw\launcher smoke"
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build-poc-mingw")) + '\'
$testRoot = [IO.Path]::GetFullPath($testRoot)
if (-not $testRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing unsafe launcher smoke-test path: $testRoot"
}
if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}

$bootstrapRoot = Join-Path $testRoot "Hyrule Co-op Bootstrap"
$bootstrapPayload = Join-Path $bootstrapRoot "_bootstrap"
$standaloneRoot = Join-Path $testRoot "Standalone Download"
$standaloneReleaseRoot = Join-Path $testRoot "Standalone Release"
$standaloneAppData = Join-Path $testRoot "Standalone App Data\HyruleCoop"
$legacyRoot = Join-Path $testRoot "Legacy Shipwright"
$appDataRoot = Join-Path $testRoot "App Data\HyruleCoop"
New-Item -ItemType Directory -Path $bootstrapPayload, $standaloneRoot, $standaloneReleaseRoot,
    $legacyRoot, $appDataRoot -Force | Out-Null
Copy-Item -LiteralPath $bootstrapExe -Destination (Join-Path $bootstrapRoot "HyruleCoop.exe")
Copy-Item -LiteralPath $bootstrapExe -Destination (Join-Path $standaloneRoot "HyruleCoop.exe")
Copy-Item -LiteralPath $launcherExe -Destination (Join-Path $bootstrapPayload "HyruleCoopLauncher.exe")
[IO.File]::WriteAllText((Join-Path $bootstrapPayload "default-shipofharkinian.json"), '{"default":true}')
[IO.Directory]::CreateDirectory((Join-Path $appDataRoot "UserData")) | Out-Null
[IO.File]::WriteAllText((Join-Path $appDataRoot "UserData\shipofharkinian.json"), '{"default":true}')

[IO.File]::WriteAllText((Join-Path $legacyRoot "soh.exe"), "legacy executable")
[IO.File]::WriteAllText((Join-Path $legacyRoot "oot.o2r"), "legacy generated archive")
[IO.File]::WriteAllText((Join-Path $legacyRoot "shipofharkinian.json"), '{"legacy":true}')
New-Item -ItemType Directory -Path (Join-Path $legacyRoot "Save"), (Join-Path $legacyRoot "mods") -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $legacyRoot "Save\global.sav"), "legacy global save")
[IO.File]::WriteAllText((Join-Path $legacyRoot "Save\file1.sav"), "legacy slot")
[IO.File]::WriteAllText((Join-Path $legacyRoot "mods\player-mod.txt"), "legacy mod")

function New-TestRelease {
    param(
        [string]$ReleaseId,
        [string]$Version,
        [string]$Content,
        [string]$Destination,
        [int]$AssetSchema = 1,
        [switch]$Bootstrap
    )

    $stage = Join-Path $testRoot ("stage-" + $ReleaseId)
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $stage "soh.exe"), "soh-" + $Content)
    [IO.File]::WriteAllText((Join-Path $stage "soh.o2r"), "port-" + $Content)
    [IO.File]::WriteAllText((Join-Path $stage "extractor-assets.zip"), "extractor-" + $Content)
    $archivePath = Join-Path $Destination ("runtime-" + $ReleaseId + ".zip")
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $archivePath -Force
    $files = @("soh.exe", "soh.o2r", "extractor-assets.zip") | ForEach-Object {
        $path = Join-Path $stage $_
        [ordered]@{
            path = $_
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
            size = (Get-Item -LiteralPath $path).Length
        }
    }
    $manifest = [ordered]@{
        schemaVersion = 1
        channel = "test"
        releaseId = $ReleaseId
        version = $Version
        publishedUtc = [DateTime]::UtcNow.ToString("o")
        compatibilityId = "hyrule-coop-poc.3"
        assetSchema = $AssetSchema
        minimumLauncherVersion = "1.0.0"
        launcherVersion = "1.1.0"
        launcherUrl = ""
        launcherSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $launcherExe).Hash
        runtimeUrl = $(if ($Bootstrap) { "" } else { $archivePath })
        runtimeArchive = $(if ($Bootstrap) { [IO.Path]::GetFileName($archivePath) } else { "" })
        runtimeSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash
        runtimeSize = (Get-Item -LiteralPath $archivePath).Length
        files = @($files)
    }
    return [pscustomobject]@{
        Manifest = $manifest
        Archive = $archivePath
    }
}

function Invoke-TestLauncher {
    param([string[]]$Arguments)
    $process = Start-Process -FilePath $launcherExe -ArgumentList $Arguments -WorkingDirectory $LauncherBuildPath `
        -PassThru -Wait
    if ($process.ExitCode -ne 0) {
        throw "Launcher process failed with exit code $($process.ExitCode)."
    }
}

$release1 = New-TestRelease -ReleaseId "launcher-smoke-1" -Version "1.0.0" -Content "v1" `
    -Destination $bootstrapPayload -Bootstrap
$release1.Manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $bootstrapPayload "hyrule-coop-release.json") -Encoding UTF8
$standaloneRelease = New-TestRelease -ReleaseId "launcher-standalone-1" -Version "1.0.0" -Content "standalone" `
    -Destination $standaloneReleaseRoot
$standaloneManifest = Join-Path $standaloneReleaseRoot "hyrule-coop-release.json"
$standaloneRelease.Manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $standaloneManifest -Encoding UTF8

$saveOnlyRoot = Join-Path $testRoot "Legacy Saves Only"
$incompleteAppData = Join-Path $testRoot "Incomplete App Data\HyruleCoop"
$customAppData = Join-Path $testRoot "Customized App Data\HyruleCoop"
New-Item -ItemType Directory -Path (Join-Path $saveOnlyRoot "Save"), $incompleteAppData,
    (Join-Path $customAppData "UserData") -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $saveOnlyRoot "soh.exe"), "legacy executable")
[IO.File]::WriteAllText((Join-Path $saveOnlyRoot "Save\global.sav"), "save without generated assets")
[IO.File]::WriteAllText((Join-Path $customAppData "UserData\shipofharkinian.json"), '{"custom":true}')

$oldRoot = $env:HYRULE_COOP_ROOT
$oldManifestUrl = $env:HYRULE_COOP_UPDATE_MANIFEST_URL
try {
    $env:HYRULE_COOP_ROOT = $standaloneAppData
    $env:HYRULE_COOP_UPDATE_MANIFEST_URL = $standaloneManifest
    $standalone = Start-Process -FilePath (Join-Path $standaloneRoot "HyruleCoop.exe") `
        -ArgumentList @("--headless", "--no-launch", "--skip-import") `
        -WorkingDirectory $standaloneRoot -PassThru
    $standalone.WaitForExit(30000) | Out-Null
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline -and
        -not (Test-Path -LiteralPath (Join-Path $standaloneAppData "current-runtime.txt"))) {
        Start-Sleep -Milliseconds 200
    }
    $standalonePointer = Join-Path $standaloneAppData "Launcher\current.txt"
    if (-not (Test-Path -LiteralPath $standalonePointer) -or
        -not (Test-Path -LiteralPath (Join-Path $standaloneAppData "UserData\shipofharkinian.json")) -or
        (Get-Content -LiteralPath (Join-Path $standaloneAppData "current-runtime.txt") -Raw).Trim() -ne
            "launcher-standalone-1") {
        throw "The standalone HyruleCoop.exe did not install its launcher, default configuration, and runtime."
    }
    $installedLauncher = [IO.Path]::GetFullPath((Get-Content -LiteralPath $standalonePointer -Raw).Trim())
    $standaloneLauncherRoot = [IO.Path]::GetFullPath((Join-Path $standaloneAppData "Launcher")) + '\'
    if (-not $installedLauncher.StartsWith($standaloneLauncherRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $installedLauncher) -or
        @(Get-ChildItem -LiteralPath $standaloneRoot -Force).Count -ne 1) {
        throw "The standalone bootstrap wrote outside AppData or modified its download folder."
    }

    $env:HYRULE_COOP_ROOT = $customAppData
    $env:HYRULE_COOP_UPDATE_MANIFEST_URL = $oldManifestUrl
    $customized = Start-Process -FilePath (Join-Path $bootstrapRoot "HyruleCoop.exe") `
        -ArgumentList @("--headless", "--no-launch", "--offline", "--import-candidate",
            ('"' + $legacyRoot + '"')) -WorkingDirectory $bootstrapRoot -PassThru
    $customized.WaitForExit(30000) | Out-Null
    $preferenceMarker = Join-Path $customAppData "preferences-migration-v1.json"
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $preferenceMarker)) {
        Start-Sleep -Milliseconds 200
    }
    $preservedPreferences = Get-Content -LiteralPath $preferenceMarker -Raw | ConvertFrom-Json
    if ((Get-Content -LiteralPath (Join-Path $customAppData "UserData\shipofharkinian.json") -Raw) -ne
        '{"custom":true}' -or $preservedPreferences.action -ne "preserved") {
        throw "Existing customized AppData preferences were overwritten during import."
    }

    $env:HYRULE_COOP_ROOT = $incompleteAppData
    $incomplete = Start-Process -FilePath (Join-Path $bootstrapRoot "HyruleCoop.exe") `
        -ArgumentList @("--headless", "--no-launch", "--offline", "--import-candidate",
            ('"' + $saveOnlyRoot + '"')) -WorkingDirectory $bootstrapRoot -PassThru
    $incomplete.WaitForExit(30000) | Out-Null
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline -and
        -not (Test-Path -LiteralPath (Join-Path $incompleteAppData "Save\global.sav"))) {
        Start-Sleep -Milliseconds 200
    }
    if (-not (Test-Path -LiteralPath (Join-Path $incompleteAppData "Save\global.sav")) -or
        (Test-Path -LiteralPath (Join-Path $incompleteAppData "migration-v1.json"))) {
        throw "Save-only import did not preserve the incomplete asset-setup state."
    }

    $env:HYRULE_COOP_ROOT = $appDataRoot
    $process = Start-Process -FilePath (Join-Path $bootstrapRoot "HyruleCoop.exe") `
        -ArgumentList @("--headless", "--no-launch", "--offline", "--import-candidate", ('"' + $legacyRoot + '"')) `
        -WorkingDirectory $bootstrapRoot -PassThru
    $process.WaitForExit(30000) | Out-Null
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath (Join-Path $appDataRoot "current-runtime.txt"))) {
        Start-Sleep -Milliseconds 200
    }

    if ((Get-Content -LiteralPath (Join-Path $appDataRoot "current-runtime.txt") -Raw).Trim() -ne "launcher-smoke-1") {
        throw "Bootstrap did not install runtime v1."
    }
    foreach ($required in @(
        "UserData\oot.o2r",
        "UserData\shipofharkinian.json",
        "UserData\mods\player-mod.txt",
        "Save\global.sav",
        "Save\file1.sav",
        "migration-v1.json",
        "preferences-migration-v1.json",
        "Launcher\current.txt"
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $appDataRoot $required))) {
            throw "Bootstrap/import smoke test is missing: $required"
        }
    }
    if ((Get-Content -LiteralPath (Join-Path $appDataRoot "UserData\shipofharkinian.json") -Raw) -ne
        '{"legacy":true}') {
        throw "Bootstrap/import did not replace the pristine default with migrated player preferences."
    }

    $protected = @(
        "UserData\oot.o2r",
        "UserData\shipofharkinian.json",
        "UserData\mods\player-mod.txt",
        "Save\global.sav",
        "Save\file1.sav"
    )
    $protectedHashes = @{}
    foreach ($relative in $protected) {
        $protectedHashes[$relative] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $appDataRoot $relative)).Hash
    }

    $release2Directory = Join-Path $testRoot "release-2"
    New-Item -ItemType Directory -Path $release2Directory -Force | Out-Null
    $release2 = New-TestRelease -ReleaseId "launcher-smoke-2" -Version "1.0.1" -Content "v2" `
        -Destination $release2Directory -AssetSchema 2
    $release2Manifest = Join-Path $release2Directory "hyrule-coop-release.json"
    $release2.Manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $release2Manifest -Encoding UTF8

    $brokenManifest = Join-Path $release2Directory "broken-release.json"
    $release2.Manifest.runtimeUrl = Join-Path $release2Directory "missing-runtime.zip"
    $release2.Manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $brokenManifest -Encoding UTF8
    Invoke-TestLauncher -Arguments @("--delegated", "--headless", "--no-launch", "--skip-import", "--root",
        ('"' + $appDataRoot + '"'), "--bootstrap-root", ('"' + $bootstrapRoot + '"'), "--manifest",
        ('"' + $brokenManifest + '"'))
    if ((Get-Content -LiteralPath (Join-Path $appDataRoot "current-runtime.txt") -Raw).Trim() -ne
        "launcher-smoke-1") {
        throw "Failed update did not retain the last verified runtime."
    }
    $release2.Manifest.runtimeUrl = $release2.Archive

    $unsafeManifest = Join-Path $release2Directory "unsafe-launcher-release.json"
    $release2.Manifest.launcherVersion = "9.0.0\..\..\escaped"
    $release2.Manifest.launcherUrl = $launcherExe
    $release2.Manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $unsafeManifest -Encoding UTF8
    Invoke-TestLauncher -Arguments @("--delegated", "--headless", "--no-launch", "--skip-import", "--root",
        ('"' + $appDataRoot + '"'), "--bootstrap-root", ('"' + $bootstrapRoot + '"'), "--manifest",
        ('"' + $unsafeManifest + '"'))
    if ((Get-Content -LiteralPath (Join-Path $appDataRoot "current-runtime.txt") -Raw).Trim() -ne
        "launcher-smoke-1" -or (Test-Path -LiteralPath (Join-Path $appDataRoot "escaped"))) {
        throw "Unsafe launcher version metadata escaped its versioned launcher directory."
    }

    $release2.Manifest.launcherVersion = "1.1.0"
    $release2.Manifest.launcherUrl = ""
    $release2.Manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $release2Manifest -Encoding UTF8

    Invoke-TestLauncher -Arguments @("--delegated", "--headless", "--no-launch", "--skip-import", "--root",
        ('"' + $appDataRoot + '"'), "--bootstrap-root", ('"' + $bootstrapRoot + '"'), "--manifest",
        ('"' + $release2Manifest + '"'))
    $currentRuntime = (Get-Content -LiteralPath (Join-Path $appDataRoot "current-runtime.txt") -Raw).Trim()
    $previousRuntime = (Get-Content -LiteralPath (Join-Path $appDataRoot "previous-runtime.txt") -Raw).Trim()
    if ($currentRuntime -ne "launcher-smoke-2" -or $previousRuntime -ne "launcher-smoke-1") {
        throw "Launcher did not atomically advance runtime pointers."
    }
    foreach ($relative in $protected) {
        $after = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $appDataRoot $relative)).Hash
        if ($after -ne $protectedHashes[$relative]) {
            throw "Runtime update modified player-owned data: $relative"
        }
    }
    $assetMetadata = Get-Content -LiteralPath (Join-Path $appDataRoot "UserData\asset-metadata.json") -Raw |
        ConvertFrom-Json
    if (-not $assetMetadata.regenerationRequired -or $assetMetadata.assetSchema -ne 2 -or
        $assetMetadata.generatedAssetSchema -ne 1) {
        throw "Launcher did not retain the O2R schema and report required regeneration."
    }

    [IO.File]::AppendAllText((Join-Path $appDataRoot "Runtime\launcher-smoke-2\soh.exe"), "corrupt")
    Invoke-TestLauncher -Arguments @("--delegated", "--headless", "--no-launch", "--skip-import", "--root",
        ('"' + $appDataRoot + '"'), "--bootstrap-root", ('"' + $bootstrapRoot + '"'), "--offline")
    $currentRuntime = (Get-Content -LiteralPath (Join-Path $appDataRoot "current-runtime.txt") -Raw).Trim()
    if ($currentRuntime -ne "launcher-smoke-1" -or
        (Get-Content -LiteralPath (Join-Path $appDataRoot "failed-runtime.txt") -Raw).Trim() -ne
        "launcher-smoke-2") {
        throw "Launcher did not automatically restore the previous verified runtime."
    }

    Invoke-TestLauncher -Arguments @("--delegated", "--headless", "--no-launch", "--skip-import", "--root",
        ('"' + $appDataRoot + '"'), "--bootstrap-root", ('"' + $bootstrapRoot + '"'), "--manifest",
        ('"' + $release2Manifest + '"'))
    if ((Get-Content -LiteralPath (Join-Path $appDataRoot "current-runtime.txt") -Raw).Trim() -ne
        "launcher-smoke-2") {
        throw "Launcher did not reinstall the failed runtime from a verified release."
    }

    Invoke-TestLauncher -Arguments @("--delegated", "--headless", "--no-launch", "--skip-import", "--root",
        ('"' + $appDataRoot + '"'), "--bootstrap-root", ('"' + $bootstrapRoot + '"'), "--offline", "--rollback")
    $currentRuntime = (Get-Content -LiteralPath (Join-Path $appDataRoot "current-runtime.txt") -Raw).Trim()
    if ($currentRuntime -ne "launcher-smoke-1") {
        throw "Launcher rollback smoke test failed."
    }

    Invoke-TestLauncher -Arguments @("--delegated", "--headless", "--no-launch", "--skip-import", "--root",
        ('"' + $appDataRoot + '"'), "--bootstrap-root", ('"' + $bootstrapRoot + '"'), "--offline")
} finally {
    $env:HYRULE_COOP_ROOT = $oldRoot
    $env:HYRULE_COOP_UPDATE_MANIFEST_URL = $oldManifestUrl
}

Write-Host "Hyrule Co-op launcher smoke test passed."
Write-Host "Verified single-EXE setup, bootstrap delegation, conservative import, update, preservation, rollback, and offline launch."
