#!/usr/bin/env python3
"""Analyze screenshot bursts for flicker and sudden render corruption.

The tool is intentionally simple: it compares adjacent PNG/JPEG frames from a
burst directory, writes per-frame/per-transition metrics, and creates a contact
sheet of the largest jumps. It is meant to catch cases where a short lucky
burst looks clean but a longer rotating scene still flips between good and bad
render states.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw


IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}


@dataclass(frozen=True)
class FrameStats:
    index: int
    path: Path
    mean_luma: float
    dark_pct: float
    bright_pct: float
    magenta_pct: float
    gray_pct: float
    hash_bits: int


@dataclass(frozen=True)
class TransitionStats:
    previous: int
    current: int
    mean_abs_delta: float
    changed_pct: float
    luma_delta: float
    dark_pct_delta: float
    magenta_pct_delta: float
    gray_pct_delta: float
    phash_distance: int
    spike_score: float = 0.0


def find_frame_dir(path: Path) -> Path:
    if (path / "frames").is_dir():
        return path / "frames"
    return path


def list_frames(frame_dir: Path) -> list[Path]:
    frames = [
        path
        for path in frame_dir.iterdir()
        if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS
    ]
    return sorted(frames, key=lambda p: p.name.lower())


def open_rgb(path: Path, sample_width: int) -> Image.Image:
    image = Image.open(path).convert("RGB")
    if sample_width > 0 and image.width > sample_width:
        sample_height = max(1, round(image.height * sample_width / image.width))
        image = image.resize((sample_width, sample_height), Image.Resampling.BILINEAR)
    return image


def average_hash(image: Image.Image) -> int:
    tiny = image.convert("L").resize((8, 8), Image.Resampling.BILINEAR)
    values = list(tiny.getdata())
    mean = sum(values) / len(values)
    bits = 0
    for value in values:
        bits = (bits << 1) | int(value >= mean)
    return bits


def popcount(value: int) -> int:
    return value.bit_count()


def frame_stats(index: int, path: Path, image: Image.Image) -> FrameStats:
    pixels = list(image.getdata())
    total = len(pixels)
    luma_sum = 0.0
    dark = 0
    bright = 0
    magenta = 0
    gray = 0

    for red, green, blue in pixels:
        luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue
        luma_sum += luma
        if luma < 24:
            dark += 1
        if luma > 232:
            bright += 1
        if red > 160 and blue > 140 and green < 95:
            magenta += 1
        if abs(red - green) < 12 and abs(green - blue) < 12 and 70 <= luma <= 230:
            gray += 1

    return FrameStats(
        index=index,
        path=path,
        mean_luma=luma_sum / total,
        dark_pct=100.0 * dark / total,
        bright_pct=100.0 * bright / total,
        magenta_pct=100.0 * magenta / total,
        gray_pct=100.0 * gray / total,
        hash_bits=average_hash(image),
    )


def transition_stats(
    previous_stats: FrameStats,
    current_stats: FrameStats,
    previous_image: Image.Image,
    current_image: Image.Image,
    changed_threshold: int,
) -> TransitionStats:
    if previous_image.size != current_image.size:
        current_image = current_image.resize(previous_image.size, Image.Resampling.BILINEAR)

    previous_pixels = list(previous_image.getdata())
    current_pixels = list(current_image.getdata())
    total_pixels = len(previous_pixels)
    total_abs_delta = 0
    changed = 0

    for old, new in zip(previous_pixels, current_pixels):
        pixel_delta = (
            abs(old[0] - new[0]) + abs(old[1] - new[1]) + abs(old[2] - new[2])
        ) / 3.0
        total_abs_delta += pixel_delta
        if pixel_delta >= changed_threshold:
            changed += 1

    return TransitionStats(
        previous=previous_stats.index,
        current=current_stats.index,
        mean_abs_delta=total_abs_delta / total_pixels,
        changed_pct=100.0 * changed / total_pixels,
        luma_delta=current_stats.mean_luma - previous_stats.mean_luma,
        dark_pct_delta=current_stats.dark_pct - previous_stats.dark_pct,
        magenta_pct_delta=current_stats.magenta_pct - previous_stats.magenta_pct,
        gray_pct_delta=current_stats.gray_pct - previous_stats.gray_pct,
        phash_distance=popcount(previous_stats.hash_bits ^ current_stats.hash_bits),
    )


def with_spike_scores(transitions: list[TransitionStats]) -> list[TransitionStats]:
    if not transitions:
        return transitions

    deltas = [item.mean_abs_delta for item in transitions]
    median_delta = statistics.median(deltas)
    mad = statistics.median([abs(value - median_delta) for value in deltas])
    scale = max(mad * 1.4826, 1.0)

    scored = []
    for item in transitions:
        spike_score = (item.mean_abs_delta - median_delta) / scale
        scored.append(
            TransitionStats(
                previous=item.previous,
                current=item.current,
                mean_abs_delta=item.mean_abs_delta,
                changed_pct=item.changed_pct,
                luma_delta=item.luma_delta,
                dark_pct_delta=item.dark_pct_delta,
                magenta_pct_delta=item.magenta_pct_delta,
                gray_pct_delta=item.gray_pct_delta,
                phash_distance=item.phash_distance,
                spike_score=spike_score,
            )
        )
    return scored


def write_csv(
    output_path: Path,
    frames: list[FrameStats],
    transitions: list[TransitionStats],
) -> None:
    transition_by_current = {item.current: item for item in transitions}
    with output_path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(
            [
                "frame",
                "file",
                "mean_luma",
                "dark_pct",
                "bright_pct",
                "magenta_pct",
                "gray_pct",
                "prev_mean_abs_delta",
                "prev_changed_pct",
                "prev_luma_delta",
                "prev_dark_pct_delta",
                "prev_magenta_pct_delta",
                "prev_gray_pct_delta",
                "prev_phash_distance",
                "prev_spike_score",
            ]
        )
        for frame in frames:
            transition = transition_by_current.get(frame.index)
            writer.writerow(
                [
                    frame.index,
                    str(frame.path),
                    f"{frame.mean_luma:.3f}",
                    f"{frame.dark_pct:.3f}",
                    f"{frame.bright_pct:.3f}",
                    f"{frame.magenta_pct:.3f}",
                    f"{frame.gray_pct:.3f}",
                    f"{transition.mean_abs_delta:.3f}" if transition else "",
                    f"{transition.changed_pct:.3f}" if transition else "",
                    f"{transition.luma_delta:.3f}" if transition else "",
                    f"{transition.dark_pct_delta:.3f}" if transition else "",
                    f"{transition.magenta_pct_delta:.3f}" if transition else "",
                    f"{transition.gray_pct_delta:.3f}" if transition else "",
                    transition.phash_distance if transition else "",
                    f"{transition.spike_score:.3f}" if transition else "",
                ]
            )


def make_contact_sheet(
    output_path: Path,
    frame_paths: list[Path],
    transitions: list[TransitionStats],
    max_pairs: int,
    thumb_width: int,
) -> None:
    selected = sorted(
        transitions,
        key=lambda item: (
            item.spike_score,
            item.mean_abs_delta,
            item.changed_pct,
            item.phash_distance,
        ),
        reverse=True,
    )[:max_pairs]
    if not selected:
        return

    thumbs: list[tuple[TransitionStats, Image.Image, Image.Image]] = []
    for transition in selected:
        previous = Image.open(frame_paths[transition.previous - 1]).convert("RGB")
        current = Image.open(frame_paths[transition.current - 1]).convert("RGB")
        thumb_height = max(1, round(previous.height * thumb_width / previous.width))
        previous = previous.resize((thumb_width, thumb_height), Image.Resampling.BILINEAR)
        current = current.resize((thumb_width, thumb_height), Image.Resampling.BILINEAR)
        thumbs.append((transition, previous, current))

    label_height = 52
    gutter = 8
    row_width = thumb_width * 2 + gutter * 3
    row_height = thumbs[0][1].height + label_height + gutter
    sheet = Image.new("RGB", (row_width, row_height * len(thumbs)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)

    for row, (transition, previous, current) in enumerate(thumbs):
        y = row * row_height
        draw.text(
            (gutter, y + 4),
            (
                f"{transition.previous:04d}->{transition.current:04d} "
                f"delta={transition.mean_abs_delta:.1f} "
                f"changed={transition.changed_pct:.1f}% "
                f"spike={transition.spike_score:.1f} "
                f"hash={transition.phash_distance}"
            ),
            fill=(235, 235, 235),
        )
        sheet.paste(previous, (gutter, y + label_height))
        sheet.paste(current, (thumb_width + gutter * 2, y + label_height))

    sheet.save(output_path)


def write_summary(
    output_path: Path,
    frame_dir: Path,
    frames: list[FrameStats],
    transitions: list[TransitionStats],
    top_count: int,
) -> None:
    lines: list[str] = []
    lines.append(f"Frame dir: {frame_dir}")
    lines.append(f"Frame count: {len(frames)}")
    if transitions:
        lines.append(
            "Mean adjacent delta: "
            f"{statistics.mean(item.mean_abs_delta for item in transitions):.3f}"
        )
        lines.append(
            "Median adjacent delta: "
            f"{statistics.median(item.mean_abs_delta for item in transitions):.3f}"
        )
        lines.append(
            "Max adjacent delta: "
            f"{max(item.mean_abs_delta for item in transitions):.3f}"
        )
        lines.append("")
        lines.append("Largest transitions:")
        for transition in sorted(
            transitions,
            key=lambda item: (
                item.spike_score,
                item.mean_abs_delta,
                item.changed_pct,
                item.phash_distance,
            ),
            reverse=True,
        )[:top_count]:
            lines.append(
                "- "
                f"{transition.previous:04d}->{transition.current:04d}: "
                f"delta={transition.mean_abs_delta:.3f}, "
                f"changed={transition.changed_pct:.3f}%, "
                f"luma_delta={transition.luma_delta:.3f}, "
                f"dark_delta={transition.dark_pct_delta:.3f}%, "
                f"magenta_delta={transition.magenta_pct_delta:.3f}%, "
                f"gray_delta={transition.gray_pct_delta:.3f}%, "
                f"hash={transition.phash_distance}, "
                f"spike={transition.spike_score:.3f}"
            )

    lines.append("")
    lines.append("Most magenta frames:")
    for frame in sorted(frames, key=lambda item: item.magenta_pct, reverse=True)[:top_count]:
        lines.append(
            f"- {frame.index:04d}: magenta={frame.magenta_pct:.3f}% "
            f"dark={frame.dark_pct:.3f}% gray={frame.gray_pct:.3f}% file={frame.path.name}"
        )

    lines.append("")
    lines.append("Darkest frames:")
    for frame in sorted(frames, key=lambda item: item.dark_pct, reverse=True)[:top_count]:
        lines.append(
            f"- {frame.index:04d}: dark={frame.dark_pct:.3f}% "
            f"luma={frame.mean_luma:.3f} file={frame.path.name}"
        )

    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("burst_dir", help="Burst session directory or frames directory")
    parser.add_argument(
        "--sample-width",
        type=int,
        default=320,
        help="Downsample width for analysis, or 0 for original size",
    )
    parser.add_argument(
        "--changed-threshold",
        type=int,
        default=32,
        help="RGB mean delta threshold for changed-pixel percentage",
    )
    parser.add_argument("--top", type=int, default=12, help="Number of transitions to summarize")
    parser.add_argument(
        "--contact-sheet-width",
        type=int,
        default=320,
        help="Thumbnail width for the contact sheet",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    burst_dir = Path(args.burst_dir)
    frame_dir = find_frame_dir(burst_dir)
    frame_paths = list_frames(frame_dir)
    if len(frame_paths) < 2:
        raise SystemExit(f"Need at least two frames in {frame_dir}")

    images: list[Image.Image] = []
    frames: list[FrameStats] = []
    for index, path in enumerate(frame_paths, start=1):
        image = open_rgb(path, args.sample_width)
        images.append(image)
        frames.append(frame_stats(index, path, image))

    transitions = []
    for index in range(1, len(frames)):
        transitions.append(
            transition_stats(
                frames[index - 1],
                frames[index],
                images[index - 1],
                images[index],
                args.changed_threshold,
            )
        )
    transitions = with_spike_scores(transitions)

    output_root = burst_dir if (burst_dir / "frames").is_dir() else frame_dir
    csv_path = output_root / "flicker_metrics.csv"
    summary_path = output_root / "flicker_summary.txt"
    contact_sheet_path = output_root / "flicker_contact_sheet.jpg"

    write_csv(csv_path, frames, transitions)
    write_summary(summary_path, frame_dir, frames, transitions, args.top)
    make_contact_sheet(
        contact_sheet_path,
        frame_paths,
        transitions,
        max_pairs=args.top,
        thumb_width=args.contact_sheet_width,
    )

    top_transition = max(
        transitions,
        key=lambda item: (item.spike_score, item.mean_abs_delta, item.changed_pct),
    )
    print(f"Wrote metrics: {csv_path}")
    print(f"Wrote summary: {summary_path}")
    print(f"Wrote contact sheet: {contact_sheet_path}")
    print(
        "Largest transition: "
        f"{top_transition.previous:04d}->{top_transition.current:04d} "
        f"delta={top_transition.mean_abs_delta:.3f} "
        f"changed={top_transition.changed_pct:.3f}% "
        f"spike={top_transition.spike_score:.3f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
