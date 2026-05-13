# Android ZIP Open-With Bridge

## What Changed

- Added Android `ACTION_VIEW` handlers for archive launches from file managers and frontends.
- Added Android `ACTION_SEND` handlers for archive share/open flows.
- Supported MIME types:
  - `application/zip`
  - `application/x-zip-compressed`
  - `application/octet-stream`
- Supported path patterns:
  - `.zip`
  - `.vpk`
- Added Java argument bridge:
  - Incoming archive intent becomes `-a true --cartridge <path>`.
  - `file://` URIs use the filesystem path directly.
  - `content://` URIs first try to resolve a real path; if unavailable, they are copied into app-local `cartridge_launch/`.

## Verification

- Built Android reldebug APK successfully:
  - `.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug`
- Installed to connected device:
  - Model: `AYN Thor`
  - Android: `13`
  - Serial: `c3ca0370`
  - `adb -s c3ca0370 install -r android\build\outputs\apk\reldebug\android-reldebug.apk`
- Confirmed Android resolver sees the debug package for archive VIEW intents:
  - `file:///sdcard/Download/test.zip` with `application/zip`
  - `file:///sdcard/Download/test.vpk` with `application/octet-stream`
  - Both resolved to `org.vita3k.emulator.debug/org.vita3k.emulator.Emulator`.
- Confirmed package resolver table includes the new archive MIME handlers and file/content schemes.

## Cocoon Notes

- In Cocoon or another Android frontend, configure the emulator package/activity as:
  - Package: `org.vita3k.emulator.debug` for reldebug/dev builds.
  - Activity: `org.vita3k.emulator.Emulator`.
- Use an Android open/view file intent for `.zip` or `.vpk` archives.
- Release builds without the `.debug` suffix use package `org.vita3k.emulator`.

## Pure ZIP Play Notes

- This commit does not add true direct ZIP random-access.
- True pure ZIP play needs an archive-backed `app0:` VFS that implements file open/read/seek/stat and directory iteration against ZIP entries without extracting the whole archive.
- The current cartridge flow still uses a virtual cartridge cache under `ux0/cart/<TITLEID>`.
