# Vendored IANA time zone database

`Temps`'s named-timezone support (`src/interpreter/stdlib/temps.cpp`) reads
compiled TZif binary files (RFC 8536). Linux and macOS normally get these
from the OS itself (`/usr/share/zoneinfo/`), but Windows ships no such
database, so `Fuseau("Europe/Paris")` and friends had no way to resolve a
named zone there at all.

This directory is a pre-compiled copy of the IANA tz database, so the same
lookup works identically on every platform without depending on what (if
anything) the host OS provides. `CMakeLists.txt` copies it next to the
built `lumiere`/`lumiere.exe` binary (and installs it the same way), and
`tz_zoneinfo_root()` looks there first, before falling back to `TZDIR` or
the host's own `/usr/share/zoneinfo/` on Unix.

## Provenance

Compiled with `zic` (Ubuntu GLIBC 2.35, `zic` 2.35) from the official IANA
tz database mirror, tag `2025c`:

    git clone --depth 1 --branch 2025c https://github.com/eggert/tz.git src
    cd src
    zic -d <output-dir> africa antarctica asia australasia europe \
        northamerica southamerica etcetera factory backward

That command is exactly `$(TDATA)` from the upstream Makefile
(`PRIMARY_YDATA` + `etcetera` + `factory` + `backward`), matching what a
normal `zic`-based install produces (see the `public.ck` Makefile
target). Don't drop `etcetera` -- it's what defines Etc/UTC, Etc/GMT and
friends; without it, every alias that points at them (UTC, Zulu, GMT0,
Universal, ...) is a dangling symlink.

## Regenerating for a newer release

Rebuilding `zic` itself isn't required on every platform this repo builds
for -- it only needs to run once, on any machine with a POSIX `zic`
(Linux and macOS both ship one), to regenerate this directory. Bump the
`--branch` tag above to a newer IANA release, rerun the two commands, and
replace this directory's contents.
