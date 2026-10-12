# LakoOS build environment

This folder builds LakoOS from nothing. It installs the host tools, the compilers, configures the tree and produces the image.

## Linux and macOS

```sh
./buildenv/build.sh
```

The result is `output-Clang-amd64-debug/livecd.iso`.

Options:

| Option | Meaning |
|---|---|
| `-a`, `--arch` | `amd64` (default), `i386`, `arm64` |
| `-t`, `--target` | `livecd` (default), `bootcd`, `reactosimg`, `reactosvhd` |
| `-r`, `--release` | Release build |
| `--env-only` | Install the tools and stop |

## Windows

```powershell
.\buildenv\build.ps1                 # WSL2, installs Ubuntu if needed
.\buildenv\build.ps1 -Backend docker # Docker Desktop
```

Images are copied to `dist\`.

## Docker

```sh
docker build -t lakoos-buildenv buildenv
docker run --rm -v "$PWD:/src" lakoos-buildenv
```

## What gets installed

- Host tools: git, curl, CMake, Ninja, flex, bison, Python 3, NASM, a native compiler. Installed with `apt`, `dnf`, `pacman`, `zypper`, `apk` or Homebrew when missing.
- RosBE (LLVM and MinGW cross toolchains) into `~/.local/opt/rosbe`, with no administrator rights needed.

The first full build takes a while and needs about 15 GB of disk.
