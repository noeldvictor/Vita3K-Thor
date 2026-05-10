# 2026-05-10 14:58:29 - Real ZIP Cartridge Load

## Summary

- Reworked archive cartridge launch so `.zip`/`.vpk` content is mounted directly as `app0:` instead of being extracted into `ux0:cart/<TITLEID>`.
- Added an archive-backed app0 VFS index with virtual directory entries, virtual stat results, and read-only file opens.
- File reads now lazily inflate the requested archive entry into memory on open. This removes whole-archive staging/install for the cartridge launch path.
- Kept the path read-only and rejects writes to archive-backed `app0:`.
- Updated Android UI text and `AGENTS.md` to describe direct archive launch.

## Build

- Command: `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`
- Result: success
- APK: `android/build/outputs/apk/reldebug/android-reldebug.apk`

## Thor Test

- Device: AYN Thor
- Serial: `c3ca0370`
- Install command: `adb -s c3ca0370 install -r android\build\outputs\apk\reldebug\android-reldebug.apk`
- Install result: success
- Launch command: `adb -s c3ca0370 shell monkey -p org.vita3k.emulator.debug 1`
- Runtime check: `pidof org.vita3k.emulator.debug` returned `25963`

## Notes

- This is real no-staging ZIP/VPK launch for the cartridge path: the emulator does not extract the whole archive into a staging folder before booting.
- It is not compressed random-access streaming inside a ZIP entry; each opened entry is inflated to memory when the game requests it.
- NoNpDrm packages that require install-time decryption may still need the normal install path unless the content is already directly loadable.
- I did not run a commercial game ZIP as a functional boot test in this pass.
