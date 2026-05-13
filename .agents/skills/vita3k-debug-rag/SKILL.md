---
name: vita3k-debug-rag
description: Use for Vita3K Thor emulator/game bug resolution, renderer regressions, Windows-first/Android-second repro loops, SQLite debug knowledge searches, and recording durable observations, decisions, fixes, and regression checks without relying on aging Markdown reports.
metadata:
  short-description: SQLite RAG workflow for Vita3K Thor debugging
---

# Vita3K Thor Debug RAG

Use this skill whenever debugging a Vita3K Thor emulator or game issue, especially renderer corruption, crashes, VFS/cartridge behavior, controller/OSD problems, fast-forward/save-state regressions, or Android/Windows differences.

## Source Of Truth

- The canonical report store is `reports/debug_knowledge.sqlite`.
- Use `tools/debug_knowledge.py` to query and update it.
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

Prefer recent entries for immediate bug work. Use long-term matches to avoid repeating old failed experiments and to preserve architectural lessons.

## Local ROM Layout

- Stage issue games under ignored `roms/issues/<TITLEID>/`.
- Stage regression games under ignored `roms/regression/<TITLEID>/`.
- Use `tools/sync_issue_rom.ps1` to copy a local game ZIP or pull one from Thor into the ignored layout.
- Keep issue ROM names stable, ideally `game.zip`, so Windows launch helpers can find them.

## Bug Resolution Loop

1. Create or update a case in SQLite.
2. Record the current symptom as an observation with platform, title ID, screenshot/log paths, shader hashes, and draw filters when available.
3. Classify the bug before patching: emulator core, shader translator, renderer state, VFS/cartridge, platform driver/presentation, input/OSD, timing/audio, or unknown.
4. For serious emulator/render bugs, reproduce on Windows first unless the evidence already proves Android-only behavior.
5. Patch the smallest plausible emulator subsystem.
6. Verify Windows proof first.
7. Verify Android/Thor proof second for Android-affecting changes.
8. Record tests, regression risk, final fix, and commit hash in SQLite.
9. Commit and push small checkpoints.

## Windows Fast Loop

Use the generic launcher:

```powershell
.\tools\windows\start-game-render-debug.ps1 -TitleId PCSH00250 -CaseSlug doa-venus-render-corruption
```

Use the generated control file to pause/resume, save/load state, toggle draw filters, dump shader/texture/pipeline state, and run stop-after/skip sweeps without rebuilding or relaunching when possible.

## Android Final Gate

Android/Thor remains required for:

- Adreno/Turnip driver behavior.
- SurfaceFlinger/present issues.
- Android input routing.
- APK asset/package correctness.
- Real handheld performance and thermals.

Use `tools/thor_profile_dump.ps1` and `tools/thor_live_debug_stream.ps1` for device evidence. Record final Android observations and test results in SQLite.

## SQLite Commands

Create/update a game case:

```powershell
python tools/debug_knowledge.py case upsert --domain game --title-id PCSH00250 --slug doa-venus-render-corruption --status active --severity high --platform-scope windows-android --summary "DOA Venus terrain/foliage corruption differs between Windows and Thor" --hypothesis "Renderer state or surface-cache behavior needs isolated Windows-first proof before Android final verification."
```

Add an observation:

```powershell
python tools/debug_knowledge.py entry add --case doa-venus-render-corruption --type observation --platform android --summary "Thor screenshot shows black terrain masks" --shader-hash 564cd0 --shader-hash 1c4c944 --artifact tmp/thor-burst/example/current.png --body "Describe exact scene, driver, build commit, and what changed."
```

Show the current case:

```powershell
python tools/debug_knowledge.py case show doa-venus-render-corruption
```
