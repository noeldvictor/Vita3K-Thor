# doa-turnip-full-macroblock-test - 2026-05-13 16:42:46

## Session

- Package: `org.vita3k.emulator.debug`
- PID: `13288`
- Title ID: `PCSH00250`
- Game path: `/storage/2664-21DE/Roms/psvita/Dead or Alive Xtreme 3 - Venus (Asia)(v1.15)(En,Zh,Ko)[vita3k].zip`
- Output directory: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test`
- Logcat lines: `8000`
- Warmup seconds: `18`
- Render trace requested: `False`
- Render trace left enabled: `false`
- Focus: `mCurrentFocus=Window{125dc68 u0 org.vita3k.emulator.debug/org.vita3k.emulator.Emulator} | mFocusedApp=ActivityRecord{35cd297 u0 org.vita3k.emulator.debug/org.vita3k.emulator.Emulator} t1558}`

## Profile Summary

- Render trace lines: `0`
- Scene lines: `0`
- Texture lines: `0`
- No-color scene lines: `0`
- Suspicious macroblock scene lines: `0`
- Large draw lines: `0`
- Error/crash key lines: `41`

## Artifacts

- Logcat: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\logcat.txt`
- Crash buffer: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\crashbuffer.txt`
- Window dump: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\window.txt`
- Gfxinfo: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\gfxinfo.txt`
- Frame stats: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\gfxinfo-framestats.txt`
- Meminfo: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\meminfo.txt`
- CPU info: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\cpuinfo.txt`
- Thermal: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\thermalservice.txt`
- SurfaceFlinger: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\surfaceflinger.txt`
- Top threads: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\top-threads.txt`
- Device props: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\device-props.txt`
- Save list: `tmp\thor-profile\20260513_164227_doa-turnip-full-macroblock-test\savedata-list.txt`

## Suspicious Render Lines

No suspicious render-trace lines matched the built-in filters.

## Error Lines

```text
05-13 12:42:28.430  1196  1196 E Diag_Lib:  Diag_LSM_Init: Failed to open handle to diag driver, error = 111
05-13 12:42:28.996 13288 13326 I spdlog  : [12:42:28.996] |I| [create]: Enabling vulkan validation layers (has a performance impact but allows better error messages)
05-13 12:42:29.068   979  1002 E keystore2: keystore2::remote_provisioning: In get_remote_provisioning_key_and_certs: Error occurred: In get_rem_prov_attest_key: Failed to get a key
05-13 12:42:29.068   979  1002 E keystore2:     4: Error::Rc(ResponseCode(22))
05-13 12:42:29.071   979  1002 E keystore2: keystore2::error: In generate_key.
05-13 12:42:29.071   979  1002 E keystore2:     1: Error::Km(ErrorCode(-67))
05-13 12:42:29.071 29912 13261 E Finsky  :     1: Error::Km(ErrorCode(-67))) (public error code: 12 internal Keystore code: -67)
05-13 12:42:31.166   979  1002 E keystore2: keystore2::remote_provisioning: In get_remote_provisioning_key_and_certs: Error occurred: In get_rem_prov_attest_key: Failed to get a key
05-13 12:42:31.166   979  1002 E keystore2:     4: Error::Rc(ResponseCode(22))
05-13 12:42:31.168   979  1002 E keystore2: keystore2::error: In generate_key.
05-13 12:42:31.168   979  1002 E keystore2:     1: Error::Km(ErrorCode(-67))
05-13 12:42:31.169 29912 13261 E Finsky  :     1: Error::Km(ErrorCode(-67))) (public error code: 12 internal Keystore code: -67)
05-13 12:42:31.208  2263  4900 E ACDB    : AcdbCmdGetGraphAlias:3933 Error[19]: Unable to find graph key vector. No matching key value pairs were found
05-13 12:42:31.221  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-13 12:42:31.222  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-13 12:42:31.222  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-13 12:42:31.222  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-13 12:42:31.232  2263  4900 E AGM: session: session_obj_set_sess_aif_cal: 1770 Error:-114 setting calibration on sess_id:125, aif_id:27
05-13 12:42:31.232  2263  4900 E AGM: API: agm_session_aif_set_cal: 388 Error:-114 setting calibration for session obj                    with session id=125, aif_id=27
05-13 12:42:31.298 13146 13146 E PlayIntegrity: com.google.android.play.core.integrity.StandardIntegrityException: -8: Standard Integrity API error (-8): The calling app has made too many requests to the API and has been throttled, or your app has exceeded its daily request quota.
05-13 12:42:31.298 13146 13146 E PlayIntegrity:  (https://developer.android.com/google/play/integrity/reference/com/google/android/play/core/integrity/model/StandardIntegrityErrorCode.html#TOO_MANY_REQUESTS).
05-13 12:42:31.298 13146 13146 E PlayIntegrity: com.google.android.play.core.integrity.StandardIntegrityException: -8: Standard Integrity API error (-8): The calling app has made too many requests to the API and has been throttled, or your app has exceeded its daily request quota.
05-13 12:42:31.298 13146 13146 E PlayIntegrity:  (https://developer.android.com/google/play/integrity/reference/com/google/android/play/core/integrity/model/StandardIntegrityErrorCode.html#TOO_MANY_REQUESTS).
05-13 12:42:32.394 13288 13619 E spdlog  : [12:42:32.394] |E| [operator()]: _sceKernelWaitSema returned SCE_KERNEL_ERROR_WAIT_TIMEOUT (0x80028005)
05-13 12:42:32.534 13288 13587 W spdlog  : [12:42:32.534] |W| [io_error_impl]: stat_file (_sceIoGetstat) returned 0x80010002
05-13 12:42:32.534 13288 13587 W spdlog  : [12:42:32.534] |W| [io_error_impl]: stat_file (_sceIoGetstat) returned 0x80010002
05-13 12:42:33.147 13288 13587 W spdlog  : [12:42:33.147] |W| [io_error_impl]: stat_file (_sceIoGetstat) returned 0x80010002
05-13 12:42:33.147 13288 13587 W spdlog  : [12:42:33.147] |W| [io_error_impl]: stat_file (_sceIoGetstat) returned 0x80010002
05-13 12:42:33.986 13288 13587 E spdlog  : [12:42:33.986] |E| [operator()]: sceFiberSwitch returned SCE_FIBER_ERROR_PERMISSION (0x80590005)
05-13 12:42:35.241 13288 13619 W spdlog  : [12:42:35.241] |W| [io_error_impl]: open_file (sceAppUtilSaveDataSlotGetParam) returned 0x80010002
05-13 12:42:35.241 13288 13619 E spdlog  : [12:42:35.241] |E| [operator()]: sceAppUtilSaveDataSlotGetParam returned SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND (0x80100641)
05-13 12:42:36.265 13288 13628 I spdlog  : [12:42:36.265] |I| [export_sceIoOpen]: Opening file: app0:/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_KSCL.kscl
05-13 12:42:36.279 13288 13628 V spdlog  : [12:42:36.279] |T| [open_file]: sceIoOpen: Opening archive file app0:/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_KSCL.kscl (ux0:/app/PCSH00250/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_KSCL.kscl), fd: 0x89
05-13 12:42:36.280 13288 13628 I spdlog  : [12:42:36.280] |I| [export_sceIoOpen]: Opening file: app0:/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_ANIME_ANI.ani
05-13 12:42:36.294 13288 13628 V spdlog  : [12:42:36.294] |T| [open_file]: sceIoOpen: Opening archive file app0:/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_ANIME_ANI.ani (ux0:/app/PCSH00250/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_ANIME_ANI.ani), fd: 0x8A
05-13 12:42:36.299 13288 13628 I spdlog  : [12:42:36.299] |I| [export_sceIoOpen]: Opening file: app0:/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_KSLT.kslt
05-13 12:42:36.313 13288 13628 V spdlog  : [12:42:36.313] |T| [open_file]: sceIoOpen: Opening archive file app0:/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_KSLT.kslt (ux0:/app/PCSH00250/00/ui/99_messagewindow_error/LAYOUT_99_MESSAGEWINDOW_ERROR_KSLT.kslt), fd: 0x8B
05-13 12:42:37.910  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 10068 pid 13264 in 0ms
05-13 12:42:37.914  1826  1826 I Zygote  : Process 13264 exited due to signal 9 (Killed)
05-13 12:42:42.032 13288 13687 E spdlog  : [12:42:42.032] |E| [operator()]: sceKernelTryLockLwMutex returned SCE_KERNEL_ERROR_LW_MUTEX_FAILED_TO_OWN (0x80028185)
05-13 12:42:42.187 13288 13692 W spdlog  : [12:42:42.187] |W| [receive]: Error receiving H264 frame: Requires Another Call (AVERROR(EAGAIN)).
```

## Notes

- Generated by `tools/thor_profile_dump.ps1`.
- Keep bulky raw artifacts under `tmp/thor-profile` unless a specific artifact is needed for a committed report.
