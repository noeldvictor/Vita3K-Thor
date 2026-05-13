# UPPERS Thor README Proof - 2026-05-12 22:49:22

## Summary

UPPERS was confirmed working on AYN Thor after the Android present-alpha fix. A fresh device screenshot was captured and added to the README as proof that the scene renders on hardware.

## Artifact

- Screenshot: `docs/screenshots/uppers-thor-android-working-20260512_224811.png`

## Fix Split

- Windows/core renderer path: vertex trap sizing for register-format attributes, depth clamp behavior, disabled-channel blend translation, and repeat-aware `i32mad2` shader translation.
- Android-specific path: Vulkan present shaders now force opaque alpha so SurfaceFlinger does not display a valid rendered frame as black.
- Thor driver note: use an Adreno 7xx-compatible Turnip profile rather than the old A8xx driver path.

## README Update

The old placeholder Thor screenshot section was replaced with the UPPERS proof image and a nontechnical explanation of why both the Windows renderer fixes and the Android present fix were needed.

## Verification

- Fresh screenshot was pulled from the connected AYN Thor with `adb screencap`.
- Screenshot was inspected locally and shows UPPERS rendering in the alley/tutorial scene.
