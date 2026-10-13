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
