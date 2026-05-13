# Back Long-Press Fast-Forward LiveArea Crash - 2026-05-11 18:55:57

## Issue

On AYN Thor, holding Android Back while a virtual cartridge game was running at fast-forward speed 2x or higher could crash Vita3K instead of returning to the Vita LiveArea/home flow.

Crash buffer symbolication pointed at `gui::init_live_area()` dereferencing a missing app entry. Virtual cartridge launch mounted the game by title ID after launch, while the app grid entry remained keyed by the source ZIP path.

## Changes

- Restored fast-forward to 100% before routing long Android Back to the Vita PS/Home path.
- Made app lookups resolve either source app path/archive path or title ID.
- Made LiveArea initialization fail closed with a log message if no app entry can be resolved.
- Updated agent notes so future Back/fast-forward/virtual-cartridge changes preserve this behavior.

## Thor Verification

- Built `:android:assembleReldebug` successfully with the local Java 21, Android SDK/NDK, and vcpkg toolchain.
- Installed `android/build/outputs/apk/reldebug/android-reldebug.apk` to connected AYN Thor with `adb install -r`.
- Launched `org.vita3k.emulator.debug` on AYN Thor.
- Started virtual cartridge title `PCSG00500` (`Chaos Rings III`) from the ZIP-backed game list.
- Opened OSD with short Back, selected `2x`, resumed gameplay, then sent long Back.
- Confirmed the app stayed alive and returned to the Vita LiveArea/default contents for `PCSG00500`.
- Confirmed Android crash buffer was empty after the verification pass.

## Notes

- The LiveArea path may use the title ID for virtual cartridges, while cached grid icons may still be keyed by the source archive path. `get_app_icon()` now falls back through the resolved app entry so both paths remain usable.
- The fix does not change ownership, licensing, or encrypted-content behavior; it only stabilizes UI routing for already-launched virtual cartridge entries.
