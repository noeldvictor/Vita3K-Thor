#!/usr/bin/env python3
"""Turn the Vita3K Thor MCP server on or off for Claude Code.

The server is a development tool. It builds, installs, launches, drives and
inspects the emulator on a real device, so it has no business being registered
while doing anything other than working on this fork - and on a machine where
several agents share one AYN Thor, an idle registration is one more thing that
can reach for the device.

    python tools/mcp_toggle.py status
    python tools/mcp_toggle.py on
    python tools/mcp_toggle.py off

This is a thin, readable wrapper over `claude mcp add|remove|list`; run those
directly if you prefer.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

NAME = "vita3k-thor"
REPO_ROOT = Path(__file__).resolve().parent.parent
SERVER = REPO_ROOT / "tools" / "mcp_server.py"


def _claude() -> str:
    found = shutil.which("claude")
    if not found:
        sys.exit("`claude` is not on PATH, so the MCP registration cannot be changed "
                 "from here. Run `claude mcp add/remove` in a shell that has it.")
    return found


def _run(args: list[str]) -> tuple[int, str]:
    proc = subprocess.run([_claude(), "mcp", *args], capture_output=True, text=True,
                          errors="replace", timeout=120)
    return proc.returncode, ((proc.stdout or "") + (proc.stderr or "")).strip()


def _registered() -> bool:
    _code, out = _run(["list"])
    return any(line.strip().startswith(NAME) for line in out.splitlines())


def status() -> int:
    print(f"{NAME}: {'on' if _registered() else 'off'}")
    return 0


def on() -> int:
    if _registered():
        print(f"{NAME}: already on")
        return 0
    if not SERVER.exists():
        sys.exit(f"{SERVER} not found")

    code, out = _run(["add", NAME, "--", sys.executable, str(SERVER)])
    print(out or f"{NAME}: on")
    return code


def off() -> int:
    if not _registered():
        print(f"{NAME}: already off")
        return 0

    code, out = _run(["remove", NAME])
    print(out or f"{NAME}: off")
    return code


def main() -> int:
    action = (sys.argv[1] if len(sys.argv) > 1 else "status").strip().lower()
    actions = {"status": status, "on": on, "enable": on, "off": off, "disable": off}
    if action not in actions:
        sys.exit(f"usage: {Path(__file__).name} [status|on|off]")
    return actions[action]()


if __name__ == "__main__":
    raise SystemExit(main())
