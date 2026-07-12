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

$DotnetTools = Join-Path $HOME ".dotnet\tools"
$env:PATH = "$DotnetTools;$env:PATH"

if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
    dotnet tool install --global wix
}

$OutputDir = Split-Path -Parent $OutputMsi
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

wix build $WixSource `
    -arch x64 `
    -d LumiereVersion=$Version `
    -d LumiereExe="$ResolvedExePath" `
    -o $OutputMsi
