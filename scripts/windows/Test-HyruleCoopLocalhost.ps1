param(
    [string]$ExecutablePath,
    [string]$SeedSavePath,
    [int]$Port = 43493,
    [int]$TimeoutSeconds = 120
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
            }
        }
    }
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
    }
}

Write-Host "Host report: $hostReport"
Get-Content -LiteralPath $hostReport
Write-Host "Client report: $clientReport"
Get-Content -LiteralPath $clientReport

if ($outcome -ne "PASS") {
    throw "Hyrule Co-op localhost proof failed: $outcome"
}

Write-Host "Hyrule Co-op localhost proof: PASS"
