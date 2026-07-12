# Lumière

A programming language interpreter (WIP).

## Docs

Project notes live in [`docs/`](./docs/README.md). They are short design documents about both Lumiere internals and the C++ techniques used to build them.

For a practical "what works today" reference, see [`docs/implemented-language-overview.md`](./docs/implemented-language-overview.md).
The complete execution and inspection command matrix is in [`docs/cli.md`](./docs/cli.md).

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

In production, execute a Lumiere source file directly. The bytecode VM is the
default backend:

```bash
lumiere programme.lum
```

From a local build, use the same command with the build path:

```bash
build/lumiere examples/bonjour.lum
```

Select the tree-walk interpreter explicitly with `--tw`:

```bash
lumiere --tw programme.lum
build/lumiere --tw examples/bonjour.lum
```

Select the VM explicitly when a script should not depend on the configured
default:

```bash
lumiere --vm programme.lum
build/lumiere --vm examples/bonjour.lum
```

`--tree-walker` is accepted as the long form of `--tw`. The older `--run` flag
is retained for compatibility, but files execute without it.

The repository-only `run` helper requires an explicit backend selector:

```bash
run --tw examples/bonjour.lum  # tree-walk interpreter
run --vm examples/bonjour.lum
```

The long `--tree-walker` alias is also accepted by `run`.

### Inspect IR and bytecode

Print the linked, human-readable intermediate representation without executing
the program:

```bash
build/lumiere ir examples/bonjour.lum
```

Compile the program and disassemble its bytecode:

```bash
build/lumiere bytecode examples/bonjour.lum
```

The bytecode output includes each function's metadata, instruction offsets,
decoded operands, referenced names or constants, and source coordinates.

### Interactive shell

Launch the Lumiere interactive shell by running the executable without
arguments:

```bash
build/lumiere
# or, after installation:
lumiere
```

The shell currently uses the tree-walk interpreter. Variables, functions, and
closures remain available between submissions. Blocks can span multiple lines.

```text
>>> soit base = 40
>>> fonction ajouter(x: Entier) {
...   retourne base + x
... }
>>> ajouter(2)
42
```

Use `:aide` for help and `:quitter` or `:q` to exit.

Helpful CLI checks:

```bash
build/lumiere --help
build/lumiere --version
```

Run LumiTest files ending in `_test.lum` with the test command:

```bash
build/lumiere tester tests
build/lumiere tester tests --verbeux
build/lumiere tester tests --filtre "nom du test"
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
curl -fsSL https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.sh | sh -s -- --version v0.1.3
```

On Windows PowerShell:

```powershell
irm https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.ps1 | iex
```

To publish a release:

```bash
git tag -a v0.1.3 -m "Release v0.1.3"
git push origin v0.1.3
```

Useful checks before or after pushing:

```bash
git tag
git show v0.1.3
```
