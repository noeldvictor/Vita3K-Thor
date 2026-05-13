# Thor renderer fix APK refresh

## Summary

- The Windows UPPERS renderer improvement did not fully make it to Thor in the previous install because the APK installed at `2026-05-12 22:00:12` reused an existing `android-reldebug.apk`.
- That APK was last built at `2026-05-12 18:03:05`, before the committed i32mad2 shader translator fix was available for Android testing.
- A fresh Android `reldebug` APK was built and installed to the connected AYN Thor.
- The shader translator cache version was bumped from `13` to `14` in `92c4ad82` so stale translated shader blobs are invalidated.

## Device Install

- Device: AYN Thor / Android 13 / `kalama`
- Package: `org.vita3k.emulator.debug`
- APK: `android/build/outputs/apk/reldebug/android-reldebug.apk`
- APK build time after rebuild: `2026-05-12 22:04:28` before cache bump, then rebuilt again after `92c4ad82`
- Install result: `adb install -r` returned `Success`
- Android package metadata after final install: `lastUpdateTime=2026-05-12 22:07:36`

## Shader Cache

- UPPERS title ID: `PCSG00633`
- Existing Thor cache had `vk13` shader blobs under `/sdcard/Android/data/org.vita3k.emulator.debug/files/cache/shaders/PCSG00633/eboot.bin`.
- Cleared only the UPPERS shader cache and shader log:
  - `/sdcard/Android/data/org.vita3k.emulator.debug/files/cache/shaders/PCSG00633`
  - `/sdcard/Android/data/org.vita3k.emulator.debug/files/shaderlog/PCSG00633`
- Next UPPERS launch should compile fresh `vk14` shaders from the fixed translator.

## Verification

- Android build completed successfully with `:android:assembleReldebug`.
- Fresh APK installed successfully with `adb install -r`.
- Vita3K was launched through ADB.
- Window focus included `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator`.

## Notes

- If the Thor scene still renders incorrectly after this refresh, the issue is no longer "Windows fix missing from Android APK" or stale UPPERS shader cache. The next debugging step should compare a fresh Thor screenshot/logcat/render trace against the Windows fixed scene.
