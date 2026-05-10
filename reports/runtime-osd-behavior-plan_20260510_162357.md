# 2026-05-10 16:23:57

## Change

- Updated `AGENTS.md` with the planned runtime OSD behavior.
- Captured the decisions that Back/Select short press should open the OSD, existing Select chord hotkeys must remain available, OSD should pause by default, and runtime feedback should use OSD/overlay instead of Android toasts.

## Notes

- This was a documentation-only update. No APK build or Android install was needed because emulator binaries did not change.
- Next code pass should implement the OSD in the existing ImGui runtime rendering path.
