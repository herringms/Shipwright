param(
    [Parameter(Mandatory = $true)]
    [string]$RomPath,
    [string]$OutputPath,
    [switch]$SkipLocalhostTest,
    [switch]$SkipExtractionTest
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$executablePath = Join-Path $repoRoot "x64\Release\soh.exe"
$runtimeRoot = Join-Path $repoRoot "build-poc-mingw\runtime\host"
$assetsPath = Join-Path (Split-Path -Parent $executablePath) "extractor-assets.zip"
$portArchive = Join-Path $repoRoot "soh.o2r"
$readmePath = Join-Path $repoRoot "docs\WINDOWS_POC_PACKAGE.txt"
$configPath = Join-Path $repoRoot "docs\WINDOWS_POC_CONFIG.json"
$RomPath = [IO.Path]::GetFullPath($RomPath)

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot "dist\Hyrule-Coop-PoC-2-Windows.zip"
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

foreach ($required in @($executablePath, $assetsPath, $portArchive, $readmePath, $configPath, $RomPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required baseline input is missing: $required"
    }
}
if ([IO.Path]::GetExtension($RomPath).ToLowerInvariant() -notin @(".z64", ".n64", ".v64")) {
    throw "Extraction test ROM must use a supported N64 ROM extension."
}

if (-not $SkipLocalhostTest) {
    & (Join-Path $PSScriptRoot "Test-HyruleCoopLocalhost.ps1") -ExecutablePath $executablePath
}

$stageContainer = Join-Path $repoRoot "build-poc-mingw\baseline-package"
$packageName = "Hyrule-Coop-PoC-2-Windows"
$packageRoot = Join-Path $stageContainer $packageName
$resolvedStageContainer = [IO.Path]::GetFullPath($stageContainer)
$resolvedPackageRoot = [IO.Path]::GetFullPath($packageRoot)
if (-not $resolvedPackageRoot.StartsWith($resolvedStageContainer + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing unsafe baseline staging path: $resolvedPackageRoot"
}
if (Test-Path -LiteralPath $resolvedPackageRoot) {
    Remove-Item -LiteralPath $resolvedPackageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedPackageRoot -Force | Out-Null

$runtimeFiles = @(
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

Copy-Item -LiteralPath $executablePath -Destination (Join-Path $resolvedPackageRoot "soh.exe")
Copy-Item -LiteralPath $portArchive -Destination (Join-Path $resolvedPackageRoot "soh.o2r")
Copy-Item -LiteralPath $readmePath -Destination (Join-Path $resolvedPackageRoot "README.txt")
Copy-Item -LiteralPath $configPath -Destination (Join-Path $resolvedPackageRoot "shipofharkinian.json")
Copy-Item -LiteralPath $assetsPath -Destination (Join-Path $resolvedPackageRoot "extractor-assets.zip")
foreach ($name in $runtimeFiles) {
    $source = Join-Path $runtimeRoot $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Allowlisted runtime dependency is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $resolvedPackageRoot $name)
}

$forbidden = Get-ChildItem -LiteralPath $resolvedPackageRoot -Recurse -File | Where-Object {
    $_.Name -in @("oot.o2r", "oot-mq.o2r", "global.sav") -or
    $_.Extension.ToLowerInvariant() -in @(".z64", ".n64", ".v64", ".sav") -or
    $_.FullName -match '\\(Save|logs|mods)\\'
}
if ($forbidden) {
    throw "Baseline staging contains forbidden personal files: $($forbidden.FullName -join ', ')"
}

if (-not $SkipExtractionTest) {
    $smokeRoot = Join-Path $env:USERPROFILE ("Hyrule-Coop-Baseline-Smoke-" + $PID)
    $expectedSmokeRoot = [IO.Path]::GetFullPath($smokeRoot)
    if (Test-Path -LiteralPath $expectedSmokeRoot) {
        throw "Baseline extraction smoke folder already exists: $expectedSmokeRoot"
    }
    Copy-Item -LiteralPath $resolvedPackageRoot -Destination $expectedSmokeRoot -Recurse
    $smokeExe = Join-Path $expectedSmokeRoot "soh.exe"
    $generatedArchive = Join-Path $expectedSmokeRoot "oot.o2r"
    $smokeProcess = $null
    try {
        $env:SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS = "0"
        $startOptions = @{
            FilePath = $smokeExe
            ArgumentList = ('"' + $RomPath + '"')
            WorkingDirectory = $expectedSmokeRoot
            WindowStyle = "Minimized"
            PassThru = $true
        }
        $smokeProcess = Start-Process @startOptions
        $deadline = (Get-Date).AddMinutes(5)
        $lastLength = -1L
        $stableChecks = 0
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 2
            if (Test-Path -LiteralPath $generatedArchive) {
                $length = (Get-Item -LiteralPath $generatedArchive).Length
                if ($length -eq $lastLength -and $length -gt 1000000) {
                    $stableChecks++
                } else {
                    $stableChecks = 0
                    $lastLength = $length
                }
                if ($stableChecks -ge 3) {
                    break
                }
            }
            $smokeProcess.Refresh()
            if ($smokeProcess.HasExited) {
                break
            }
        }
        if (-not (Test-Path -LiteralPath $generatedArchive) -or
            (Get-Item -LiteralPath $generatedArchive).Length -le 1000000) {
            throw "Clean baseline failed to generate oot.o2r from the supplied ROM."
        }
    } finally {
        if ($null -ne $smokeProcess) {
            $smokeProcess.Refresh()
            if (-not $smokeProcess.HasExited) {
                $actual = (Get-CimInstance Win32_Process -Filter "ProcessId=$($smokeProcess.Id)").ExecutablePath
                if ($actual -eq $smokeExe) {
                    Stop-Process -Id $smokeProcess.Id -Force
                    $smokeProcess.WaitForExit(10000) | Out-Null
                }
            }
        }
        if ([IO.Directory]::Exists($expectedSmokeRoot)) {
            $cleanupAttempts = 0
            while ([IO.Directory]::Exists($expectedSmokeRoot)) {
                try {
                    [IO.Directory]::Delete($expectedSmokeRoot, $true)
                } catch [System.IO.IOException], [System.UnauthorizedAccessException] {
                    $cleanupAttempts++
                    if ($cleanupAttempts -ge 10) {
                        throw
                    }
                    Start-Sleep -Milliseconds 500
                }
            }
        }
    }
}

if (Test-Path -LiteralPath $OutputPath) {
    [IO.File]::Delete($OutputPath)
}
& tar -a -c -f $OutputPath -C $resolvedStageContainer $packageName
if ($LASTEXITCODE -ne 0) {
    throw "bsdtar failed to create the sanitized baseline archive."
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($OutputPath)
try {
    $badEntries = @($archive.Entries | Where-Object {
        $_.Name -in @("oot.o2r", "oot-mq.o2r", "global.sav") -or
        [IO.Path]::GetExtension($_.Name).ToLowerInvariant() -in @(".z64", ".n64", ".v64", ".sav") -or
        $_.FullName -match '(^|/)(Save|logs|mods)/'
    })
    if ($badEntries.Count -ne 0) {
        throw "Built baseline archive contains forbidden personal entries."
    }
    if (@($archive.Entries | Where-Object { $_.Name -eq "soh.exe" }).Count -ne 1 -or
        @($archive.Entries | Where-Object { $_.Name -eq "extractor-assets.zip" }).Count -ne 1) {
        throw "Built baseline archive is incomplete."
    }
} finally {
    $archive.Dispose()
}

$zipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash
"$zipHash *$([IO.Path]::GetFileName($OutputPath))" |
    Set-Content -LiteralPath ($OutputPath + ".sha256") -Encoding ASCII

Write-Host "Created sanitized baseline: $OutputPath"
Write-Host "SHA-256: $zipHash"
