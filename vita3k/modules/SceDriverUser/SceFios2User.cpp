// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <module/module.h>

#include <atomic>

#include "io/functions.h"
#include <io/io.h>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceFios2User);

enum SceFiosErrorCode {
    SCE_FIOS_OK = 0
};

typedef SceUID SceFiosOverlayID;

enum SceFiosOverlayResolveMode {
    SCE_FIOS_OVERLAY_RESOLVE_FOR_READ = 0,
    SCE_FIOS_OVERLAY_RESOLVE_FOR_WRITE = 1
};

template <>
std::string to_debug_str<SceFiosOverlayResolveMode>(const MemState &mem, SceFiosOverlayResolveMode type) {
    switch (type) {
    case SCE_FIOS_OVERLAY_RESOLVE_FOR_READ:
        return "SCE_FIOS_OVERLAY_RESOLVE_FOR_READ";
    case SCE_FIOS_OVERLAY_RESOLVE_FOR_WRITE:
        return "SCE_FIOS_OVERLAY_RESOLVE_FOR_WRITE";
    }
    return std::to_string(type);
}

EXPORT(int, sceFiosOverlayAddForProcess02, SceUID processId, SceFiosProcessOverlay *pOverlay, SceFiosOverlayID *pOutID) {
    TRACY_FUNC(sceFiosOverlayAddForProcess02, processId, pOverlay, pOutID);
    if (pOverlay->type != SCE_FIOS_OVERLAY_TYPE_OPAQUE)
        LOG_WARN("Using unimplemented overlay type {}.", fmt::underlying(pOverlay->type));

    *pOutID = create_overlay(emuenv.io, pOverlay);

    return SCE_FIOS_OK;
}

EXPORT(int, sceFiosOverlayGetInfoForProcess02, SceUID processId, SceFiosOverlayID id, SceFiosProcessOverlay *pOutOverlay) {
    TRACY_FUNC(sceFiosOverlayGetInfoForProcess02, processId, id, pOutOverlay);
    if (!pOutOverlay)
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);

    const std::lock_guard<std::mutex> guard(emuenv.io.overlay_mutex);
    for (const auto &overlay : emuenv.io.overlays) {
        if (overlay.id != id)
            continue;

        memset(pOutOverlay, 0, sizeof(*pOutOverlay));
        pOutOverlay->type = overlay.type;
        pOutOverlay->order = overlay.order;
        pOutOverlay->process_id = overlay.process_id;
        strncpy(pOutOverlay->dst, overlay.dst.c_str(), sizeof(pOutOverlay->dst) - 1);
        strncpy(pOutOverlay->src, overlay.src.c_str(), sizeof(pOutOverlay->src) - 1);
        pOutOverlay->dst_size = static_cast<int16_t>(strlen(pOutOverlay->dst));
        pOutOverlay->src_size = static_cast<int16_t>(strlen(pOutOverlay->src));
        return SCE_FIOS_OK;
    }

    return RET_ERROR(SCE_ERROR_ERRNO_ENOENT);
}

EXPORT(int, sceFiosOverlayGetList02, SceUID processId, uint32_t minOrder, uint32_t maxOrder, SceFiosOverlayID *pOutIDs, SceUInt32 maxIDs, SceUInt32 *pActualIDs) {
    TRACY_FUNC(sceFiosOverlayGetList02, processId, minOrder, maxOrder, pOutIDs, maxIDs, pActualIDs);
    const std::lock_guard<std::mutex> guard(emuenv.io.overlay_mutex);

    std::vector<SceFiosOverlayID> overlay_ids;
    for (const auto &overlay : emuenv.io.overlays) {
        if (overlay.order >= minOrder && overlay.order <= maxOrder)
            overlay_ids.push_back(overlay.id);
    }

    if (pActualIDs)
        *pActualIDs = overlay_ids.size();

    if (pOutIDs)
        memcpy(pOutIDs, overlay_ids.data(), std::min<uint32_t>(overlay_ids.size(), maxIDs) * sizeof(SceFiosOverlayID));

    return SCE_FIOS_OK;
}

EXPORT(int, sceFiosOverlayGetRecommendedScheduler02, int param1, const char *path) {
    TRACY_FUNC(sceFiosOverlayGetRecommendedScheduler02, param1, path);
    // reversed engineered
    if (param1 <= 1)
        return 0;

    // returns if path starts with hostk: with k an integer
    if (strlen(path) < strlen("host0:"))
        return 0;

    return memcmp(path, "host", 4) == 0 && path[4] <= '9' && path[5] == ':';
}

EXPORT(int, sceFiosOverlayModifyForProcess02) {
    TRACY_FUNC(sceFiosOverlayModifyForProcess02);
    return UNIMPLEMENTED();
}

EXPORT(int, sceFiosOverlayRemoveForProcess02, SceUID processId, SceFiosOverlayID id) {
    TRACY_FUNC(sceFiosOverlayRemoveForProcess02, processId, id);
    if (!remove_overlay(emuenv.io, id))
        return RET_ERROR(SCE_ERROR_ERRNO_ENOENT);

    return SCE_FIOS_OK;
}

EXPORT(int, sceFiosOverlayResolveSync02) {
    TRACY_FUNC(sceFiosOverlayResolveSync02);
    return UNIMPLEMENTED();
}

EXPORT(int, sceFiosOverlayResolveWithRangeSync02, SceUID processId, SceFiosOverlayResolveMode resolveFlag, const char *pInPath, char *pOutPath, SceUInt32 maxPath, SceUInt32 min_order, SceUInt32 max_order) {
    TRACY_FUNC(sceFiosOverlayResolveWithRangeSync02, processId, resolveFlag, pInPath, pOutPath, maxPath, min_order, max_order);
    const std::string resolved = resolve_path(emuenv.io, pInPath, min_order, max_order);
    // Thor: the first resolutions show how the game's FIOS walks its overlays,
    // which is the evidence that decided the Trails PSARC layering. Keep it cheap.
    static std::atomic<int> logged_resolves{ 0 };
    if (logged_resolves.fetch_add(1) < 48)
        LOG_INFO("FIOS resolve flag={} order=[{}, {}] {} -> {}", fmt::underlying(resolveFlag), min_order, max_order, pInPath, resolved);
    strncpy(pOutPath, resolved.c_str(), maxPath);

    return SCE_FIOS_OK;
}

EXPORT(int, sceFiosOverlayThreadIsDisabled02) {
    TRACY_FUNC(sceFiosOverlayThreadIsDisabled02);
    return UNIMPLEMENTED();
}

EXPORT(int, sceFiosOverlayThreadSetDisabled02) {
    TRACY_FUNC(sceFiosOverlayThreadSetDisabled02);
    return UNIMPLEMENTED();
}
