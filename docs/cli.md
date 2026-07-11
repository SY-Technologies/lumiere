# Lumiere command line

The installed production executable is named `lumiere`.

## Execute a program

The bytecode VM is the production default:

```bash
lumiere programme.lum
```

Backend selection can be made explicit:

```bash
lumiere --vm programme.lum
lumiere --tw programme.lum
```

`--tree-walker` is the long alias for `--tw`. Supplying both VM and tree-walker
selectors is an error. `--run` is accepted for compatibility with older command
lines, but is no longer required.

Use the VM for normal production execution. The tree-walker remains useful for
backend comparison, diagnostics, and interpreter development.

## Development helper

Inside the repository, direnv exposes a `run` helper that launches the binary
from the build directory. It deliberately requires an explicit backend:

```bash
run --vm programme.lum
run --tw programme.lum
```

The helper is not installed and is not part of the production interface.

## Inspect compilation

Print the linked intermediate representation:

```bash
lumiere ir programme.lum
```

Print decoded bytecode without executing it:

```bash
lumiere bytecode programme.lum
```

Both commands can inspect application files and library modules without a
`principal` function. VM execution still requires `principal`.

## Interactive shell

Running Lumiere without arguments starts the persistent tree-walker shell:

```bash
lumiere
```

The shell preserves declarations between submissions and accepts multiline
blocks. Use `:aide` for help and `:quitter` or `:q` to exit.

## Other commands

```bash
lumiere tester tests
lumiere --help
lumiere --version
```
