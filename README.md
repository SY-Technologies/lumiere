# Lumière

A programming language interpreter (WIP).

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
