"""Pack extracted game folders on the Thor's SD card into stored zips.

The inverse of unpack_cartridges.py. A stored (uncompressed) zip is one file
per game that copies like any zip, and the emulator reads its members in
place at any offset, so it needs no cartridge cache; a deflated zip would.
PSARC content is already compressed, so stored costs only a few percent.

The packing runs on the device: tools/android/Packer.java, compiled to a dex
and launched through app_process, walks the folder and writes
<zip>.part, then renames it. Each result is checked with `unzip -t` (every
member's CRC) and by member count before the folder is deleted.

  python tools/pack_cartridges.py --dry-run
  python tools/pack_cartridges.py --delete
"""
from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mcp_server as m  # noqa: E402
from unpack_cartridges import DEFAULT_ROOT, TITLE_RE, extracted_sizes, free_kb, listing, q, shell  # noqa: E402

PACKER_JAR = "/data/local/tmp/packer.jar"
PACKER_DATA = "/data/local/tmp"


def read_title_id(folder: str) -> str:
    """TITLE_ID out of <folder>/sce_sys/param.sfo, read straight off the device."""
    raw = subprocess.run([m._adb(), *m._device_args(None), "exec-out", f"cat {q(folder + '/sce_sys/param.sfo')}"],
                         capture_output=True, timeout=120).stdout
    if len(raw) < 20 or raw[:4] != b"\0PSF":
        raise RuntimeError(f"{folder}: sce_sys/param.sfo is not a PSF file")
    _, _, key_off, data_off, count = struct.unpack_from("<IIIII", raw, 0)
    for i in range(count):
        ko, fmt, dlen, _dmax, do = struct.unpack_from("<HHIII", raw, 20 + i * 16)
        key = raw[key_off + ko:raw.index(b"\0", key_off + ko)].decode()
        if key == "TITLE_ID":
            value = raw[data_off + do:data_off + do + dlen].rstrip(b"\0").decode("ascii", "replace")
            if TITLE_RE.match(value):
                return value.upper()
    raise RuntimeError(f"{folder}: no TITLE_ID in param.sfo")


def ensure_packer() -> None:
    code, _ = shell(f"[ -s {PACKER_JAR} ]")
    if code == 0:
        return
    local = Path(__file__).resolve().parent / "android" / "packer.jar"
    if not local.exists():
        raise RuntimeError(f"{PACKER_JAR} is not on the device and {local} does not exist; build it with "
                           "javac --release 8 + d8 from tools/android/Packer.java")
    print(m._run([m._adb(), *m._device_args(None), "push", str(local), PACKER_JAR]).splitlines()[-1])


def pack(root: str, name: str, delete_folder: bool, dry_run: bool) -> str:
    folder = f"{root}/{name}"
    zip_path = f"{folder}.zip"
    started = time.monotonic()

    title_id = read_title_id(folder)
    sources = [f"app/{title_id}={folder}"]
    dlc = f"{root}/addcont/{title_id}"
    code, _ = shell(f"[ -d {q(dlc)} ]")
    if code == 0:
        sources.append(f"addcont/{title_id}={dlc}")

    folder_files = extracted_sizes(folder)
    expected = len(folder_files) + (len(extracted_sizes(dlc)) if len(sources) > 1 else 0)
    total = sum(folder_files.values())

    code, _ = shell(f"[ -e {q(zip_path)} ]")
    zip_exists = code == 0
    plan = f"{name}: {title_id} {len(folder_files)} files, {total / 2**30:.2f} GiB, sources={sources} -> {zip_path}"
    if dry_run:
        return ("PLAN (zip exists, will verify) " if zip_exists else "PLAN ") + plan

    if not zip_exists:
        available_kb = free_kb(root)
        if available_kb < total // 1024 + 1024 * 1024:
            return f"SKIP {name}: needs {total // 2**20} MB free on the card, only {available_kb // 1024} MB"
        print("  " + plan, flush=True)
        shell(f"rm -f {q(zip_path + '.part')}")
        args = " ".join(q(s) for s in sources)
        code, out = shell(f"cd {q(root)} && CLASSPATH={PACKER_JAR} ANDROID_DATA={PACKER_DATA} "
                          f"app_process {PACKER_DATA} Packer {q(zip_path)} {args} 2>&1 | tail -3", timeout=7200)
        if code != 0 or "done " not in out:
            shell(f"rm -f {q(zip_path + '.part')}")
            return f"FAIL {name}: packer exit {code}: {out[-300:]}"

    code, out = shell(f"unzip -tq {q(zip_path)} 2>&1 | tail -1", timeout=3600)
    if code != 0 or "No errors" not in out:
        return f"FAIL {name}: zip did not verify: {out[-200:]}; folder kept"
    members = listing(zip_path)
    if len(members) != expected:
        return f"FAIL {name}: zip has {len(members)} members, folder has {expected} files; folder kept"
    stored = sum(1 for n, s in members.items() if s == folder_files.get(n[len(f'app/{title_id}/'):], -1))
    if stored != len(folder_files):
        return f"FAIL {name}: {len(folder_files) - stored} member sizes differ from the folder; folder kept"

    notes = [f"OK {name} -> {zip_path} ({len(members)} members, {time.monotonic() - started:.0f}s)"]
    if delete_folder:
        code, out = shell(f"rm -rf {q(folder)}" + (f" && rm -rf {q(dlc)}" if len(sources) > 1 else ""))
        notes.append("folder deleted" if code == 0 else f"folder NOT deleted: {out[-200:]}")
    return "; ".join(notes)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=DEFAULT_ROOT)
    parser.add_argument("--only", action="append", default=[], help="folder name(s) to pack; default is every game folder")
    parser.add_argument("--delete", action="store_true", help="delete each folder after its zip verified")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    ensure_packer()
    _, out = shell(f"for d in {q(args.root)}/*/; do [ -f \"$d/sce_sys/param.sfo\" ] && basename \"$d\"; done")
    folders = sorted(n for n in out.splitlines() if n.strip())
    if args.only:
        folders = [n for n in folders if n in args.only]
    print(f"{len(folders)} game folders under {args.root}, {free_kb(args.root) // 1024} MB free", flush=True)
    for name in folders:
        try:
            print(pack(args.root, name, args.delete, args.dry_run), flush=True)
        except Exception as error:
            print(f"FAIL {name}: {error}", flush=True)
    print(f"done, {free_kb(args.root) // 1024} MB free on the card", flush=True)


if __name__ == "__main__":
    main()
