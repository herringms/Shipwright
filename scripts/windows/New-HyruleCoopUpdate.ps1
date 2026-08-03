param(
    [string]$PatchId = "poc2-ui-1",
    [string]$ExecutablePath,
    [string]$OutputPath,
    [switch]$SkipLocalhostTest
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $repoRoot "x64\Release\soh.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot ("dist\Hyrule-Coop-Update-" + $PatchId + ".zip")
}
$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
    throw "Built Hyrule Co-op executable is missing: $ExecutablePath"
}

$releaseRoot = Split-Path -Parent $ExecutablePath
$runtimeRoot = Join-Path $repoRoot "build-poc-mingw\runtime\host"
$managedFiles = [ordered]@{
    "soh.exe" = $ExecutablePath
    "soh.o2r" = (Join-Path $repoRoot "soh.o2r")
    "extractor-assets.zip" = (Join-Path $releaseRoot "extractor-assets.zip")
    "README.txt" = (Join-Path $repoRoot "docs\WINDOWS_POC_PACKAGE.txt")
    "gamecontrollerdb.txt" = (Join-Path $runtimeRoot "gamecontrollerdb.txt")
    "glew32.dll" = (Join-Path $runtimeRoot "glew32.dll")
    "libbz2-1.dll" = (Join-Path $runtimeRoot "libbz2-1.dll")
    "libgcc_s_seh-1.dll" = (Join-Path $runtimeRoot "libgcc_s_seh-1.dll")
    "liblzma-5.dll" = (Join-Path $runtimeRoot "liblzma-5.dll")
    "libogg-0.dll" = (Join-Path $runtimeRoot "libogg-0.dll")
    "libopus-0.dll" = (Join-Path $runtimeRoot "libopus-0.dll")
    "libopusfile-0.dll" = (Join-Path $runtimeRoot "libopusfile-0.dll")
    "libspdlog-1.17.dll" = (Join-Path $runtimeRoot "libspdlog-1.17.dll")
    "libstdc++-6.dll" = (Join-Path $runtimeRoot "libstdc++-6.dll")
    "libvorbis-0.dll" = (Join-Path $runtimeRoot "libvorbis-0.dll")
    "libvorbisfile-3.dll" = (Join-Path $runtimeRoot "libvorbisfile-3.dll")
    "libwinpthread-1.dll" = (Join-Path $runtimeRoot "libwinpthread-1.dll")
    "libzip.dll" = (Join-Path $runtimeRoot "libzip.dll")
    "libzstd.dll" = (Join-Path $runtimeRoot "libzstd.dll")
    "SDL2.dll" = (Join-Path $runtimeRoot "SDL2.dll")
    "zlib1.dll" = (Join-Path $runtimeRoot "zlib1.dll")
}
foreach ($entry in $managedFiles.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Managed update input is missing: $($entry.Value)"
    }
}

if (-not $SkipLocalhostTest) {
    & (Join-Path $PSScriptRoot "Test-HyruleCoopLocalhost.ps1") -ExecutablePath $ExecutablePath
}

$stageRoot = Join-Path $repoRoot ("build-poc-mingw\update-package\" + $PatchId)
$allowedStageRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build-poc-mingw\update-package"))
$resolvedStage = [IO.Path]::GetFullPath($stageRoot)
if (-not $resolvedStage.StartsWith($allowedStageRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing unsafe update staging path: $resolvedStage"
}
if (Test-Path -LiteralPath $resolvedStage) {
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}

$updateRoot = Join-Path $resolvedStage "_hyrule_update"
$payloadRoot = Join-Path $updateRoot "payload"
New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
foreach ($entry in $managedFiles.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $payloadRoot $entry.Key)
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "update\Apply-HyruleCoopUpdate.ps1") -Destination $updateRoot
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "update\Apply Hyrule Co-op Update.cmd") -Destination $resolvedStage

$manifestFiles = @($managedFiles.GetEnumerator() | ForEach-Object {
    @{
        path = $_.Key
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.Value).Hash
    }
})
@{
    schemaVersion = 1
    patchId = $PatchId
    compatibilityId = "hyrule-coop-poc.2"
    files = $manifestFiles
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $updateRoot "manifest.json") -Encoding UTF8

$smokeRoot = Join-Path $repoRoot ("build-poc-mingw\update-smoke\" + $PatchId)
$allowedSmokeRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build-poc-mingw\update-smoke"))
$resolvedSmoke = [IO.Path]::GetFullPath($smokeRoot)
if (-not $resolvedSmoke.StartsWith($allowedSmokeRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing unsafe update smoke-test path: $resolvedSmoke"
}
if (Test-Path -LiteralPath $resolvedSmoke) {
    Remove-Item -LiteralPath $resolvedSmoke -Recurse -Force
}
Copy-Item -LiteralPath $resolvedStage -Destination $resolvedSmoke -Recurse
[IO.File]::WriteAllText((Join-Path $resolvedSmoke "soh.exe"), "old executable")
$protectedFixtures = [ordered]@{
    "Save\file1.sav" = "preserve-save"
    "Save\global.sav" = "preserve-global-save"
    "oot.o2r" = "preserve-o2r"
    "oot-mq.o2r" = "preserve-mq-o2r"
    "player-rom.z64" = "preserve-rom"
    "shipofharkinian.json" = "preserve-config"
    "mods\player-mod.txt" = "preserve-mod"
    "logs\player.log" = "preserve-log"
}
foreach ($entry in $protectedFixtures.GetEnumerator()) {
    $fixturePath = Join-Path $resolvedSmoke $entry.Key
    New-Item -ItemType Directory -Path (Split-Path -Parent $fixturePath) -Force | Out-Null
    [IO.File]::WriteAllText($fixturePath, $entry.Value)
}
$protectedBefore = @{}
foreach ($relative in $protectedFixtures.Keys) {
    $protectedBefore[$relative] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $resolvedSmoke $relative)).Hash
}
& (Join-Path $resolvedSmoke "_hyrule_update\Apply-HyruleCoopUpdate.ps1") -InstallDir $resolvedSmoke
foreach ($entry in $managedFiles.GetEnumerator()) {
    $expectedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.Value).Hash
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $resolvedSmoke $entry.Key)).Hash
    if ($actualHash -ne $expectedHash) {
        throw "Update smoke test did not install the expected managed file: $($entry.Key)"
    }
}
foreach ($relative in $protectedBefore.Keys) {
    $after = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $resolvedSmoke $relative)).Hash
    if ($after -ne $protectedBefore[$relative]) {
        throw "Update smoke test modified protected file: $relative"
    }
}

Compress-Archive -Path (Join-Path $resolvedStage "*") -DestinationPath $OutputPath -CompressionLevel Optimal -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($OutputPath)
try {
    $badEntries = @($archive.Entries | Where-Object {
        $_.Name -in @("oot.o2r", "oot-mq.o2r", "global.sav") -or
        [IO.Path]::GetExtension($_.Name).ToLowerInvariant() -in @(".z64", ".n64", ".v64", ".sav") -or
        $_.FullName -match '(^|/)(Save|logs|mods)/'
    })
    if ($badEntries.Count -ne 0) {
        throw "Built update contains protected player files."
    }
    foreach ($entry in $managedFiles.GetEnumerator()) {
        $suffix = "_hyrule_update/payload/" + $entry.Key
        if (@($archive.Entries | Where-Object { $_.FullName.Replace('\', '/').EndsWith($suffix) }).Count -ne 1) {
            throw "Built update is missing managed payload: $($entry.Key)"
        }
    }
} finally {
    $archive.Dispose()
}
$zipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash
"$zipHash *$([IO.Path]::GetFileName($OutputPath))" |
    Set-Content -LiteralPath ($OutputPath + ".sha256") -Encoding ASCII

Write-Host "Created update: $OutputPath"
Write-Host "SHA-256: $zipHash"
