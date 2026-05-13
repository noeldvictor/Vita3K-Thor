# DOA Venus app0 dot path Windows proof

## Summary

Dead or Alive Xtreme 3 Venus was not only showing a black-screen/render symptom. The Windows-first trace showed the game opening `app0:.` while running directly from a Vita3K cartridge ZIP, and Vita3K treated that archive-relative `.` as a missing directory instead of the app root.

## Evidence

- Command path: Windows `RelWithDebInfo` build launched with `--cartridge --thor-render-trace --backend-renderer Vulkan`.
- Game: `DEAD OR ALIVE Xtreme 3 Venus [PCSH00250]`.
- Archive mount selected `app/PCSH00250/` and applied `patch/PCSH00250/`.
- Before the fix, trace contained `Archive directory does not exist at app0:.`, followed by a guest null-PC failure near the game's archive/file-handle setup path.
- After normalizing `.` archive segments to the archive root, trace shows `_sceIoDopen: Opening archive dir app0:.` and then root entries such as `00`, `01`, `sce_module`, and `sce_sys`.
- The later 90-second Windows run reached thousands of `ThorRenderTrace` frames without the previous null-PC crash.

## Code Change

- `vita3k/io/src/io.cpp`: archive path normalization now drops `.` path segments, so `app0:.`, `app0:/./`, and `app0:./foo` resolve like Vita game code expects.
- `vita3k/main.cpp`: `--thor-render-trace` now enables import/export logging and loaded ELF dumps for Windows-first diagnosis.
- `vita3k/modules/SceGxm/SceGxm.cpp`: `sceGxmGetRenderTargetMemSize` logs params when renderer trace is active, including corrected unsigned `driverMemBlock` formatting.
- `tools/ghidra/DumpAddressContext.java`: adds a small headless helper to dump the function, references, memory blocks, and nearby instructions for a traced guest address.

## Current State

This removes a real cartridge-ZIP compatibility blocker and makes DOA progress into active rendering. It does not prove the Android/Thor black screen is fixed yet; that still needs an Android build/install pass and a fresh Thor screenshot/log after this VFS fix lands.

## Next

- Commit and push this focused Windows proof.
- Build/install Android on AYN Thor.
- Re-test DOA Venus on Thor with renderer trace enabled and compare the new Android failure against the Windows trace.
