# Installing Lumiere

## Choose An Installation Method

Check your operating system and processor architecture before downloading an
artifact:

```bash
uname -s
uname -m
```

Lumiere currently publishes release artifacts for:

| Platform | Architecture | Release label |
|---|---|---|
| Linux | x86-64 (`x86_64`) | `linux-x86_64` |
| macOS | Intel (`x86_64`) | `macos-x86_64` |
| macOS | Apple Silicon (`arm64`) | `macos-arm64` |
| Windows | x86-64 (`AMD64`) | `windows-x86_64` |

If your platform is not in this table, build from source. This includes
Raspberry Pi systems reporting `aarch64` or `arm64`.

## Install A Release Artifact

### macOS And Linux x86-64

The installer script detects supported operating systems and architectures:

```bash
curl -fsSL https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.sh | sh
```

It installs `lumiere` into `~/.local/bin` by default. If that directory is not
on `PATH`, add this line to `~/.zshrc` or `~/.bashrc`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

Open a new terminal, then verify the installation:

```bash
lumiere --version
```

### Debian Or Ubuntu x86-64

Download the `linux-x86_64.deb` release asset. The `./` prefix tells `apt` to
install a local file instead of searching configured package repositories:

```bash
sudo apt install ./lumiere-vVERSION-linux-x86_64.deb
```

An x86-64 DEB cannot be installed on an ARM64 system. Confirm compatibility
before installing:

```bash
dpkg --print-architecture
```

The published DEB requires `amd64`. Use the source-build instructions when the
command reports `arm64`.

### Windows x86-64

The MSI release asset installs Lumiere and adds its installation directory to
the system `PATH`. Open a new terminal after installation, then run:

```powershell
lumiere --version
```

Alternatively, use the per-user PowerShell installer:

```powershell
irm https://raw.githubusercontent.com/SY-Technologies/lumiere/main/scripts/install.ps1 | iex
```

The PowerShell installer verifies the executable directly and prints its
installation directory. It does not currently modify `PATH`; add the printed
directory to your user `PATH` if `lumiere` is not found in a new terminal.

```powershell
lumiere --version
```

## Build From Source

Building from source is the supported fallback for platforms without release
artifacts. It creates a native executable for the machine performing the build.

### Debian, Ubuntu, And Raspberry Pi OS

Install the build dependencies:

```bash
sudo apt update
sudo apt install -y build-essential cmake git
```

Clone, build, test, and install Lumiere:

```bash
git clone https://github.com/SY-Technologies/lumiere.git
cd lumiere
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

The default system installation places the executable at
`/usr/local/bin/lumiere`. Verify it directly if your shell cannot find it:

```bash
/usr/local/bin/lumiere --version
```

### Raspberry Pi And Memory-Constrained Systems

C++ Release builds can consume substantial memory while optimizing LumiNet,
especially the Canal translation units. Build one file at a time on Raspberry
Pi and other low-memory machines:

```bash
cmake --build build --parallel 1
```

An apparently frozen compiler is often heavy swap activity caused by too many
parallel compiler processes. Check with `free -h` and `top`. Stop the existing
build before restarting it with `--parallel 1`; CMake will preserve completed
object files.

### macOS Without A Matching Artifact

Install the command-line build tools and CMake:

```bash
xcode-select --install
brew install cmake
```

Then use the same clone, CMake, build, test, and install commands shown above.

### Other Unix-like Platforms

You need:

- a C++20 compiler
- CMake 3.23 or newer
- Git

Use the source-build commands above. Lumiere may compile successfully on other
Unix-like systems, but only the platforms in the release table are continuously
tested by the project.

## Smoke Test

Create `bonjour.lum`:

```text
fonction principal() {
    afficher("Bonjour depuis Lumiere")
}
```

Verify the production VM, tree walker, and compiler tooling:

```bash
lumiere bonjour.lum
lumiere --vm bonjour.lum
lumiere --tw bonjour.lum
lumiere check bonjour.lum
lumiere ir bonjour.lum
lumiere bytecode bonjour.lum
```

If installation fails, report the operating system, output of `uname -m` (or
`$env:PROCESSOR_ARCHITECTURE` on Windows), selected artifact name, command, and
complete error output.
