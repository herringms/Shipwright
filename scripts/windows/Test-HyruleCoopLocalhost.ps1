param(
    [string]$ExecutablePath,
    [string]$SeedSavePath,
    [int]$Port = 43493,
    [int]$TimeoutSeconds = 120,
    [int]$UdpDropEvery = 0,
    [int]$UdpDelayMs = 0,
    [switch]$UdpReorderPairs,
    [ValidateSet("None", "Host", "Client")]
    [string]$RenderRole = "None",
    [switch]$ExpectBuildMismatch
)

$ErrorActionPreference = "Stop"

function Set-TestWindowPosition([string] $path, [int] $x) {
    $config = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    $config.Window.Width = 640
    $config.Window.Height = 480
    $config.Window.PositionX = $x
    $config.Window.PositionY = 40
    $config.Window.Backend.Id = 1
    $config.Window.Backend.Name = "DirectX 11"
    if ($null -eq $config.CVars.gSettings.PSObject.Properties['VsyncEnabled']) {
        $config.CVars.gSettings | Add-Member -NotePropertyName VsyncEnabled -NotePropertyValue 0
    } else {
        $config.CVars.gSettings.VsyncEnabled = 0
    }
    $config | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $path -Encoding UTF8
}

function Read-SharedText([string] $path) {
    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
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
if ([string]::IsNullOrWhiteSpace($SeedSavePath)) {
    $SeedSavePath = Join-Path $repoRoot "build-poc-mingw\release-seed\file1.sav"
}
if ($UdpDropEvery -lt 0 -or $UdpDelayMs -lt 0 -or $UdpDelayMs -gt 1000) {
    throw "UDP impairment values must be non-negative and delay must not exceed 1000 ms."
}

$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
$SeedSavePath = [IO.Path]::GetFullPath($SeedSavePath)
$hostDir = Join-Path $repoRoot "build-poc-mingw\runtime\host"
$clientDir = Join-Path $repoRoot "build-poc-mingw\runtime\client"
$hostExe = Join-Path $hostDir "soh.exe"
$clientExe = Join-Path $clientDir "soh.exe"
$hostReport = Join-Path $hostDir "hyrule-coop-localhost-host.tsv"
$clientReport = Join-Path $clientDir "hyrule-coop-localhost-client.tsv"
$hostConfig = Join-Path $hostDir "shipofharkinian.json"
$clientConfig = Join-Path $clientDir "shipofharkinian.json"

foreach ($path in @($ExecutablePath, $SeedSavePath, $hostDir, $clientDir, $hostConfig, $clientConfig)) {
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
Set-TestWindowPosition $hostConfig 10
Set-TestWindowPosition $clientConfig 700
$guestSavePath = Join-Path $clientDir "Save\file1.sav"
$guestSave = Get-Content -LiteralPath $guestSavePath -Raw | ConvertFrom-Json
$hostSeedAge = [int]$guestSave.sections.base.data.linkAge
$guestStartingAge = if ($hostSeedAge -eq 0) { 1 } else { 0 }
$guestStartingRupees = 333
$guestSave.sections.base.data.linkAge = $guestStartingAge
$guestSave.sections.base.data.rupees = $guestStartingRupees
$guestSave | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $guestSavePath -Encoding UTF8
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
                     "HYRULE_COOP_SAVE_DIR", "HYRULE_COOP_TEST_UDP_DROP_EVERY",
                     "HYRULE_COOP_TEST_UDP_DELAY_MS", "HYRULE_COOP_TEST_UDP_REORDER_PAIRS",
                     "HYRULE_COOP_TEST_REQUIRE_DRAW")) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

$hostProcess = $null
$clientProcess = $null
$outcome = "TIMEOUT"

try {
    $env:SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS = "0"
    $env:HYRULE_COOP_TEST_PORT = $Port.ToString()
    $env:HYRULE_COOP_TEST_ADDRESS = "127.0.0.1"
    $env:HYRULE_COOP_TEST_UDP_DROP_EVERY = $UdpDropEvery.ToString()
    $env:HYRULE_COOP_TEST_UDP_DELAY_MS = $UdpDelayMs.ToString()
    $env:HYRULE_COOP_TEST_UDP_REORDER_PAIRS = if ($UdpReorderPairs.IsPresent) { "1" } else { "0" }
    $env:HYRULE_COOP_TEST_REPORT = $hostReport
    $env:HYRULE_COOP_TEST_ROLE = "host"
    $env:HYRULE_COOP_SAVE_DIR = (Join-Path $hostDir "Save")
    $env:HYRULE_COOP_TEST_REQUIRE_DRAW = if ($RenderRole -eq "Host") { "1" } else { "0" }
    $hostProcess = Start-Process -FilePath $hostExe -WorkingDirectory $hostDir -WindowStyle Normal -PassThru

    Start-Sleep -Seconds 2
    $env:HYRULE_COOP_TEST_REPORT = $clientReport
    $env:HYRULE_COOP_TEST_ROLE = "client"
    $env:HYRULE_COOP_SAVE_DIR = (Join-Path $clientDir "Save")
    $env:HYRULE_COOP_TEST_REQUIRE_DRAW = if ($RenderRole -eq "Client") { "1" } else { "0" }
    $clientProcess = Start-Process -FilePath $clientExe -WorkingDirectory $clientDir -WindowStyle Normal -PassThru

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
        $hostText = Read-SharedText $hostReport
        $clientText = Read-SharedText $clientReport
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

$hostText = Read-SharedText $hostReport
$clientText = Read-SharedText $clientReport
$hostBuild = [regex]::Match($hostText, '(?m)^\d+\thost\tconfigured\t.*\bbuild=(\S+)').Groups[1].Value
$clientBuild = [regex]::Match($clientText, '(?m)^\d+\tclient\tconfigured\t.*\bbuild=(\S+)').Groups[1].Value
if ([string]::IsNullOrWhiteSpace($hostBuild) -or $hostBuild -ne $clientBuild -or
    -not $hostBuild.StartsWith('hyrule-coop-poc.3+', [StringComparison]::Ordinal)) {
    throw "Hyrule Co-op localhost proof did not report one matching exact-build fingerprint."
}

$requiredHostEvidence = @(
    'host\tbunny-hood-client-applied\tremote Player state carries Bunny Hood',
    'host\tminimap-remote-position-read\tminimap draw consumed the same-scene remote player coordinates',
    'host\tdeku-baba-remote-target-visible\t',
    'host\tdeku-baba-remote-swing-visible\t',
    'host\tdeku-baba-remote-swing-state-applied\t',
    'host\tgeneric-enemy-client-target-visible\t',
    'host\tgeneric-enemy-client-swing-visible\t',
    'host\tgeneric-enemy-client-swing-state-applied\t',
    'host\tgeneric-enemy-host-damage-accepted\thealth=2 -> health=1',
    'host\tgeneric-enemy-host-target-acquired\t',
    'host\tgeneric-enemy-host-sword-state-entered\t',
    'host\tgeneric-enemy-host-physical-collision\thit=1',
    'host\tgeneric-enemy-target-released\t',
    'host\tgeneric-enemy-dead-synchronized\t',
    'host\tstalchild-host-target-agreed\thost selected the guest and disabled its own attack collider',
    'host\tstalchild-bystander-identity-ready\t',
    'host\tstalchild-host-guest-hit-accepted\thit=1 health=1',
    'host\tstalchild-host-guest-hit-accepted\thit=2 health=0',
    'host\tstalchild-native-death-started\t',
    'host\tstalchild-native-drop-spawned\t',
    'host\tstalchild-native-death-complete\t',
    'host\tstalchild-bystander-survived\t',
    'host\tstalchild-dead-synchronized\t',
    'host\tstalchild-dawn-retired\thost population removed on both peers',
    'host\tderived-progression-repaired\tCucco and Ruto bottles, King Zora hand-in, Silver Scale, and Song of Time recovered from durable flags',
    'host\tdungeon-rewards-map-reconciled\tJabu fixed-layout map chest restored canonical map ownership',
    'host\tdungeon-rewards-synchronized\tboth Jabu map and compass bits converged through the host snapshot',
    'host\ttemporary-scene-state-synchronized\ttemporary switch, collectible, and room clear replayed across peers',
    'host\tgohma-remote-movement-visible\t',
    'host\tgohma-remote-target-visible\t',
    'host\tgohma-remote-swing-visible\t',
    'host\tgohma-remote-swing-state-applied\t',
    'host\tgohma-host-target-acquired\t',
    'host\tgohma-host-damage-accepted\thit=1 health=1',
    'host\tgohma-host-damage-accepted\thit=2 health=0',
    'host\tgohma-death-presentation-started\t',
    'host\tgohma-host-target-released\t',
    'host\thost-campaign-save-complete\t'
)
$requiredClientEvidence = @(
    'client\tbunny-hood-host-applied\tremote Player state carries Bunny Hood',
    'client\tminimap-remote-position-read\tminimap draw consumed the same-scene remote player coordinates',
    'client\tdeku-baba-target-acquired\t',
    'client\tdeku-baba-sword-state-entered\t',
    'client\tdeku-baba-physical-collision\t',
    'client\tgeneric-enemy-client-target-acquired\t',
    'client\tgeneric-enemy-client-sword-state-entered\t',
    'client\tgeneric-enemy-client-physical-collision\thit=1',
    'client\tgeneric-enemy-client-damage-synchronized\thealth=2 -> health=1',
    'client\tgeneric-enemy-host-target-visible\t',
    'client\tgeneric-enemy-host-swing-visible\t',
    'client\tgeneric-enemy-host-swing-state-applied\t',
    'client\tgeneric-enemy-target-released\t',
    'client\tgeneric-enemy-dead-synchronized\t',
    'client\tstalchild-client-target-agreed\tguest received the host-selected guest target before enemy contact',
    'client\tstalchild-client-visual-emerged\thost emergence offset and shadow scale applied to the guest replica',
    'client\tstalchild-client-authoritative-attack-applied\tsequence=',
    'client\tstalchild-client-damaged-by-enemy\t',
    'client\tstalchild-client-first-swing-input\t',
    'client\tstalchild-client-physical-collision\thit=1',
    'client\tstalchild-client-first-sword-recovered\t',
    'client\tstalchild-client-second-sword-state-entered\t',
    'client\tstalchild-client-physical-collision\thit=2',
    'client\tstalchild-native-death-visible\t',
    'client\tstalchild-bystander-survived\t',
    'client\tstalchild-dead-synchronized\t',
    'client\tstalchild-dawn-retired\thost population removed on both peers',
    'client\tderived-progression-repaired\tCucco and Ruto bottles, King Zora hand-in, Silver Scale, and Song of Time recovered from durable flags',
    'client\tdungeon-rewards-compass-intent-sent\tguest Item_Give\(ITEM_COMPASS\) used mapIndex=Jabu',
    'client\tdungeon-rewards-synchronized\tboth Jabu map and compass bits converged through the host snapshot',
    'client\ttemporary-scene-state-synchronized\ttemporary switch, collectible, and room clear replayed across peers',
    'client\tgohma-local-movement-verified\t',
    'client\tgohma-target-acquired\t',
    'client\tgohma-sword-state-entered\t',
    'client\tgohma-physical-sword-collision\thit=1',
    'client\tgohma-physical-sword-collision\thit=2',
    'client\tgohma-first-damage-synchronized\thealth=2 -> health=1',
    'client\tgohma-death-presentation-started\t',
    'client\tgohma-death-cleanup-visible\t',
    'client\tgohma-post-death-attack-rejected\t',
    'client\tguest-save-protection-complete\t'
)
if ($RenderRole -eq "Host") {
    $requiredHostEvidence += @(
        'host\tbunny-hood-client-draw-completed\tPlayer_Draw completed with Bunny Hood state',
        'host\tdeku-baba-remote-swing-draw-completed\tPlayer_Draw completed with the guest sword state',
        'host\tgeneric-enemy-client-swing-draw-completed\tPlayer_Draw completed with the remote sword state',
        'host\tgohma-remote-swing-draw-completed\tPlayer_Draw completed with the guest sword state'
    )
}
if ($RenderRole -eq "Client") {
    $requiredClientEvidence += @(
        'client\tbunny-hood-host-draw-completed\tPlayer_Draw completed with Bunny Hood state',
        'client\tgeneric-enemy-host-swing-draw-completed\tPlayer_Draw completed with the remote sword state'
    )
}
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

function Assert-EvidenceOrder {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string[]]$Evidence,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $previousIndex = -1
    foreach ($needle in $Evidence) {
        $index = $Text.IndexOf($needle, $previousIndex + 1, [StringComparison]::Ordinal)
        if ($index -lt 0) {
            throw "$Label is missing ordered evidence: $needle"
        }
        if ($index -le $previousIndex) {
            throw "$Label evidence occurred out of order: $needle"
        }
        $previousIndex = $index
    }
}

$hostStalchildHits = [regex]::Matches(
    $hostText,
    '(?m)^\d+\thost\tstalchild-host-guest-hit-accepted\thit=([12]) health=([01])\r?$'
)
if ($hostStalchildHits.Count -ne 2 -or
    $hostStalchildHits[0].Groups[1].Value -ne '1' -or $hostStalchildHits[0].Groups[2].Value -ne '1' -or
    $hostStalchildHits[1].Groups[1].Value -ne '2' -or $hostStalchildHits[1].Groups[2].Value -ne '0') {
    throw "Host Stalchild proof did not contain exactly two ordered, authoritative damage outcomes."
}
$clientStalchildHits = [regex]::Matches(
    $clientText,
    '(?m)^\d+\tclient\tstalchild-client-physical-collision\thit=([12])\r?$'
)
if ($clientStalchildHits.Count -ne 2 -or $clientStalchildHits[0].Groups[1].Value -ne '1' -or
    $clientStalchildHits[1].Groups[1].Value -ne '2' -or
    $clientText -match '(?m)^\d+\tclient\tstalchild-client-physical-collision\thit=3\r?$') {
    throw "Guest Stalchild proof did not contain exactly one collider contact per scripted sword swing."
}

Assert-EvidenceOrder -Text $hostText -Label 'Host Stalchild authority proof' -Evidence @(
    "`thost`tstalchild-host-target-agreed`t",
    "`thost`tstalchild-host-guest-hit-accepted`thit=1 health=1",
    "`thost`tstalchild-host-guest-hit-accepted`thit=2 health=0",
    "`thost`tstalchild-native-death-started`t",
    "`thost`tstalchild-native-drop-spawned`t",
    "`thost`tstalchild-native-death-complete`t",
    "`thost`tstalchild-bystander-survived`t",
    "`thost`tstalchild-dead-synchronized`t",
    "`thost`tstalchild-dawn-retired`t"
)
Assert-EvidenceOrder -Text $clientText -Label 'Guest Stalchild incoming-attack proof' -Evidence @(
    "`tclient`tstalchild-client-target-agreed`t",
    "`tclient`tstalchild-client-authoritative-attack-applied`t",
    "`tclient`tstalchild-client-damaged-by-enemy`t"
)
Assert-EvidenceOrder -Text $clientText -Label 'Guest Stalchild sword proof' -Evidence @(
    "`tclient`tstalchild-client-first-swing-input`t",
    "`tclient`tstalchild-client-physical-collision`thit=1",
    "`tclient`tstalchild-client-first-sword-recovered`t",
    "`tclient`tstalchild-client-second-sword-state-entered`t",
    "`tclient`tstalchild-client-physical-collision`thit=2",
    "`tclient`tstalchild-native-death-visible`t",
    "`tclient`tstalchild-bystander-survived`t",
    "`tclient`tstalchild-dead-synchronized`t",
    "`tclient`tstalchild-dawn-retired`t"
)

$hostSave = Get-Content -LiteralPath (Join-Path $hostDir "Save\file1.sav") -Raw | ConvertFrom-Json
$savedGuest = Get-Content -LiteralPath $guestSavePath -Raw | ConvertFrom-Json
$hostData = $hostSave.sections.base.data
$guestData = $savedGuest.sections.base.data
# Shipwright reconciles these transient/current-load fields when the test flips
# the guest to the opposite Link age. They are unrelated to co-op campaign state.
$guestSave.sections.base.data.entranceIndex = $guestData.entranceIndex
$guestSave.sections.base.data.magicLevel = $guestData.magicLevel
$guestSave.sections.base.data.equips.buttonItems[0] = $guestData.equips.buttonItems[0]
$guestSave.sections.base.data.equips.equipment = $guestData.equips.equipment
$guestSave.sections.base.data.inventory.equipment = $guestData.inventory.equipment
$normalizedGuestData = $guestSave.sections.base.data | ConvertTo-Json -Depth 100 -Compress
$savedGuestData = $guestData | ConvertTo-Json -Depth 100 -Compress
$hostResourceMatch = [regex]::Match(
    $hostText,
    '(?m)^\d+\thost\thost-awaiting-client-reconnect\trupees=(\d+) arrows=(\d+) magic=(\d+)\r?$'
)
if (-not $hostResourceMatch.Success) {
    throw "Host reconnect checkpoint did not report its local resource baseline."
}
$expectedHostRupees = [int]$hostResourceMatch.Groups[1].Value
$expectedHostArrows = [int]$hostResourceMatch.Groups[2].Value
$expectedHostMagic = [int]$hostResourceMatch.Groups[3].Value
$forestScene = 0x55
$collectibleMask = [uint32](1 -shl 0x1E)
$switchMask = [Convert]::ToUInt32("80000000", 16)
if ([int]$hostData.inventory.items[9] -eq 255 -or [int]$hostData.inventory.items[11] -eq 255 -or
    [int]$hostData.inventory.items[18] -eq 255 -or [int]$hostData.inventory.items[19] -eq 255 -or
    (([uint32]$hostData.inventory.upgrades -band 0xE00) -lt 0x200) -or
    (([uint32]$hostData.inventory.questItems -band (1 -shl 16)) -eq 0) -or
    (([uint16]$hostData.itemGetInf[0] -band 0x1000) -eq 0) -or
    (([uint16]$hostData.eventChkInf[3] -band 0x030A) -ne 0x030A) -or
    (([uint16]$hostData.eventChkInf[10] -band 0x0200) -eq 0) -or
    (([uint32]$hostData.sceneFlags[$forestScene].collect -band $collectibleMask) -eq 0) -or
    (([uint32]$hostData.sceneFlags[$forestScene].swch -band $switchMask) -eq 0) -or
    [int]$hostData.rupees -ne $expectedHostRupees -or
    [int]$hostData.inventory.ammo[3] -ne $expectedHostArrows -or
    [int]$hostData.magic -ne $expectedHostMagic -or [int]$hostData.linkAge -ne $hostSeedAge) {
    throw "Host save did not persist the canonical co-op campaign and host-local resources."
}
if ([int]$guestData.rupees -ne $guestStartingRupees -or [int]$guestData.linkAge -ne $guestStartingAge -or
    $savedGuestData -cne $normalizedGuestData) {
    throw "Guest save was overwritten by the host campaign or guest session resources."
}
Write-Host "Host-owned campaign persistence: PASS"
Write-Host "Guest personal save protection: PASS"

Write-Host "Hyrule Co-op two-instance deterministic gameplay proof: PASS"
Write-Host "Combat coverage: specialized Deku Baba, Stalchild, and Gohma adapters plus ordinary Keese bidirectional damage/death"
Write-Host "Exact build fingerprint: $hostBuild"
if ($RenderRole -ne "None") {
    Write-Host "Role-specific Player_Draw proof: $RenderRole"
}
if ($UdpDropEvery -ne 0 -or $UdpDelayMs -ne 0 -or $UdpReorderPairs) {
    Write-Host "UDP impairment: drop every $UdpDropEvery, delay $UdpDelayMs ms, reorder pairs $($UdpReorderPairs.IsPresent)"
}
