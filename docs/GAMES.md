# Games and 3D applications

Status of 3D graphics on the amd64 live image (QEMU, no GPU, 2026-10-14).

## What was wrong

The build used the in-tree "Mesa 2.6" software renderer, which reports OpenGL 1.1
(`GL_RENDERER "Mesa ReactOS SW Implementation"`). The Direct3D layer (WineD3D) needs
`ARB_fragment_shader` and more, so `D3D11CreateDevice` failed at every feature level
(`hr=0x80004005`) and games using D3D9/10/11 could not start. OpenGL 1.1 programs
(`glgears`) worked.

## What changed

The modern Mesa port (`submodules/mesa`, Mesa 26.2) is fetched by `scripts/feeds update mesa`
and built by default as `mesa_gallium.dll` (softpipe, no LLVM). Result on the live image:

| Check | Before | After |
|---|---|---|
| `GL_RENDERER` | Mesa ReactOS SW Implementation (GL 1.1) | softpipe |
| `d3dfeat`: D3D11 device | fails at all levels | **created, feature level 10.1** |
| `d3dfeat`: D3D11 render and read-back test | not reached | **pass** |
| D3D12 (vkd3d) | no Vulkan | still no Vulkan loader in this build |

Softpipe is a slow reference rasteriser. It makes D3D11 programs start; it is not fast enough
for most games.

## LLVMpipe, Lavapipe and DXVK (works)

`cmake -DENABLE_MESA_LLVMPIPE=ON` builds Mesa with LLVMpipe (JIT) and the Lavapipe software Vulkan
ICD plus the Vulkan loader. It builds LLVM 22 from source (about 25 minutes with 64 cores, a few GB).
Results on the amd64 live image, QEMU without a GPU:

| Check | Result |
|---|---|
| `GL_RENDERER` | llvmpipe |
| `d3dprobe` (system `d3d11.dll`, WineD3D on OpenGL) | device at **feature level 11.0**; 200 clears of 512x512 in 15 ms |
| `d3dfeat` D3D12 (vkd3d on Lavapipe) | device created, command lists, Device5 interfaces; some subresource and shared-resource checks fail |
| `d3dprobe` with **DXVK 2.5.3** `d3d11.dll`/`dxgi.dll` next to it | device at **feature level 11.0** on `llvmpipe (LLVM 22.1.8)` through Vulkan |

So the same graphics stack Proton uses (DXVK on Vulkan) runs on LakoOS. To try it with a game, copy
DXVK's `d3d11.dll`, `d3d10core.dll` and `dxgi.dll` (`tools/fetch-dxvk.sh`) next to the game's `.exe`.

This is a CPU renderer. It makes D3D9 to D3D12 programs run; frame rates on a real game on a PC
without a GPU driver will be low. A native GPU driver is the remaining step for speed.

## x64 applications

64-bit Windows programs run (Chromium, OpenTTD x64, Wintun driver). 32-bit programs run through
WoW64 (the 32-bit Mesa ICD is packaged too). The remaining blockers for games are GPU speed, Steam's
own network check, and anti-cheat or DRM drivers, which are not supported.
