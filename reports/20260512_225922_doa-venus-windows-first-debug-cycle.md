# DOA Venus Windows-First Debug Cycle - 2026-05-12 22:59:22

## Summary

Dead or Alive Xtreme 3 Venus currently shows a black screen on AYN Thor. Unlike the final UPPERS Android issue, the live Vita3K log is flooding invalid memory reads with `PC: 0x00000000`, so the first lead is a guest CPU/module/import/null-function-pointer failure rather than only Android SurfaceFlinger presentation.

## Evidence Captured

- Thor screenshot: `docs/screenshots/doa-venus-thor-black-20260512_225651.png`
- Pulled raw Vita3K log to ignored local temp: `tmp/doa-venus-vita3k-20260512_225651.log`
- Running package was confirmed as `org.vita3k.emulator.debug`.
- Cartridge path on Thor: `/storage/2664-21DE/Roms/psvita/Dead or Alive Xtreme 3 - Venus (Asia)(v1.15)(En,Zh,Ko)[vita3k].zip`
- Title ID trail: `PCSG00488`
- Shader logs exist under `shaderlog/PCSG00488`, so the title reached shader compilation before or during the black-screen state.

## Lesson From UPPERS

UPPERS required a two-stage fix path:

- Windows/core renderer fixes first: vertex trap sizing, depth clamp behavior, disabled-channel blend translation, and repeat-aware `i32mad2` shader translation.
- Android/Thor fix second: Vulkan present shaders force opaque alpha so SurfaceFlinger does not show a valid game frame as black.

For DOA Venus, follow the same process but classify the failure first. If the `PC: 0x00000000` failure also reproduces on Windows, use Windows debugging and Ghidra/static analysis before chasing Android composition.

## Proposed Loop

1. Reproduce `PCSG00488` on Windows using the same ZIP/cartridge path and mirrored save/profile data.
2. Capture a Windows log and screenshot before changing code.
3. If Windows also reaches `PC: 0x00000000`, inspect the game `eboot.bin`/modules in Ghidra and map the failing LR/call-site back to imported Vita APIs or a bad callback/function table.
4. Patch the emulator core/module/VFS/shader issue with the smallest reusable fix.
5. Prove the fix on Windows first.
6. Build and install Android APK on Thor.
7. Prove on Thor with screenshot, Vita3K log, logcat, selected driver, and renderer settings.

## Immediate Next Leads

- Pull a clean startup log after relaunching DOA Venus with `--thor-render-trace` so the first bad event is not buried under repeated invalid reads.
- Try the same title on Windows before assuming Adreno/Turnip is at fault.
- If Ghidra is needed, use local headless Ghidra from `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat`.
