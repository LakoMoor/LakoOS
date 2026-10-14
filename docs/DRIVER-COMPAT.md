# Driver compatibility

Two checks, both reproducible.

## 1. Static import check

    python3 tools/driver-compat.py output-Clang-amd64-debug path/to/driver.sys ...

Needs `pip install pefile`. For each import it looks the function up in the matching module of the
build (ntoskrnl.exe, hal.dll, ndis.sys, fltmgr.sys, ...) and reports missing functions, modules not in
the build and the wrong CPU architecture. It cannot show run-time behaviour.

## 2. Load test

Copy the driver to `System32\drivers`, then `sc create x type= kernel binpath= system32\drivers\x.sys`
and `sc start x` in the live image (amd64, QEMU).

## Results (2026-10-14, debug build)

| Driver | Source | Static check | Load test |
|---|---|---|---|
| System Informer `SystemInformer.sys` (+ `ksi.dll`) amd64 | [System Informer 4.0.26241.138](https://github.com/winsiderss/systeminformer/releases) | all 269 imports resolve | `sc start` returns error 2 (file not found). Imports are all present, so the error comes from inside the driver's initialisation. Cause not found |
| System Informer arm64 | same | all 270 imports resolve | not run |
| Wintun 0.14.1 `wintun.sys` amd64 (NDIS 6 miniport) | [wintun.net](https://www.wintun.net) | all 67 imports resolve | **system stops with 0x1E** while the driver runs: `ExReleaseResourceLite` in the driver's initialisation hits `ASSERT(Owner->OwnerThread == Thread)` in `ntoskrnl/ex/resource.c:1978`. Looks like an `ExAcquire*Resource` / `ExReleaseResource` pairing problem in the kernel resource code |
| Wintun arm64 | same | all 68 imports resolve | not run |
| In-tree `usbport.sys`, `ntoskrnl.exe` | this repo | all resolve | loads (boot) |

## Reading this

- "All imports resolve" means the driver can be linked by the loader. It is necessary, not sufficient.
- The Wintun stop is a real kernel finding and the next thing to fix for NDIS drivers from outside the tree.
- Not covered: vendor GPU, audio and Wi-Fi drivers (Intel, Realtek, NVIDIA, AMD, Qualcomm). They are
  only distributed as installers that need a licence click-through; run `driver-compat.py` on their
  extracted `.sys` files to get the static result.
