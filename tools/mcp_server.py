#!/usr/bin/env python3
"""Vita3K Thor MCP server.

Exposes the Thor development loop - build, install, launch, drive, observe - as
MCP tools so an agent can run the Windows-first / Android-final workflow without
a human copying commands around.

It drives surfaces that already exist rather than adding new ones to the
emulator:

  * adb, for install / launch / screenshot / logcat on the AYN Thor
  * the runtime control file, for save-state, load-state and fast forward
    (enable-runtime-control + runtime-control-file in config.yml, or the
    VITA3K_RUNTIME_CONTROL_FILE environment variable)
  * cmake and gradle, for the two builds
  * tools/debug_knowledge.py, for the SQLite report store

Run it over stdio:

    python tools/mcp_server.py

Register it with Claude Code:

    claude mcp add vita3k-thor -- python tools/mcp_server.py

Turn it off again:

    claude mcp remove vita3k-thor

Every tool returns text. Nothing here writes to the repo except the knowledge
base tool, and nothing installs to a device unless asked.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build" / "windows-vs2022"
ANDROID_DIR = REPO_ROOT / "android"
APK_PATH = ANDROID_DIR / "app" / "build" / "intermediates" / "apk" / "reldebug" / "app-reldebug.apk"
PACKAGE = "org.vita3k.emulator.debug"

# Build environment the Android side needs. Both are discovered rather than
# hardcoded so this works on another machine.
VCPKG_ROOT = os.environ.get("VCPKG_ROOT")
ANDROID_NDK_HOME = os.environ.get("ANDROID_NDK_HOME")


def _adb() -> str:
    found = shutil.which("adb")
    if found:
        return found

    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if sdk:
        candidate = Path(sdk) / "platform-tools" / "adb.exe"
        if candidate.exists():
            return str(candidate)

    default = Path.home() / "AppData" / "Local" / "Android" / "Sdk" / "platform-tools" / "adb.exe"
    if default.exists():
        return str(default)

    raise RuntimeError("adb not found. Set ANDROID_HOME or put adb on PATH.")


def _run(cmd: list[str], cwd: Path | None = None, timeout: int = 600,
         env: dict[str, str] | None = None) -> str:
    merged = {**os.environ, **(env or {})}
    proc = subprocess.run(
        cmd, cwd=str(cwd) if cwd else None, capture_output=True, text=True,
        timeout=timeout, env=merged, errors="replace")
    out = (proc.stdout or "") + (proc.stderr or "")
    return f"exit={proc.returncode}\n{out.strip()}"


def _device_args(serial: str | None) -> list[str]:
    return ["-s", serial] if serial else []


# --------------------------------------------------------------------------
# tools
# --------------------------------------------------------------------------

def devices() -> str:
    """List connected adb devices."""
    return _run([_adb(), "devices", "-l"], timeout=60)


def connect(address: str) -> str:
    """Connect to a device over TCP, e.g. 192.168.1.3:5555."""
    return _run([_adb(), "connect", address], timeout=60)


def build_windows(target: str = "") -> str:
    """Build the Windows RelWithDebInfo binary. Optionally a single target."""
    if not (BUILD_DIR / "CMakeCache.txt").exists():
        return (f"No configured build tree at {BUILD_DIR}.\n"
                "Configure it first: cmake --preset windows-vs2022")

    cmd = ["cmake", "--build", str(BUILD_DIR), "--config", "RelWithDebInfo"]
    if target:
        cmd += ["--target", target]
    cmd += ["--", "-m"]
    result = _run(cmd, cwd=REPO_ROOT, timeout=3600)

    errors = [l for l in result.splitlines() if "error C" in l or "error LNK" in l]
    if errors:
        return "BUILD FAILED\n" + "\n".join(dict.fromkeys(errors[:25]))
    return "BUILD OK\n" + "\n".join(result.splitlines()[-5:])


def build_android(abi: str = "arm64-v8a") -> str:
    """Build the Android reldebug APK for one ABI."""
    if not VCPKG_ROOT or not ANDROID_NDK_HOME:
        return ("VCPKG_ROOT and ANDROID_NDK_HOME must be set for the Android build.\n"
                "Upstream's vcpkg.json puts the project in manifest mode, so the "
                "first build compiles its dependencies from source.")

    gradlew = ANDROID_DIR / ("gradlew.bat" if os.name == "nt" else "gradlew")
    result = _run(
        [str(gradlew), "assembleReldebug", f"-Pandroid.injected.build.abi={abi}", "--console=plain"],
        cwd=ANDROID_DIR, timeout=3600,
        env={"VCPKG_ROOT": VCPKG_ROOT, "ANDROID_NDK_HOME": ANDROID_NDK_HOME})

    if "BUILD SUCCESSFUL" in result:
        return f"BUILD OK -> {APK_PATH}"

    errors = [l for l in result.splitlines()
              if l.startswith("e:") or "error:" in l or "fatal error" in l]
    return "BUILD FAILED\n" + "\n".join(dict.fromkeys(errors[:25]))


def install(serial: str = "") -> str:
    """Install the built APK. Uses -t because the APK is marked testOnly."""
    if not APK_PATH.exists():
        return f"No APK at {APK_PATH}. Build it first."
    return _run([_adb(), *_device_args(serial), "install", "-r", "-t", str(APK_PATH)], timeout=900)


def launch(serial: str = "") -> str:
    """Launch the app's main activity."""
    return _run([_adb(), *_device_args(serial), "shell", "am", "start", "-W",
                 "-n", f"{PACKAGE}/org.vita3k.emulator.MainActivity"], timeout=120)


def launch_cartridge(archive_path: str, serial: str = "") -> str:
    """Open a .zip/.vpk on the device as a virtual cartridge and boot it.

    archive_path is a path on the device, not on this machine.
    """
    return _run([_adb(), *_device_args(serial), "shell", "am", "start",
                 "-a", "android.intent.action.VIEW", "-t", "application/zip",
                 "-d", f"file://{archive_path}"], timeout=120)


def stop(serial: str = "") -> str:
    """Force-stop the app."""
    return _run([_adb(), *_device_args(serial), "shell", "am", "force-stop", PACKAGE], timeout=60)


def is_running(serial: str = "") -> str:
    """Report whether the emulator process is alive."""
    result = _run([_adb(), *_device_args(serial), "shell", "pidof", PACKAGE], timeout=60)
    pid = result.splitlines()[-1].strip() if "exit=0" in result else ""
    return f"running (pid {pid})" if pid.isdigit() else "not running"


def screenshot(out_path: str, serial: str = "") -> str:
    """Capture the device screen to a PNG on this machine."""
    proc = subprocess.run([_adb(), *_device_args(serial), "exec-out", "screencap", "-p"],
                          capture_output=True, timeout=180)
    if proc.returncode != 0:
        return f"screencap failed: {proc.stderr.decode(errors='replace')[:400]}"

    target = Path(out_path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(proc.stdout)
    return f"wrote {len(proc.stdout)} bytes to {target}"


def logcat(pattern: str = "spdlog", lines: int = 40, clear_first: bool = False,
           serial: str = "") -> str:
    """Read emulator log lines, filtered. Set clear_first to start a fresh capture."""
    if clear_first:
        _run([_adb(), *_device_args(serial), "logcat", "-c"], timeout=60)
        return "logcat cleared"

    result = _run([_adb(), *_device_args(serial), "logcat", "-d"], timeout=180)
    matched = [l for l in result.splitlines() if pattern.lower() in l.lower()]
    # Validation-layer spam drowns everything else out; drop it unless asked for.
    if pattern == "spdlog":
        noise = ("Validation layer", "Vulkan spec states", "VUID", "Skipped ")
        matched = [l for l in matched if not any(n in l for n in noise)]
    return "\n".join(matched[-lines:]) or "(no matching lines)"


def runtime_action(action: str, control_file: str = "") -> str:
    """Drive the running emulator through the runtime control file.

    Actions: save_state, load_state, undo_load_state, toggle_fast_forward,
    screenshot. Requires enable-runtime-control in config.yml or
    VITA3K_RUNTIME_CONTROL_FILE.
    """
    path = control_file or os.environ.get("VITA3K_RUNTIME_CONTROL_FILE", "")
    if not path:
        return ("No control file. Set enable-runtime-control and "
                "runtime-control-file in config.yml, pass control_file, or set "
                "VITA3K_RUNTIME_CONTROL_FILE.")

    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(f"action = {action}\n", encoding="utf-8")
    return f"wrote 'action = {action}' to {target}"


def knowledge_search(query: str) -> str:
    """Search the SQLite debug knowledge base."""
    script = REPO_ROOT / "tools" / "debug_knowledge.py"
    if not script.exists():
        return f"{script} not found"
    return _run([sys.executable, str(script), "search", query], cwd=REPO_ROOT, timeout=180)


def knowledge_add(case: str, entry_type: str, summary: str, body: str = "",
                  platform: str = "") -> str:
    """Add an entry to the SQLite debug knowledge base."""
    script = REPO_ROOT / "tools" / "debug_knowledge.py"
    cmd = [sys.executable, str(script), "entry", "add",
           "--case", case, "--type", entry_type, "--summary", summary]
    if body:
        cmd += ["--body", body]
    if platform:
        cmd += ["--platform", platform]
    return _run(cmd, cwd=REPO_ROOT, timeout=180)


TOOLS: dict[str, tuple[Callable[..., str], str, dict[str, Any]]] = {
    "devices": (devices, "List connected adb devices.", {}),
    "connect": (connect, "Connect to a device over TCP (e.g. 192.168.1.3:5555).",
                {"address": {"type": "string"}}),
    "build_windows": (build_windows, "Build the Windows RelWithDebInfo binary.",
                      {"target": {"type": "string", "description": "optional single target"}}),
    "build_android": (build_android, "Build the Android reldebug APK.",
                      {"abi": {"type": "string", "default": "arm64-v8a"}}),
    "install": (install, "Install the built APK on the device.",
                {"serial": {"type": "string"}}),
    "launch": (launch, "Launch the emulator's main activity.", {"serial": {"type": "string"}}),
    "launch_cartridge": (launch_cartridge,
                         "Open a .zip/.vpk on the device as a virtual cartridge and boot it.",
                         {"archive_path": {"type": "string", "description": "path on the device"},
                          "serial": {"type": "string"}}),
    "stop": (stop, "Force-stop the emulator.", {"serial": {"type": "string"}}),
    "is_running": (is_running, "Report whether the emulator process is alive.",
                   {"serial": {"type": "string"}}),
    "screenshot": (screenshot, "Capture the device screen to a PNG on this machine.",
                   {"out_path": {"type": "string"}, "serial": {"type": "string"}}),
    "logcat": (logcat, "Read filtered emulator log lines.",
               {"pattern": {"type": "string", "default": "spdlog"},
                "lines": {"type": "integer", "default": 40},
                "clear_first": {"type": "boolean", "default": False},
                "serial": {"type": "string"}}),
    "runtime_action": (runtime_action,
                       "Drive the running emulator: save_state, load_state, "
                       "undo_load_state, toggle_fast_forward, screenshot.",
                       {"action": {"type": "string"}, "control_file": {"type": "string"}}),
    "knowledge_search": (knowledge_search, "Search the SQLite debug knowledge base.",
                         {"query": {"type": "string"}}),
    "knowledge_add": (knowledge_add, "Add an entry to the SQLite debug knowledge base.",
                      {"case": {"type": "string"}, "entry_type": {"type": "string"},
                       "summary": {"type": "string"}, "body": {"type": "string"},
                       "platform": {"type": "string"}}),
}

REQUIRED = {
    "connect": ["address"],
    "launch_cartridge": ["archive_path"],
    "screenshot": ["out_path"],
    "runtime_action": ["action"],
    "knowledge_search": ["query"],
    "knowledge_add": ["case", "entry_type", "summary"],
}


# --------------------------------------------------------------------------
# MCP stdio plumbing
# --------------------------------------------------------------------------

def _send(message: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def _tool_list() -> list[dict[str, Any]]:
    listed = []
    for name, (_fn, description, props) in TOOLS.items():
        listed.append({
            "name": name,
            "description": description,
            "inputSchema": {
                "type": "object",
                "properties": props,
                "required": REQUIRED.get(name, []),
            },
        })
    return listed


def _handle(request: dict[str, Any]) -> dict[str, Any] | None:
    method = request.get("method")
    request_id = request.get("id")

    if method == "initialize":
        return {
            "jsonrpc": "2.0", "id": request_id,
            "result": {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "vita3k-thor", "version": "1.0.0"},
            },
        }

    if method in ("notifications/initialized", "initialized"):
        return None

    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": request_id, "result": {"tools": _tool_list()}}

    if method == "tools/call":
        params = request.get("params", {})
        name = params.get("name", "")
        args = params.get("arguments", {}) or {}

        entry = TOOLS.get(name)
        if not entry:
            return {"jsonrpc": "2.0", "id": request_id,
                    "error": {"code": -32601, "message": f"Unknown tool: {name}"}}

        try:
            text = entry[0](**args)
        except subprocess.TimeoutExpired:
            text = f"{name} timed out"
        except Exception as exc:  # surfaced to the caller rather than killing the server
            text = f"{name} failed: {exc}"

        return {"jsonrpc": "2.0", "id": request_id,
                "result": {"content": [{"type": "text", "text": text}]}}

    if request_id is None:
        return None

    return {"jsonrpc": "2.0", "id": request_id,
            "error": {"code": -32601, "message": f"Unknown method: {method}"}}


def main() -> None:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            request = json.loads(line)
        except json.JSONDecodeError:
            continue

        response = _handle(request)
        if response is not None:
            _send(response)


if __name__ == "__main__":
    main()
