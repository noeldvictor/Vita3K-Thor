# UPPERS Draw 76 77 Render Dump - 2026-05-12 17:41:14

## Current Finding

The UPPERS glitch scene hit the targeted live filter `dump=rt=704x396:draw=76-77`.

Both bad draws share:

- render target `704x396`
- color surface `0x624F3000`
- depth surface `0x626F3000`
- vertex shader hash `1f77db0ffd47f5c4b4f467a2a587d87ed9aa2c9a6ae8f2fede37e9949ab2ad2d`
- fragment shader hash `f69d12a6c070ff8c9c07053ef4fb6493a6621215fbbe4f7b295ca12bf7df39b6`
- one vertex stream, six attributes, zero vertex textures, four fragment textures
- double-buffer memory mapping
- depth write disabled and cull mode enabled

## Draw 76

- index address `0x7356C56C`
- count `1248`
- max index `421`
- vertex stream address `0x7356E444`
- stride `36`
- required stream bytes `15192`

## Draw 77

- index address `0x7356CF98`
- count `2640`
- max index `744`
- vertex stream address `0x73571FA8`
- stride `36`
- required stream bytes `26820`

## Attribute Layout

- `reg=0`, `loc=0`, offset `0`, `F32 x3`, likely position
- `reg=4`, `loc=1`, offset `12`, `F16 x4`
- `reg=12`, `loc=3`, offset `20`, `F16 x2`
- `reg=8`, `loc=2`, offset `24`, `U8N x4`
- `reg=16`, `loc=4`, offset `28`, `U8N x4`
- `reg=20`, `loc=5`, offset `32`, `U8N x4`

## Fragment Textures

All four fragment textures are `UBC3` (`0x87000000`):

- slot `0`: `0x64936A40`, `512x512`
- slot `1`: `0x6489E840`, `128x128`
- slot `2`: `0x64848840`, `128x128`
- slot `3`: `0x6484C840`, `256x256`

## Interpretation

The first dump does not show an obvious index overrun. The next diagnostic patch adds indexed attribute min/max and first-sample logging, especially for the `F32 x3` position attribute. That should split the problem cleanly:

- If position min/max is already absurd, focus on vertex stream lifetime, memory mapping, or GXM attribute interpretation.
- If positions are sane, focus on shader uniforms, shader translation, viewport/depth state, surface sync, or texture/surface paths.
