param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$ExePath,
    [Parameter(Mandatory = $true)]
    [string]$OutputMsi
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ExePath)) {
    throw "Executable not found: $ExePath"
}

$ResolvedExePath = (Resolve-Path $ExePath).Path

$WixSource = Join-Path $PSScriptRoot "..\packaging\windows\lumiere.wxs"
if (-not (Test-Path $WixSource)) {
    throw "WiX source not found: $WixSource"
}

$WixVersion = "5.0.2"
$WixToolDir = Join-Path ([System.IO.Path]::GetTempPath()) "lumiere-wix-$WixVersion"
$WixExe = Join-Path $WixToolDir "wix.exe"

if (-not (Test-Path $WixExe)) {
    dotnet tool install wix --tool-path $WixToolDir --version $WixVersion
}

$InstalledWixVersion = (& $WixExe --version).Trim()
if (-not $InstalledWixVersion.StartsWith($WixVersion)) {
    throw "Expected WiX $WixVersion, found $InstalledWixVersion"
}

$OutputDir = Split-Path -Parent $OutputMsi
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

& $WixExe build $WixSource `
    -arch x64 `
    -d LumiereVersion=$Version `
    -d LumiereExe="$ResolvedExePath" `
    -o $OutputMsi
