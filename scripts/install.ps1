$ErrorActionPreference = "Stop"

$Repo = if ($env:LUMIERE_INSTALL_REPO) { $env:LUMIERE_INSTALL_REPO } else { "SY-Technologies/lumiere" }
$Version = "latest"
$InstallDir = Join-Path $HOME "AppData\Local\Programs\Lumiere\bin"

for ($i = 0; $i -lt $args.Length; $i++) {
    switch ($args[$i]) {
        "--version" {
            $i++
            $Version = $args[$i]
        }
        "--install-dir" {
            $i++
            $InstallDir = $args[$i]
        }
        "--help" {
            Write-Host "Usage: install.ps1 [--version v0.1.3] [--install-dir <path>]"
            exit 0
        }
        default {
            throw "Unknown argument: $($args[$i])"
        }
    }
}

function Resolve-Version {
    param([string]$RequestedVersion, [string]$RepoName)

    if ($RequestedVersion -ne "latest") {
        return $RequestedVersion
    }

    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$RepoName/releases/latest"
    if (-not $release.tag_name) {
        throw "Unable to resolve the latest release for $RepoName"
    }
    return $release.tag_name
}

function Resolve-Label {
    switch ($env:PROCESSOR_ARCHITECTURE) {
        "AMD64" { return "windows-x86_64" }
        default { throw "Unsupported Windows architecture: $env:PROCESSOR_ARCHITECTURE" }
    }
}

$Tag = Resolve-Version -RequestedVersion $Version -RepoName $Repo
$Label = Resolve-Label
$ArchiveName = "lumiere-$Tag-$Label.zip"
$DownloadUrl = "https://github.com/$Repo/releases/download/$Tag/$ArchiveName"

$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("lumiere-install-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TempDir | Out-Null

try {
    $ArchivePath = Join-Path $TempDir $ArchiveName
    Write-Host "Downloading $DownloadUrl"
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $ArchivePath

    Write-Host "Extracting $ArchiveName"
    Expand-Archive -Path $ArchivePath -DestinationPath $TempDir -Force

    $Binary = Get-ChildItem -Path $TempDir -Recurse -Filter "lumiere.exe" | Select-Object -First 1
    if (-not $Binary) {
        throw "Could not find lumiere.exe inside $ArchiveName"
    }

    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Path $Binary.FullName -Destination (Join-Path $InstallDir "lumiere.exe") -Force

    Write-Host "Installed to $(Join-Path $InstallDir 'lumiere.exe')"
    & (Join-Path $InstallDir "lumiere.exe") --version
    Write-Host "Add $InstallDir to your PATH if it is not already there."
}
finally {
    if (Test-Path $TempDir) {
        Remove-Item -Path $TempDir -Recurse -Force
    }
}
