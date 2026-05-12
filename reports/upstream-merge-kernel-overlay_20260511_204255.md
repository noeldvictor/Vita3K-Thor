# Upstream Merge: Kernel and Android Overlay - 2026-05-11 20:42:55

## Summary

Merged the latest reachable `upstream/master` work into `master` while preserving Thor-specific changes for fast forward, OSD hotkeys, quick states, ZIP cartridge loading, and Android deployment.

## Upstream Changes Included

- Android overlay update: add L3, R3, and hide-controller overlay buttons.
- Kernel cleanup: remove the old CPUProtocol abstraction.
- Kernel cleanup: inline SVC dispatch in the thread run loop.
- Debugger/kernel cleanup: remove unused trampoline machinery.
- Kernel cleanup: extract abort dispatch into `ThreadState::dispatch_abort`.

## Conflict Resolution

Resolved the merge conflict in `vita3k/kernel/src/kernel.cpp`.

Kept the upstream `call_import` initialization and preserved the Thor fast-forward timing anchors:

- `speed_anchor_host_process_us`
- `speed_anchor_guest_process_us`
- `speed_percent`

## Validation

Android build passed after conflict resolution:

```text
.\gradlew.bat --stacktrace --configuration-cache --build-cache --parallel --configure-on-demand :android:assembleReldebug
```

## Notes

The upstream fetch still reported an SDL submodule object issue, but the main `upstream/master` branch fetched and merged successfully.

The existing untracked diagnostic file `reports/thor-input-devices_20260510_173015.txt` was left untouched.
