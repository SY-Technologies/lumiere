# Lumière

A programming language interpreter (WIP).

## Docs

Project notes live in [`docs/`](./docs/README.md). They are short design documents about both Lumiere internals and the C++ techniques used to build them.

For a practical "what works today" reference, see [`docs/implemented-language-overview.md`](./docs/implemented-language-overview.md).

## Prerequisites

- CMake ≥ 3.23
- A C++20 compiler (Clang, GCC, or MSVC)
- (Optional) [direnv](https://direnv.net) — adds `build`, `run`, and `tests` commands to your shell

## Build

```bash
# Configure and build (output in build/)
cmake -S . -B build
cmake --build build
```

Or with direnv:

```bash
build
```

## Run

```bash
build/lumiere <file>
```

Or with direnv:

```bash
run <file>
```

Helpful CLI checks:

```bash
build/lumiere --help
build/lumiere --version
```

## Tests

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target lumiere_unit_tests
ctest --test-dir build --output-on-failure
```

Or with direnv:

```bash
tests
```

## direnv setup

```bash
brew install direnv        # macOS
# or: apt install direnv   # Debian/Ubuntu
direnv allow .
```

After that, `build`, `run`, and `tests` are available in your shell when inside the project directory.

## Releases

Multi-OS release scaffolding is documented in [`docs/release-scaffolding.md`](./docs/release-scaffolding.md).
Install guidance is documented in [`docs/install.md`](./docs/install.md).

In short:

- CI builds and tests on Linux, macOS, and Windows.
- Tagged releases package artifacts for `linux-x86_64`, `macos-x86_64`, `macos-arm64`, and `windows-x86_64`.
- Release assets are published on the GitHub Releases page for this repository after pushing a `v*` tag.

Quick install:

```bash
curl -fsSL https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.sh | sh
```

Install a specific release:

```bash
curl -fsSL https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.sh | sh -s -- --version v0.1.2
```

On Windows PowerShell:

```powershell
irm https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.ps1 | iex
```

To publish a release:

```bash
git tag -a v0.1.0 -m "Release v0.1.0"
git push origin v0.1.0
```

Useful checks before or after pushing:

```bash
git tag
git show v0.1.0
```
