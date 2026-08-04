param(
    [string]$ExecutablePath,
    [string]$SeedSavePath,
    [int]$Port = 43493,
    [int]$TimeoutSeconds = 120,
    [switch]$ExpectBuildMismatch
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $repoRoot "x64\Release\soh.exe"
}
if ([string]::IsNullOrWhiteSpace($SeedSavePath)) {
    $SeedSavePath = Join-Path $repoRoot "dist\Hyrule-Coop-PoC-2-Windows\Save\file1.sav"
}

$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
$SeedSavePath = [IO.Path]::GetFullPath($SeedSavePath)
$hostDir = Join-Path $repoRoot "build-poc-mingw\runtime\host"
$clientDir = Join-Path $repoRoot "build-poc-mingw\runtime\client"
$hostExe = Join-Path $hostDir "soh.exe"
$clientExe = Join-Path $clientDir "soh.exe"
$hostReport = Join-Path $hostDir "hyrule-coop-localhost-host.tsv"
$clientReport = Join-Path $clientDir "hyrule-coop-localhost-client.tsv"

foreach ($path in @($ExecutablePath, $SeedSavePath, $hostDir, $clientDir)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required localhost test path is missing: $path"
    }
}

$allowedExecutables = @($hostExe, $clientExe)
$existing = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq "soh.exe" -and $allowedExecutables -contains $_.ExecutablePath
}
if ($existing) {
    throw "A localhost Hyrule Co-op test instance is already running."
}

Copy-Item -LiteralPath $ExecutablePath -Destination $hostExe -Force
Copy-Item -LiteralPath $ExecutablePath -Destination $clientExe -Force
Copy-Item -LiteralPath $SeedSavePath -Destination (Join-Path $hostDir "Save\file1.sav") -Force
Copy-Item -LiteralPath $SeedSavePath -Destination (Join-Path $clientDir "Save\file1.sav") -Force
[IO.File]::WriteAllText($hostReport, "")
[IO.File]::WriteAllText($clientReport, "")

if ($ExpectBuildMismatch) {
    $markerBytes = [Text.Encoding]::ASCII.GetBytes("`nHYRULE_COOP_INTENTIONAL_BUILD_MISMATCH`n")
    $clientStream = [IO.File]::Open($clientExe, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $clientStream.Write($markerBytes, 0, $markerBytes.Length)
    } finally {
        $clientStream.Dispose()
    }
}

$savedEnvironment = @{}
foreach ($name in @("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "HYRULE_COOP_TEST_PORT",
                     "HYRULE_COOP_TEST_REPORT", "HYRULE_COOP_TEST_ROLE", "HYRULE_COOP_TEST_ADDRESS",
                     "HYRULE_COOP_SAVE_DIR")) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

$hostProcess = $null
$clientProcess = $null
$outcome = "TIMEOUT"

try {
    $env:SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS = "0"
    $env:HYRULE_COOP_TEST_PORT = $Port.ToString()
    $env:HYRULE_COOP_TEST_ADDRESS = "127.0.0.1"
    $env:HYRULE_COOP_TEST_REPORT = $hostReport
    $env:HYRULE_COOP_TEST_ROLE = "host"
    $env:HYRULE_COOP_SAVE_DIR = (Join-Path $hostDir "Save")
    $hostProcess = Start-Process -FilePath $hostExe -WorkingDirectory $hostDir -WindowStyle Minimized -PassThru

    Start-Sleep -Seconds 2
    $env:HYRULE_COOP_TEST_REPORT = $clientReport
    $env:HYRULE_COOP_TEST_ROLE = "client"
    $env:HYRULE_COOP_SAVE_DIR = (Join-Path $clientDir "Save")
    $clientProcess = Start-Process -FilePath $clientExe -WorkingDirectory $clientDir -WindowStyle Minimized -PassThru

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
        $hostText = [IO.File]::ReadAllText($hostReport)
        $clientText = [IO.File]::ReadAllText($clientReport)
        if ($ExpectBuildMismatch -and
            $clientText -match "`tFAIL`tThe host and guest are running different builds") {
            $outcome = "EXPECTED_BUILD_REJECTION"
            break
        }
        if ($hostText -match "`tFAIL`t" -or $clientText -match "`tFAIL`t") {
            $outcome = "FAIL"
            break
        }
        if ($hostText -match "`tPASS`t" -and $clientText -match "`tPASS`t") {
            $outcome = "PASS"
            break
        }
        $hostProcess.Refresh()
        $clientProcess.Refresh()
        if ($hostProcess.HasExited -or $clientProcess.HasExited) {
            $outcome = "EARLY_EXIT"
            break
        }
    }
} finally {
    foreach ($process in @($hostProcess, $clientProcess)) {
        if ($null -eq $process) {
            continue
        }
        $process.Refresh()
        if (-not $process.HasExited) {
            $actual = (Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)").ExecutablePath
            if ($allowedExecutables -contains $actual) {
                Stop-Process -Id $process.Id -Force
                $null = $process.WaitForExit(5000)
            }
        }
    }
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
    }
    if ($ExpectBuildMismatch) {
        Copy-Item -LiteralPath $hostExe -Destination $clientExe -Force
    }
}

Write-Host "Host report: $hostReport"
Get-Content -LiteralPath $hostReport
Write-Host "Client report: $clientReport"
Get-Content -LiteralPath $clientReport

if ($ExpectBuildMismatch) {
    $hostBuild = [regex]::Match($hostText, '(?m)^\d+\thost\tconfigured\t.*\bbuild=(\S+)').Groups[1].Value
    $clientBuild = [regex]::Match($clientText, '(?m)^\d+\tclient\tconfigured\t.*\bbuild=(\S+)').Groups[1].Value
    if ($outcome -ne "EXPECTED_BUILD_REJECTION" -or [string]::IsNullOrWhiteSpace($hostBuild) -or
        [string]::IsNullOrWhiteSpace($clientBuild) -or $hostBuild -eq $clientBuild) {
        throw "Hyrule Co-op exact-build rejection proof failed: $outcome"
    }
    Write-Host "Hyrule Co-op exact-build rejection proof: PASS"
    Write-Host "Host fingerprint:   $hostBuild"
    Write-Host "Client fingerprint: $clientBuild"
    return
}

if ($outcome -ne "PASS") {
    throw "Hyrule Co-op localhost proof failed: $outcome"
}

$hostText = [IO.File]::ReadAllText($hostReport)
$clientText = [IO.File]::ReadAllText($clientReport)
$hostBuild = [regex]::Match($hostText, '(?m)^\d+\thost\tconfigured\t.*\bbuild=(\S+)').Groups[1].Value
$clientBuild = [regex]::Match($clientText, '(?m)^\d+\tclient\tconfigured\t.*\bbuild=(\S+)').Groups[1].Value
if ([string]::IsNullOrWhiteSpace($hostBuild) -or $hostBuild -ne $clientBuild -or
    -not $hostBuild.StartsWith('hyrule-coop-poc.3+', [StringComparison]::Ordinal)) {
    throw "Hyrule Co-op localhost proof did not report one matching exact-build fingerprint."
}

$requiredHostEvidence = @(
    'host\tdeku-baba-remote-target-visible\t',
    'host\tdeku-baba-remote-swing-visible\t',
    'host\tdeku-baba-remote-swing-rendered\t',
    'host\tgohma-remote-movement-visible\t',
    'host\tgohma-remote-target-visible\t',
    'host\tgohma-remote-swing-visible\t',
    'host\tgohma-remote-swing-rendered\t',
    'host\tgohma-host-target-acquired\t',
    'host\tgohma-host-damage-accepted\thit=1 health=1',
    'host\tgohma-host-damage-accepted\thit=2 health=0',
    'host\tgohma-host-target-released\t'
)
$requiredClientEvidence = @(
    'client\tdeku-baba-target-acquired\t',
    'client\tdeku-baba-sword-state-entered\t',
    'client\tdeku-baba-physical-collision\t',
    'client\tgohma-local-movement-verified\t',
    'client\tgohma-target-acquired\t',
    'client\tgohma-sword-state-entered\t',
    'client\tgohma-physical-sword-collision\thit=1',
    'client\tgohma-physical-sword-collision\thit=2',
    'client\tgohma-first-damage-synchronized\thealth=2 -> health=1',
    'client\tgohma-death-cleanup-visible\t',
    'client\tgohma-post-death-attack-rejected\t'
)
foreach ($pattern in $requiredHostEvidence) {
    if ($hostText -notmatch $pattern) {
        throw "Hyrule Co-op localhost proof is missing host gameplay evidence: $pattern"
    }
}
foreach ($pattern in $requiredClientEvidence) {
    if ($clientText -notmatch $pattern) {
        throw "Hyrule Co-op localhost proof is missing client gameplay evidence: $pattern"
    }
}

Write-Host "Hyrule Co-op localhost proof: PASS"
Write-Host "Exact build fingerprint: $hostBuild"
