#!/usr/bin/env python3
# Vita3K Thor helper
# Copyright (C) 2026 Vita3K team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


WRITE_TYPES = {
    0x0000: ("u8", 1),
    0x0100: ("u16", 2),
    0x0200: ("u32", 4),
}

ARM_WRITE_TYPES = {
    0xA000: ("arm_u8", 1),
    0xA100: ("arm_u16", 2),
    0xA200: ("arm_u32", 4),
}


def parse_hex(token: str) -> int | None:
    token = token.strip()
    if token.startswith("$"):
        token = token[1:]
    if token.lower().startswith("0x"):
        token = token[2:]
    if not token:
        return None
    try:
        return int(token, 16)
    except ValueError:
        return None


def parse_file(path: Path, title_override: str | None = None) -> dict[str, Any]:
    title_id = title_override or path.stem.upper()
    game_name = ""
    cheats: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    relative_segment: int | None = None
    pending_pointer: dict[str, Any] | None = None

    def unsupported(line_number: int, line: str, reason: str) -> None:
        nonlocal pending_pointer
        if current is not None:
            current["unsupported"].append({"line": line_number, "text": line, "reason": reason})
        pending_pointer = None

    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith("//"):
            continue

        if line.startswith("_S"):
            parts = line.split(maxsplit=1)
            if len(parts) == 2 and not title_override:
                title_id = parts[1].strip().upper()
            continue

        if line.startswith("_G"):
            parts = line.split(maxsplit=1)
            if len(parts) == 2:
                game_name = parts[1].strip()
            continue

        if line.startswith("_V0") or line.startswith("_V1"):
            if pending_pointer is not None:
                unsupported(pending_pointer["line"], pending_pointer["text"], "unterminated pointer write")
            name = line[3:].strip() or f"Cheat {len(cheats) + 1}"
            current = {
                "name": name,
                "default_enabled": line.startswith("_V1"),
                "writes": [],
                "unsupported": [],
            }
            cheats.append(current)
            relative_segment = None
            continue

        if not line.startswith("$"):
            continue

        if current is None:
            current = {
                "name": "Loose VitaCheat codes",
                "default_enabled": True,
                "writes": [],
                "unsupported": [],
            }
            cheats.append(current)

        fields = line.split()
        if len(fields) < 3:
            unsupported(line_number, line, "expected code, address, and value")
            continue

        code_type = parse_hex(fields[0])
        address = parse_hex(fields[1])
        value = parse_hex(fields[2])
        if code_type is None or address is None or value is None:
            unsupported(line_number, line, "invalid hex token")
            continue

        if pending_pointer is not None:
            if code_type == 0x3300:
                current["writes"].append(
                    {
                        "line": pending_pointer["line"],
                        "type": pending_pointer["type"],
                        "width": pending_pointer["width"],
                        "pointer_base": pending_pointer["pointer_base"],
                        "pointer_offset": pending_pointer["pointer_offset"],
                        "final_offset": f"0x{address:08X}",
                        "value": f"0x{value:08X}",
                        "relative_segment": pending_pointer["relative_segment"],
                    }
                )
                pending_pointer = None
                continue

            unsupported(pending_pointer["line"], pending_pointer["text"], "unterminated pointer write")

        if code_type == 0xB200:
            if address in range(4) and value == 0:
                relative_segment = address
            else:
                unsupported(line_number, line, "unsupported base selector")
            continue

        write_type = WRITE_TYPES.get(code_type)
        arm_write_type = ARM_WRITE_TYPES.get(code_type)
        pointer_prefix = code_type & 0xF000
        pointer_width_type = (code_type >> 8) & 0xF
        pointer_level = code_type & 0xFF

        if write_type is None and arm_write_type is not None:
            write_type = arm_write_type
            code_patch = True
        else:
            code_patch = False

        if write_type is None and pointer_prefix == 0x3000 and pointer_width_type in range(3):
            if pointer_level == 1:
                value_type, width = WRITE_TYPES[pointer_width_type << 8]
                pending_pointer = {
                    "line": line_number,
                    "text": line,
                    "type": f"ptr1_{value_type}",
                    "width": width,
                    "pointer_base": f"0x{address:08X}",
                    "pointer_offset": f"0x{value:08X}",
                    "relative_segment": relative_segment,
                }
            else:
                unsupported(line_number, line, "only level-1 pointer writes are converted")
            continue

        if write_type is None:
            unsupported(line_number, line, "unsupported VitaCheat code type")
            continue

        value_type, width = write_type
        current["writes"].append(
            {
                "line": line_number,
                "type": value_type,
                "width": width,
                "address": f"0x{address:08X}",
                "value": f"0x{value:08X}",
                "relative_segment": relative_segment,
                "code_patch": code_patch,
            }
        )

    if pending_pointer is not None:
        unsupported(pending_pointer["line"], pending_pointer["text"], "unterminated pointer write")

    return {
        "schema": "vita3k-thor.cheats.v1",
        "source": str(path),
        "title_id": title_id,
        "game": game_name,
        "notes": [
            "Converted from VitaCheat format for offline single-player use.",
            "Static u8/u16/u32 writes, ARM/code writes, level-1 pointer writes, and simple $B200 module-relative selectors are converted.",
            "Unsupported lines are preserved for review but are not applied by Vita3K Thor.",
        ],
        "cheats": cheats,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert VitaCheat .psv files into Vita3K Thor JSON metadata.")
    parser.add_argument("inputs", nargs="+", type=Path, help="VitaCheat .psv file(s) to convert")
    parser.add_argument("-o", "--output", type=Path, default=Path("cheats") / "converted", help="Output directory")
    parser.add_argument("--title-id", help="Force a title ID when converting one file")
    args = parser.parse_args()

    if args.title_id and len(args.inputs) != 1:
        parser.error("--title-id can only be used with one input file")

    args.output.mkdir(parents=True, exist_ok=True)
    for input_path in args.inputs:
        converted = parse_file(input_path, args.title_id)
        output_path = args.output / f"{converted['title_id']}.json"
        output_path.write_text(json.dumps(converted, indent=2) + "\n", encoding="utf-8")
        print(output_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
