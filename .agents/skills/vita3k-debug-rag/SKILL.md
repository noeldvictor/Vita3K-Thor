---
name: vita3k-debug-rag
description: Use for Vita3K Thor emulator/game bug resolution, renderer regressions, Windows-first/Android-second repro loops, SQLite debug knowledge search/read/write, input automation evidence, and recording durable observations, decisions, fixes, and regression checks without relying on aging Markdown reports.
metadata:
  short-description: SQLite RAG workflow for Vita3K Thor debugging
---

# Vita3K Thor Debug RAG

Use this skill whenever debugging a Vita3K Thor emulator or game issue, especially renderer corruption, crashes, VFS/cartridge behavior, controller/OSD problems, fast-forward/save-state regressions, or Android/Windows differences.

## Source Of Truth

- The canonical report store is `reports/debug_knowledge.sqlite`.
- Use `tools/debug_knowledge.py` to search, read, and write it.
- SQLite keeps raw searchable text in `chunks.text`, plus a repo-local deterministic sparse hashed vector in `chunks.embedding_json`; this is not an external/model embedding. FTS5 is enabled when the local SQLite build supports it.
- Treat existing Markdown reports as legacy context only. Do not rely on them until the SQLite DB has no relevant recent or long-term answer.
- Never commit commercial games, saves, firmware, decrypted binaries, ELF dumps, shader caches, APKs, raw log dumps, or screenshots unless the user explicitly asks for a specific proof asset.

## First Move

Run a recent DB search before editing emulator code:

```powershell
python tools/debug_knowledge.py search "<symptom title id shader hash platform>" --recent-days 30
```

If the issue is architectural or recurring, also run:

```powershell
python tools/debug_knowledge.py search "<symptom title id shader hash platform>" --long-term
```

Before repeating a renderer/core experiment, also check the attempt ledger:

```powershell
python tools/debug_knowledge.py attempt check --case <case-slug> --platform windows --subsystem surface-cache --hypothesis "<specific thing you are about to try>"
```

Prefer recent entries for immediate bug work. Use long-term matches to avoid repeating old failed experiments and to preserve architectural lessons.

## Local ROM Layout

- Stage issue games under ignored `roms/issues/<TITLEID>/`.
- Stage regression games under ignored `roms/regression/<TITLEID>/`.
- Use `tools/sync_issue_rom.ps1` to copy a local game ZIP or pull one from Thor into the ignored layout.
- Keep issue ROM names stable, ideally `game.zip`, so Windows launch helpers can find them.

## Bug Resolution Loop

1. Create or update a case in SQLite.
2. Record the current symptom as an observation with platform, title ID, screenshot/log paths, shader hashes, and draw filters when available.
3. Run `attempt check` for the next planned hypothesis/change before editing.
4. Classify the bug before patching: emulator core, shader translator, renderer state, VFS/cartridge, platform driver/presentation, input/OSD, timing/audio, or unknown.
5. For serious emulator/render bugs, reproduce on Windows first unless the evidence already proves Android-only behavior.
6. Patch the smallest plausible emulator subsystem.
7. Verify Windows proof first.
8. Verify Android/Thor proof second for Android-affecting changes.
9. Record the attempt as `succeeded`, `failed`, `inconclusive`, or `superseded`, including artifacts and commands.
10. Record tests, regression risk, final fix, and commit hash in SQLite.
11. Commit and push small checkpoints.

## Compatibility Ledger

When a game is known-good, newly broken, partially fixed, or blocked at a specific commit, record a compatibility checkpoint. This prevents future agents from treating "it used to work" as memory instead of evidence:

```powershell
python tools/debug_knowledge.py compat add --case uppers-render-regression --title-id PCSG00633 --title-name UPPERS --platform android-thor --commit <hash> --status works --scene "exact scene" --summary "what worked" --artifact <burst-or-proof>
python tools/debug_knowledge.py compat list --title-id PCSG00633 --platform android-thor
```

Use `status=works`, `broken`, `regressed`, `partial`, `blocked`, or `unknown`. Include the exact scene and proof artifact. For renderer regressions, add one checkpoint for the last known-good commit and one for the current broken commit before bisecting.

## Burst Screenshot Rule

Emulator/render screenshots default to burst capture. Single screenshots are only for static UI proof after a burst exists, or when the user explicitly asks for one image.

For Windows-first renderer checks:

```powershell
.\tools\windows\capture-vita3k-burst.ps1 -Topic doa-venus-render-corruption -Count 10 -IntervalMs 250
```

For Thor/Android checks:

```powershell
.\tools\android\capture-thor-burst.ps1 -Adb $adb -Serial c3ca0370 -Topic doa-venus-render-corruption -Count 10 -IntervalMs 350
```

Use 8-12 frames for normal flicker checks, and 60-120 frames when the issue appears only after a camera/menu rotation. After capture, run `python tools/analyze_screenshot_burst.py <burst-dir>` and use `flicker_summary.txt`, `flicker_metrics.csv`, and `flicker_contact_sheet.jpg` to find the largest transitions. Store the raw PNGs under ignored `tmp/`, then record the burst directory, analysis outputs, and the few meaningful frame numbers in SQLite.

## Windows Fast Loop

Use the generic launcher:

```powershell
.\tools\windows\start-game-render-debug.ps1 -TitleId PCSH00250 -CaseSlug doa-venus-render-corruption
```

Use the generated control file to pause/resume, save/load state, toggle draw filters, dump shader/texture/pipeline state, and run stop-after/skip sweeps without rebuilding or relaunching when possible.

Automate button presses against the Vita3K window:

```powershell
.\tools\windows\send-vita3k-input.ps1 -Sequence circle,wait:500,start
.\tools\windows\send-vita3k-input.ps1 -Sequence osd
.\tools\windows\send-vita3k-input.ps1 -Sequence fast_forward
.\tools\windows\send-vita3k-input.ps1 -Sequence click
```

Use `click` for Vita3K/ImGui modal buttons that do not respond to game/Vita controls.

## Android Final Gate

Android/Thor remains required for:

- Adreno/Turnip driver behavior.
- SurfaceFlinger/present issues.
- Android input routing.
- APK asset/package correctness.
- Real handheld performance and thermals.

Use `tools/thor_profile_dump.ps1`, `tools/thor_adb_debug_capture.ps1`, and `tools/thor_live_debug_stream.ps1` for device evidence. Profile/debug captures save screenshot bursts by default and keep `screen.png` as a compatibility copy. Record final Android observations and test results in SQLite.

Automate Thor button presses:

```powershell
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Sequence circle,wait:500,start
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Mode Sendevent -Sequence back
.\tools\android\send-thor-input.ps1 -Adb $adb -Serial c3ca0370 -Mode Sendevent -Sequence osd
```

Use `KeyEvent` mode for simple UI/game prompts. Use `Sendevent` mode when debugging Thor/Odin controller routing, Back/Select conflicts, or OSD chords.
For Android quickstate actions, prefer runtime control or OSD automation over fake right-stick axis events until the exact Thor axis codes are captured for the current firmware.

## SQLite Commands

Create/update a game case:

```powershell
python tools/debug_knowledge.py case upsert --domain game --title-id PCSH00250 --slug doa-venus-render-corruption --status active --severity high --platform-scope windows-android --summary "DOA Venus terrain/foliage corruption differs between Windows and Thor" --hypothesis "Renderer state or surface-cache behavior needs isolated Windows-first proof before Android final verification."
```

Add an observation:

```powershell
python tools/debug_knowledge.py entry add --case doa-venus-render-corruption --type observation --platform android --summary "Thor burst shows black terrain masks" --shader-hash 564cd0 --shader-hash 1c4c944 --artifact tmp/thor-burst/example/ --body "Describe exact scene, driver, build commit, which burst frames matter, and what changed."
```

Check whether the next experiment was already tried:

```powershell
python tools/debug_knowledge.py attempt check --case doa-venus-render-corruption --platform windows --subsystem surface-cache --hypothesis "Force copied sampling for MSAA downscaled U2F color surfaces" --shader-hash 1c4c944
```

Record an experiment result:

```powershell
python tools/debug_knowledge.py attempt add --case doa-venus-render-corruption --status failed --platform windows --subsystem surface-cache --hypothesis "Force copied sampling for MSAA downscaled U2F color surfaces" --change "Removed the Adreno-only gate so Windows also avoids direct sampling" --test-command ".\tools\windows\capture-vita3k-burst.ps1 -Topic doa-venus-render-corruption -Count 10 -IntervalMs 250" --result "Burst still alternated clean and corrupted title frames." --artifact tmp/vita3k-win-debug/example-burst/ --shader-hash 1c4c944
```

List the recent attempt history for a case:

```powershell
python tools/debug_knowledge.py attempt list --case doa-venus-render-corruption --limit 10
```

Show the current case:

```powershell
python tools/debug_knowledge.py case show doa-venus-render-corruption
```

Record an automation step:

```powershell
python tools/debug_knowledge.py entry add --case doa-venus-render-corruption --type test --platform android --summary "Automated Circle then Start to reach title prompt" --body "Used tools/android/send-thor-input.ps1 -Mode KeyEvent -Sequence circle,wait:500,start. Result: prompt advanced without manual input."
```
