# Virtual Cartridge Cache And Encrypted Dump Detection - 2026-05-10 19:51

## Summary

Added app-list caching for virtual ZIP/VPK cartridges and cached archive icon/background assets so unchanged ROM folders do not need full archive introspection on every launcher start.

Also added encrypted-content detection for virtual cartridges. Several user ZIPs have readable `sce_sys/param.sfo` metadata but encrypted-looking `eboot.bin` and `sce_sys/icon0.png` payloads. Those entries now show an `E` badge in the game list and fail direct ZIP launch with a clear error instead of showing blank icons or trying to boot encrypted bytes.

The direct archive mount path also checks the same headers before mounting, so Android open-with/front-end launches fail early when a ZIP contains encrypted app files.

## Specific ZIPs Checked

Detected as encrypted-content entries:

- `PCSE00871` - `A.W. - Phoenix Festa (USA)[NoNpDrm].zip`
- `PCSA00011` - `Gravity Rush (USA).zip`
- `PCSA00155` - `Oreshika Tainted Bloodlines (USA).zip`
- `PCSE00429` - `TALES OF HEARTS R [PCSE00429] [NTSC].zip`
- `PCSE00905` - `Adventures Of Mana (USA).zip`
- `PCSG01293` - `Amanane[eng].zip`
- `PCSG00599` - `Date-A-Live Twin Edition - Rio Reincarnation (English v0.95).zip`

Working `[vita3k]` comparison ZIPs had plaintext PNG icon headers and Vita executable headers, while the entries above had readable SFO headers but non-PNG icon bytes and non-`SCE\0` executable bytes.

## Thor Verification

- Device: AYN Thor, ADB serial `c3ca0370`
- Package: `org.vita3k.emulator.debug`
- APK: `android/build/outputs/apk/reldebug/android-reldebug.apk`
- Build: `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`
- Install: `adb install -r android/build/outputs/apk/reldebug/android-reldebug.apk`

Results:

- Fresh cache version invalidated old `apps.dat` and rescanned virtual cartridges.
- Logcat reported encrypted-content warnings for the seven entries listed above.
- Screenshot confirmed `E` badges in the app list.
- Launching `PCSE00871` from the list showed the new error dialog: pure ZIP mode needs Vita3K-readable app files from the user's own legally dumped content.
- Rebuilt after adding the direct archive mount guard and reinstalled the APK on Thor with `adb install -r`.

## Notes

This is detection and UX only. It does not add DRM, license, key, or PFS bypass code. For those physical cards, the practical next step is to use the user's own Vita/game cards to produce Vita3K-readable dumps through a legitimate personal dumping workflow, then retest pure ZIP mode.
