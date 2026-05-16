---
name: vita3k-ghidra-escalation
description: Use when Vita3K renderer/core evidence asks a concrete Vita-side question that needs Ghidra/static analysis of legally owned game executables, imports, GXM calls, material setup, or shader/resource usage.
metadata:
  short-description: Ghidra escalation for Vita evidence
---

# Vita3K Ghidra Escalation

Use Ghidra only after runtime evidence names a specific Vita-side question, such as `sceGxm*` call ordering, material/depth flags, texture descriptors, render target setup, shader constants, or imported NIDs near a suspicious draw.

## Preconditions

- A focused SQLite case exists.
- The question references concrete evidence: title ID, scene, shader hash, texture/surface address, draw range, log line, or RenderDoc/resource capture.
- Files come from the user's legally owned local dumps and stay under ignored `tmp/`.
- No commercial game binaries, decrypted modules, Ghidra projects, or extracted ELFs are committed.

## Local Tooling

Local Ghidra:

```powershell
C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat
```

VitaLoaderRedux is installed under the local Ghidra extensions. If an `eboot.bin` starts with an `SCE\0` container and stock Ghidra cannot load it, extract the embedded ELF first:

```powershell
.\tools\ghidra\ExtractEmbeddedVitaElf.ps1 -InputPath <eboot.bin> -OutputPath tmp\<case>\<title>.elf
```

## Output

Record a SQLite entry with:

- What Vita-side question was asked.
- Input file path under ignored `tmp/`.
- Imports/NIDs/functions/call order inspected.
- How the finding changes the emulator hypothesis.
- Any scripts or local tooling added to the repo.

If static analysis does not answer the runtime renderer question, record that explicitly and return to capture/instrumentation rather than guessing.
