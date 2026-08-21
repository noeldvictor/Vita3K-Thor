# Qualcomm references (Snapdragon 8 Gen 2 / QCS8550, Adreno 740)

> **The PDFs are gitignored** (~25MB; see `.gitignore`). They are present in a
> working checkout but not committed. Copy them from
> `ps3-thor/rpcsx-ui-android/docs/hardware/` and
> `xenia-thor/docs/reference/snapdragon/`, which carry the same set. Only this
> README is tracked here.

The Arm SWOGs next door cover the CPU side. These cover everything else on the
Thor's SoC - the GPU the whole renderer runs on, and the kernel it runs under.

| file | what | pages |
|---|---|---|
| `adreno-game-developer-guide.pdf` | **Qualcomm Adreno Game Developer Guide**, 80-78185-2 AL (March 2026) | 200 |
| `snapdragon-opencl-optimization-guide.pdf` | Snapdragon OpenCL General Programming and Optimization | 116 |
| `qualcomm-linux-kernel-guide.pdf` | Qualcomm Linux Kernel Guide, 80-70022-3 AB (Nov 2025) | 159 |
| `snapdragon-8-gen-2-product-brief.pdf` | 8 Gen 2 product brief - clocks and block counts only | 2 |

**Start with the Adreno guide.** It is the primary source for the things
`vita3k/renderer/` gets wrong on this hardware: tiled (binning) vs direct
rendering and what forces a flush out of tiling, render-pass load/store op
costs, when `vkCmdClearAttachments` beats `LOAD_OP_CLEAR`, texture/compression
formats the hardware actually samples natively, UBO vs SSBO access costs, and
descriptor-set update patterns. Reach for it before profiling; several of Thor's
open renderer items are questions it answers directly.

The OpenCL guide is here for its memory-system chapters - Adreno's cache
hierarchy, UBWC, and load/store coalescing are the same silicon whether you
drive it from CL or Vulkan - not because anything in Vita3K uses OpenCL.

The kernel guide matters for scheduler and cpuset behaviour when reasoning about
which cluster a thread landed on. See also `../thor/ayn-thor-user-manual.pdf`.

## Reading them

The `Read` tool cannot render these. Use pypdf, which is installed:

```python
import pypdf
r = pypdf.PdfReader("docs/reference/snapdragon/adreno-game-developer-guide.pdf")
print(r.pages[13].extract_text())
```
