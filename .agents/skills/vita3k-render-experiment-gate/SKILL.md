---
name: vita3k-render-experiment-gate
description: Use before any Vita3K Thor renderer/core experiment, risky debug prop, shader/depth/texture toggle, draw skip, or code edit to prevent repeated hypotheses and record SQLite/experiment-packet evidence.
metadata:
  short-description: Gate renderer experiments
---

# Vita3K Render Experiment Gate

Use this skill before changing renderer/core code or running a live graphics toggle that could affect another game.

## Gate

1. Verify the focused case:

```powershell
python tools/debug_knowledge.py case focus
python tools/debug_knowledge.py case show <case-slug>
```

2. Search recent and long-term memory:

```powershell
python tools/debug_knowledge.py attempt list --case <case-slug> --limit 20
python tools/debug_knowledge.py search "<title id symptom shader hash surface address prop platform>" --recent-days 30
python tools/debug_knowledge.py search "<title id symptom shader hash surface address prop platform>" --long-term
```

3. Check the exact planned hypothesis:

```powershell
python tools/debug_knowledge.py attempt check --case <case-slug> --platform <windows|android> --subsystem <area> --hypothesis "<one-variable hypothesis>"
```

4. If a comparable attempt already failed or was inconclusive, do not repeat it unchanged. Either add instrumentation, change one controlled condition, or record why the old result is now superseded.

## Experiment Packet

Start a packet before risky toggles or code:

```powershell
python tools/renderer_experiment.py start --case <case-slug> --title-id <TITLEID> --platform <windows|android|windows-android> --subsystem <area> --hypothesis "<one-variable hypothesis>" --expected "<visible/log outcome>" --scene "<exact scene>" --baseline-artifact <burst-or-log-dir>
```

Close it after proof:

```powershell
python tools/renderer_experiment.py finish --manifest tmp/renderer-experiments/<packet>/manifest.json --status succeeded|failed|inconclusive|superseded --result "<actual result>" --artifact <burst-or-log-dir>
```

Use stable outcome language: `fixed`, `improved`, `unchanged`, `worse`, `mixed-supports-involvement`, or `contaminated-inconclusive`.

## Rules

- One variable per experiment.
- Clear stale Windows env vars and Android `debug.vita3k.render_*` props before A/B tests.
- Prefer A/B/A when a live toggle exists.
- Stop after two failed or inconclusive guesses in the same subsystem; the next move is instrumentation, dumps, RenderDoc, or Ghidra evidence.
- Keep raw artifacts under ignored `tmp/`; record summaries and paths in SQLite.
- A diagnostic prop, draw skip, shader hash guard, or forced depth/texture path is not a fix until it becomes emulator-semantics code and passes regression checks.
