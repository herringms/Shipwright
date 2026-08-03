param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]{1,80}$')]
    [string]$ReleaseId,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [string]$ExecutablePath,
    [string]$RuntimeDependencyPath,
    [string]$PortArchivePath,
    [string]$ExtractorArchivePath,
    [string]$SeedSavePath,
    [string]$OutputDirectory,
    [string]$GitHubRepository = "herringms/Shipwright",
    [string]$ReleaseTag,
    [ValidateSet("stable", "test")]
    [string]$Channel = "test",
    [int]$AssetSchema = 1,
    [ValidateRange(1024, 65534)]
    [int]$LocalhostPort = 43493,
    [switch]$SkipLocalhostTest
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $repoRoot "x64\Release\soh.exe"
}
if ([string]::IsNullOrWhiteSpace($RuntimeDependencyPath)) {
    $RuntimeDependencyPath = Join-Path $repoRoot "build-poc-mingw\runtime\host"
}
if ([string]::IsNullOrWhiteSpace($PortArchivePath)) {
    $PortArchivePath = Join-Path $repoRoot "soh.o2r"
}
if ([string]::IsNullOrWhiteSpace($ExtractorArchivePath)) {
    $ExtractorArchivePath = Join-Path (Split-Path -Parent $ExecutablePath) "extractor-assets.zip"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "dist\release-$ReleaseId"
}
if ([string]::IsNullOrWhiteSpace($ReleaseTag)) {
    $ReleaseTag = $ReleaseId
}

$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
$RuntimeDependencyPath = [IO.Path]::GetFullPath($RuntimeDependencyPath)
$PortArchivePath = [IO.Path]::GetFullPath($PortArchivePath)
$ExtractorArchivePath = [IO.Path]::GetFullPath($ExtractorArchivePath)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not [string]::IsNullOrWhiteSpace($SeedSavePath)) {
    $SeedSavePath = [IO.Path]::GetFullPath($SeedSavePath)
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$launcherBuild = Join-Path $repoRoot "build-poc-mingw\launcher"
& (Join-Path $PSScriptRoot "Build-HyruleCoopLauncher.ps1") -OutputDirectory $launcherBuild
$bootstrapExecutable = Join-Path $launcherBuild "HyruleCoop.exe"
$launcherExecutable = Join-Path $launcherBuild "HyruleCoopLauncher.exe"

$runtimeDependencies = @(
    "gamecontrollerdb.txt",
    "glew32.dll",
    "libbz2-1.dll",
    "libgcc_s_seh-1.dll",
    "liblzma-5.dll",
    "libogg-0.dll",
    "libopus-0.dll",
    "libopusfile-0.dll",
    "libspdlog-1.17.dll",
    "libstdc++-6.dll",
    "libvorbis-0.dll",
    "libvorbisfile-3.dll",
    "libwinpthread-1.dll",
    "libzip.dll",
    "libzstd.dll",
    "SDL2.dll",
    "zlib1.dll"
)

$managedSources = [ordered]@{
    "soh.exe" = $ExecutablePath
    "soh.o2r" = $PortArchivePath
    "extractor-assets.zip" = $ExtractorArchivePath
    "README.txt" = (Join-Path $repoRoot "docs\WINDOWS_POC_PACKAGE.txt")
}
foreach ($name in $runtimeDependencies) {
    $managedSources[$name] = Join-Path $RuntimeDependencyPath $name
}
foreach ($entry in $managedSources.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Managed release input is missing: $($entry.Value)"
    }
}

if (-not $SkipLocalhostTest) {
    if ([string]::IsNullOrWhiteSpace($SeedSavePath) -or
        -not (Test-Path -LiteralPath $SeedSavePath -PathType Leaf)) {
        throw "SeedSavePath must name an existing save unless -SkipLocalhostTest is used."
    }
    $localhostTest = Join-Path $PSScriptRoot "Test-HyruleCoopLocalhost.ps1"
    & $localhostTest -ExecutablePath $ExecutablePath -SeedSavePath $SeedSavePath -Port $LocalhostPort
    & $localhostTest -ExecutablePath $ExecutablePath -SeedSavePath $SeedSavePath `
        -Port ($LocalhostPort + 1) -ExpectBuildMismatch
}

$stageContainer = Join-Path $repoRoot "build-poc-mingw\release-package"
$runtimeStage = Join-Path $stageContainer ("runtime-" + $ReleaseId)
$bootstrapStage = Join-Path $stageContainer ("bootstrap-" + $ReleaseId)
$allowedStageRoot = [IO.Path]::GetFullPath($stageContainer)
foreach ($stage in @($runtimeStage, $bootstrapStage)) {
    $resolved = [IO.Path]::GetFullPath($stage)
    if (-not $resolved.StartsWith($allowedStageRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing unsafe release staging path: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $resolved -Force | Out-Null
}

foreach ($entry in $managedSources.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $runtimeStage $entry.Key)
}

$runtimeFiles = @($managedSources.Keys | ForEach-Object {
    $path = Join-Path $runtimeStage $_
    [ordered]@{
        path = $_
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
        size = (Get-Item -LiteralPath $path).Length
    }
})

$runtimeArchiveName = "Hyrule-Coop-Runtime-Windows-x64-$Version.zip"
$runtimeArchive = Join-Path $OutputDirectory $runtimeArchiveName
if (Test-Path -LiteralPath $runtimeArchive) {
    Remove-Item -LiteralPath $runtimeArchive -Force
}
& tar -a -c -f $runtimeArchive -C $runtimeStage .
if ($LASTEXITCODE -ne 0) {
    throw "bsdtar failed to create the Hyrule Co-op runtime archive."
}
$runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeArchive).Hash
$launcherHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $launcherExecutable).Hash
$launcherAssetName = "HyruleCoopLauncher.exe"
$launcherAsset = Join-Path $OutputDirectory $launcherAssetName
Copy-Item -LiteralPath $launcherExecutable -Destination $launcherAsset -Force

$releaseBaseUrl = "https://github.com/$GitHubRepository/releases/download/$ReleaseTag"
$manifest = [ordered]@{
    schemaVersion = 1
    channel = $Channel
    releaseId = $ReleaseId
    version = $Version
    publishedUtc = [DateTime]::UtcNow.ToString("o")
    compatibilityId = "hyrule-coop-poc.3"
    assetSchema = $AssetSchema
    minimumLauncherVersion = "1.0.0"
    launcherVersion = "1.0.0"
    launcherUrl = "$releaseBaseUrl/$launcherAssetName"
    launcherSha256 = $launcherHash
    runtimeUrl = "$releaseBaseUrl/$runtimeArchiveName"
    runtimeArchive = ""
    runtimeSha256 = $runtimeHash
    runtimeSize = (Get-Item -LiteralPath $runtimeArchive).Length
    files = $runtimeFiles
}
$manifestPath = Join-Path $OutputDirectory "hyrule-coop-release.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

$bootstrapPackageName = "Hyrule-Coop-Bootstrap-$Version-Windows"
$bootstrapRoot = Join-Path $bootstrapStage $bootstrapPackageName
$bootstrapPayload = Join-Path $bootstrapRoot "_bootstrap"
New-Item -ItemType Directory -Path $bootstrapPayload -Force | Out-Null
Copy-Item -LiteralPath $bootstrapExecutable -Destination (Join-Path $bootstrapRoot "HyruleCoop.exe")
Copy-Item -LiteralPath (Join-Path $repoRoot "docs\HYRULE_COOP_LAUNCHER.txt") -Destination (Join-Path $bootstrapRoot "README.txt")
Copy-Item -LiteralPath $launcherExecutable -Destination (Join-Path $bootstrapPayload "HyruleCoopLauncher.exe")
Copy-Item -LiteralPath $runtimeArchive -Destination (Join-Path $bootstrapPayload $runtimeArchiveName)
Copy-Item -LiteralPath (Join-Path $repoRoot "docs\WINDOWS_POC_CONFIG.json") `
    -Destination (Join-Path $bootstrapPayload "default-shipofharkinian.json")

$bootstrapManifest = [ordered]@{}
foreach ($entry in $manifest.GetEnumerator()) {
    $bootstrapManifest[$entry.Key] = $entry.Value
}
$bootstrapManifest.runtimeArchive = $runtimeArchiveName
$bootstrapManifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $bootstrapPayload "hyrule-coop-release.json") -Encoding UTF8

$forbidden = Get-ChildItem -LiteralPath $bootstrapRoot -Recurse -File | Where-Object {
    $_.Name -in @("oot.o2r", "oot-mq.o2r", "global.sav", "shipofharkinian.json") -or
    $_.Extension.ToLowerInvariant() -in @(".z64", ".n64", ".v64", ".sav") -or
    $_.FullName -match '\\(Save|logs|mods|UserData)\\'
}
if ($forbidden) {
    throw "Bootstrap staging contains player-owned files: $($forbidden.FullName -join ', ')"
}

$bootstrapArchive = Join-Path $OutputDirectory ($bootstrapPackageName + ".zip")
if (Test-Path -LiteralPath $bootstrapArchive) {
    Remove-Item -LiteralPath $bootstrapArchive -Force
}
& tar -a -c -f $bootstrapArchive -C $bootstrapStage $bootstrapPackageName
if ($LASTEXITCODE -ne 0) {
    throw "bsdtar failed to create the Hyrule Co-op bootstrap archive."
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($bootstrapArchive)
try {
    $entries = @($archive.Entries)
    if (@($entries | Where-Object { $_.Name -eq "HyruleCoop.exe" }).Count -ne 1 -or
        @($entries | Where-Object { $_.Name -eq "HyruleCoopLauncher.exe" }).Count -ne 1 -or
        @($entries | Where-Object { $_.Name -eq $runtimeArchiveName }).Count -ne 1) {
        throw "The bootstrap ZIP is incomplete."
    }
    $bad = @($entries | Where-Object {
        $_.Name -in @("oot.o2r", "oot-mq.o2r", "global.sav", "shipofharkinian.json") -or
        [IO.Path]::GetExtension($_.Name).ToLowerInvariant() -in @(".z64", ".n64", ".v64", ".sav") -or
        $_.FullName -match '(^|/)(Save|logs|mods|UserData)/'
    })
    if ($bad.Count -ne 0) {
        throw "The bootstrap ZIP contains player-owned files."
    }
} finally {
    $archive.Dispose()
}

foreach ($artifact in @($runtimeArchive, $launcherAsset, $manifestPath, $bootstrapArchive)) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash
    "$hash *$([IO.Path]::GetFileName($artifact))" |
        Set-Content -LiteralPath ($artifact + ".sha256") -Encoding ASCII
}

Write-Host "Created GitHub release assets in: $OutputDirectory"
Write-Host "Bootstrap ZIP: $bootstrapArchive"
Write-Host "Runtime ZIP: $runtimeArchive"
Write-Host "Channel manifest: $manifestPath"
