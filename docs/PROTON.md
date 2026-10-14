# Proton and application compatibility

Short version: LakoOS cannot swap its Wine-derived DLLs for Proton's. Proton is a
Linux runtime layer; LakoOS is a native NT system. What can be reused is listed below.

## What Proton is made of

| Part | Runs on | Reusable in LakoOS? |
|---|---|---|
| Valve's Wine fork (Wine 10.0 base) | Linux, translates NT calls to the Linux kernel | No. LakoOS already has a real NT kernel. Its Wine-synced DLLs are at Wine 11.14, newer than Proton's base, so swapping would be a downgrade. |
| esync / fsync, winevulkan, steam.exe helper, winex11 | Linux only | No. |
| DXVK (D3D9/10/11 on Vulkan) | Windows DLLs, zlib license | Yes, needs a Vulkan driver. |
| vkd3d-proton (D3D12 on Vulkan) | Windows DLLs, LGPL | Yes, needs a Vulkan driver. |
| FAudio, dxvk-nvapi, media converters | Mostly portable | Case by case. |

## Plan

1. Vulkan: `dll/win32/vulkan-1` is the loader. A usable GPU driver (ICD) is the blocker;
   a software ICD (lavapipe) is the first target for verification.
2. DXVK and vkd3d-proton ship as optional drop-in DLLs, never replacing the in-tree
   `d3d9`/`d3d11`/`dxgi` by default. `tools/fetch-dxvk.sh` downloads a release for review.
3. Per-application fixes found in Proton's patch set are ported individually into the
   matching in-tree DLL when LakoOS shows the same failure.
4. On ARM64, x86 and x86-64 applications run through FEX; ARM64EC is supported for native mixes.

Status: nothing in 2 and 3 is verified yet. No Vulkan ICD exists for LakoOS today.

## Triage of Proton's own patches (proton_10.0, last 400 commits)

Most of Proton's value over upstream Wine is a long list of small compatibility fixes, many written by
Paul Gofman and Remi Bernon. Taking a sample of the ntdll, kernelbase, win32u, mf and audio commits:

| Proton change | In LakoOS |
|---|---|
| Synchronization barrier functions (ntdll, kernelbase) | present (kernelbase exports `EnterSynchronizationBarrier`) |
| `GetIntegratedDisplaySize` stub | present |
| `GetWindowDisplayAffinity` | present (user32) |
| `NtUnlockFile` fills the IO status block | exported; behaviour not checked |
| x64 epilogue unwind fixes (jmp 0xff, chained unwind, tail call) | not compared. `RtlVirtualUnwind` apitest does not compile on the current compiler, which is a reason to look |
| `RtlCreateHeap` aligned commit size | not compared |
| `ProcessImageFileNameWin32` in `GetModuleFileNameExW` | not compared |
| Gamescope and per-game HACK commits | not applicable (Linux compositor / per-title workarounds) |
| `PROTON_LIMIT_ADDRESS_SPACE` | not applicable |

These come from reading commit titles, not from running games. The right next step is to use them as
a checklist while testing real titles, as listed in GAMES.md.
