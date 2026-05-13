# UPPERS Ghidra Renderer Depth Plan - 2026-05-12 21:29

## Summary

We have Ghidra locally and can use it for UPPERS, but the raw Vita `eboot.bin` path needs one preprocessing step. The files in the UPPERS ZIP start with an `SCE\0` container header; stock Ghidra rejects that as "No load spec found". The embedded ARM ELF starts at offset `0xA0`, and extracting from that offset gives Ghidra something it can import.

This is worth doing because the live renderer probes already point away from blind shader guessing and toward GXM scene/depth semantics.

## Local Ghidra Setup

- Ghidra path: `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC`
- Headless path: `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat`
- VitaLoaderRedux release checked: `https://github.com/CreepNT/VitaLoaderRedux/releases/tag/1.09`
- Downloaded asset: `ghidra_12.0_PUBLIC_20251228_VitaLoaderRedux.zip`
- SHA256 observed locally: `9DA723EE6080E26B758F832D43F268F3B3A2CCE119BDEEF79A3D974B795AA7FE`
- Installed locally under: `Ghidra\Extensions\VitaLoaderRedux`

## UPPERS Import Findings

- Raw files extracted to ignored temp:
  - `tmp\ghidra-uppers\inputs\patch_PCSG00633_eboot.bin`
  - `tmp\ghidra-uppers\inputs\app_PCSG00633_eboot.bin`
- Raw header starts with `SCE\0`, so headless import fails with no load spec.
- Embedded ELF offset for both app and patch eboots: `0xA0`.
- Extracted temp ELFs:
  - `tmp\ghidra-uppers\inputs\patch_PCSG00633_eboot.elf`
  - `tmp\ghidra-uppers\inputs\app_PCSG00633_eboot.elf`
- Ghidra generic ELF import succeeds on the extracted patch ELF as `ARM:LE:32:v8:default`.
- The forced VitaLoaderRedux loader name did not resolve from headless after manual extension install, so NID naming still needs either a proper Ghidra UI extension enable step or a repo script that walks Vita import tables directly.

## Renderer Evidence So Far

- `i32mad2` repeat handling fixed the worst character geometry corruption: hair and pants stopped exploding after repeat-aware translation.
- Forcing the UPPERS world fragment shader depth compare to `Always` made the stage/environment appear, which proves the world is being submitted and textured.
- Forcing shadow compare/predicate behavior did not restore the stage.
- Forcing depth clear/load behavior did not restore the stage.
- The active UPPERS scene is a 704x396 render target using depth surface `0x626F3000`, with early world draws using fragment hash `453c0f4281afb3602a6f29f18fc01ca834d36415ae48768640a2810b75d3d41f`.

## Strong Emulator Lead

Vita3K's `sceGxmBeginSceneEx` implementation is currently a stub:

```cpp
STUBBED("Using sceGxmBeginScene");
return CALL_EXPORT(sceGxmBeginScene, immediateContext, flags, renderTarget, validRegion, vertexSyncObject, fragmentSyncObject, colorSurface, loadDepthStencilSurface);
```

That ignores `storeDepthStencilSurface`. Since UPPERS behaves like a depth/load/store problem rather than a missing draw problem, this is the next high-signal area. Do not keep restarting and replaying the intro until we know how UPPERS sets the `BeginSceneEx` load/store surfaces.

## Next Debug Loop

1. Added `sceGxmBeginSceneEx` trace logging when `--thor-render-trace` is enabled:
   - flags, render target size
   - color address/format/type/stride
   - load depth/stencil addresses, format, type, stride, force load/store, background depth/stencil
   - store depth/stencil addresses, format, type, stride, force load/store, background depth/stencil
   - whether load/store pointers and backing addresses match
2. Reach the UPPERS bad scene once on Windows, enable render trace, and capture the `BeginSceneEx` lines plus the first 150 draw lines.
3. If load and store surfaces differ, change the renderer command model to carry both instead of flattening the API into one `SceGxmDepthStencilSurface`.
4. Keep Android compatibility by verifying the final behavior on Thor after the Windows loop identifies the emulator-side fix.

## Useful Commands

```powershell
.\tools\ghidra\ExtractEmbeddedVitaElf.ps1 -InputPath tmp\ghidra-uppers\inputs\patch_PCSG00633_eboot.bin -OutputPath tmp\ghidra-uppers\inputs\patch_PCSG00633_eboot.elf
```

```powershell
& 'C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat' `
  tmp\ghidra-uppers UPPERS_PCSG00633 `
  -import tmp\ghidra-uppers\inputs\patch_PCSG00633_eboot.elf `
  -overwrite -analysisTimeoutPerFile 240 `
  -scriptPath tools\ghidra `
  -postScript DumpSymbolsAndRefs.java tmp\ghidra-uppers\uppers_patch_symbols.txt sceGxm Gxm 4709CF5A 8734FF4E depth stencil shader
```
