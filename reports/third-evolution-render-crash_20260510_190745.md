# third-evolution-render-crash - 2026-05-10 19:08:09

## Launch

- Package: `org.vita3k.emulator.debug`
- Game path: `/storage/2664-21DE/Roms/psvita/Eiyuu Densetsu - Sora no Kiseki the 3rd Evolution (English v1.0)(PCSG00490)[vita3k].zip`
- Seconds captured: `22`
- Log level: `0`
- Render trace: `True`

## Artifacts

- Logcat: `reports\third-evolution-render-crash_20260510_190745-logcat.txt`
- Crash buffer: `reports\third-evolution-render-crash_20260510_190745-crashbuffer.txt`
- Window dump: `reports\third-evolution-render-crash_20260510_190745-window.txt`
- Meminfo: `reports\third-evolution-render-crash_20260510_190745-meminfo.txt`
- Screenshot: `reports\third-evolution-render-crash_20260510_190745-screen.png`

## Key Lines

```text
05-10 19:07:47.051  2140 11759 W WindowManager: Failed looking up window session=Session{4c28b0b 5221:u0a10159} callers=com.android.server.wm.WindowManagerService.windowForClientLocked:6579 com.android.server.wm.Session.updateRequestedVisibilities:701 android.view.IWindowSession$Stub.onTransact:1075 
05-10 19:07:47.065  1196  1196 E Diag_Lib: diag:failed to connect to diag socket
05-10 19:07:47.065  1196  1196 E Diag_Lib:  Diag_LSM_Init: Failed to open handle to diag driver, error = 111
05-10 19:07:47.067  5221  5261 I Vita3KThor: Native arguments: [-a, true, --cartridge, /storage/2664-21DE/Roms/psvita/Eiyuu Densetsu - Sora no Kiseki the 3rd Evolution (English v1.0)(PCSG00490)[vita3k].zip, --log-level, 0, --thor-render-trace]
05-10 19:07:47.067  5221  5221 E SurfaceSyncer: Failed to find sync for id=0
05-10 19:07:47.084  2278  2278 E HWComposer: setContentType: setContentType failed for display 4630946441858561667: Unsupported (8)
05-10 19:07:47.084  5221  5221 E SurfaceSyncer: Failed to find sync for id=0
05-10 19:07:47.084  5221  5221 E SurfaceSyncer: Failed to find sync for id=1
05-10 19:07:47.084  5221  5221 E SurfaceSyncer: Failed to find sync for id=0
05-10 19:07:47.087  5221  5261 I spdlog  : [19:07:47.087] |I| [init_config]: input-content-path: /storage/2664-21DE/Roms/psvita/Eiyuu Densetsu - Sora no Kiseki the 3rd Evolution (English v1.0)(PCSG00490)[vita3k].zip
05-10 19:07:47.634  5221  5261 I spdlog  : [19:07:47.634] |I| [create]: Enabling vulkan validation layers (has a performance impact but allows better error messages)
05-10 19:07:47.659  5221  5261 I spdlog  : [19:07:47.659] |I| [SDL_main]: Thor renderer GXM trace enabled from command line
05-10 19:07:48.571  2277 30788 D AAudioServiceEndpointMMAP: open() FLOAT failed, perhaps due to format. Try again with 32_BIT
05-10 19:07:48.573  2277 30788 D AAudioServiceEndpointMMAP: open() 32_BIT failed, perhaps due to format. Try again with 24_BIT_PACKED
05-10 19:07:48.574  2277 30788 D AAudioServiceEndpointMMAP: open() 24_BIT failed, perhaps due to format. Try again with 16_BIT
05-10 19:07:48.588  2277 15880 D AAudioServiceEndpointMMAP: open() FLOAT failed, perhaps due to format. Try again with 32_BIT
05-10 19:07:48.589  2277 15880 D AAudioServiceEndpointMMAP: open() 32_BIT failed, perhaps due to format. Try again with 24_BIT_PACKED
05-10 19:07:48.591  2277 15880 D AAudioServiceEndpointMMAP: open() 24_BIT failed, perhaps due to format. Try again with 16_BIT
05-10 19:07:49.491  5221  5261 I spdlog  : [19:07:49.491] |I| [mount_archive_content_as_cartridge]: 英雄伝説 空の軌跡 the 3rd Evolution [PCSG00490] mounted directly from archive "/storage/2664-21DE/Roms/psvita/Eiyuu Densetsu - Sora no Kiseki the 3rd Evolution (English v1.0)(PCSG00490)[vita3k].zip" root app/PCSG00490/
05-10 19:07:49.543  2263  4900 E ACDB    : AcdbCmdGetGraphAlias:3933 Error[19]: Unable to find graph key vector. No matching key value pairs were found
05-10 19:07:49.543  2263  4900 E gsl     : gsl_get_graph_alias:2241 acdb get graph alias failed 19, len 255
05-10 19:07:49.560  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-10 19:07:49.560  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-10 19:07:49.560  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-10 19:07:49.560  2263  4900 E ACDB    : AcdbCmdGetProcSubgraphCalDataPersist:8217 Error[19]: No calibration found
05-10 19:07:49.571  2263  4900 E gsl     : gsl_graph_send_nonpersist_cal:789 send non-perist cal failed 9
05-10 19:07:49.571  2263  4900 E gsl     : gsl_graph_set_sg_cal:1988 graph send non-persist cal failed 9
05-10 19:07:49.571  2263  4900 E gsl     : gsl_graph_set_cal:3482 set cal failed: 9
05-10 19:07:49.571  2263  4900 E gsl     : gsl_set_cal:1419 graph set cal failed: 9
05-10 19:07:49.571  2263  4900 E AGM: graph: graph_set_cal: 1179 graph_set_cal failed -114
05-10 19:07:49.571  2263  4900 E AGM: session: session_obj_set_sess_aif_cal: 1770 Error:-114 setting calibration on sess_id:125, aif_id:27
05-10 19:07:49.571  2263  4900 E AGM: API: agm_session_aif_set_cal: 388 Error:-114 setting calibration for session obj                    with session id=125, aif_id=27
05-10 19:07:49.571  2263  4900 E PLUGIN: mixer: amp_pcm_calibration_put: 1296 amp_pcm_calibration_put: set_calbration failed, err -114, aif_id 27
05-10 19:07:49.571  2263  4900 E PAL: SessionAlsaPcm: setConfig: 669: failed to set the tag calibration -114
05-10 19:07:49.571  2263  4900 E PAL: SessionAlsaPcm: start: 1672: Setting volume failed
05-10 19:07:49.586  5221  5261 E spdlog  : The Vulkan spec states: Each binary semaphore element of the pSignalSemaphores member of any element of pSubmits must be unsignaled when the semaphore signal operation it defines is executed on the device (https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html#VUID-vkQueueSubmit-pSignalSemaphores-00067)
05-10 19:07:49.588  5221  5261 E spdlog  : The Vulkan spec states: Each binary semaphore element of the pSignalSemaphores member of any element of pSubmits must be unsignaled when the semaphore signal operation it defines is executed on the device (https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html#VUID-vkQueueSubmit-pSignalSemaphores-00067)
05-10 19:07:50.033  5221  5673 I spdlog  : [19:07:50.033] |I| [export_sceIoOpen]: Opening file: app0:/gamedata/data.psarc
05-10 19:07:50.229  5221  5261 E spdlog  : The Vulkan spec states: Each binary semaphore element of the pSignalSemaphores member of any element of pSubmits must be unsignaled when the semaphore signal operation it defines is executed on the device (https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html#VUID-vkQueueSubmit-pSignalSemaphores-00067)
05-10 19:07:59.053  5221  5673 V spdlog  : [19:07:59.053] |T| [open_file]: sceIoOpen: Opening archive file app0:/gamedata/data.psarc (ux0:/app/PCSG00490/gamedata/data.psarc), fd: 0x3
05-10 19:07:59.190  5221  5673 I spdlog  : [19:07:59.190] |I| [export_sceIoOpen]: Opening file: app0:/gamedata/data.psarc
05-10 19:08:08.215  5221  5673 V spdlog  : [19:08:08.215] |T| [open_file]: sceIoOpen: Opening archive file app0:/gamedata/data.psarc (ux0:/app/PCSG00490/gamedata/data.psarc), fd: 0x4
05-10 19:08:08.371  5221  5673 I spdlog  : [19:08:08.371] |I| [export_sceIoOpen]: Opening file: app0:/gamedata/data0.psarc
```

## Notes

- Generated by `tools/thor_adb_debug_capture.ps1`.
- Keep bulky raw logs/screenshots out of git unless they are specifically needed for a report.
