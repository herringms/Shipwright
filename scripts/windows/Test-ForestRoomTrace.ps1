[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$LogPath,
    [int]$MinimumCycles = 2
)

$ErrorActionPreference = 'Stop'

if ($MinimumCycles -lt 1) {
    throw 'MinimumCycles must be at least 1.'
}

if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
    throw "Trace log was not found: $LogPath"
}

$expectedRoute = @(5, 2, 4, 6, 4, 2, 5)
$phasePattern = '\[ForestRoomTrace\] phase=(?<phase>[^ ]+) cur=(?<current>-?\d+) prev=(?<previous>-?\d+) status=(?<status>-?\d+) frames=(?<frame>\d+)'
$records = @()
$lineNumber = 0

foreach ($line in Get-Content -LiteralPath $LogPath) {
    ++$lineNumber
    $match = [regex]::Match($line, $phasePattern)
    if (-not $match.Success) {
        continue
    }
    $records += [pscustomobject]@{
        Line = $lineNumber
        Phase = $match.Groups['phase'].Value
        Current = [int]$match.Groups['current'].Value
        Previous = [int]$match.Groups['previous'].Value
        Status = [int]$match.Groups['status'].Value
        Frame = [uint32]$match.Groups['frame'].Value
    }
}

if ($records.Count -eq 0) {
    throw "No ForestRoomTrace entries were found. Confirm this diagnostic build is running and HYRULE_COOP_FOREST_ROOM_TRACE is not set to 0."
}

$completedLoads = @($records | Where-Object { $_.Phase -eq 'after-scene-commands-complete' })
if ($completedLoads.Count -eq 0) {
    throw 'The trace never reached after-scene-commands-complete. The final trace entry identifies the blocked lifecycle phase.'
}

$incompleteLoads = @()
foreach ($loadReady in @($records | Where-Object { $_.Phase -eq 'load-ready' })) {
    $nextLoad = $records | Where-Object {
        $_.Phase -eq 'load-ready' -and $_.Line -gt $loadReady.Line
    } | Select-Object -First 1
    $completion = $completedLoads | Where-Object {
        $_.Current -eq $loadReady.Current -and $_.Line -gt $loadReady.Line -and
        ($null -eq $nextLoad -or $_.Line -lt $nextLoad.Line)
    } | Select-Object -First 1
    if ($null -eq $completion) {
        $incompleteLoads += $loadReady
    }
}

if ($incompleteLoads.Count -gt 0) {
    $first = $incompleteLoads[0]
    throw "Forest room $($first.Current) reached load-ready on line $($first.Line) but never reached post-scene completion."
}

$routeCompletions = 0
for ($start = 0; $start -le $completedLoads.Count - $expectedRoute.Count; ++$start) {
    $window = @($completedLoads[$start..($start + $expectedRoute.Count - 1)] | ForEach-Object { $_.Current })
    if ((Compare-Object -ReferenceObject $expectedRoute -DifferenceObject $window -SyncWindow 0).Count -eq 0) {
        ++$routeCompletions
    }
}

if ($routeCompletions -lt $MinimumCycles) {
    $observed = ($completedLoads | ForEach-Object { $_.Current }) -join ','
    throw "Observed $routeCompletions completed Forest routes; expected at least $MinimumCycles. Completed rooms: $observed"
}

if (@($completedLoads | Where-Object { $_.Status -ne 0 }).Count -gt 0) {
    throw 'One or more completed Forest room loads retained a nonzero RoomContext status.'
}

Write-Host "PASS: $routeCompletions completed Forest Temple routes reached post-scene completion."
