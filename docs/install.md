# Installing Lumiere

This is the path from "the release builds work" to "installing Lumiere feels smooth."

## Today: one-command install

### macOS and Linux

Install the latest release into `~/.local/bin`:

```bash
curl -fsSL https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.sh | sh
```

Install a specific version:

```bash
curl -fsSL https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.sh | sh -s -- --version v0.1.2
```

Change the install location:

```bash
curl -fsSL https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.sh | sh -s -- --bin-dir /usr/local/bin
```

### Windows PowerShell

Install the latest release into `%USERPROFILE%\AppData\Local\Programs\Lumiere\bin`:

```powershell
irm https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.ps1 | iex
```

Install a specific version:

```powershell
irm https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.ps1 | iex --% --version v0.1.2
```

## What "flawless" usually means

For users, polished installation usually means:

- one obvious install command on the README
- automatic platform detection
- installing into a sensible per-user location by default
- a quick `lumiere --version` verification step
- PATH guidance when the binary is installed successfully
- predictable versioned GitHub release assets

The new install scripts handle those basics without requiring a package manager first.

## Next upgrades after this

If you want the install story to feel closer to Python, the usual order is:

1. First-party installer scripts
2. Package manager support
3. Native signed installers
4. Upgrade and uninstall commands

### Package managers

The biggest UX jump after the scripts is publishing to:

- Homebrew for macOS and Linux
- Scoop for Windows
- Chocolatey or Winget for Windows later

That gives users commands like:

```bash
brew install lumiere
```

or:

```powershell
scoop install lumiere
```

### Native installers

After package managers, the next polish layer is:

- `.pkg` for macOS
- `.msi` for Windows
- `.deb` for Debian/Ubuntu

Those matter most when you want non-technical users to install Lumiere without touching a terminal.

### Trust and safety

For a language runtime, installation quality also depends on:

- checksums published with each release
- code signing
- macOS notarization
- release notes that include copy-paste install commands

## Recommended roadmap

If we want to keep momentum and avoid overbuilding too early, the clean sequence is:

1. Ship and document the installer scripts
2. Add Homebrew and Scoop publishing in CI
3. Add signed native installers
4. Add `lumiere self update` only if distribution remains direct-download-heavy
