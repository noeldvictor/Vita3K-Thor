# Android Present Alpha Fix - 2026-05-12 22:48:30

## Context

After the Windows UPPERS renderer fixes were installed on AYN Thor, the same UPPERS scene rendered as an almost black frame on Android. The black capture still showed faint scene-line detail, and switching from the wrong A8XX Turnip driver to an Adreno 7xx GMEM Turnip driver did not fix it.

That pointed to an Android presentation/compositor path problem, not only a game draw problem.

## Change

Forced the Vulkan screen-present shaders to write opaque alpha:

- `render_main.frag` now writes `vec4(rgb, 1.0)` instead of a `vec3` color output.
- `render_main_fxaa.frag` now writes `vec4(rgb, 1.0)` instead of a `vec3` color output.
- `render_main_bicubic.frag` now forces sampled output alpha to `1.0`.
- Regenerated the matching SPIR-V assets.

This avoids undefined or source-frame alpha reaching Android/SurfaceFlinger, where it can make an otherwise valid emulator frame composite as black.

## Build And Device

- Rebuilt Android reldebug successfully with:
  `.\gradlew.bat :android:assembleReldebug`
- Installed:
  `android/build/outputs/apk/reldebug/android-reldebug.apk`
- Device:
  `c3ca0370` / `AYN_Thor`
- Package:
  `org.vita3k.emulator.debug`

## Verification

Launched UPPERS directly as a virtual cartridge:

```powershell
adb shell am start -n org.vita3k.emulator.debug/org.vita3k.emulator.Emulator --esa AppStartParameters '-a,true,--cartridge,/storage/2664-21DE/Roms/psvita/Uppers (English v0.97)[vita3k].zip,--log-level,0'
```

Post-install screenshot `tmp/vita3k-alpha-present-20260512_224602.png` shows the UPPERS auto-save screen rendered visibly on Thor. The exact late glitch scene still needs a fresh user repro after this build.

## Next Check

Have the user return to the UPPERS glitch scene on Thor and capture again. If the scene is still black, the next candidate is not driver selection or final present alpha; continue with the 704x396 render-target/depth path and Android-specific Adreno/Turnip behavior.
