# uppers-profile-launch - 2026-05-11 22:24:50

## Session

- Package: `org.vita3k.emulator.debug`
- PID: `15268`
- Title ID: `PCSG00633`
- Game path: `/storage/2664-21DE/Roms/psvita/Uppers (English v0.97)[vita3k].zip`
- Output directory: `tmp\thor-profile\uppers-profile-launch_20260511_222430`
- Logcat lines: `24000`
- Warmup seconds: `18`
- Render trace requested: `True`
- Render trace left enabled: `false`
- Focus: `mCurrentFocus=Window{c029ae8 u0 com.android.launcher3/com.android.launcher3.secondarydisplay.SecondaryDisplayLauncher} | mFocusedApp=ActivityRecord{af28eb0 u0 com.android.launcher3/.secondarydisplay.SecondaryDisplayLauncher} t1431} | mCurrentFocus=Window{4cb1ae9 u0 org.vita3k.emulator.debug/org.vita3k.emulator.Emulator} | mFocusedApp=ActivityRecord{f28895e u0 org.vita3k.emulator.debug/org.vita3k.emulator.Emulator} t1447}`

## Profile Summary

- Render trace lines: `2514`
- Scene lines: `523`
- Texture lines: `86`
- No-color scene lines: `0`
- Suspicious macroblock scene lines: `0`
- Large draw lines: `0`
- Error/crash key lines: `80`

## Artifacts

- Logcat: `tmp\thor-profile\uppers-profile-launch_20260511_222430\logcat.txt`
- Crash buffer: `tmp\thor-profile\uppers-profile-launch_20260511_222430\crashbuffer.txt`
- Window dump: `tmp\thor-profile\uppers-profile-launch_20260511_222430\window.txt`
- Gfxinfo: `tmp\thor-profile\uppers-profile-launch_20260511_222430\gfxinfo.txt`
- Frame stats: `tmp\thor-profile\uppers-profile-launch_20260511_222430\gfxinfo-framestats.txt`
- Meminfo: `tmp\thor-profile\uppers-profile-launch_20260511_222430\meminfo.txt`
- CPU info: `tmp\thor-profile\uppers-profile-launch_20260511_222430\cpuinfo.txt`
- Thermal: `tmp\thor-profile\uppers-profile-launch_20260511_222430\thermalservice.txt`
- SurfaceFlinger: `tmp\thor-profile\uppers-profile-launch_20260511_222430\surfaceflinger.txt`
- Top threads: `tmp\thor-profile\uppers-profile-launch_20260511_222430\top-threads.txt`
- Device props: `tmp\thor-profile\uppers-profile-launch_20260511_222430\device-props.txt`
- Save list: `tmp\thor-profile\uppers-profile-launch_20260511_222430\savedata-list.txt`
- Screenshot: `tmp\thor-profile\uppers-profile-launch_20260511_222430\screen.png`

## Suspicious Render Lines

No suspicious render-trace lines matched the built-in filters.

## Error Lines

```text
05-11 22:24:34.287  2263  4900 E ACDB    : AcdbCmdGetGraphAlias:3933 Error[19]: Unable to find graph key vector. No matching key value pairs were found
05-11 22:24:34.304  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-11 22:24:34.304  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-11 22:24:34.304  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-11 22:24:34.304  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-11 22:24:34.315  2263  4900 E AGM: session: session_obj_set_sess_aif_cal: 1770 Error:-114 setting calibration on sess_id:125, aif_id:27
05-11 22:24:34.315  2263  4900 E AGM: API: agm_session_aif_set_cal: 388 Error:-114 setting calibration for session obj                    with session id=125, aif_id=27
05-11 22:24:35.654 15268 15601 E spdlog  : [22:24:35.654] |E| [operator()]: sceKernelTryLockLwMutex returned SCE_KERNEL_ERROR_LW_MUTEX_FAILED_TO_OWN (0x80028185)
05-11 22:24:38.770 15268 15307 D spdlog  : [22:24:38.770] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:40.342 15268 15627 E spdlog  : [22:24:40.342] |E| [operator()]: _sceKernelWaitCond returned SCE_KERNEL_ERROR_WAIT_TIMEOUT (0x80028005)
05-11 22:24:40.813 15268 15307 D spdlog  : [22:24:40.813] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:40.847 15268 15307 D spdlog  : [22:24:40.847] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:40.897 15268 15307 D spdlog  : [22:24:40.897] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:40.913 15268 15307 D spdlog  : [22:24:40.913] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:40.947 15268 15307 D spdlog  : [22:24:40.947] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:40.980 15268 15307 D spdlog  : [22:24:40.980] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.030 15268 15307 D spdlog  : [22:24:41.030] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.047 15268 15307 D spdlog  : [22:24:41.047] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.080 15268 15307 D spdlog  : [22:24:41.080] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.113 15268 15307 D spdlog  : [22:24:41.113] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.164 15268 15307 D spdlog  : [22:24:41.164] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.180 15268 15307 D spdlog  : [22:24:41.180] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.214 15268 15307 D spdlog  : [22:24:41.214] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.248 15268 15307 D spdlog  : [22:24:41.248] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.297 15268 15307 D spdlog  : [22:24:41.297] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.313 15268 15307 D spdlog  : [22:24:41.313] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.347 15268 15307 D spdlog  : [22:24:41.347] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.397 15268 15307 D spdlog  : [22:24:41.397] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.413 15268 15307 D spdlog  : [22:24:41.413] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.447 15268 15307 D spdlog  : [22:24:41.447] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.480 15268 15307 D spdlog  : [22:24:41.480] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.513 15268 15307 D spdlog  : [22:24:41.513] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.564 15268 15307 D spdlog  : [22:24:41.563] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.580 15268 15307 D spdlog  : [22:24:41.580] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.613 15268 15307 D spdlog  : [22:24:41.613] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.664 15268 15307 D spdlog  : [22:24:41.663] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.697 15268 15307 D spdlog  : [22:24:41.697] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.713 15268 15307 D spdlog  : [22:24:41.713] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.747 15268 15307 D spdlog  : [22:24:41.747] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.780 15268 15307 D spdlog  : [22:24:41.780] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.813 15268 15307 D spdlog  : [22:24:41.813] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.847 15268 15307 D spdlog  : [22:24:41.847] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.880 15268 15307 D spdlog  : [22:24:41.880] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.913 15268 15307 D spdlog  : [22:24:41.913] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.964 15268 15307 D spdlog  : [22:24:41.963] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:41.980 15268 15307 D spdlog  : [22:24:41.980] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.013 15268 15307 D spdlog  : [22:24:42.013] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.047 15268 15307 D spdlog  : [22:24:42.047] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.097 15268 15307 D spdlog  : [22:24:42.097] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.113 15268 15307 D spdlog  : [22:24:42.113] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.147 15268 15307 D spdlog  : [22:24:42.147] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.180 15268 15307 D spdlog  : [22:24:42.180] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.213 15268 15307 D spdlog  : [22:24:42.213] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.247 15268 15307 D spdlog  : [22:24:42.247] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.280 15268 15307 D spdlog  : [22:24:42.280] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.313 15268 15307 D spdlog  : [22:24:42.313] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.364 15268 15307 D spdlog  : [22:24:42.363] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.380 15268 15307 D spdlog  : [22:24:42.380] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.413 15268 15307 D spdlog  : [22:24:42.413] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.463 15268 15307 D spdlog  : [22:24:42.463] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.480 15268 15307 D spdlog  : [22:24:42.480] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.513 15268 15307 D spdlog  : [22:24:42.513] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.547 15268 15307 D spdlog  : [22:24:42.547] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.597 15268 15307 D spdlog  : [22:24:42.597] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.613 15268 15307 D spdlog  : [22:24:42.613] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.648 15268 15307 D spdlog  : [22:24:42.647] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.680 15268 15307 D spdlog  : [22:24:42.680] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.730 15268 15307 D spdlog  : [22:24:42.730] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.749 15268 15307 D spdlog  : [22:24:42.749] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.780 15268 15307 D spdlog  : [22:24:42.780] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.813 15268 15307 D spdlog  : [22:24:42.813] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.847 15268 15307 D spdlog  : [22:24:42.847] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.880 15268 15307 D spdlog  : [22:24:42.880] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.913 15268 15307 D spdlog  : [22:24:42.913] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.947 15268 15307 D spdlog  : [22:24:42.947] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:42.980 15268 15307 D spdlog  : [22:24:42.980] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:43.014 15268 15307 D spdlog  : [22:24:43.013] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:43.047 15268 15307 D spdlog  : [22:24:43.047] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:43.975 15268 15307 D spdlog  : [22:24:43.975] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
05-11 22:24:47.329 15268 15307 D spdlog  : [22:24:47.329] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775d077180
```

## Notes

- Generated by `tools/thor_profile_dump.ps1`.
- Keep bulky raw artifacts under `tmp/thor-profile` unless a specific artifact is needed for a committed report.
