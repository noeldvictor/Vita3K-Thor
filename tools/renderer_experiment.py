#!/usr/bin/env python3
"""Create and close structured Vita3K renderer experiment packets.

Renderer bugs are easy to contaminate with stale props, old configs, lucky
screenshots, and half-remembered hypotheses. This helper makes each experiment
explicit before code changes: it snapshots repo/device context, checks the
SQLite attempt ledger, writes a manifest/outcome template, and records a
planned attempt. The finish command updates the same attempt with the outcome.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXPERIMENT_ROOT = REPO_ROOT / "tmp" / "renderer-experiments"
DEBUG_KNOWLEDGE = REPO_ROOT / "tools" / "debug_knowledge.py"


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def slugify(value: str) -> str:
    import re

    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return slug or "renderer-experiment"


def rel(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


def run(cmd: list[str], *, timeout: int = 20) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
        return {
            "command": cmd,
            "returncode": proc.returncode,
            "output": proc.stdout.strip(),
        }
    except Exception as exc:  # noqa: BLE001 - diagnostics should not kill packet creation
        return {
            "command": cmd,
            "returncode": -1,
            "output": f"{type(exc).__name__}: {exc}",
        }


def git_context() -> dict[str, Any]:
    return {
        "branch": run(["git", "branch", "--show-current"])["output"],
        "commit": run(["git", "rev-parse", "--short=12", "HEAD"])["output"],
        "status_short": run(["git", "status", "--short"])["output"],
        "diff_stat": run(["git", "diff", "--stat"])["output"],
    }


def windows_process_context() -> dict[str, Any]:
    command = (
        "Get-Process Vita3K -ErrorAction SilentlyContinue | "
        "Select-Object ProcessName,Id,CPU,StartTime,MainWindowTitle | ConvertTo-Json -Depth 3"
    )
    return run(["powershell", "-NoProfile", "-Command", command], timeout=10)


def android_context(adb: str, serial: str) -> dict[str, Any]:
    base = [adb]
    if serial:
        base += ["-s", serial]
    return {
        "devices": run([adb, "devices"], timeout=10),
        "debug_props": run(base + ["shell", "getprop"], timeout=20),
        "focus": run(base + ["shell", "dumpsys", "window"], timeout=20),
    }


def config_context() -> dict[str, str]:
    candidates = [
        Path(os.environ.get("APPDATA", "")) / "Vita3K" / "Vita3K" / "config.yml",
        REPO_ROOT / "tmp" / "vita3k-win-debug" / "config_highacc.yml",
    ]
    output: dict[str, str] = {}
    for path in candidates:
        if path and path.exists():
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError as exc:
                text = f"ERROR reading config: {exc}"
            interesting = []
            for line in text.splitlines():
                lowered = line.lower()
                if any(
                    key in lowered
                    for key in (
                        "backend-renderer",
                        "high-accuracy",
                        "resolution-multiplier",
                        "memory-mapping",
                        "disable-surface-sync",
                        "psn-signed-in",
                        "custom-driver-name",
                        "pref-path",
                    )
                ):
                    interesting.append(line)
            output[rel(path)] = "\n".join(interesting)
    return output


def call_knowledge(args: list[str]) -> dict[str, Any]:
    return run([sys.executable, str(DEBUG_KNOWLEDGE), *args], timeout=30)


def focus_context() -> dict[str, Any]:
    result = call_knowledge(["case", "focus", "--json"])
    if result.get("returncode") != 0:
        return {"focus": None, "error": result.get("output", "")}
    try:
        return json.loads(result.get("output", "") or "{}")
    except json.JSONDecodeError as exc:
        return {"focus": None, "error": f"Could not parse focus case JSON: {exc}"}


def check_focus_guard(args: argparse.Namespace, focus: dict[str, Any]) -> dict[str, Any]:
    focused = focus.get("focus") if isinstance(focus, dict) else None
    if not focused:
        return {"mode": "unfocused", "message": "No focused case is set."}

    focused_slug = focused.get("slug", "")
    if args.case == focused_slug:
        return {"mode": "focused", "message": f"Experiment matches focused case {focused_slug}."}

    if args.regression_guard:
        return {
            "mode": "regression-guard",
            "message": (
                f"Experiment case {args.case} differs from focused case {focused_slug}, "
                "but --regression-guard was provided."
            ),
        }

    if args.allow_non_focus_case:
        return {
            "mode": "override",
            "message": (
                f"Experiment case {args.case} differs from focused case {focused_slug}; "
                "--allow-non-focus-case override was provided."
            ),
        }

    raise SystemExit(
        "Refusing to create renderer experiment for a non-focused case.\n"
        f"Focused case: {focused_slug} ({focused.get('title_id') or '-'})\n"
        f"Requested case: {args.case}\n"
        "Use `python tools/debug_knowledge.py case focus <case>` to change focus, "
        "or pass `--regression-guard` for a deliberate neighboring-game check."
    )


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_start_templates(exp_dir: Path, manifest: dict[str, Any], check_output: str) -> None:
    planned = manifest["planned_attempt"]
    artifacts = "\n".join(f"- {item}" for item in planned.get("baseline_artifacts", [])) or "- TBD"
    regressions = "\n".join(f"- {item}" for item in planned.get("regression_targets", [])) or "- TBD"
    text = f"""# Renderer Experiment Packet

## Identity

- Case: `{planned["case"]}`
- Title ID: `{planned.get("title_id", "")}`
- Platform: `{planned["platform"]}`
- Subsystem: `{planned["subsystem"]}`
- Created: `{manifest["created_at"]}`
- Experiment dir: `{rel(exp_dir)}`

## Hypothesis

{planned["hypothesis"]}

## Expected Signal

{planned.get("expected", "") or "TBD"}

## Scene / Repro

{planned.get("scene", "") or "TBD"}

## Baseline Artifacts

{artifacts}

## Regression Targets

{regressions}

## Outcome Taxonomy

Use one of these labels in the result: `fixed`, `improved`, `unchanged`, `worse`, `mixed-supports-involvement`, or `contaminated-inconclusive`.

## Result

TBD. Include exact burst paths, log snippets, shader hashes, surface addresses, and whether A/B/A returned to baseline.

## Attempt Ledger Check

```text
{check_output}
```
"""
    (exp_dir / "outcome.md").write_text(text, encoding="utf-8")

    commands = f"""# Suggested commands for this experiment.
# Edit paths/counts for the actual scene. Keep commands copied here after use.

python tools/debug_knowledge.py case show {planned["case"]}
python tools/debug_knowledge.py attempt list --case {planned["case"]} --limit 20

# Windows proof burst:
.\\tools\\windows\\capture-vita3k-burst.ps1 -Topic {slugify(planned["case"] + "-" + planned["subsystem"])} -Count 12 -IntervalMs 250
python tools\\analyze_screenshot_burst.py <burst-dir>

# Android proof burst:
$adb = 'C:\\Users\\leanerdesigner\\Documents\\SteamPortableTools\\toolchains\\android-sdk\\platform-tools\\adb.exe'
.\\tools\\android\\capture-thor-burst.ps1 -Adb $adb -Serial c3ca0370 -Topic {slugify(planned["case"] + "-" + planned["subsystem"])} -Count 12 -IntervalMs 350
python tools\\analyze_screenshot_burst.py <burst-dir>

# Finish packet:
python tools\\renderer_experiment.py finish --manifest {rel(exp_dir / "manifest.json")} --status inconclusive --result "replace with actual outcome" --artifact <burst-dir>
"""
    (exp_dir / "commands.ps1").write_text(commands, encoding="utf-8")


def start(args: argparse.Namespace) -> None:
    focus = focus_context()
    focus_guard = check_focus_guard(args, focus)

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    name = f"{stamp}_{slugify(args.case)}_{slugify(args.subsystem)}"
    exp_root = (REPO_ROOT / args.out_dir).resolve() if args.out_dir else DEFAULT_EXPERIMENT_ROOT
    exp_dir = exp_root / name
    exp_dir.mkdir(parents=True, exist_ok=False)

    planned = {
        "case": args.case,
        "title_id": args.title_id,
        "platform": args.platform,
        "subsystem": args.subsystem,
        "hypothesis": args.hypothesis,
        "expected": args.expected,
        "scene": args.scene,
        "game_path": args.game_path,
        "risk": args.risk,
        "baseline_artifacts": args.baseline_artifact,
        "regression_targets": args.regression_target,
    }

    check = call_knowledge(
        [
            "attempt",
            "check",
            "--case",
            args.case,
            "--platform",
            args.platform,
            "--subsystem",
            args.subsystem,
            "--hypothesis",
            args.hypothesis,
            "--expected",
            args.expected,
        ]
    )

    add_args = [
        "attempt",
        "add",
        "--case",
        args.case,
        "--status",
        "planned",
        "--platform",
        args.platform,
        "--subsystem",
        args.subsystem,
        "--hypothesis",
        args.hypothesis,
        "--expected",
        args.expected,
        "--change",
        f"Renderer experiment packet created at {rel(exp_dir)}. No code change yet.",
        "--artifact",
        rel(exp_dir),
    ]
    for artifact in args.baseline_artifact:
        add_args += ["--artifact", artifact]
    add = {"skipped": True, "output": ""}
    if not args.no_db:
        add = call_knowledge(add_args)

    manifest = {
        "created_at": utc_now(),
        "experiment_dir": rel(exp_dir),
        "planned_attempt": planned,
        "git": git_context(),
        "configs": config_context(),
        "windows_process": windows_process_context(),
        "android": android_context(args.adb, args.serial) if args.android_context else {},
        "attempt_check": check,
        "attempt_add": add,
        "focus_case": focus,
        "focus_guard": focus_guard,
        "finish": {},
    }
    write_json(exp_dir / "manifest.json", manifest)
    write_start_templates(exp_dir, manifest, check.get("output", ""))

    print(f"created renderer experiment packet: {rel(exp_dir)}")
    print(focus_guard["message"])
    print((exp_dir / "outcome.md").relative_to(REPO_ROOT))


def finish(args: argparse.Namespace) -> None:
    manifest_path = (REPO_ROOT / args.manifest).resolve() if not Path(args.manifest).is_absolute() else Path(args.manifest)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    planned = manifest["planned_attempt"]
    exp_dir = manifest_path.parent

    artifacts = [manifest["experiment_dir"]]
    artifacts.extend(planned.get("baseline_artifacts", []))
    artifacts.extend(args.artifact)
    artifacts = list(dict.fromkeys(artifacts))

    add_args = [
        "attempt",
        "add",
        "--case",
        planned["case"],
        "--status",
        args.status,
        "--platform",
        planned["platform"],
        "--subsystem",
        planned["subsystem"],
        "--hypothesis",
        planned["hypothesis"],
        "--expected",
        planned.get("expected", ""),
        "--change",
        args.change or f"Closed renderer experiment packet {manifest['experiment_dir']}.",
        "--test-command",
        args.test_command,
        "--result",
        args.result,
    ]
    if args.commit:
        add_args += ["--commit", args.commit]
    for artifact in artifacts:
        add_args += ["--artifact", artifact]

    add = call_knowledge(add_args)
    manifest["finish"] = {
        "closed_at": utc_now(),
        "status": args.status,
        "result": args.result,
        "change": args.change,
        "test_command": args.test_command,
        "artifacts": artifacts,
        "commit": args.commit,
        "attempt_add": add,
    }
    write_json(manifest_path, manifest)

    outcome_path = exp_dir / "outcome.md"
    with outcome_path.open("a", encoding="utf-8") as handle:
        handle.write(
            f"\n## Closed\n\n- Status: `{args.status}`\n- Commit: `{args.commit}`\n- Artifacts: {', '.join(artifacts)}\n\n{args.result}\n"
        )

    print(f"closed renderer experiment packet: {rel(exp_dir)}")
    print(add.get("output", ""))


def main() -> None:
    parser = argparse.ArgumentParser(description="Structured renderer experiment packets for Vita3K Thor")
    sub = parser.add_subparsers(dest="command", required=True)

    start_parser = sub.add_parser("start", help="Create a renderer experiment packet and planned attempt")
    start_parser.add_argument("--case", required=True)
    start_parser.add_argument("--title-id", default="")
    start_parser.add_argument("--platform", required=True)
    start_parser.add_argument("--subsystem", required=True)
    start_parser.add_argument("--hypothesis", required=True)
    start_parser.add_argument("--expected", default="")
    start_parser.add_argument("--scene", default="")
    start_parser.add_argument("--game-path", default="")
    start_parser.add_argument("--risk", default="")
    start_parser.add_argument("--baseline-artifact", action="append", default=[])
    start_parser.add_argument("--regression-target", action="append", default=[])
    start_parser.add_argument("--out-dir", default="")
    start_parser.add_argument("--android-context", action="store_true")
    start_parser.add_argument("--adb", default="adb")
    start_parser.add_argument("--serial", default="")
    start_parser.add_argument("--no-db", action="store_true")
    start_parser.add_argument("--regression-guard", action="store_true", help="Allow a non-focused case only as an explicit regression check")
    start_parser.add_argument("--allow-non-focus-case", action="store_true", help="Override the focused-case guard for deliberate priority changes")
    start_parser.set_defaults(func=start)

    finish_parser = sub.add_parser("finish", help="Close a renderer experiment packet and update its attempt")
    finish_parser.add_argument("--manifest", required=True)
    finish_parser.add_argument(
        "--status",
        required=True,
        choices=["succeeded", "failed", "inconclusive", "superseded"],
    )
    finish_parser.add_argument("--result", required=True)
    finish_parser.add_argument("--change", default="")
    finish_parser.add_argument("--test-command", default="")
    finish_parser.add_argument("--artifact", action="append", default=[])
    finish_parser.add_argument("--commit", default="")
    finish_parser.set_defaults(func=finish)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
