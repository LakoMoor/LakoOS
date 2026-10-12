# ARM64 delay-import unwind information

LLD omits unwind records for its ARM64 delay-import tail thunk. The thunk changes
the stack pointer and calls the delay-load helper, so an exception from that
helper cannot unwind to its caller. The unmodified LakoOS delayimp hook tests
terminate with `0xC06D007F` on both Windows 11 ARM64 and LakoOS.

[`llvm-arm64-delayload.patch`](llvm-arm64-delayload.patch) adds a packed `.pdata`
entry for the existing thunk. Its 116-byte code saves FP/LR, establishes FP and
allocates 224 bytes. The packed word is `0x07600075`: function length 29
instructions, CR 3, frame size 14 units of 16 bytes. Clang's assembler produces
the same record for the thunk with its corresponding SEH directives. The format
comes from [Microsoft's ARM64 exception-handling specification](https://learn.microsoft.com/en-us/cpp/build/arm64-exception-handling).

The patch was built and tested with LLVM 22.1.8. Both patch contexts also match
LLVM 23.1.0 commit `ea7d852a70e8bdfaf601d6626a760f9771b2c4b4`; its ARM64 thunk
bytes are identical. The corrected delayimp binaries pass Windows 11 and
LakoOS: nohook 18/0, globalhook 396/0 and runtimehook 396/0. Their flag check
permits the SDK's reserved `EXCEPTION_SOFTWARE_ORIGINATE` bit.

## Build and select the linker

Use an existing LLVM source tree and its native host build directory. Prefer
the version shipped with the selected Clang, particularly for LTO. Set these
paths to the existing directories; do not duplicate a source tree or build cache:

```sh
LAKOOS_SOURCE=/absolute/path/to/LakoOS
LLD_SOURCE=/absolute/path/to/existing/llvm-project
LLD_BUILD=/absolute/path/to/existing/native-llvm-build
LAKOOS_BUILD=/absolute/path/to/existing/LakoOS-build
```

Apply the patch once, then enable and build LLD in that native build. Keep its
existing compiler, target and build-type settings:

```sh
patch --dry-run -d "$LLD_SOURCE" -p1 < "$LAKOOS_SOURCE/submodules/llvm-arm64-delayload.patch"
patch -d "$LLD_SOURCE" -p1 < "$LAKOOS_SOURCE/submodules/llvm-arm64-delayload.patch"
CCACHE_DISABLE=1 cmake -S "$LLD_SOURCE/llvm" -B "$LLD_BUILD" -DLLVM_ENABLE_PROJECTS=lld
CCACHE_DISABLE=1 cmake --build "$LLD_BUILD" --target lld
cmake -S "$LAKOOS_SOURCE" -B "$LAKOOS_BUILD" -DCMAKE_LINKER:FILEPATH="$LLD_BUILD/bin/ld.lld"
CCACHE_DISABLE=1 cmake --build "$LAKOOS_BUILD" --target delayimp_nohook_apitest delayimp_globalhook_apitest delayimp_runtimehook_apitest
```

On Windows hosts, use `ld.lld.exe`. If the native LLVM build already enables
other projects, retain them and append `lld` to `LLVM_ENABLE_PROJECTS`.
`toolchain-clang.cmake` passes the selected `CMAKE_LINKER` through Clang's
[`--ld-path`](https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-ld-path),
while retaining `-fuse-ld=lld`. With no override, normal RosBE discovery applies.
No installed compiler files or Mesa build definitions need modification.

Check `llvm-readobj --unwind` on the resulting executables for the delay
thunks (five in nohook, six in each hook test) with FunctionLength 116, CR 3 and
FrameSize 224, then run the same binaries
on Windows 11 and LakoOS. Selecting another linker changes link commands for
the whole configured build; revalidate images rebuilt with that selection.

## RosBE packaging

RosBE's `rosbe-unix-bootstrap.sh` and `scripts/versions.env` currently pin the
upstream `llvm-mingw` 20260826 release, containing LLVM 23.1.0. RosBE downloads
and repackages those binaries; it does not build LLVM. The upstream
[`build-llvm.sh`](https://github.com/mstorsjo/llvm-mingw/blob/20260826/build-llvm.sh)
checks out LLVM and builds `clang;lld`. Apply the patch to that LLVM source when
producing a corrected package, then update the RosBE archive pin. Replacing only
the installed `lld` executable would be lost when RosBE is reinstalled.
