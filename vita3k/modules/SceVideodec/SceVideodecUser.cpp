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

#include <codec/state.h>
#include <kernel/state.h>
#include <modules/SceVideodec/quick_state.h>
#include <util/lock_and_find.h>

#include <limits>
#include <sstream>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceVideodecUser);

typedef std::shared_ptr<H264DecoderState> H264DecoderPtr;
typedef std::map<SceUID, H264DecoderPtr> H264DecoderStates;

struct VideodecState {
    std::mutex mutex;
    H264DecoderStates decoders;
};

namespace sce_videodec {

static bool quick_state_parse_u64_text(const std::string &text, uint64_t &out, const int base = 10) {
    if (text.empty())
        return false;

    size_t consumed = 0;
    try {
        out = std::stoull(text, &consumed, base);
    } catch (...) {
        return false;
    }
    return consumed == text.size();
}

static bool quick_state_parse_u32_text(const std::string &text, uint32_t &out, const int base = 10) {
    uint64_t parsed = 0;
    if (!quick_state_parse_u64_text(text, parsed, base) || parsed > std::numeric_limits<uint32_t>::max())
        return false;
    out = static_cast<uint32_t>(parsed);
    return true;
}

static bool quick_state_parse_bool_text(const std::string &text, bool &out) {
    if (text == "1" || text == "true") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false") {
        out = false;
        return true;
    }
    return false;
}

static std::map<std::string, std::string> quick_state_parse_fields(const std::string &text) {
    std::map<std::string, std::string> fields;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(';', start);
        const std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t separator = item.find('=');
        if (separator != std::string::npos)
            fields[item.substr(0, separator)] = item.substr(separator + 1);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return fields;
}

std::string quick_state_snapshot_text(EmuEnvState &emuenv) {
    std::ostringstream text;
    text << "schema=thor.videodec.v1\n";

    VideodecState *state = emuenv.kernel.obj_store.get_if<VideodecState>();
    if (!state) {
        text << "decoders=0\n";
        return text.str();
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    size_t decoder_count = 0;
    for (const auto &[handle, decoder] : state->decoders) {
        if (decoder)
            decoder_count++;
    }
    text << "decoders=" << decoder_count << "\n";
    size_t index = 0;
    for (const auto &[handle, decoder] : state->decoders) {
        if (!decoder)
            continue;

        text << "decoder." << index++
             << "=handle=" << handle
             << ";width_in=" << decoder->width_in
             << ";height_in=" << decoder->height_in
             << ";width_out=" << decoder->width_out
             << ";height_out=" << decoder->height_out
             << ";pts=" << decoder->pts
             << ";dts=" << decoder->dts
             << ";pts_out=" << decoder->pts_out
             << ";output_yuvp3=" << decoder->output_yuvp3
             << ";stopped=" << decoder->is_stopped
             << "\n";
    }

    return text.str();
}

bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, size_t *decoder_count, std::string *detail) {
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.videodec.v1") {
        if (detail)
            *detail = "Videodec section schema is invalid";
        return false;
    }

    uint64_t parsed_decoders = 0;
    if (!values.contains("decoders") || !quick_state_parse_u64_text(values.at("decoders"), parsed_decoders) || parsed_decoders > 64) {
        if (detail)
            *detail = "Videodec section header is invalid";
        return false;
    }
    if (decoder_count)
        *decoder_count = static_cast<size_t>(parsed_decoders);

    for (size_t index = 0; index < static_cast<size_t>(parsed_decoders); index++) {
        const std::string key = fmt::format("decoder.{}", index);
        const auto value = values.find(key);
        if (value == values.end()) {
            if (detail)
                *detail = fmt::format("Videodec decoder {} is missing", index);
            return false;
        }

        const auto fields = quick_state_parse_fields(value->second);
        static constexpr const char *required_fields[] = {
            "handle",
            "width_in",
            "height_in",
            "width_out",
            "height_out",
            "pts",
            "dts",
            "pts_out",
            "output_yuvp3",
            "stopped",
        };
        for (const char *field : required_fields) {
            if (!fields.contains(field)) {
                if (detail)
                    *detail = fmt::format("Videodec decoder {} field '{}' is missing", index, field);
                return false;
            }
        }

        uint32_t parsed_u32 = 0;
        uint64_t parsed_u64 = 0;
        bool parsed_bool = false;
        if (!quick_state_parse_u32_text(fields.at("handle"), parsed_u32) || parsed_u32 == 0
            || !quick_state_parse_u32_text(fields.at("width_in"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("height_in"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("width_out"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("height_out"), parsed_u32)
            || !quick_state_parse_u64_text(fields.at("pts"), parsed_u64)
            || !quick_state_parse_u64_text(fields.at("dts"), parsed_u64)
            || !quick_state_parse_u64_text(fields.at("pts_out"), parsed_u64)
            || !quick_state_parse_bool_text(fields.at("output_yuvp3"), parsed_bool)
            || !quick_state_parse_bool_text(fields.at("stopped"), parsed_bool)) {
            if (detail)
                *detail = fmt::format("Videodec decoder {} metadata is invalid", index);
            return false;
        }
    }

    return true;
}

bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail) {
    size_t decoder_count = 0;
    if (!quick_state_validate_snapshot_values(values, &decoder_count, detail))
        return false;

    VideodecState *state = emuenv.kernel.obj_store.get_if<VideodecState>();
    if (!state) {
        emuenv.kernel.obj_store.create<VideodecState>();
        state = emuenv.kernel.obj_store.get_if<VideodecState>();
    }
    if (!state) {
        if (detail)
            *detail = "Videodec state object could not be created";
        return false;
    }

    H264DecoderStates restored_decoders;
    for (size_t index = 0; index < decoder_count; index++) {
        const auto fields = quick_state_parse_fields(values.at(fmt::format("decoder.{}", index)));
        uint32_t handle = 0;
        uint32_t width_in = 0;
        uint32_t height_in = 0;
        uint32_t width_out = 0;
        uint32_t height_out = 0;
        bool output_yuvp3 = false;
        bool stopped = true;
        uint64_t pts = ~0ull;
        uint64_t dts = ~0ull;
        uint64_t pts_out = ~0ull;
        if (!quick_state_parse_u32_text(fields.at("handle"), handle)
            || !quick_state_parse_u32_text(fields.at("width_in"), width_in)
            || !quick_state_parse_u32_text(fields.at("height_in"), height_in)
            || !quick_state_parse_u32_text(fields.at("width_out"), width_out)
            || !quick_state_parse_u32_text(fields.at("height_out"), height_out)
            || !quick_state_parse_u64_text(fields.at("pts"), pts)
            || !quick_state_parse_u64_text(fields.at("dts"), dts)
            || !quick_state_parse_u64_text(fields.at("pts_out"), pts_out)
            || !quick_state_parse_bool_text(fields.at("output_yuvp3"), output_yuvp3)
            || !quick_state_parse_bool_text(fields.at("stopped"), stopped)) {
            if (detail)
                *detail = fmt::format("Videodec decoder {} restore fields are invalid", index);
            return false;
        }

        auto decoder = std::make_shared<H264DecoderState>(width_in, height_in);
        decoder->set_res(width_out, height_out);
        decoder->set_output_format(output_yuvp3);
        decoder->pts = pts;
        decoder->dts = dts;
        decoder->pts_out = pts_out;
        decoder->is_stopped = stopped;
        restored_decoders.emplace(static_cast<SceUID>(handle), std::move(decoder));
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->decoders.swap(restored_decoders);
    }
    return true;
}

} // namespace sce_videodec

enum {
    SCE_AVCDEC_ERROR_INVALID_PARAM = 0x80620002,
};

enum SceVideodecType {
    SCE_VIDEODEC_TYPE_HW_AVCDEC = 0x1001
};

struct SceVideodecTimeStamp {
    uint32_t upper;
    uint32_t lower;
};

enum SceCscCoefficient : uint8_t {
    SCE_AVCDEC_CSC_COEFFICIENT_DEFAULT = 0,
    SCE_AVCDEC_CSC_COEFFICIENT_ITU601 = 1,
    SCE_AVCDEC_CSC_COEFFICIENT_ITU709 = 2,
    SCE_AVCDEC_CSC_COEFFICIENT_ITU601_FULL = 3,
    SCE_AVCDEC_CSC_COEFFICIENT_ITU709_FULL = 4,
};

struct SceAvcdecFrameOptionRGBA {
    uint8_t alpha;
    SceCscCoefficient cscCoefficient;
    uint8_t reserved[14];
};

union SceAvcdecFrameOption {
    uint8_t reserved[16];
    SceAvcdecFrameOptionRGBA rgba;
};

struct SceAvcdecQueryDecoderInfo {
    uint32_t horizontal;
    uint32_t vertical;
    uint32_t numOfRefFrames; //!< Number of reference frames
};

struct SceAvcdecDecoderInfo {
    uint32_t frameMemSize;
};

struct SceAvcdecInfo {
    uint32_t numUnitsInTick;
    uint32_t timeScale;
    uint8_t fixedFrameRateFlag;

    uint8_t aspectRatioIdc;
    uint16_t sarWidth;
    uint16_t sarHeight;

    uint8_t colourPrimaries;
    uint8_t transferCharacteristics;
    uint8_t matrixCoefficients;

    uint8_t videoFullRangeFlag;

    uint8_t picStruct[2];
    uint8_t ctType;

    SceVideodecTimeStamp pts;
};

struct SceAvcdecBuf {
    Ptr<void> pBuf;
    uint32_t size;
};

struct SceAvcdecCtrl {
    SceUID handle;
    SceAvcdecBuf frameBuf;
};

struct SceAvcdecAu {
    SceVideodecTimeStamp pts;
    SceVideodecTimeStamp dts;
    SceAvcdecBuf es;
};

enum ScePixelType : uint32_t {
    SCE_AVCDEC_PIXEL_RGBA8888 = 0x0U, // A:B:G:R=8:8:8:8    32bit   U8U8U8U8_ABGR
    SCE_AVCDEC_PIXEL_BGRA8888 = 0x8U, // A:R:G:B=8:8:8:8    32bit   U8U8U8U8_ARGB
    SCE_AVCDEC_PIXEL_YUV420_RASTER = 0x10U, // YUV420P3
    SCE_AVCDEC_PIXEL_YUV420_PACKED_RASTER = 0x20U, // YVU420P2
};

struct SceAvcdecFrame {
    ScePixelType pixelType;
    uint32_t framePitch;
    uint32_t frameWidth;
    uint32_t frameHeight;

    uint32_t horizontalSize;
    uint32_t verticalSize;

    uint32_t frameCropLeftOffset;
    uint32_t frameCropRightOffset;
    uint32_t frameCropTopOffset;
    uint32_t frameCropBottomOffset;

    SceAvcdecFrameOption opt;

    Ptr<void> pPicture[2];
};

struct SceAvcdecPicture {
    uint32_t size;
    SceAvcdecFrame frame;
    SceAvcdecInfo info;
};

struct SceAvcdecArrayPicture {
    uint32_t numOfOutput;
    uint32_t numOfElm;
    Ptr<Ptr<SceAvcdecPicture>> pPicture;
};

EXPORT(int, sceAvcdecCreateDecoder, uint32_t codec_type, SceAvcdecCtrl *decoder, const SceAvcdecQueryDecoderInfo *query) {
    TRACY_FUNC(sceAvcdecCreateDecoder, codec_type, decoder, query);
    assert(codec_type == SCE_VIDEODEC_TYPE_HW_AVCDEC);
    const auto state = emuenv.kernel.obj_store.get<VideodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);

    SceUID handle = emuenv.kernel.get_next_uid();
    decoder->handle = handle;

    state->decoders[handle] = std::make_shared<H264DecoderState>(query->horizontal, query->vertical);

    return 0;
}

EXPORT(int, sceAvcdecCreateDecoderInternal) {
    TRACY_FUNC(sceAvcdecCreateDecoderInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecCreateDecoderNongameapp) {
    TRACY_FUNC(sceAvcdecCreateDecoderNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecCsc) {
    TRACY_FUNC(sceAvcdecCsc);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecCscInternal) {
    TRACY_FUNC(sceAvcdecCscInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecode, SceAvcdecCtrl *decoder, const SceAvcdecAu *au, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceAvcdecDecode, decoder, au, picture);
    const auto state = emuenv.kernel.obj_store.get<VideodecState>();
    const H264DecoderPtr &decoder_info = lock_and_find(decoder->handle, state->decoders, state->mutex);
    if (!decoder_info)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    H264DecoderOptions options = {};
    options.pts_upper = au->pts.upper;
    options.pts_lower = au->pts.lower;
    options.dts_upper = au->dts.upper;
    options.dts_lower = au->dts.lower;

    // This is quite long...
    SceAvcdecPicture *pPicture = picture->pPicture.get(emuenv.mem)[0].get(emuenv.mem);
    uint8_t *output = pPicture->frame.pPicture[0].cast<uint8_t>().get(emuenv.mem);

    if ((pPicture->frame.pixelType & (SCE_AVCDEC_PIXEL_YUV420_RASTER | SCE_AVCDEC_PIXEL_YUV420_PACKED_RASTER)) == 0) {
        LOG_ERROR_ONCE("Avcdec rgba output is not implemented");
        picture->numOfOutput++;
        return 0;
    }
    bool is_yuvp3 = static_cast<bool>(pPicture->frame.pixelType & SCE_AVCDEC_PIXEL_YUV420_RASTER);
    decoder_info->set_output_format(is_yuvp3);

    decoder_info->configure(&options);
    const auto send = decoder_info->send(au->es.pBuf.cast<uint8_t>().get(emuenv.mem), au->es.size);
    decoder_info->set_res(pPicture->frame.frameWidth, pPicture->frame.frameHeight);
    if (send && decoder_info->receive(output)) {
        decoder_info->get_res(pPicture->frame.horizontalSize, pPicture->frame.verticalSize);
        decoder_info->get_pts(pPicture->info.pts.upper, pPicture->info.pts.lower);
        picture->numOfOutput++;
    }

    decoder_info->is_stopped = false;

    return 0;
}

EXPORT(int, sceAvcdecDecodeAuInternal) {
    TRACY_FUNC(sceAvcdecDecodeAuInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeAuNalAuInternal) {
    TRACY_FUNC(sceAvcdecDecodeAuNalAuInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeAuNalAuNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeAuNalAuNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeAuNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeAuNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeAvailableSize, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceAvcdecDecodeAvailableSize, decoder);
    const auto state = emuenv.kernel.obj_store.get<VideodecState>();
    const H264DecoderPtr &decoder_info = lock_and_find(decoder->handle, state->decoders, state->mutex);
    if (!decoder_info)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    return H264DecoderState::buffer_size(
        { { decoder_info->get(DecoderQuery::WIDTH), decoder_info->get(DecoderQuery::HEIGHT) } });
}

EXPORT(int, sceAvcdecDecodeFlush, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceAvcdecDecodeFlush, decoder);
    const auto state = emuenv.kernel.obj_store.get<VideodecState>();
    const H264DecoderPtr &decoder_info = lock_and_find(decoder->handle, state->decoders, state->mutex);
    if (!decoder_info)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    decoder_info->flush();
    decoder_info->is_stopped = true;

    return 0;
}

EXPORT(int, sceAvcdecDecodeGetPictureInternal) {
    TRACY_FUNC(sceAvcdecDecodeGetPictureInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeGetPictureNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeGetPictureNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeGetPictureWithWorkPictureInternal) {
    TRACY_FUNC(sceAvcdecDecodeGetPictureWithWorkPictureInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeNalAu) {
    TRACY_FUNC(sceAvcdecDecodeNalAu);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeNalAuWithWorkPicture) {
    TRACY_FUNC(sceAvcdecDecodeNalAuWithWorkPicture);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeSetTrickModeNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeSetTrickModeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeSetUserDataSei1FieldMemSizeNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeSetUserDataSei1FieldMemSizeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeStop, SceAvcdecCtrl *decoder, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceAvcdecDecodeStop, decoder, picture);
    const auto state = emuenv.kernel.obj_store.get<VideodecState>();
    const H264DecoderPtr &decoder_info = lock_and_find(decoder->handle, state->decoders, state->mutex);
    if (!decoder_info)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    if (!decoder_info->is_stopped) {
        SceAvcdecPicture *pPicture = picture->pPicture.get(emuenv.mem)[0].get(emuenv.mem);

        // we get the values from the last frame, maybe we should slightly increase the pts value?
        decoder_info->get_res(pPicture->frame.horizontalSize, pPicture->frame.verticalSize);
        decoder_info->get_pts(pPicture->info.pts.upper, pPicture->info.pts.lower);

        picture->numOfOutput = 1;
    } else {
        picture->numOfOutput = 0;
    }
    decoder_info->is_stopped = true;

    return 0;
}

EXPORT(int, sceAvcdecDecodeStopWithWorkPicture) {
    TRACY_FUNC(sceAvcdecDecodeStopWithWorkPicture);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeWithWorkPicture) {
    TRACY_FUNC(sceAvcdecDecodeWithWorkPicture);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDeleteDecoder, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceAvcdecDeleteDecoder, decoder);
    const auto state = emuenv.kernel.obj_store.get<VideodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    state->decoders.erase(decoder->handle);

    return 0;
}

EXPORT(int, sceAvcdecGetSeiPictureTimingInternal) {
    TRACY_FUNC(sceAvcdecGetSeiPictureTimingInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecGetSeiUserDataNongameapp) {
    TRACY_FUNC(sceAvcdecGetSeiUserDataNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecQueryDecoderMemSize, uint32_t codec_type, const SceAvcdecQueryDecoderInfo *query_info, SceAvcdecDecoderInfo *decoder_info) {
    TRACY_FUNC(sceAvcdecQueryDecoderMemSize, codec_type, query_info, decoder_info);
    assert(codec_type == SCE_VIDEODEC_TYPE_HW_AVCDEC);

    decoder_info->frameMemSize = H264DecoderState::buffer_size({ { query_info->horizontal, query_info->vertical } }) * query_info->numOfRefFrames;

    return 0;
}

EXPORT(int, sceAvcdecQueryDecoderMemSizeInternal) {
    TRACY_FUNC(sceAvcdecQueryDecoderMemSizeInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecQueryDecoderMemSizeNongameapp) {
    TRACY_FUNC(sceAvcdecQueryDecoderMemSizeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecRegisterCallbackInternal) {
    TRACY_FUNC(sceAvcdecRegisterCallbackInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecRegisterCallbackNongameapp) {
    TRACY_FUNC(sceAvcdecRegisterCallbackNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecSetDecodeMode) {
    TRACY_FUNC(sceAvcdecSetDecodeMode);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecSetDecodeModeInternal) {
    TRACY_FUNC(sceAvcdecSetDecodeModeInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecSetInterlacedStreamMode) {
    TRACY_FUNC(sceAvcdecSetInterlacedStreamMode);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecSetLowDelayModeNongameapp) {
    TRACY_FUNC(sceAvcdecSetLowDelayModeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecSetRecoveryPointSEIMode) {
    TRACY_FUNC(sceAvcdecSetRecoveryPointSEIMode);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecUnregisterCallbackInternal) {
    TRACY_FUNC(sceAvcdecUnregisterCallbackInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecUnregisterCallbackNongameapp) {
    TRACY_FUNC(sceAvcdecUnregisterCallbackNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecUnregisterCallbackWithCbidInternal) {
    TRACY_FUNC(sceAvcdecUnregisterCallbackWithCbidInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecUnregisterCallbackWithCbidNongameapp) {
    TRACY_FUNC(sceAvcdecUnregisterCallbackWithCbidNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecCreateDecoder) {
    TRACY_FUNC(sceM4vdecCreateDecoder);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecCreateDecoderInternal) {
    TRACY_FUNC(sceM4vdecCreateDecoderInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecCsc) {
    TRACY_FUNC(sceM4vdecCsc);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecDecode) {
    TRACY_FUNC(sceM4vdecDecode);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecDecodeAvailableSize) {
    TRACY_FUNC(sceM4vdecDecodeAvailableSize);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecDecodeFlush) {
    TRACY_FUNC(sceM4vdecDecodeFlush);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecDecodeStop) {
    TRACY_FUNC(sceM4vdecDecodeStop);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecDecodeStopWithWorkPicture) {
    TRACY_FUNC(sceM4vdecDecodeStopWithWorkPicture);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecDecodeWithWorkPicture) {
    TRACY_FUNC(sceM4vdecDecodeWithWorkPicture);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecDeleteDecoder) {
    TRACY_FUNC(sceM4vdecDeleteDecoder);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecQueryDecoderMemSize) {
    TRACY_FUNC(sceM4vdecQueryDecoderMemSize);
    STUBBED("fake size");
    return 53;
}

EXPORT(int, sceM4vdecQueryDecoderMemSizeInternal) {
    TRACY_FUNC(sceM4vdecQueryDecoderMemSizeInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecInitLibrary) {
    TRACY_FUNC(sceVideodecInitLibrary);
    emuenv.kernel.obj_store.create<VideodecState>();
    return 0;
}

EXPORT(int, sceVideodecInitLibraryInternal) {
    TRACY_FUNC(sceVideodecInitLibraryInternal);
    emuenv.kernel.obj_store.create<VideodecState>();
    return 0;
}

EXPORT(int, sceVideodecInitLibraryNongameapp) {
    TRACY_FUNC(sceVideodecInitLibraryNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecInitLibraryWithUnmapMem) {
    TRACY_FUNC(sceVideodecInitLibraryWithUnmapMem);
    emuenv.kernel.obj_store.create<VideodecState>();
    return 0;
}

EXPORT(int, sceVideodecInitLibraryWithUnmapMemInternal) {
    TRACY_FUNC(sceVideodecInitLibraryWithUnmapMemInternal);
    emuenv.kernel.obj_store.create<VideodecState>();
    return 0;
}

EXPORT(int, sceVideodecInitLibraryWithUnmapMemNongameapp) {
    TRACY_FUNC(sceVideodecInitLibraryWithUnmapMemNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecQueryInstanceNongameapp) {
    TRACY_FUNC(sceVideodecQueryInstanceNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecQueryMemSize) {
    TRACY_FUNC(sceVideodecQueryMemSize);
    STUBBED("fake size");
    return 53;
}

EXPORT(int, sceVideodecQueryMemSizeInternal) {
    TRACY_FUNC(sceVideodecQueryMemSizeInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecQueryMemSizeNongameapp) {
    TRACY_FUNC(sceVideodecQueryMemSizeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecSetConfig) {
    TRACY_FUNC(sceVideodecSetConfig);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecSetConfigInternal) {
    TRACY_FUNC(sceVideodecSetConfigInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecTermLibrary) {
    TRACY_FUNC(sceVideodecTermLibrary);
    emuenv.kernel.obj_store.erase<VideodecState>();
    return 0;
}
