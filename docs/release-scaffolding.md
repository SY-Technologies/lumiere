# Release Scaffolding

This document explains the release pipeline we want for Lumiere v1 and beyond.

## Why this exists

One machine should not decide whether Lumiere is releasable.

If we only build on one developer laptop, we learn nothing about:

- Linux compatibility
- Windows compatibility
- Intel vs Apple Silicon macOS packaging
- Whether the published binary actually matches the tagged version

The release scaffolding solves that by asking GitHub Actions to build, test, and package Lumiere on every target platform we care about.

## Current targets

The workflows produce these artifacts:

- `linux-x86_64`
- `macos-x86_64`
- `macos-arm64`
- `windows-x86_64`

That gives us a practical first release matrix without trying to support every architecture on day one.

Windows is part of the build and release matrix. At the moment, the Windows build disables LumiNet because that networking layer still depends on POSIX socket APIs. The core language, CLI, packaging, and non-networking stdlib still build and ship there.

## What the workflows do

### CI workflow

File: `.github/workflows/ci.yml`

On every push to `main` and every pull request, CI:

1. Configures CMake with tests enabled.
2. Builds Lumiere.
3. Runs the test suite on Linux, macOS, and Windows.

This is the safety net that keeps platform regressions from slipping into a release tag.

### Release workflow

File: `.github/workflows/release.yml`

When a tag like `v0.1.0` is pushed, the release workflow:

1. Builds Lumiere in `Release` mode.
2. Runs the test suite.
3. Packages archive builds with CPack.
4. Builds native Linux `.deb` installers on Ubuntu runners.
5. Builds native macOS `.pkg` installers on macOS runners.
6. Builds native Windows `.msi` installers on Windows runners.
7. Verifies that the installer payloads contain the expected CLI binary.
8. Renames the archive assets into predictable platform-specific filenames.
9. Uploads the archives and installers to the GitHub release.

## Version identity

The CLI now supports:

- `lumiere --version`
- `lumiere --help`

That matters for release support because a user can immediately confirm which binary they downloaded.

## How to cut a release

From a clean commit on `main`:

1. Bump the version in `CMakeLists.txt`.
2. Commit the version bump.
3. Create a tag such as `v0.1.0`.
4. Push the branch and the tag.

Example:

```bash
git tag v0.1.0
git push origin main --tags
```

## What this does not do yet

This is solid release scaffolding, but it is intentionally still v1-sized.

It does not yet include:

- code signing or notarization
- Linux-native installers beyond `.deb`, such as `.rpm`
- Homebrew, Scoop, Chocolatey, Winget, or package repository publishing
- cross-compiling from one host to every host

The repository now includes first-party install scripts in [`scripts/install.sh`](../scripts/install.sh) and [`scripts/install.ps1`](../scripts/install.ps1) so releases already have a stable one-command install path while those larger packaging steps are still pending.

The native installer jobs are intentionally staged in a conservative way:

- macOS packages install `lumiere` into `/usr/local/bin`
- Windows installers place `lumiere.exe` under `Program Files\Lumiere` and append that directory to `PATH`
- Linux `.deb` packages are validated to make sure the CLI payload is present before publishing

Those are good later enhancements once the runtime and VM shape settle down.
