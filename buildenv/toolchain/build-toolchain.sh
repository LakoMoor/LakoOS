#!/bin/sh
set -eu

LLVM_REPO="${LLVM_REPO:-https://github.com/sjljeh/llvm-project}"
LLVM_REV="${LLVM_REV:-8c8a8565e1bde6d027790350bcbc5f1a0ab3e144}"
LLVM_TARGETS="${LLVM_TARGETS:-AArch64;ARM;PowerPC;RISCV;X86}"
PREFIX="${LAKOOS_TOOLCHAIN:-$HOME/.local/opt/lakoos}/llvm"
WORK="${LAKOOS_TOOLCHAIN_WORK:-${TMPDIR:-/tmp}/lakoos-toolchain-work}"
JOBS="${JOBS:-}"

log() { printf '\n==> %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

for tool in git cmake ninja python3; do
    have "$tool" || { echo "build-toolchain.sh: $tool is required" >&2; exit 1; }
done
if have clang && have clang++; then
    HOST_CC=clang; HOST_CXX=clang++
elif have gcc && have g++; then
    HOST_CC=gcc; HOST_CXX=g++
else
    echo "build-toolchain.sh: a host C/C++ compiler is required" >&2
    exit 1
fi

if [ -x "$PREFIX/bin/clang" ] && [ -f "$PREFIX/native-toolchain.json" ] &&
   grep -q "$LLVM_REV" "$PREFIX/native-toolchain.json" 2>/dev/null; then
    log "Toolchain $LLVM_REV is already installed in $PREFIX"
    exit 0
fi

SRC="$WORK/llvm-project"
BUILD="$WORK/build"
mkdir -p "$SRC" "$BUILD"

log "Fetching LLVM $LLVM_REV from $LLVM_REPO"
if [ ! -d "$SRC/.git" ]; then
    git init -q "$SRC"
    git -C "$SRC" remote add origin "$LLVM_REPO"
fi
if ! git -C "$SRC" cat-file -e "$LLVM_REV^{commit}" 2>/dev/null; then
    git -C "$SRC" fetch --depth 1 origin "$LLVM_REV"
fi
git -C "$SRC" -c advice.detachedHead=false checkout -q --force "$LLVM_REV"

COMPONENTS="clang;clang-resource-headers;lld;llvm-ar;llvm-ranlib;llvm-dlltool;llvm-lib;llvm-rc;llvm-windres;llvm-nm;llvm-objcopy;llvm-strip;llvm-objdump;llvm-readobj;llvm-symbolizer"

log "Configuring"
cmake -G Ninja -S "$SRC/llvm" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_C_COMPILER="$HOST_CC" \
    -DCMAKE_CXX_COMPILER="$HOST_CXX" \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    -DLLVM_TARGETS_TO_BUILD="$LLVM_TARGETS" \
    -DLLVM_ENABLE_ASSERTIONS=OFF \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DCLANG_INCLUDE_TESTS=OFF \
    -DCLANG_INCLUDE_DOCS=OFF \
    -DLLVM_ENABLE_BINDINGS=OFF \
    -DLLVM_ENABLE_ZSTD=OFF \
    -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_ENABLE_TERMINFO=OFF \
    -DLLVM_ENABLE_ZLIB=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF \
    -DLLVM_PARALLEL_LINK_JOBS=4 \
    -DLLVM_DISTRIBUTION_COMPONENTS="$COMPONENTS"

log "Building (this takes a while)"
if [ -n "$JOBS" ]; then
    ninja -C "$BUILD" -j "$JOBS" distribution
else
    ninja -C "$BUILD" distribution
fi

log "Installing into $PREFIX"
rm -rf "$PREFIX"
ninja -C "$BUILD" install-distribution

log "Installing the runtime sources used by the OS build"
mkdir -p "$PREFIX/src/llvm-project"
for d in cmake compiler-rt libc libcxx libcxxabi libunwind llvm runtimes third-party; do
    if [ -d "$SRC/$d" ]; then
        rm -rf "$PREFIX/src/llvm-project/$d"
        cp -a "$SRC/$d" "$PREFIX/src/llvm-project/$d"
    fi
done
[ -f "$SRC/LICENSE.TXT" ] && cp "$SRC/LICENSE.TXT" "$PREFIX/LICENSE.TXT"

cat > "$PREFIX/native-toolchain.json" <<EOF
{
  "version": "$(date +%Y%m%d)",
  "host": "$(uname -s)-$(uname -m)",
  "source": "$LLVM_REPO",
  "revision": "$LLVM_REV",
  "targets": "$(printf '%s' "$LLVM_TARGETS" | tr ';' ',')"
}
EOF

log "Toolchain installed: $PREFIX"
"$PREFIX/bin/clang" --version | head -n 1
