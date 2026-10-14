#!/bin/sh
set -eu

SELF_DIR=$(cd "$(dirname "$0")" && pwd -P)
SRC_DIR=$(cd "$SELF_DIR/.." && pwd -P)

ARCH=amd64
TARGET=livecd
BUILD_TYPE=
ENV_ONLY=0

usage() {
    cat <<EOF
Usage: buildenv/build.sh [options]

Prepares a complete build environment from scratch and builds LakoOS.

  -a, --arch <arch>     amd64 (default), i386, arm64
  -t, --target <name>   livecd (default), bootcd, lakoosimg, lakoosvhd
  -r, --release         Release build instead of Debug
      --env-only        Install the build environment and stop
  -h, --help            Show this help

Environment:
  LAKOOS_TOOLCHAIN      Toolchain install directory (default: ~/.local/opt/lakoos)
  LLVM_REPO, LLVM_REV   LLVM source repository and revision to build
  JOBS                  Parallel jobs for the toolchain build
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -a|--arch) ARCH="$2"; shift 2 ;;
        -t|--target) TARGET="$2"; shift 2 ;;
        -r|--release) BUILD_TYPE="-r"; shift ;;
        --env-only) ENV_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "build.sh: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

log() { printf '\n==> %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

SUDO=
if [ "$(id -u)" -ne 0 ]; then
    if have sudo; then SUDO=sudo; fi
fi

install_packages() {
    OS=$(uname -s)
    case "$OS" in
        Linux)
            if have apt-get; then
                $SUDO apt-get update
                $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
                    build-essential git curl ca-certificates cmake ninja-build flex bison python3 \
                    nasm xz-utils unzip pkg-config glslang-tools python3-mako python3-yaml python3-packaging ccache wget
            elif have dnf; then
                $SUDO dnf install -y @development-tools git curl ca-certificates cmake ninja-build flex bison python3 nasm xz unzip pkgconf
            elif have pacman; then
                $SUDO pacman -Sy --noconfirm --needed base-devel git curl ca-certificates cmake ninja flex bison python nasm xz unzip pkgconf
            elif have zypper; then
                $SUDO zypper --non-interactive install -t pattern devel_basis
                $SUDO zypper --non-interactive install git curl ca-certificates cmake ninja flex bison python3 nasm xz unzip pkg-config
            elif have apk; then
                $SUDO apk add --no-cache build-base git curl ca-certificates cmake samurai flex bison python3 nasm xz unzip pkgconf bash
            else
                echo "build.sh: unsupported Linux distribution. Install git, curl, cmake, ninja, flex, bison, python3 and nasm, then run again." >&2
                exit 1
            fi
            ;;
        Darwin)
            if ! have brew; then
                log "Installing Homebrew"
                NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
                for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
                    if [ -x "$b" ]; then eval "$("$b" shellenv)"; break; fi
                done
            fi
            brew install git curl cmake ninja flex bison python nasm xz
            BISON_PREFIX=$(brew --prefix bison)
            FLEX_PREFIX=$(brew --prefix flex)
            PATH="$BISON_PREFIX/bin:$FLEX_PREFIX/bin:$PATH"
            export PATH
            ;;
        *)
            echo "build.sh: $OS is not supported. On Windows use buildenv/build.ps1." >&2
            exit 1
            ;;
    esac
}

missing=
for tool in git cmake ninja flex bison python3 nasm xz; do
    have "$tool" || missing="$missing $tool"
done

if [ -n "$missing" ]; then
    log "Installing missing tools:$missing"
    install_packages
else
    log "Host tools are present"
fi

TOOLCHAIN_ROOT="${LAKOOS_TOOLCHAIN:-$HOME/.local/opt/lakoos}"
export LAKOOS_TOOLCHAIN="$TOOLCHAIN_ROOT"
log "Preparing the compiler toolchain in $TOOLCHAIN_ROOT"
"$SELF_DIR/toolchain/build-toolchain.sh"

if [ "$ENV_ONLY" -eq 1 ]; then
    log "Build environment is ready"
    exit 0
fi

cd "$SRC_DIR"
log "Configuring LakoOS ($ARCH)"
scripts/feeds update mesa || echo "build.sh: could not fetch the Mesa feed; OpenGL will be the 1.1 software renderer"
./configure.sh --no-feeds-update -a "$ARCH" $BUILD_TYPE -DENABLE_MESA_LLVMPIPE=ON

case "$BUILD_TYPE" in
    -r) OUT_SUFFIX=release ;;
    *) OUT_SUFFIX=debug ;;
esac
OUT_DIR=$(ls -d "$SRC_DIR"/output-*-"$ARCH"-"$OUT_SUFFIX" 2>/dev/null | head -n 1)
if [ -z "$OUT_DIR" ]; then
    echo "build.sh: output directory was not created" >&2
    exit 1
fi

log "Building $TARGET"
cd "$OUT_DIR"
CCACHE_DISABLE=1 ninja "$TARGET"

log "Done"
ls -lh "$OUT_DIR"/*.iso "$OUT_DIR"/*.img "$OUT_DIR"/*.vhd 2>/dev/null || true
