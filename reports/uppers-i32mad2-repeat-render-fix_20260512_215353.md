# UPPERS i32mad2 Repeat Render Fix - 2026-05-12 21:53

## Result

UPPERS rendering is visibly much better in the Windows Vulkan debug loop after commit `8dc74a9e` (`Handle repeat offsets for i32mad2 shaders`). The live repro that previously showed severe missing/corrupt character geometry now keeps the character much more intact. The user confirmed the scene "looks a lot better", with the remaining visible issue currently described as a weird artifact near the shoes.

## Fix

The fix is in `vita3k/shader/src/translator/ialu.cpp`.

`USSETranslatorVisitor::i32mad2` was not honoring USSE repeat behavior. It loaded all operands and stored the destination with offset `0`, so repeated integer multiply-add instructions could reuse the wrong source/destination registers. In UPPERS this showed up as broken skinning/geometry in the glitch scene.

The updated translator now:

- Enables repeat handling with `set_repeat_multiplier(1, 1, 1, 1)`.
- Runs the instruction through `BEGIN_REPEAT(count)` / `END_REPEAT()`.
- Applies `GET_REPEAT(inst, RepeatMode::SLMSI)`.
- Loads repeated source operands with the proper repeat offsets.
- Stores the repeated destination with `dest_repeat_offset`.
- Keeps immediate/fpconstant operands fixed at offset `0`, because literals should not walk like registers.
- Resets repeat state after translation.

## Verification

- Built Windows target successfully with:
  `cmake --build build\windows-vs2022 --config RelWithDebInfo --target vita3k --parallel`
- Launched UPPERS cartridge on Windows Vulkan:
  `Vita3K.exe --config-location tmp\vita3k-win-debug\config.yml --cartridge --thor-render-trace --backend-renderer Vulkan tmp\local-games\Uppers (English v0.97)[vita3k].zip`
- Confirmed UPPERS running in the window title:
  `UPPERS (PCSG00633) | Vulkan`
- Captured current proof screenshot locally:
  `tmp\vita3k-win-debug\uppers-loaded-game-proof_20260512_214520.png`
- Earlier bad-scene screenshot for comparison:
  `tmp\vita3k-win-debug\uppers-glitch-beginscene_20260512_213935.png`

## Remaining Issue

The main scene is not fully clean yet. The next visible artifact is near the character's shoes/feet. Treat that as a separate renderer/shader follow-up, not as proof that the repeat fix failed.

The active next lead is still depth/scene semantics around `sceGxmBeginSceneEx`: we added trace logging for load/store depth surfaces in commit `02f75314`, and the next clean capture should compare those surfaces before changing depth compare/clear behavior again.

## Notes

Do not commit the older local force-depth or force-shader debug probes as fixes. They were useful experiments, but the repeat-aware `i32mad2` change is the real confirmed improvement so far.
