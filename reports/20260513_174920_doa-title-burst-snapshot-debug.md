# DOA Venus Title Burst Snapshot Debug - 2026-05-13 17:49:20

## Context

- Device: AYN Thor over ADB.
- Title: `PCSH00250` / Dead or Alive Xtreme 3 Venus.
- Launch path: virtual ZIP cartridge.
- Renderer trace: enabled.
- Diagnostic toggle active: `debug.vita3k.force_bcn_decompress=1`.

## Burst Capture

- Burst directory: `tmp/thor-burst/20260513_174806_doa-title-current/`
- Captured 10 screenshots at roughly 300 ms spacing.
- Frame hashes and file sizes changed substantially across the burst.

## Findings

- Single-frame screenshots are misleading for this bug. The title alternates between badly corrupted frames and much cleaner frames.
- `burst_01.png` still shows large flat gray/purple slabs over the beach/title background.
- `burst_08.png` shows a severe transient composite/clear style frame with huge light/dark triangles and most beach detail gone.
- `burst_10.png` is much closer to correct: the beach background, sky, palms, and logo are visible, with remaining overlay/UI/alpha artifacts.
- Earlier live skip testing showed `fhash=70a54078` hides most of the slabs. That shader is a no-texture full-screen/clear-color pass, so the likely problem is not "one bad beach texture"; it is probably render-target copy/composite/load-store ordering, alpha, or surface synchronization around the title background.

## Next Steps

- Use burst snapshots before and after every live render property change for this title.
- Dump the composite pass following `fhash=70a54078` (`fhash=e29e2948`) and compare how it samples `0x62FF8000` across good/bad frames.
- Investigate whether alternating `color_addr` values (`0x62800000`, `0x62BFC000`, `0x62FF8000`) are being loaded, stored, or resolved inconsistently on Android/Turnip.
- Keep `debug.vita3k.force_bcn_decompress` as a diagnostic switch only until we know whether the BCn path is actually part of the title corruption or just changed timing/surface reuse.
