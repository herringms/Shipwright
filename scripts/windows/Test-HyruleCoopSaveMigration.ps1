param(
    [string]$ExecutablePath,
    [string]$SeedSavePath,
    [string]$RuntimeTemplatePath,
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $repoRoot "x64\Release\soh.exe"
}
if ([string]::IsNullOrWhiteSpace($SeedSavePath)) {
    $SeedSavePath = Join-Path $repoRoot "dist\Hyrule-Coop-PoC-2-Windows\Save\file1.sav"
}
if ([string]::IsNullOrWhiteSpace($RuntimeTemplatePath)) {
    $RuntimeTemplatePath = Join-Path $repoRoot "build-poc-mingw\runtime\host"
}

$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
$SeedSavePath = [IO.Path]::GetFullPath($SeedSavePath)
$RuntimeTemplatePath = [IO.Path]::GetFullPath($RuntimeTemplatePath)
$testRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build-poc-mingw\runtime\save-migration"))
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build-poc-mingw\runtime"))

foreach ($path in @($ExecutablePath, $SeedSavePath, $RuntimeTemplatePath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required save migration test path is missing: $path"
    }
}
if (-not $testRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean a save migration test path outside the runtime workspace: $testRoot"
}

if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}

$versionA = Join-Path $testRoot "Games\soh-v1"
$versionB = Join-Path $testRoot "Games\soh-v2"
$fakeAppData = Join-Path $testRoot "AppData\Local"
$canonicalSave = Join-Path $fakeAppData "HyruleCoop\Save\file1.sav"
$marker = Join-Path $fakeAppData "HyruleCoop\save-storage-v1.txt"
$versionALog = Join-Path $versionA "logs\Ship of Harkinian.log"
$versionBLog = Join-Path $versionB "logs\Ship of Harkinian.log"

foreach ($version in @($versionA, $versionB)) {
    New-Item -ItemType Directory -Path $version -Force | Out-Null
    Get-ChildItem -LiteralPath $RuntimeTemplatePath -Force | Where-Object {
        $_.Name -notin @("Save", "logs") -and $_.Name -notlike "hyrule-coop-*.tsv"
    } | Copy-Item -Destination $version -Recurse -Force
    Copy-Item -LiteralPath $ExecutablePath -Destination (Join-Path $version "soh.exe") -Force
    New-Item -ItemType Directory -Path (Join-Path $version "Save") -Force | Out-Null
}

$sourceSave = Join-Path $versionA "Save\file1.sav"
$conflictingSave = Join-Path $versionB "Save\file1.sav"
Copy-Item -LiteralPath $SeedSavePath -Destination $sourceSave -Force
[IO.File]::WriteAllText($conflictingSave, "this portable save must not overwrite AppData")
$expectedHash = (Get-FileHash -LiteralPath $sourceSave -Algorithm SHA256).Hash

$savedEnvironment = @{}
foreach ($name in @("LOCALAPPDATA", "HYRULE_COOP_SAVE_DIR", "HYRULE_COOP_PORTABLE_SAVES",
                     "HYRULE_COOP_TEST_ROLE", "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS")) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

function Invoke-MigrationLaunch([string]$installPath, [scriptblock]$ready) {
    $exe = Join-Path $installPath "soh.exe"
    $process = Start-Process -FilePath $exe -WorkingDirectory $installPath -WindowStyle Minimized -PassThru
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (& $ready) {
                return
            }
            $process.Refresh()
            if ($process.HasExited) {
                throw "Save migration test executable exited before initialization: $installPath"
            }
        }
        throw "Timed out waiting for save migration initialization: $installPath"
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            $actual = (Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)").ExecutablePath
            if ([string]::Equals($actual, $exe, [StringComparison]::OrdinalIgnoreCase)) {
                Stop-Process -Id $process.Id -Force
            }
        }
    }
}

try {
    $env:LOCALAPPDATA = $fakeAppData
    Remove-Item Env:HYRULE_COOP_SAVE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:HYRULE_COOP_PORTABLE_SAVES -ErrorAction SilentlyContinue
    Remove-Item Env:HYRULE_COOP_TEST_ROLE -ErrorAction SilentlyContinue
    $env:SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS = "0"

    Invoke-MigrationLaunch $versionA {
        (Test-Path -LiteralPath $canonicalSave) -and (Test-Path -LiteralPath $marker) -and
        (Test-Path -LiteralPath $versionALog)
    }

    $migratedHash = (Get-FileHash -LiteralPath $canonicalSave -Algorithm SHA256).Hash
    $originalHash = (Get-FileHash -LiteralPath $sourceSave -Algorithm SHA256).Hash
    if ($migratedHash -ne $expectedHash -or $originalHash -ne $expectedHash) {
        throw "Initial migration did not produce a byte-identical non-destructive copy."
    }

    Invoke-MigrationLaunch $versionB { Test-Path -LiteralPath $versionBLog }

    $afterSecondLaunchHash = (Get-FileHash -LiteralPath $canonicalSave -Algorithm SHA256).Hash
    if ($afterSecondLaunchHash -ne $expectedHash) {
        throw "A later launch folder overwrote the canonical AppData save."
    }
    if ([IO.File]::ReadAllText($conflictingSave) -ne "this portable save must not overwrite AppData") {
        throw "The conflicting portable save was unexpectedly modified."
    }

    # A clean new version folder should discover a valid save in a sibling old
    # version without crawling the drive.
    $discoveryAppData = Join-Path $testRoot "DiscoveryAppData\Local"
    $discoveredSave = Join-Path $discoveryAppData "HyruleCoop\Save\file1.sav"
    $discoveryMarker = Join-Path $discoveryAppData "HyruleCoop\save-storage-v1.txt"
    Remove-Item -LiteralPath $conflictingSave -Force
    Remove-Item -LiteralPath (Split-Path $versionBLog -Parent) -Recurse -Force -ErrorAction SilentlyContinue

    # A newer-looking nested installation is intentionally out of scope. Discovery may inspect only immediate
    # siblings of the current install, never descendants of another sibling.
    $nestedDecoy = Join-Path $testRoot "Games\Archive\soh-old"
    New-Item -ItemType Directory -Path (Join-Path $nestedDecoy "Save") -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $versionA "soh.exe") -Destination (Join-Path $nestedDecoy "soh.exe") -Force
    $assetMarker = @("oot.o2r", "oot-mq.o2r") | ForEach-Object { Join-Path $versionA $_ } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($null -ne $assetMarker) {
        Copy-Item -LiteralPath $assetMarker -Destination (Join-Path $nestedDecoy (Split-Path $assetMarker -Leaf)) -Force
    } else {
        New-Item -ItemType Directory -Path (Join-Path $nestedDecoy "logs") -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $nestedDecoy "logs\Ship of Harkinian.log"), "recognized nested decoy")
    }
    $nestedSave = Join-Path $nestedDecoy "Save\file1.sav"
    [IO.File]::WriteAllText($nestedSave, "nested save must not be discovered")
    [IO.File]::SetLastWriteTimeUtc($nestedSave, [DateTime]::UtcNow.AddMinutes(5))

    $env:LOCALAPPDATA = $discoveryAppData
    Invoke-MigrationLaunch $versionB {
        (Test-Path -LiteralPath $discoveredSave) -and (Test-Path -LiteralPath $discoveryMarker) -and
        (Test-Path -LiteralPath $versionBLog)
    }
    if ((Get-FileHash -LiteralPath $discoveredSave -Algorithm SHA256).Hash -ne $expectedHash) {
        throw "Bounded sibling-version discovery did not select the existing save."
    }
} finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
    }
}

Write-Host "Hyrule Co-op save migration: PASS"
Write-Host "Source retained: $sourceSave"
Write-Host "Canonical AppData copy: $canonicalSave"
Write-Host "SHA256: $expectedHash"
