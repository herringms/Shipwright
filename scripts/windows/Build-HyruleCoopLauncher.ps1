param(
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "build-poc-mingw\launcher"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$frameworkRoots = @(
    (Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319"),
    (Join-Path $env:WINDIR "Microsoft.NET\Framework\v4.0.30319")
)
$frameworkRoot = $frameworkRoots | Where-Object { Test-Path -LiteralPath (Join-Path $_ "csc.exe") } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($frameworkRoot)) {
    throw ".NET Framework 4.x C# compiler was not found. Windows 10 or newer with .NET Framework is required."
}
$csc = Join-Path $frameworkRoot "csc.exe"
$icon = Join-Path $repoRoot "soh\SHIPOFHARKINIAN.ico"

$common = @(
    "/nologo",
    "/target:winexe",
    "/optimize+",
    "/platform:anycpu",
    "/win32icon:$icon",
    "/reference:System.dll",
    "/reference:System.Core.dll",
    "/reference:System.Drawing.dll",
    "/reference:System.Windows.Forms.dll"
)

$launcherOutput = Join-Path $OutputDirectory "HyruleCoopLauncher.exe"
$defaultConfiguration = Join-Path $repoRoot "docs\WINDOWS_POC_CONFIG.json"
$launcherReferences = @(
    "/reference:System.IO.Compression.dll",
    "/reference:System.IO.Compression.FileSystem.dll",
    "/reference:System.Web.Extensions.dll"
)
& $csc @common @launcherReferences "/resource:$defaultConfiguration,HyruleCoop.DefaultConfig" `
    "/out:$launcherOutput" `
    (Join-Path $repoRoot "launcher\HyruleCoopLauncher.cs") `
    (Join-Path $repoRoot "launcher\LauncherEngine.cs")
if ($LASTEXITCODE -ne 0) {
    throw "Hyrule Co-op launcher compilation failed."
}

$bootstrapOutput = Join-Path $OutputDirectory "HyruleCoop.exe"
& $csc @common "/resource:$launcherOutput,HyruleCoop.EmbeddedLauncher" `
    "/resource:$defaultConfiguration,HyruleCoop.DefaultConfig" `
    "/out:$bootstrapOutput" (Join-Path $repoRoot "launcher\HyruleCoopBootstrap.cs")
if ($LASTEXITCODE -ne 0) {
    throw "Hyrule Co-op bootstrap compilation failed."
}

Write-Host "Created launcher: $launcherOutput"
Write-Host "Created bootstrap: $bootstrapOutput"
