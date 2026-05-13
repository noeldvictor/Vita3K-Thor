# doa-title-surface-sync-on - 2026-05-13 15:23:29

## Session

- Package: `org.vita3k.emulator.debug`
- PID: `14402`
- Title ID: `PCSH00250`
- Game path: `/storage/2664-21DE/Roms/psvita/Dead or Alive Xtreme 3 - Venus (Asia)(v1.15)(En,Zh,Ko)[vita3k].zip`
- Output directory: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on`
- Logcat lines: `22000`
- Warmup seconds: `28`
- Render trace requested: `True`
- Render trace left enabled: `false`
- Focus: `mCurrentFocus=Window{6791aaa u0 org.vita3k.emulator.debug/org.vita3k.emulator.Emulator} | mFocusedApp=ActivityRecord{ea75ee7 u0 org.vita3k.emulator.debug/org.vita3k.emulator.Emulator} t1542}`

## Profile Summary

- Render trace lines: `50`
- Scene lines: `3`
- Texture lines: `0`
- No-color scene lines: `0`
- Suspicious macroblock scene lines: `0`
- Large draw lines: `12`
- Error/crash key lines: `42`

## Artifacts

- Logcat: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\logcat.txt`
- Crash buffer: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\crashbuffer.txt`
- Window dump: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\window.txt`
- Gfxinfo: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\gfxinfo.txt`
- Frame stats: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\gfxinfo-framestats.txt`
- Meminfo: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\meminfo.txt`
- CPU info: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\cpuinfo.txt`
- Thermal: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\thermalservice.txt`
- SurfaceFlinger: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\surfaceflinger.txt`
- Top threads: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\top-threads.txt`
- Device props: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\device-props.txt`
- Save list: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\savedata-list.txt`
- Screenshot: `tmp\thor-profile\20260513_152259_doa-title-surface-sync-on\screen.png`

## Suspicious Render Lines

```text
05-13 11:23:00.110  7356  7396 I spdlog  : [11:23:00.110] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45519 draw=3 prim=0 index_fmt=0 count=2073 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=1 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.111  7356  7396 I spdlog  : [11:23:00.111] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=4 prim=0 index_fmt=0 count=3840 instances=1 pipeline=true framebuffer_fetch=false vhash=231f36a1f96641357a09fc05a20293c84eee5f063c7fc4ec3c6d888071ec0489 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=0 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.111  7356  7396 I spdlog  : [11:23:00.111] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=8 prim=0 index_fmt=0 count=8790 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.111  7356  7396 I spdlog  : [11:23:00.111] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=10 prim=0 index_fmt=0 count=4266 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.111  7356  7396 I spdlog  : [11:23:00.111] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=11 prim=0 index_fmt=0 count=2199 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.112  7356  7396 I spdlog  : [11:23:00.112] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=13 prim=0 index_fmt=0 count=9129 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.112  7356  7396 I spdlog  : [11:23:00.112] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=14 prim=0 index_fmt=0 count=7011 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.112  7356  7396 I spdlog  : [11:23:00.112] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=15 prim=0 index_fmt=0 count=3093 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.112  7356  7396 I spdlog  : [11:23:00.112] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=17 prim=0 index_fmt=0 count=3402 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.112  7356  7396 I spdlog  : [11:23:00.112] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=18 prim=0 index_fmt=0 count=6141 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.113  7356  7396 I spdlog  : [11:23:00.113] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=29 prim=0 index_fmt=0 count=2442 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=0 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
05-13 11:23:00.113  7356  7396 I spdlog  : [11:23:00.113] |I| [draw]: ThorRenderTrace draw frame=11905 scene=45520 draw=31 prim=0 index_fmt=0 count=5694 instances=1 pipeline=true framebuffer_fetch=false vhash=699d720f79db59d666feed09a5f01c0ec3ba81fb710ab1c7b7dad3c10dfb5582 fhash=564cd0f61cb3c743bb718b8199b44f413a1e9fdbf1fe134159bc6ad0bf9bb2b5 vtex=0 ftex=1 vbufs=11 fbufs=4 depth_func=25165824/12582912 depth_write=0/0 stencil_func=234881024/234881024 cull=2 two_sided=0 vp_flat=false z_offset=0 z_scale=1 writing_mask=0
```

## Error Lines

```text
05-13 11:23:00.076  7356  7397 D spdlog  : [11:23:00.076] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775e2b0514
05-13 11:23:00.107  7356  7397 D spdlog  : [11:23:00.107] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775e2b0514
05-13 11:23:00.109  7356  7397 D spdlog  : [11:23:00.109] |D| [operator()]: Unhandled SIGSEGV at pc 0x000000775e2b0514
05-13 11:23:00.144  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 10159 pid 7356 in 5ms
05-13 11:23:00.246  1826  1826 I Zygote  : Process 7356 exited due to signal 9 (Killed)
05-13 11:23:00.251  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 10123 pid 6841 in 0ms
05-13 11:23:00.271  1826  1826 I Zygote  : Process 28380 exited due to signal 9 (Killed)
05-13 11:23:00.292  1826  1826 I Zygote  : Process 28474 exited due to signal 9 (Killed)
05-13 11:23:00.300  1826  1826 I Zygote  : Process 28383 exited due to signal 9 (Killed)
05-13 11:23:00.316  1826  1826 I Zygote  : Process 28372 exited due to signal 9 (Killed)
05-13 11:23:00.319  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 10035 pid 28474 in 0ms
05-13 11:23:00.319  1826  1826 I Zygote  : Process 6841 exited due to signal 9 (Killed)
05-13 11:23:00.319  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 10147 pid 28372 in 0ms
05-13 11:23:00.320  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 10131 pid 28383 in 0ms
05-13 11:23:00.320  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 10139 pid 28380 in 0ms
05-13 11:23:00.341  2263  3437 E AGM: session: session_obj_set_sess_aif_cal: 1770 Error:-114 setting calibration on sess_id:125, aif_id:27
05-13 11:23:00.342  2263  3437 E AGM: API: agm_session_aif_set_cal: 388 Error:-114 setting calibration for session obj                    with session id=125, aif_id=27
05-13 11:23:00.384  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 1000 pid 25095 in 0ms
05-13 11:23:00.405  1826  1826 I Zygote  : Process 25095 exited due to signal 9 (Killed)
05-13 11:23:00.436   979  1002 E keystore2: keystore2::remote_provisioning: In get_remote_provisioning_key_and_certs: Error occurred: In get_rem_prov_attest_key: Failed to get a key
05-13 11:23:00.436   979  1002 E keystore2:     4: Error::Rc(ResponseCode(22))
05-13 11:23:00.439   979  1002 E keystore2: keystore2::error: In generate_key.
05-13 11:23:00.439   979  1002 E keystore2:     1: Error::Km(ErrorCode(-67))
05-13 11:23:00.440 29912 14501 E Finsky  :     1: Error::Km(ErrorCode(-67))) (public error code: 12 internal Keystore code: -67)
05-13 11:23:00.801  1196  1196 E Diag_Lib:  Diag_LSM_Init: Failed to open handle to diag driver, error = 111
05-13 11:23:00.865 14402 14529 I spdlog  : [11:23:00.865] |I| [create]: Enabling vulkan validation layers (has a performance impact but allows better error messages)
05-13 11:23:01.640   979  1002 E keystore2: keystore2::remote_provisioning: In get_remote_provisioning_key_and_certs: Error occurred: In get_rem_prov_attest_key: Failed to get a key
05-13 11:23:01.640   979  1002 E keystore2:     4: Error::Rc(ResponseCode(22))
05-13 11:23:01.642   979  1002 E keystore2: keystore2::error: In generate_key.
05-13 11:23:01.642   979  1002 E keystore2:     1: Error::Km(ErrorCode(-67))
05-13 11:23:01.643 29912 14501 E Finsky  :     1: Error::Km(ErrorCode(-67))) (public error code: 12 internal Keystore code: -67)
05-13 11:23:03.726   979  1002 E keystore2: keystore2::remote_provisioning: In get_remote_provisioning_key_and_certs: Error occurred: In get_rem_prov_attest_key: Failed to get a key
05-13 11:23:03.726   979  1002 E keystore2:     4: Error::Rc(ResponseCode(22))
05-13 11:23:03.728   979  1002 E keystore2: keystore2::error: In generate_key.
05-13 11:23:03.728   979  1002 E keystore2:     1: Error::Km(ErrorCode(-67))
05-13 11:23:03.729 29912 14501 E Finsky  :     1: Error::Km(ErrorCode(-67))) (public error code: 12 internal Keystore code: -67)
05-13 11:23:03.807 14312 14312 E PlayIntegrity: com.google.android.play.core.integrity.StandardIntegrityException: -8: Standard Integrity API error (-8): The calling app has made too many requests to the API and has been throttled, or your app has exceeded its daily request quota.
05-13 11:23:03.807 14312 14312 E PlayIntegrity:  (https://developer.android.com/google/play/integrity/reference/com/google/android/play/core/integrity/model/StandardIntegrityErrorCode.html#TOO_MANY_REQUESTS).
05-13 11:23:03.807 14312 14312 E PlayIntegrity: com.google.android.play.core.integrity.StandardIntegrityException: -8: Standard Integrity API error (-8): The calling app has made too many requests to the API and has been throttled, or your app has exceeded its daily request quota.
05-13 11:23:03.807 14312 14312 E PlayIntegrity:  (https://developer.android.com/google/play/integrity/reference/com/google/android/play/core/integrity/model/StandardIntegrityErrorCode.html#TOO_MANY_REQUESTS).
05-13 11:23:10.445  2140  2188 I libprocessgroup: Successfully killed process cgroup uid 10068 pid 14504 in 0ms
05-13 11:23:10.448  1826  1826 I Zygote  : Process 14504 exited due to signal 9 (Killed)
```

## Notes

- Generated by `tools/thor_profile_dump.ps1`.
- Keep bulky raw artifacts under `tmp/thor-profile` unless a specific artifact is needed for a committed report.
