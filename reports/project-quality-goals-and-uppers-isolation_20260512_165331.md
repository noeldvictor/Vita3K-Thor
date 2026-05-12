# Project Quality Goals And UPPERS Isolation - 2026-05-12 16:53:31

## Context

Vita3K Thor should be treated as a performance, quality, and usability fork, not only a compatibility fork. A game booting is not enough if renderer quality, frame pacing, audio behavior, OSD readability, input routing, save state durability, or debug tooling are weak.

## AGENTS.md Update

- Added a `Project Goals` section near the top of `AGENTS.md`.
- Called out performance, quality, usability, renderer correctness, audio, input, OSD readability, and debug-loop speed as first-class outcomes.
- Documented that performance work should be measured with logs, screenshots, profiles, frame pacing data, or before/after reports.
- Documented that the Windows and ADB debug loops are part of the product direction.

## Current UPPERS Renderer State

- Windows live render control is working through `tmp/vita3k-win-debug/render-control.txt`.
- The visible bad blue geometry in UPPERS has been isolated to visible render target `704x396`.
- The strongest culprit range is draw `76-77` on that render target.
- The shader pair for the bad geometry is:
  - vertex hash `1f77db0ffd47f5c4b4f467a2a587d87ed9aa2c9a6ae8f2fede37e9949ab2ad2d`
  - fragment hash `f69d12a6c070ff8c9c07053ef4fb6493a6621215fbbe4f7b295ca12bf7df39b6`
- Skipping `rt=704x396:draw=76-77` removes the large blue slab, which confirms the issue is in those draws or the state feeding them.

## Evidence Files

- `reports/windows-uppers-targeted-baseline_20260512_162756.png`
- `reports/windows-uppers-targeted-skip-rt704-070-090_20260512_163631.png`
- `reports/windows-uppers-targeted-skip-rt704-070-079_20260512_164009.png`
- `reports/windows-uppers-targeted-skip-rt704-075-079_20260512_164430.png`
- `reports/windows-uppers-targeted-skip-rt704-076-077_20260512_164658.png`
- `reports/windows-uppers-targeted-skip-rt704-076_20260512_164915.png`
- `reports/windows-uppers-targeted-skip-rt704-077_20260512_165020.png`

## Next Useful Debug Step

Add a targeted vertex and texture metadata dump for the matching draw filters. The current trace identifies the bad draw range and shader hashes, but the next fix likely needs stream address, stride, attribute format, index metadata, and texture source details for draw `76-77`.
