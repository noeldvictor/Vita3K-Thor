# PS Vita hardware references (the guest side)

> **The PDFs are gitignored**; only this README is tracked. Re-fetch with the
> commands at the bottom.

Everything else under `docs/reference/` describes the *host* - the Thor's Arm
cores and Adreno GPU. This directory is the other half: the hardware Vita3K is
pretending to be. Emulator bugs usually come from a mismatch between the two, so
both sides matter.

| file | what | pages |
|---|---|---|
| `sgx-architecture-guide-for-developers.pdf` | **PowerVR Series 5 SGX Architecture Guide** - the Vita GPU | 27 |
| `powervr-hardware-architecture-overview.pdf` | PowerVR TBDR overview - tiling, the deferred pass, HSR | 15 |
| `powervr-performance-recommendations.pdf` | PowerVR "Golden Rules" | 11 |
| `cortex-a9-trm.pdf` | **Cortex-A9 TRM**, ARM DDI 0388G r3p0 - the Vita CPU | 214 |

## Why these matter here

The Vita is an **SGX543MP4+**: four SGX543+ cores doing **tile based deferred
rendering**, fed by a Master VDM that splits one libgxm command stream across
them. Two consequences show up constantly in this codebase:

* **The Vita defers and the Adreno bins.** Both are tilers, so a lot of GXM
  behaviour maps over cleanly - but not all of it. Where a GXM program relies on
  the SGX resolving a tile before a transfer reads it, Vita3K has to make that
  ordering explicit on Vulkan. That is exactly the class of bug behind the
  `disable-surface-sync` default in CLAUDE.md.
* **Hidden Surface Removal is free on the Vita and is not on Adreno.** Games
  written for the SGX submit geometry in orders that would be wasteful
  elsewhere, because HSR made overdraw cost nothing. Judge a game's draw
  patterns against the SGX guide before assuming the game is doing something
  silly.

The CPU side is a quad-core **Cortex-A9** (ARMv7-A, VFPv3, NEON) at 444MHz-2GHz.
The TRM is the reference for what the guest instruction stream is *defined* to
do - memory ordering, the exclusive monitor, VFP rounding and NaN behaviour -
when a dynarmic translation looks wrong. Pair it with `../arm/` for the host
side: `../arm/arm-architecture-reference-manual-a-profile.pdf` is the AArch64
architecture the JIT emits *into*.

## Reading them

The `Read` tool cannot render these. Use pypdf:

```python
import pypdf
r = pypdf.PdfReader("docs/reference/vita/sgx-architecture-guide-for-developers.pdf")
print(r.pages[8].extract_text())
```

## Re-fetching

```
cd docs/reference/vita
curl -L -o sgx-architecture-guide-for-developers.pdf \
  "https://raw.githubusercontent.com/anonymousjustice/pvr-pi/master/SDKPackage_OGLES2/Documentation/PowerVR%20Series%205.SGX%20Architecture%20Guide%20for%20Developers.1.0.13.External.pdf"
curl -L -o powervr-hardware-architecture-overview.pdf \
  "https://powervr-graphics.github.io/WebGL_SDK/WebGL_SDK/Documentation/Architecture%20Guides/PowerVR%20Hardware.Architecture%20Overview%20for%20Developers.pdf"
curl -L -o powervr-performance-recommendations.pdf \
  "https://powervr-graphics.github.io/WebGL_SDK/WebGL_SDK/Documentation/Architecture%20Guides/PowerVR%20Performance%20Recommendations.The%20Golden%20Rules.pdf"
curl -L -o cortex-a9-trm.pdf \
  "https://documentation-service.arm.com/static/5f0370fccafe527e86f5bfb2"
```

Imagination's own CDN (`cdn.imgtec.com`) 403s on direct fetch; the GitHub and
`powervr-graphics.github.io` mirrors above are the working routes.

Reverse-engineered material the vendor docs do not cover - GXM command formats,
SGX543 instruction encodings - lives at
[psdevwiki Graphics](https://www.psdevwiki.com/vita/Graphics) and
[henkaku wiki SGX543](https://wiki.henkaku.xyz/vita/SGX543).
