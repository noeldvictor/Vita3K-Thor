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


# --------------------------------------------------------------------------
# debugging tools
#
# Everything below exists because it was hand-rolled repeatedly while chasing
# renderer bugs. The device is shared with other agents' emulators, so anything
# that reads the screen or the log has to say whose screen and whose log.
# --------------------------------------------------------------------------

EMU_FILES = f"/sdcard/Android/data/{PACKAGE}/files"
EMU_LOG = f"{EMU_FILES}/vita3k.log"
EMU_CONFIG = f"{EMU_FILES}/config.yml"


def _shell(command: str, serial: str = "", timeout: int = 180) -> str:
    """Run one shell command on the device and return its output without the exit line."""
    result = _run([_adb(), *_device_args(serial), "shell", command], timeout=timeout)
    return result.split("\n", 1)[1] if "\n" in result else ""


def foreground(serial: str = "") -> str:
    """Report the top resumed activity, and whether it is ours.

    Check this before trusting a screenshot. Several agents drive this device,
    and a backgrounded emulator stops stepping - its log goes quiet and its last
    frame persists, which reads exactly like a hang.
    """
    out = _shell("dumpsys activity activities | grep -m1 topResumedActivity", serial)
    ours = PACKAGE in out
    return f"ours={ours}\n{out.strip() or '(none reported)'}"


def emu_log(pattern: str = "", lines: int = 40, clear_first: bool = False,
            serial: str = "") -> str:
    """Read the emulator's own vita3k.log, optionally filtered.

    This is not logcat - it is the spdlog file the emulator writes, which keeps
    the full boot trace rather than whatever survived the ring buffer.
    """
    if clear_first:
        _shell(f"rm -f {EMU_LOG}", serial)
        return "vita3k.log removed"

    quoted = pattern.replace("'", "'\\''")
    cmd = (f"grep -iE '{quoted}' {EMU_LOG} | tail -{int(lines)}" if pattern
           else f"tail -{int(lines)} {EMU_LOG}")
    return _shell(cmd, serial).strip() or "(no matching lines)"


def wait_for_log(pattern: str, timeout_s: int = 180, serial: str = "") -> str:
    """Block until a pattern appears in vita3k.log, or the timeout expires.

    Polls on the device with a real sleep rather than spinning on adb latency,
    which otherwise burns the whole budget in round trips.
    """
    deadline = time.monotonic() + max(1, int(timeout_s))
    quoted = pattern.replace("'", "'\\''")
    while time.monotonic() < deadline:
        hits = _shell(f"sleep 2; grep -c -iE '{quoted}' {EMU_LOG} 2>/dev/null", serial).strip()
        if hits.isdigit() and int(hits) > 0:
            return f"matched after {int(timeout_s - (deadline - time.monotonic()))}s ({hits} lines)"
    return f"TIMEOUT after {timeout_s}s waiting for {pattern!r}"


def boot_title(title_id: str, wait_for: str = "", timeout_s: int = 240,
               serial: str = "") -> str:
    """Force-stop, clear the log, boot a title id, and wait for a marker.

    The whole inner loop of renderer debugging in one call. wait_for is a regex
    matched against vita3k.log; leave it empty to return as soon as the activity
    is up.
    """
    steps = [f"force-stop: {_shell(f'am force-stop {PACKAGE}', serial).strip() or 'ok'}"]
    _shell(f"rm -f {EMU_LOG}", serial)
    started = _shell(
        f"am start -n {PACKAGE}/org.vita3k.emulator.Emulator --es title_id {title_id}",
        serial)
    steps.append(f"start: {started.strip().splitlines()[0] if started.strip() else 'ok'}")
    if wait_for:
        steps.append(f"wait: {wait_for_log(wait_for, timeout_s, serial)}")
    steps.append(foreground(serial).splitlines()[0])
    return "\n".join(steps)


def capture(out_path: str, require_foreground: bool = True, serial: str = "") -> str:
    """Screenshot, refusing by default when the emulator is not the top activity.

    A plain screencap returns whatever app is in front, which on a shared device
    is regularly somebody else's emulator.
    """
    fg = foreground(serial)
    if require_foreground and not fg.startswith("ours=True"):
        return (f"REFUSED: the emulator is not in the foreground, so a capture would "
                f"show another app.\n{fg}\n"
                f"Pass require_foreground=false to capture anyway.")
    return f"{fg.splitlines()[0]}\n{screenshot(out_path, serial)}"


def config_get(keys: str = "", serial: str = "") -> str:
    """Read config.yml from the device. keys is an optional regex of key names."""
    if not keys:
        return _shell(f"cat {EMU_CONFIG}", serial).strip() or "(empty)"
    quoted = keys.replace("'", "'\\''")
    return _shell(f"grep -iE '{quoted}' {EMU_CONFIG}", serial).strip() or "(no matching keys)"


def config_set(key: str, value: str, serial: str = "") -> str:
    """Set one scalar key in the device's config.yml, keeping a .bak.

    Config flags are the cheapest A/B available - no rebuild, no reinstall.
    disable-surface-sync was found this way.
    """
    safe_key = key.replace("'", "")
    before = _shell(f"grep -E '^{safe_key}:' {EMU_CONFIG}", serial).strip()
    if not before:
        return f"key {key!r} not present in {EMU_CONFIG}"

    _shell(f"cp {EMU_CONFIG} {EMU_CONFIG}.bak", serial)
    _shell(f"sed -i 's|^{safe_key}:.*|{safe_key}: {value}|' {EMU_CONFIG}", serial)
    after = _shell(f"grep -E '^{safe_key}:' {EMU_CONFIG}", serial).strip()
    return f"before: {before}\nafter:  {after}\n(previous config kept at config.yml.bak)"


def validation_errors(lines: int = 10, serial: str = "") -> str:
    """Count and sample Vulkan validation-layer errors in vita3k.log.

    A regression check with a number attached: capture the count before a change
    and after it.
    """
    count = _shell(f"grep -c 'Validation layer' {EMU_LOG} 2>/dev/null", serial).strip()
    if not count.isdigit():
        return "(no log, or the emulator has not run yet)"
    if count == "0":
        return "0 validation errors"
    sample = _shell(
        f"grep 'Validation layer' {EMU_LOG} | sed 's/.*Validation layer: //' "
        f"| cut -c1-160 | sort | uniq -c | sort -rn | head -{int(lines)}", serial)
    return f"{count} validation errors\n{sample.strip()}"


def release(serial: str = "") -> str:
    """Force-stop the emulator. Call this when you are done with the device.

    Leaving it resident makes the next agent fight it for the foreground and the
    GPU.
    """
    _shell(f"am force-stop {PACKAGE}", serial)
    alive = _shell(f"ps -A | grep -c {PACKAGE}", serial).strip()
    return f"stopped; processes left: {alive or '0'}"


# --------------------------------------------------------------------------
# input and capture
#
# The emulator takes touch and gamepad input, and a still frame often does not
# show a bug - flicker, ordering and sync problems only read as motion. So:
# taps, swipes, pad buttons, and video.
# --------------------------------------------------------------------------

# Pad buttons, named the way a person would say them rather than by keycode.
# The Vita face layout is cross/circle/square/triangle; both spellings work.
BUTTONS = {
    "a": "BUTTON_A", "cross": "BUTTON_A", "x": "BUTTON_A",
    "b": "BUTTON_B", "circle": "BUTTON_B", "o": "BUTTON_B",
    "x_btn": "BUTTON_X", "square": "BUTTON_X",
    "y": "BUTTON_Y", "triangle": "BUTTON_Y",
    "l1": "BUTTON_L1", "r1": "BUTTON_R1",
    "l2": "BUTTON_L2", "r2": "BUTTON_R2",
    "l3": "BUTTON_THUMBL", "r3": "BUTTON_THUMBR",
    "start": "BUTTON_START", "select": "BUTTON_SELECT",
    "menu": "BUTTON_MODE", "ps": "BUTTON_MODE", "pause": "BUTTON_MODE",
    "up": "DPAD_UP", "down": "DPAD_DOWN", "left": "DPAD_LEFT", "right": "DPAD_RIGHT",
    "ok": "DPAD_CENTER", "confirm": "DPAD_CENTER",
    "back": "BACK", "home": "HOME",
}


def screen_size(serial: str = "") -> str:
    """Report the display size, so taps can be given in real coordinates."""
    out = _shell("wm size", serial).strip()
    return out or "(could not read size)"


def tap(x: int, y: int, serial: str = "") -> str:
    """Tap a screen coordinate.

    Coordinates are physical pixels and match what `capture` writes, so read
    them straight off a screenshot.
    """
    _shell(f"input tap {int(x)} {int(y)}", serial)
    return f"tapped {int(x)},{int(y)}"


def swipe(x1: int, y1: int, x2: int, y2: int, duration_ms: int = 300,
          serial: str = "") -> str:
    """Swipe between two coordinates. Also scrolls lists - swipe up to go down."""
    _shell(f"input swipe {int(x1)} {int(y1)} {int(x2)} {int(y2)} {int(duration_ms)}", serial)
    return f"swiped {int(x1)},{int(y1)} -> {int(x2)},{int(y2)} over {int(duration_ms)}ms"


def button(name: str, repeat: int = 1, serial: str = "") -> str:
    """Press a controller button by name.

    Names: a/cross, b/circle, square, triangle, l1, r1, l2, r2, l3, r3,
    start, select, menu (the PS button, which opens the pause OSD), up, down,
    left, right, ok, back, home.
    """
    key = BUTTONS.get(name.strip().lower())
    if not key:
        return f"unknown button {name!r}. Known: {', '.join(sorted(BUTTONS))}"

    for _ in range(max(1, int(repeat))):
        _shell(f"input keyevent KEYCODE_{key}", serial)
    return f"pressed {name} (KEYCODE_{key}) x{max(1, int(repeat))}"


def type_text(text: str, serial: str = "") -> str:
    """Type text into whatever has focus, for naming saves and the like."""
    escaped = text.replace("'", "").replace(" ", "%s")
    _shell(f"input text '{escaped}'", serial)
    return f"typed {text!r}"


def record_video(out_path: str, seconds: int = 10, size: str = "",
                 bit_rate_mbps: int = 8, serial: str = "") -> str:
    """Record the screen to an mp4 on this machine.

    Still frames hide anything that is only visible in motion - flicker,
    tearing, one-frame corruption. Keep clips short; screenrecord caps at 180s
    and the file has to come back over adb.
    """
    seconds = max(1, min(int(seconds), 180))
    remote = "/sdcard/vita3k-mcp-capture.mp4"
    cmd = f"screenrecord --time-limit {seconds} --bit-rate {int(bit_rate_mbps) * 1000000}"
    if size:
        cmd += f" --size {size}"
    cmd += f" {remote}"

    fg = foreground(serial)
    _shell(f"rm -f {remote}", serial)
    # screenrecord runs for the full duration, so allow for it plus the pull.
    _run([_adb(), *_device_args(serial), "shell", cmd], timeout=seconds + 180)

    target = Path(out_path)
    target.parent.mkdir(parents=True, exist_ok=True)
    pull = _run([_adb(), *_device_args(serial), "pull", remote, str(target)],
                timeout=300)
    _shell(f"rm -f {remote}", serial)

    if not target.exists():
        return f"recording failed\n{pull}"
    return (f"{fg.splitlines()[0]}\n"
            f"wrote {target.stat().st_size} bytes to {target} ({seconds}s)")


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
    "foreground": (foreground,
                   "Report the top resumed activity and whether it is ours. Check "
                   "before trusting a screenshot; the device is shared.",
                   {"serial": {"type": "string"}}),
    "emu_log": (emu_log,
                "Read the emulator's own vita3k.log (not logcat), optionally filtered.",
                {"pattern": {"type": "string", "description": "optional regex"},
                 "lines": {"type": "integer", "default": 40},
                 "clear_first": {"type": "boolean", "default": False},
                 "serial": {"type": "string"}}),
    "wait_for_log": (wait_for_log,
                     "Block until a regex appears in vita3k.log, or time out.",
                     {"pattern": {"type": "string"},
                      "timeout_s": {"type": "integer", "default": 180},
                      "serial": {"type": "string"}}),
    "boot_title": (boot_title,
                   "Force-stop, clear the log, boot a title id, wait for a log marker. "
                   "The inner loop of renderer debugging.",
                   {"title_id": {"type": "string", "description": "e.g. PCSG00500"},
                    "wait_for": {"type": "string", "description": "regex to wait for"},
                    "timeout_s": {"type": "integer", "default": 240},
                    "serial": {"type": "string"}}),
    "capture": (capture,
                "Screenshot, refusing when the emulator is not in the foreground.",
                {"out_path": {"type": "string"},
                 "require_foreground": {"type": "boolean", "default": True},
                 "serial": {"type": "string"}}),
    "config_get": (config_get, "Read config.yml from the device, optionally filtered.",
                   {"keys": {"type": "string", "description": "optional regex of key names"},
                    "serial": {"type": "string"}}),
    "config_set": (config_set,
                   "Set one scalar key in the device's config.yml, keeping a .bak. "
                   "The cheapest A/B available - no rebuild.",
                   {"key": {"type": "string"}, "value": {"type": "string"},
                    "serial": {"type": "string"}}),
    "validation_errors": (validation_errors,
                          "Count and sample Vulkan validation errors in vita3k.log.",
                          {"lines": {"type": "integer", "default": 10},
                           "serial": {"type": "string"}}),
    "release": (release, "Force-stop the emulator. Call when done - the device is shared.",
                {"serial": {"type": "string"}}),
    "screen_size": (screen_size, "Report the display size for coordinate math.",
                    {"serial": {"type": "string"}}),
    "tap": (tap, "Tap a screen coordinate, in the same pixels a screenshot uses.",
            {"x": {"type": "integer"}, "y": {"type": "integer"},
             "serial": {"type": "string"}}),
    "swipe": (swipe, "Swipe between two coordinates; also scrolls lists.",
              {"x1": {"type": "integer"}, "y1": {"type": "integer"},
               "x2": {"type": "integer"}, "y2": {"type": "integer"},
               "duration_ms": {"type": "integer", "default": 300},
               "serial": {"type": "string"}}),
    "button": (button,
               "Press a pad button by name: a/cross, b/circle, square, triangle, "
               "l1, r1, l2, r2, l3, r3, start, select, menu (opens the pause OSD), "
               "up, down, left, right, ok, back, home.",
               {"name": {"type": "string"}, "repeat": {"type": "integer", "default": 1},
                "serial": {"type": "string"}}),
    "type_text": (type_text, "Type text into whatever has focus.",
                  {"text": {"type": "string"}, "serial": {"type": "string"}}),
    "record_video": (record_video,
                     "Record the screen to an mp4 here. Use for anything only "
                     "visible in motion - flicker, tearing, one-frame corruption.",
                     {"out_path": {"type": "string"},
                      "seconds": {"type": "integer", "default": 10},
                      "size": {"type": "string", "description": "e.g. 1280x720"},
                      "bit_rate_mbps": {"type": "integer", "default": 8},
                      "serial": {"type": "string"}}),
}

REQUIRED = {
    "connect": ["address"],
    "launch_cartridge": ["archive_path"],
    "screenshot": ["out_path"],
    "runtime_action": ["action"],
    "knowledge_search": ["query"],
    "knowledge_add": ["case", "entry_type", "summary"],
    "wait_for_log": ["pattern"],
    "boot_title": ["title_id"],
    "capture": ["out_path"],
    "config_set": ["key", "value"],
    "tap": ["x", "y"],
    "swipe": ["x1", "y1", "x2", "y2"],
    "button": ["name"],
    "type_text": ["text"],
    "record_video": ["out_path"],
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
