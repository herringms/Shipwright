param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir
)

$ErrorActionPreference = "Stop"
$installRoot = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$updateRoot = Join-Path $installRoot "_hyrule_update"
$payloadRoot = Join-Path $updateRoot "payload"
$manifestPath = Join-Path $updateRoot "manifest.json"
$versionPath = Join-Path $installRoot "hyrule-coop-version.json"

if (-not (Test-Path -LiteralPath (Join-Path $installRoot "soh.exe"))) {
    throw "Run this update from an extracted Hyrule Co-op installation containing soh.exe."
}
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Update manifest is missing: $manifestPath"
}

$protectedNames = @(
    "save", "mods", "logs", "shipofharkinian.json", "oot.o2r", "oot-mq.o2r"
)

function Test-ProtectedPath([string]$relativePath) {
    $normalized = $relativePath.Replace('\', '/').TrimStart('/').ToLowerInvariant()
    $first = $normalized.Split('/')[0]
    if ($protectedNames -contains $first) {
        return $true
    }
    return $normalized.EndsWith(".z64") -or $normalized.EndsWith(".n64") -or
           $normalized.EndsWith(".v64") -or $normalized.EndsWith(".sav")
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or [string]::IsNullOrWhiteSpace($manifest.patchId)) {
    throw "Unsupported or invalid Hyrule Co-op update manifest."
}

$validated = @()
foreach ($file in $manifest.files) {
    $relative = [string]$file.path
    if ([string]::IsNullOrWhiteSpace($relative) -or (Test-ProtectedPath $relative)) {
        throw "Update attempts to modify a protected path: $relative"
    }

    $payloadPath = [IO.Path]::GetFullPath((Join-Path $payloadRoot $relative))
    $targetPath = [IO.Path]::GetFullPath((Join-Path $installRoot $relative))
    if (-not $payloadPath.StartsWith($payloadRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not $targetPath.StartsWith($installRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Update contains an invalid relative path: $relative"
    }
    if (-not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) {
        throw "Update payload is missing: $relative"
    }
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $payloadPath).Hash
    if ($actualHash -ne ([string]$file.sha256).ToUpperInvariant()) {
        throw "Update payload hash mismatch: $relative"
    }
    $validated += [pscustomobject]@{
        Relative = $relative
        Payload = $payloadPath
        Target = $targetPath
        Hash = $actualHash
    }
}

$liveExe = Join-Path $installRoot "soh.exe"
$running = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq "soh.exe" -and $_.ExecutablePath -eq $liveExe
}
if ($running) {
    throw "Close Ship of Harkinian before applying this update."
}

$backupRoot = Join-Path $installRoot ("_hyrule_backup\" + $manifest.patchId)
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null

foreach ($file in $validated) {
    $targetDirectory = Split-Path -Parent $file.Target
    $backupPath = Join-Path $backupRoot $file.Relative
    New-Item -ItemType Directory -Path $targetDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path (Split-Path -Parent $backupPath) -Force | Out-Null
    if (Test-Path -LiteralPath $file.Target) {
        Copy-Item -LiteralPath $file.Target -Destination $backupPath -Force
    }

    $temporaryPath = $file.Target + ".hyrule-new"
    Copy-Item -LiteralPath $file.Payload -Destination $temporaryPath -Force
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $temporaryPath).Hash -ne $file.Hash) {
        throw "Copied update payload failed verification: $($file.Relative)"
    }
    Move-Item -LiteralPath $temporaryPath -Destination $file.Target -Force
}

@{
    patchId = $manifest.patchId
    compatibilityId = $manifest.compatibilityId
    installedAtUtc = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json | Set-Content -LiteralPath $versionPath -Encoding UTF8

Write-Host "Installed Hyrule Co-op update $($manifest.patchId)."
Write-Host "Preserved Save, ROM/O2R files, mods, logs, controller settings, and shipofharkinian.json."
