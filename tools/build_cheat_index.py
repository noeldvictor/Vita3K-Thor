#!/usr/bin/env python3
# Vita3K Thor helper
# Copyright (C) 2026 Vita3K team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
"""Build cheats/index.json from the .psv files under cheats/db.

The index lets a frontend list every game the database covers without
opening 678 files: title, title id, region, game version the codes target,
author, and the cheat names. Run it after changing anything under cheats/db.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

HEADER_KEYS = {
    "title": "title",
    "id": "title_id",
    "region": "region",
    "version": "version",
    "type": "type",
    "code author": "author",
    "author": "author",
}
CHEAT_LINE = re.compile(r"^_V([01])\s+(.*?)\s*$")
TITLE_ID = re.compile(r"^(PCS[A-Z]\d{5})", re.IGNORECASE)
# The region header is free text (EU, EUR, PAL, USA, US, Jap, JPN...). The
# title id prefix is fixed, so the index carries a code derived from it.
REGION_BY_PREFIX = {
    "PCSA": "US",
    "PCSE": "US",
    "PCSB": "EU",
    "PCSF": "EU",
    "PCSC": "JP",
    "PCSG": "JP",
    "PCSD": "ASIA",
    "PCSH": "ASIA",
}


def parse_psv(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace").lstrip("﻿")
    info: dict = {
        "title_id": path.stem.upper()[:9],
        "title": "",
        "region": "",
        "version": "",
        "type": "",
        "author": "",
        "cheats": [],
        "enabled_on_boot": 0,
        "file": path.name,
    }
    first_comment = ""
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            body = line.lstrip("#").strip()
            if not body:
                continue
            if not first_comment:
                first_comment = body
            if ":" in body:
                key, _, value = body.partition(":")
                field = HEADER_KEYS.get(key.strip().lower())
                if field and value.strip() and not info[field]:
                    info[field] = value.strip()
            continue
        match = CHEAT_LINE.match(line)
        if match:
            name = match.group(2) or "(unnamed)"
            info["cheats"].append(name)
            if match.group(1) == "1":
                info["enabled_on_boot"] += 1
    if not info["title"]:
        # Some files only carry "# PCSA00147 Freedom Wars" as their first comment.
        m = TITLE_ID.match(first_comment)
        info["title"] = first_comment[m.end():].strip() if m else first_comment
    if not info["title"]:
        info["title"] = info["title_id"]
    info["cheat_count"] = len(info["cheats"])
    info["region_code"] = REGION_BY_PREFIX.get(info["title_id"][:4], "")
    return info


def main() -> int:
    parser = argparse.ArgumentParser(description="Build cheats/index.json from cheats/db/*.psv")
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--db", type=Path, default=root / "cheats" / "db", help="Folder of .psv files")
    parser.add_argument("--output", type=Path, default=root / "cheats" / "index.json", help="Index file to write")
    args = parser.parse_args()

    files = sorted(p for p in args.db.iterdir() if p.suffix.lower() in {".psv", ".txt"})
    games = [parse_psv(p) for p in files]
    games.sort(key=lambda g: g["title_id"])
    index = {
        "schema": "vita3k-thor-cheat-index/1",
        "source": "cheats/db",
        "game_count": len(games),
        "cheat_count": sum(g["cheat_count"] for g in games),
        "games": games,
    }
    args.output.write_text(json.dumps(index, indent=1, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    regions = {}
    for g in games:
        regions[g["region"] or "?"] = regions.get(g["region"] or "?", 0) + 1
    print(f"wrote {args.output}: {index['game_count']} games, {index['cheat_count']} cheats")
    print("regions:", ", ".join(f"{k}={v}" for k, v in sorted(regions.items(), key=lambda kv: -kv[1])))
    untitled = [g["title_id"] for g in games if g["title"] == g["title_id"]]
    if untitled:
        print(f"{len(untitled)} files without a readable title: {' '.join(untitled[:10])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
