# LakoOS build environment

This folder builds LakoOS from nothing. It installs the host tools, **builds the compiler toolchain from source**, configures the tree and produces the image.

## Linux and macOS

```sh
./buildenv/build.sh
```

The result is `output-Clang-amd64-debug/livecd.iso`.

| Option | Meaning |
|---|---|
| `-a`, `--arch` | `amd64` (default), `arm64` |
| `-t`, `--target` | `livecd` (default), `bootcd`, `lakoosimg`, `lakoosvhd` |
| `-r`, `--release` | Release build |
| `--env-only` | Install the tools, build the toolchain and stop |

## Windows

```powershell
.\buildenv\build.ps1
```

The script uses WSL2 and installs Ubuntu if it is missing. Images are copied to `dist\`.

## The toolchain

`toolchain/build-toolchain.sh` fetches the LLVM sources at a pinned revision and builds Clang, LLD and the binary tools for amd64, arm64, ARM, PowerPC and RISC-V. It installs them into `~/.local/opt/lakoos/llvm` together with the runtime sources the OS build needs. Nothing is downloaded as a prebuilt binary.

| Variable | Meaning |
|---|---|
| `LAKOOS_TOOLCHAIN` | Install directory (default `~/.local/opt/lakoos`) |
| `LLVM_REPO`, `LLVM_REV` | LLVM source repository and revision |
| `JOBS` | Parallel jobs |

The first run needs about 15 GB of disk and from 20 minutes to a few hours depending on the machine.

## Host tools

git, CMake, Ninja, flex, bison, Python 3, NASM, xz and a native C/C++ compiler. They are installed with `apt`, `dnf`, `pacman`, `zypper`, `apk` or Homebrew when missing.
