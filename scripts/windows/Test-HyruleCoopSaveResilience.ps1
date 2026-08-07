param(
    [string]$ExecutablePath,
    [string]$RuntimeTemplatePath,
    [int]$StartupSeconds = 6
)

$ErrorActionPreference = "Stop"

function Read-SharedText([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                              [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $reader = [IO.StreamReader]::new($stream)
        try {
            return $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $repoRoot "x64\Release\soh.exe"
}
if ([string]::IsNullOrWhiteSpace($RuntimeTemplatePath)) {
    $RuntimeTemplatePath = Join-Path $repoRoot "build-poc-mingw\runtime\host"
}

$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
$RuntimeTemplatePath = [IO.Path]::GetFullPath($RuntimeTemplatePath)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build-poc-mingw\runtime"))
$testRoot = [IO.Path]::GetFullPath((Join-Path $allowedRoot "save-resilience"))
if (-not $testRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean a save resilience path outside the runtime workspace: $testRoot"
}
foreach ($path in @($ExecutablePath, $RuntimeTemplatePath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required save resilience test path is missing: $path"
    }
}

if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Get-ChildItem -LiteralPath $RuntimeTemplatePath -Force | Where-Object {
    $_.Name -notin @("Save", "logs") -and $_.Name -notlike "hyrule-coop-*.tsv"
} | Copy-Item -Destination $testRoot -Recurse -Force
Copy-Item -LiteralPath $ExecutablePath -Destination (Join-Path $testRoot "soh.exe") -Force

$saveDirectory = Join-Path $testRoot "MalformedSave"
New-Item -ItemType Directory -Path $saveDirectory -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $saveDirectory "global.sav"), "{invalid global json")
[IO.File]::WriteAllText((Join-Path $saveDirectory "file1.sav"), "{invalid slot json")

$savedEnvironment = @{}
foreach ($name in @("HYRULE_COOP_SAVE_DIR", "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "HYRULE_COOP_TEST_ROLE",
                     "HYRULE_COOP_TEST_PORT", "HYRULE_COOP_TEST_ADDRESS", "HYRULE_COOP_TEST_REPORT",
                     "HYRULE_COOP_TEST_REQUIRE_DRAW")) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

$process = $null
try {
    $env:HYRULE_COOP_SAVE_DIR = $saveDirectory
    $env:SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS = "0"
    $env:HYRULE_COOP_TEST_ROLE = "host"
    $env:HYRULE_COOP_TEST_PORT = "43529"
    $env:HYRULE_COOP_TEST_ADDRESS = "127.0.0.1"
    $env:HYRULE_COOP_TEST_REPORT = (Join-Path $testRoot "save-resilience.tsv")
    $env:HYRULE_COOP_TEST_REQUIRE_DRAW = "0"
    $process = Start-Process -FilePath (Join-Path $testRoot "soh.exe") -WorkingDirectory $testRoot `
                             -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds $StartupSeconds
    $process.Refresh()
    if ($process.HasExited) {
        throw "Malformed-save startup exited unexpectedly with code $($process.ExitCode)."
    }

    $logPath = Join-Path $testRoot "logs\Ship of Harkinian.log"
    if (-not (Test-Path -LiteralPath $logPath)) {
        throw "Malformed-save startup did not create a log."
    }
    $log = Read-SharedText $logPath
    $slotRecovered = $log -match "Unable to read save metadata" -or
                     $log -match "Unable to initialize save metadata" -or
                     $log -match "Error loading save file"
    if ($log -notmatch "Unable to load global save" -or -not $slotRecovered) {
        throw "Malformed-save startup did not emit both controlled recovery diagnostics."
    }
    if ($log -match "CrashHandler\.cpp" -or $log -match "\[critical\].*Exception") {
        throw "Malformed-save startup reached the crash handler instead of recovering."
    }
} finally {
    if ($null -ne $process) {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
    }
}

Write-Host "Hyrule Co-op malformed local save startup: PASS"
Write-Host "Fixture: $saveDirectory"
