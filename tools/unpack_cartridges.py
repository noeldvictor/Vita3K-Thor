"""Turn zip cartridges on the Thor's SD card into extracted folders.

A zip cartridge is mounted through miniz and every member over 64 MiB gets
unpacked into the emulator's cartridge cache on internal storage, because a
deflated zip entry cannot be read at an offset. An extracted folder is mounted
straight from the SD card and needs no cache at all. This does the conversion
on the device itself, one archive at a time:

  1. unzip the whole archive into <root>/.unpack_tmp/<name>/
  2. check every member's size against the zip listing
  3. move the content root (app/<TITLEID>/) to <root>/<name>/, then copy the
     patch/<TITLEID>/ and rePatch/<TITLEID>/ overlays over it in that order,
     the way the emulator layers them - except sce_sys/param.sfo, which stays
     the game's own: the scanner only lists game-card categories, and a
     patch's param.sfo says "gp"
  4. delete the zip (only with --delete), and optionally the title's cache

Run it from the repo: python tools/unpack_cartridges.py --delete
"""
from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mcp_server as m  # noqa: E402

TITLE_RE = re.compile(r"^PCS[A-Z][0-9]{5}$", re.IGNORECASE)
DEFAULT_ROOT = "/storage/2664-21DE/Roms/psvita"


def q(text: str) -> str:
    """POSIX single-quote a path for adb shell."""
    return "'" + text.replace("'", "'\\''") + "'"


def shell(command: str, timeout: int = 3600) -> tuple[int, str]:
    result = m._run([m._adb(), "shell", command], timeout=timeout)
    first, _, rest = result.partition("\n")
    code = int(first.removeprefix("exit=") or "1")
    return code, rest


def listing(zip_path: str) -> dict[str, int]:
    code, out = shell(f"unzip -lq {q(zip_path)}")
    if code != 0:
        raise RuntimeError(f"unzip -l failed for {zip_path}: {out[-300:]}")
    entries: dict[str, int] = {}
    for line in out.splitlines():
        match = re.match(r"^\s*(\d+)\s+\S+\s+\S+\s+(.+?)\s*$", line)
        if not match:
            continue
        name = match.group(2).replace("\\", "/")
        if name.endswith("/") or name == "Name":
            continue
        entries[name] = int(match.group(1))
    return entries


def content_roots(entries: dict[str, int]) -> list[str]:
    """Directories holding a game's sce_sys/param.sfo.

    Skips patch/rePatch overlays and addcont (DLC has its own param.sfo), and
    drops a param.sfo nested inside another candidate - UPPERS ships a savedata
    template with one under app/<id>/disc/savedata/.
    """
    roots = []
    for name in entries:
        if not name.lower().endswith("sce_sys/param.sfo"):
            continue
        root = name[: -len("sce_sys/param.sfo")]
        segments = [s.lower() for s in root.strip("/").split("/") if s]
        if any(s in ("patch", "repatch", "addcont") for s in segments):
            continue
        roots.append(root)
    return [r for r in roots if not any(o != r and r.lower().startswith(o.lower()) for o in roots)]


def addcont_roots(entries: dict[str, int], title_id: str) -> list[str]:
    """DLC directories (addcont/<TITLEID>/) - kept next to the games, not inside them."""
    found: list[str] = []
    for name in entries:
        segments = name.split("/")
        for i in range(len(segments) - 2):
            if segments[i].lower() == "addcont" and segments[i + 1].lower() == title_id.lower():
                root = "/".join(segments[: i + 2]) + "/"
                if root not in found:
                    found.append(root)
                break
    return found


def overlay_roots(entries: dict[str, int], title_id: str) -> list[str]:
    patch, repatch = [], []
    for name in entries:
        segments = name.split("/")
        for i in range(len(segments) - 2):
            kind = segments[i].lower()
            if kind in ("patch", "repatch") and segments[i + 1].lower() == title_id.lower():
                root = "/".join(segments[: i + 2]) + "/"
                target = patch if kind == "patch" else repatch
                if root not in target:
                    target.append(root)
                break
    return patch + repatch


def title_id_for(zip_path: str, root: str) -> str:
    last = root.strip("/").split("/")[-1] if root.strip("/") else ""
    if TITLE_RE.match(last):
        return last.upper()
    _, out = shell(f"unzip -p {q(zip_path)} {q(root + 'sce_sys/param.sfo')} | grep -a -o 'PCS[A-Z][0-9]\\{{5\\}}' | head -1")
    found = out.strip().splitlines()[-1] if out.strip() else ""
    if not TITLE_RE.match(found):
        raise RuntimeError(f"could not read a title id from {zip_path} root {root!r}")
    return found.upper()


def free_kb(path: str) -> int:
    _, out = shell(f"df -k {q(path)} | tail -1")
    fields = out.split()
    return int(fields[3]) if len(fields) >= 4 and fields[3].isdigit() else 0


def extracted_sizes(tmp: str) -> dict[str, int]:
    code, out = shell(f"find {q(tmp)} -type f -exec stat -c '%s|%n' {{}} +")
    if code != 0:
        raise RuntimeError(f"listing extracted files failed: {out[-300:]}")
    sizes: dict[str, int] = {}
    prefix = tmp.rstrip("/") + "/"
    for line in out.splitlines():
        size, _, path = line.partition("|")
        if not size.isdigit() or not path.startswith(prefix):
            continue
        sizes[path[len(prefix):]] = int(size)
    return sizes


def convert(root: str, zip_name: str, delete_zip: bool, purge_cache: bool, dry_run: bool) -> str:
    zip_path = f"{root}/{zip_name}"
    stem = zip_name[: -len(".zip")]
    dest = f"{root}/{stem}"
    tmp = f"{root}/.unpack_tmp/{stem}"
    started = time.monotonic()

    entries = listing(zip_path)
    roots = content_roots(entries)
    if len(roots) != 1:
        return f"SKIP {zip_name}: expected one content root, found {roots}"
    content_root = roots[0]
    title_id = title_id_for(zip_path, content_root)
    overlays = overlay_roots(entries, title_id)
    total = sum(entries.values())

    code, _ = shell(f"[ -e {q(dest)} ]")
    if code == 0:
        if not delete_zip or dry_run:
            return f"SKIP {zip_name}: {dest} already exists"
        # A folder from an earlier pass: re-verify it against the zip and
        # only then drop the zip (and the cache), so a half-made folder never
        # becomes the only copy.
        bad = verify_folder(dest, entries, content_root, overlays)
        if bad:
            return f"SKIP {zip_name}: {dest} exists but {len(bad)} files differ (e.g. {bad[:3]}); zip kept"
        return "; ".join(finish(zip_path, zip_name, dest, title_id, delete_zip, purge_cache, started))

    needed_kb = total // 1024 + 1024 * 1024
    available_kb = free_kb(root)
    if available_kb < needed_kb:
        return f"SKIP {zip_name}: needs {needed_kb // 1024} MB free on the card, only {available_kb // 1024} MB"

    plan = (f"{zip_name}: {title_id} root={content_root!r} overlays={overlays} addcont={addcont_roots(entries, title_id)} "
            f"{len(entries)} files, {total / 2**30:.2f} GiB -> {dest}")
    if dry_run:
        return "PLAN " + plan
    print("  " + plan, flush=True)

    shell(f"rm -rf {q(tmp)}; mkdir -p {q(tmp)}")
    code, out = shell(f"unzip -q -o -d {q(tmp)} {q(zip_path)}", timeout=7200)
    if code != 0:
        shell(f"rm -rf {q(tmp)}")
        return f"FAIL {zip_name}: unzip exit {code}: {out[-300:]}"

    sizes = extracted_sizes(tmp)
    missing = [n for n in entries if n not in sizes]
    wrong = [n for n in entries if n in sizes and sizes[n] != entries[n]]
    if missing or wrong:
        shell(f"rm -rf {q(tmp)}")
        return (f"FAIL {zip_name}: extraction mismatch, {len(missing)} missing "
                f"(e.g. {missing[:3]}), {len(wrong)} wrong size (e.g. {wrong[:3]})")

    steps = [f"mv {q(tmp + '/' + content_root.rstrip('/'))} {q(dest)}",
             f"cp {q(dest + '/sce_sys/param.sfo')} {q(tmp + '/param.sfo.app')}"]
    for overlay in overlays:
        steps.append(f"cp -r {q(tmp + '/' + overlay.rstrip('/') + '/.')} {q(dest + '/')}")
    steps.append(f"cp {q(tmp + '/param.sfo.app')} {q(dest + '/sce_sys/param.sfo')}")
    for dlc in addcont_roots(entries, title_id):
        # The zip mount never served DLC anyway; park it where an addcont
        # importer can find it later instead of throwing it away with the zip.
        dlc_dest = f"{root}/addcont/{title_id}"
        steps.append(f"mkdir -p {q(dlc_dest)} && cp -r {q(tmp + '/' + dlc.rstrip('/') + '/.')} {q(dlc_dest + '/')}")
    steps.append(f"[ -f {q(dest + '/sce_sys/param.sfo')} ] && [ -f {q(dest + '/eboot.bin')} ]")
    code, out = shell(" && ".join(steps))
    shell(f"rm -rf {q(tmp)}")
    if code != 0:
        return f"FAIL {zip_name}: assembling {dest} failed: {out[-300:]}"

    # The card is exFAT, so names that differ only by case are one file there,
    # and an overlay member replaces the base member the way the emulator's
    # archive mount does (Catherine ships data/Keyfree.cpk in app/ and
    # data/keyfree.cpk in patch/). Compare case-insensitively, last writer wins.
    bad = verify_folder(dest, entries, content_root, overlays)
    if bad:
        return f"FAIL {zip_name}: {len(bad)} files differ in {dest} (e.g. {bad[:3]}); zip kept"

    return "; ".join(finish(zip_path, zip_name, dest, title_id, delete_zip, purge_cache, started))


def verify_folder(dest: str, entries: dict[str, int], content_root: str, overlays: list[str]) -> list[str]:
    """Names whose size in the folder does not match what the zip promised."""
    dest_sizes = {n.lower(): s for n, s in extracted_sizes(dest).items()}
    expected = {n[len(content_root):].lower(): s for n, s in entries.items() if n.startswith(content_root)}
    for overlay in overlays:
        for n, s in entries.items():
            if n.startswith(overlay) and not n.lower().endswith("sce_sys/param.sfo"):
                expected[n[len(overlay):].lower()] = s
    return [n for n, s in expected.items() if dest_sizes.get(n) != s]


def finish(zip_path: str, zip_name: str, dest: str, title_id: str, delete_zip: bool, purge_cache: bool, started: float) -> list[str]:
    notes = [f"OK {zip_name} -> {dest} ({time.monotonic() - started:.0f}s)"]
    if delete_zip:
        code, out = shell(f"rm {q(zip_path)}")
        notes.append("zip deleted" if code == 0 else f"zip NOT deleted: {out[-200:]}")
    if purge_cache:
        notes.append(m.vita_rm(f"cache/cartridge_archive/{title_id}").strip())
    return notes


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=DEFAULT_ROOT, help="scan root on the device holding the .zip cartridges")
    parser.add_argument("--only", action="append", default=[], help="zip file name(s) to convert; default is every .zip")
    parser.add_argument("--delete", action="store_true", help="delete each zip after its folder verified")
    parser.add_argument("--purge-cache", action="store_true", help="drop the title's cartridge cache on internal storage too")
    parser.add_argument("--dry-run", action="store_true", help="only print what would happen")
    args = parser.parse_args()

    _, out = shell(f"ls {q(args.root)}")
    zips = sorted(n for n in out.splitlines() if n.lower().endswith(".zip"))
    if args.only:
        zips = [n for n in zips if n in args.only]
    print(f"{len(zips)} zip cartridges under {args.root}, {free_kb(args.root) // 1024} MB free", flush=True)
    for name in zips:
        try:
            print(convert(args.root, name, args.delete, args.purge_cache, args.dry_run), flush=True)
        except Exception as error:  # keep going, the next archive is independent
            print(f"FAIL {name}: {error}", flush=True)
    print(f"done, {free_kb(args.root) // 1024} MB free on the card", flush=True)


if __name__ == "__main__":
    main()
