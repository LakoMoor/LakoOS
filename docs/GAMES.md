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

## Next: LLVMpipe and Lavapipe

`cmake -DENABLE_MESA_LLVMPIPE=ON` builds Mesa with LLVMpipe (JIT, much faster) and the Lavapipe
software Vulkan ICD plus the Vulkan loader. That is the base for DXVK and vkd3d-proton
(see PROTON.md). It builds LLVM 22 from source, so it takes a long time and several GB.

## x64 applications

64-bit Windows programs run (Chromium, OpenTTD x64, Wintun driver). 32-bit programs run through
WoW64. The remaining blockers for games are graphics speed, Steam's own network check, and
anti-cheat or DRM drivers that are not supported.
