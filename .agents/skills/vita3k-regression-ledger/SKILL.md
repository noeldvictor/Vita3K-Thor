---
name: vita3k-regression-ledger
description: Use when proving a Vita3K fix did not break other games, researching which commit a game worked on, recording compatibility checkpoints, or running a Windows/Thor regression matrix.
metadata:
  short-description: Game compatibility ledger
---

# Vita3K Regression Ledger

Use this skill when the user says a game used to work, a new fix may have broken another game, or a renderer change needs UPPERS/DOA-style regression proof.

## Query First

```powershell
python tools/debug_knowledge.py compat list --title-id <TITLEID> --platform windows
python tools/debug_knowledge.py compat list --title-id <TITLEID> --platform android-thor
python tools/debug_knowledge.py search "<TITLEID> worked broken regressed commit scene>" --recent-days 90
python tools/debug_knowledge.py search "<TITLEID> worked broken regressed commit scene>" --long-term
```

Do not trust memory or screenshots without commit/scene/platform context.

## Checkpoint

Record known states:

```powershell
python tools/debug_knowledge.py compat add --case <case-slug> --title-id <TITLEID> --title-name "<game name>" --platform <windows|android-thor> --commit <hash> --status works|broken|regressed|partial|blocked|unknown --scene "<exact scene>" --summary "<what proof showed>" --artifact <burst-or-log-dir>
```

For renderer regressions, record both the last known-good commit and the current broken commit before bisecting or reverting anything.

## Matrix

Use the smallest matrix that answers the risk:

- Windows focused game at the exact bad scene.
- Windows regression guard game at its known-good scene.
- Android/Thor focused game if the code path affects Android.
- Android/Thor regression guard game if the fix touches shared renderer, texture, input, save-state, or APK behavior.

Raw bursts/logs stay under ignored `tmp/`. SQLite gets the durable summary, artifact path, commit, platform, and result.

## Rules

- A game is not "fixed" unless the acceptance scene is named and captured.
- A game is not "broken by this commit" unless the prior known-good state and current bad state are comparable.
- If evidence is blocked by input automation, ANR, save/load failure, missing ROM, or scene mismatch, record `blocked` instead of guessing.
