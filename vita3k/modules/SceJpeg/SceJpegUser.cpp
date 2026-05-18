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
#include <codec/types.h>
#include <kernel/state.h>
#include <modules/SceJpeg/quick_state.h>

#include <fmt/format.h>
#include <limits>
#include <map>
#include <sstream>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceJpegUser);

typedef std::shared_ptr<MjpegDecoderState> DecoderPtr;

struct MJpegState {
    bool initialized = false;
    std::mutex decoderMutex;
    DecoderPtr decoder;
};

namespace sce_jpeg {

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

static bool quick_state_parse_i32_text(const std::string &text, int32_t &out) {
    if (text.empty())
        return false;

    size_t consumed = 0;
    int64_t parsed = 0;
    try {
        parsed = std::stoll(text, &consumed, 10);
    } catch (...) {
        return false;
    }
    if (consumed != text.size() || parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max())
        return false;
    out = static_cast<int32_t>(parsed);
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

static std::string quick_state_pitch_text(const MJpegPitch &pitch) {
    return fmt::format("{},{}", pitch.x, pitch.y);
}

static bool quick_state_parse_pitch_text(const std::string &text, MJpegPitch &pitch) {
    const size_t separator = text.find(',');
    if (separator == std::string::npos)
        return false;

    uint32_t x = 0;
    uint32_t y = 0;
    if (!quick_state_parse_u32_text(text.substr(0, separator), x)
        || !quick_state_parse_u32_text(text.substr(separator + 1), y)) {
        return false;
    }

    pitch = { x, y };
    return true;
}

static bool quick_state_valid_color_space(const uint32_t color_space) {
    switch (static_cast<DecoderColorSpace>(color_space)) {
    case COLORSPACE_UNKNOWN:
    case COLORSPACE_GRAYSCALE:
    case COLORSPACE_YUV444P:
    case COLORSPACE_YUV440P:
    case COLORSPACE_YUV441P:
    case COLORSPACE_YUV422P:
    case COLORSPACE_YUV420P:
    case COLORSPACE_YUV411P:
        return true;
    default:
        return false;
    }
}

std::string quick_state_snapshot_text(EmuEnvState &emuenv) {
    std::ostringstream text;
    text << "schema=thor.jpeg.v1\n";

    MJpegState *state = emuenv.kernel.obj_store.get_if<MJpegState>();
    if (!state) {
        text << "initialized=0\n";
        text << "decoder=0\n";
        text << "exact=1\n";
        return text.str();
    }

    std::lock_guard<std::mutex> lock(state->decoderMutex);
    const bool decoder_present = static_cast<bool>(state->decoder);
    const bool initialized = state->initialized || decoder_present;
    text << "initialized=" << (initialized ? 1 : 0) << "\n";
    text << "decoder=" << (decoder_present ? 1 : 0) << "\n";
    text << "exact=" << ((initialized == decoder_present) ? 1 : 0) << "\n";

    if (state->decoder) {
        MJpegPitch pitch[4] = {};
        state->decoder->get_pitch_info(pitch);
        text << "use_standard_decoder=" << (state->decoder->use_standard_decoder ? 1 : 0) << "\n";
        text << "downscale_ratio=" << state->decoder->downscale_ratio << "\n";
        text << "color_space=" << static_cast<uint32_t>(state->decoder->get_color_space()) << "\n";
        for (size_t index = 0; index < 4; index++)
            text << "pitch." << index << "=" << quick_state_pitch_text(pitch[index]) << "\n";
    }

    return text.str();
}

bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, bool *initialized, bool *decoder_present, bool *exact_restore, std::string *detail) {
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.jpeg.v1") {
        if (detail)
            *detail = "Jpeg section schema is invalid";
        return false;
    }

    bool parsed_initialized = false;
    bool parsed_decoder = false;
    bool parsed_exact = false;
    if (!values.contains("initialized") || !quick_state_parse_bool_text(values.at("initialized"), parsed_initialized)
        || !values.contains("decoder") || !quick_state_parse_bool_text(values.at("decoder"), parsed_decoder)
        || !values.contains("exact") || !quick_state_parse_bool_text(values.at("exact"), parsed_exact)) {
        if (detail)
            *detail = "Jpeg section header is invalid";
        return false;
    }

    if (parsed_initialized != parsed_decoder) {
        if (detail)
            *detail = "Jpeg initialized state does not match decoder presence";
        return false;
    }

    if (parsed_decoder) {
        static constexpr const char *required_fields[] = {
            "use_standard_decoder",
            "downscale_ratio",
            "color_space",
            "pitch.0",
            "pitch.1",
            "pitch.2",
            "pitch.3",
        };
        for (const char *field : required_fields) {
            if (!values.contains(field)) {
                if (detail)
                    *detail = std::string("Jpeg decoder field is missing: ") + field;
                return false;
            }
        }

        bool parsed_bool = false;
        int32_t parsed_i32 = 0;
        uint32_t parsed_u32 = 0;
        MJpegPitch parsed_pitch = {};
        if (!quick_state_parse_bool_text(values.at("use_standard_decoder"), parsed_bool)
            || !quick_state_parse_i32_text(values.at("downscale_ratio"), parsed_i32)
            || parsed_i32 < 0
            || parsed_i32 > 8
            || !quick_state_parse_u32_text(values.at("color_space"), parsed_u32)
            || !quick_state_valid_color_space(parsed_u32)
            || !quick_state_parse_pitch_text(values.at("pitch.0"), parsed_pitch)
            || !quick_state_parse_pitch_text(values.at("pitch.1"), parsed_pitch)
            || !quick_state_parse_pitch_text(values.at("pitch.2"), parsed_pitch)
            || !quick_state_parse_pitch_text(values.at("pitch.3"), parsed_pitch)) {
            if (detail)
                *detail = "Jpeg decoder metadata is invalid";
            return false;
        }
    }

    if (initialized)
        *initialized = parsed_initialized;
    if (decoder_present)
        *decoder_present = parsed_decoder;
    if (exact_restore)
        *exact_restore = parsed_exact && (!parsed_initialized || parsed_decoder);
    return true;
}

bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail) {
    bool initialized = false;
    bool exact_restore = false;
    if (!quick_state_validate_snapshot_values(values, &initialized, nullptr, &exact_restore, detail))
        return false;

    if (!initialized) {
        emuenv.kernel.obj_store.erase<MJpegState>();
        return true;
    }

    MJpegState *state = emuenv.kernel.obj_store.get_if<MJpegState>();
    if (!state) {
        emuenv.kernel.obj_store.create<MJpegState>();
        state = emuenv.kernel.obj_store.get_if<MJpegState>();
    }
    if (!state) {
        if (detail)
            *detail = "Jpeg state object could not be created";
        return false;
    }

    bool use_standard_decoder = false;
    int32_t downscale_ratio = 1;
    uint32_t color_space = 0;
    MJpegPitch pitch[4] = {};
    if (!quick_state_parse_bool_text(values.at("use_standard_decoder"), use_standard_decoder)
        || !quick_state_parse_i32_text(values.at("downscale_ratio"), downscale_ratio)
        || !quick_state_parse_u32_text(values.at("color_space"), color_space)) {
        if (detail)
            *detail = "Jpeg decoder restore fields are invalid";
        return false;
    }
    for (size_t index = 0; index < 4; index++) {
        if (!quick_state_parse_pitch_text(values.at(fmt::format("pitch.{}", index)), pitch[index])) {
            if (detail)
                *detail = fmt::format("Jpeg pitch {} restore field is invalid", index);
            return false;
        }
    }

    auto decoder = std::make_shared<MjpegDecoderState>();
    MJpegDecoderOptions options = {};
    options.use_standard_decoder = use_standard_decoder;
    options.downscale_ratio = downscale_ratio;
    decoder->configure(&options);
    for (size_t index = 0; index < 4; index++)
        decoder->pitch[index] = pitch[index];
    decoder->color_space_out = static_cast<DecoderColorSpace>(color_space);

    {
        std::lock_guard<std::mutex> lock(state->decoderMutex);
        state->initialized = true;
        state->decoder = std::move(decoder);
    }
    return exact_restore;
}

} // namespace sce_jpeg

struct SceJpegMJpegInitInfo {
    SceSize size;
    int maxSplitDecoder;
    int option;
};

typedef MJpegPitch SceJpegPitch;

enum SceJpegColorSpace : int {
    SCE_JPEG_COLORSPACE_UNKNOWN = 0x00000,
    SCE_JPEG_COLORSPACE_GRAYSCALE = 0x10101,
    SCE_JPEG_COLORSPACE_YUV = 0x20000,
    SCE_JPEG_COLORSPACE_YUV444 = 0x20101,
    SCE_JPEG_COLORSPACE_YUV440 = 0x20102,
    SCE_JPEG_COLORSPACE_YUV441 = 0x20104,
    SCE_JPEG_COLORSPACE_YUV422 = 0x20201,
    SCE_JPEG_COLORSPACE_YUV420 = 0x20202,
    SCE_JPEG_COLORSPACE_YUV411 = 0x20401,
};
BOOST_DESCRIBE_ENUM(SceJpegColorSpace, SCE_JPEG_COLORSPACE_UNKNOWN, SCE_JPEG_COLORSPACE_GRAYSCALE, SCE_JPEG_COLORSPACE_YUV, SCE_JPEG_COLORSPACE_YUV444, SCE_JPEG_COLORSPACE_YUV440, SCE_JPEG_COLORSPACE_YUV441, SCE_JPEG_COLORSPACE_YUV422, SCE_JPEG_COLORSPACE_YUV420, SCE_JPEG_COLORSPACE_YUV411)

enum SceJpegDHTMode : int {
    SCE_JPEG_MJPEG_WITH_DHT,
    SCE_JPEG_MJPEG_WITHOUT_DHT,
    SCE_JPEG_MJPEG_ANY_SAMPLING_WITHOUT_DHT,
    SCE_JPEG_MJPEG_ANY_SAMPLING
};
BOOST_DESCRIBE_ENUM(SceJpegDHTMode, SCE_JPEG_MJPEG_WITH_DHT, SCE_JPEG_MJPEG_WITHOUT_DHT, SCE_JPEG_MJPEG_ANY_SAMPLING_WITHOUT_DHT, SCE_JPEG_MJPEG_ANY_SAMPLING)

enum SceJpegDownscaleMode : int {
    SCE_JPEG_MJPEG_DOWNSCALE_1_2 = 1 << 4,
    SCE_JPEG_MJPEG_DOWNSCALE_1_4 = 1 << 5,
    SCE_JPEG_MJPEG_DOWNSCALE_1_8 = 1 << 6,
    SCE_JPEG_MJPEG_DOWNSCALE_ANY = 0b111 << 4
};
BOOST_DESCRIBE_ENUM(SceJpegDownscaleMode, SCE_JPEG_MJPEG_DOWNSCALE_1_2, SCE_JPEG_MJPEG_DOWNSCALE_1_4, SCE_JPEG_MJPEG_DOWNSCALE_1_8, SCE_JPEG_MJPEG_DOWNSCALE_ANY)

enum SceJpegFormat : int {
    // YUV format
    SCE_JPEG_NO_CSC_OUTPUT = -1,
    SCE_JPEG_PIXEL_RGBA8888 = 0,
    SCE_JPEG_PIXEL_BGRA8888 = 4
};
BOOST_DESCRIBE_ENUM(SceJpegFormat, SCE_JPEG_NO_CSC_OUTPUT, SCE_JPEG_PIXEL_RGBA8888, SCE_JPEG_PIXEL_BGRA8888)

enum SceJpegColorConversion : int {
    SCE_JPEG_COLORSPACE_JFIF = 0,
    SCE_JPEG_COLORSPACE_BT601 = 0x10
};
BOOST_DESCRIBE_ENUM(SceJpegColorConversion, SCE_JPEG_COLORSPACE_JFIF, SCE_JPEG_COLORSPACE_BT601)

struct SceJpegOutputInfo {
    SceJpegColorSpace color_space;
    uint16_t width;
    uint16_t height;
    uint32_t output_size;
    uint32_t temp_buffer_size;
    uint32_t coef_buffer_size;
    SceJpegPitch pitch[4];
};

static SceJpegColorSpace convert_color_space_decoder_to_jpeg(DecoderColorSpace color_space) {
    switch (color_space) {
    case COLORSPACE_GRAYSCALE:
        return SCE_JPEG_COLORSPACE_GRAYSCALE;
    case COLORSPACE_YUV444P:
        return SCE_JPEG_COLORSPACE_YUV444;
    case COLORSPACE_YUV440P:
        return SCE_JPEG_COLORSPACE_YUV440;
    case COLORSPACE_YUV441P:
        return SCE_JPEG_COLORSPACE_YUV441;
    case COLORSPACE_YUV422P:
        return SCE_JPEG_COLORSPACE_YUV422;
    case COLORSPACE_YUV420P:
        return SCE_JPEG_COLORSPACE_YUV420;
    case COLORSPACE_YUV411P:
        return SCE_JPEG_COLORSPACE_YUV411;
    default:
        return SCE_JPEG_COLORSPACE_UNKNOWN;
    }
}

static DecoderColorSpace convert_color_space_jpeg_to_decoder(SceJpegColorSpace color_space) {
    switch (color_space) {
    case SCE_JPEG_COLORSPACE_GRAYSCALE:
        return COLORSPACE_GRAYSCALE;
    case SCE_JPEG_COLORSPACE_YUV444:
        return COLORSPACE_YUV444P;
    case SCE_JPEG_COLORSPACE_YUV440:
        return COLORSPACE_YUV440P;
    case SCE_JPEG_COLORSPACE_YUV441:
        return COLORSPACE_YUV441P;
    case SCE_JPEG_COLORSPACE_YUV422:
        return COLORSPACE_YUV422P;
    case SCE_JPEG_COLORSPACE_YUV420:
        return COLORSPACE_YUV420P;
    case SCE_JPEG_COLORSPACE_YUV411:
        return COLORSPACE_YUV411P;
    default:
        return COLORSPACE_UNKNOWN;
    }
}

// Helper functions
static SceJpegDHTMode get_DHT_mode(int decodeMode) {
    return static_cast<SceJpegDHTMode>(decodeMode & 0b111);
}

static SceJpegDownscaleMode get_downscale_mode(int decodeMode) {
    return static_cast<SceJpegDownscaleMode>(decodeMode & (0b111 << 4));
}

static int get_downscale_ratio(SceJpegDownscaleMode downscaleMode) {
    return downscaleMode != 0 ? downscaleMode / 8 : 1;
}

static bool is_standard_decoding(SceJpegDHTMode dhtMode) {
    return dhtMode == SCE_JPEG_MJPEG_WITH_DHT || dhtMode == SCE_JPEG_MJPEG_WITHOUT_DHT;
}

static bool is_unsupported_image_size(uint32_t width, uint32_t height) {
    /* Note: SCE_JPEG_ERROR_UNSUPPORT_IMAGE_SIZE is determined by the size of the decoded result,
     * not the actual image dimensions. It checks the pitch width and height, not the image's width and height.
     * If downscaling brings the image size within the supported range, this error won't occur.
     * Conversely, if downscaling results in dimensions outside the supported range, this error will be triggered.
     */
    return width < 64 || height < 64 || width > 2032 || height > 1088;
}

// Common decoder configuration
static void configure_decoder(MJpegState *state, int decodeMode) {
    SceJpegDHTMode dhtMode = get_DHT_mode(decodeMode);
    SceJpegDownscaleMode downscaleMode = get_downscale_mode(decodeMode);

    MJpegDecoderOptions options = {};
    options.use_standard_decoder = is_standard_decoding(dhtMode);
    options.downscale_ratio = get_downscale_ratio(downscaleMode);

    state->decoder->configure(&options);
}

EXPORT(int, sceJpegCreateSplitDecoder) {
    TRACY_FUNC(sceJpegCreateSplitDecoder);
    return UNIMPLEMENTED();
}

EXPORT(int, sceJpegCsc, uint8_t *pRGBA, const uint8_t *pYCbCr,
    uint32_t xysize, int iFrameWidth, int colorOption, int sampling) {
    TRACY_FUNC(sceJpegCsc);

    if (colorOption & SCE_JPEG_COLORSPACE_BT601) {
        STUBBED("Unhandled BT601 color conversion");
        colorOption &= ~SCE_JPEG_COLORSPACE_BT601;
    }

    if (colorOption != SCE_JPEG_PIXEL_RGBA8888)
        // sceJpegCsc only supports RGBA8888
        return RET_ERROR(SCE_JPEG_ERROR_INVALID_COLOR_FORMAT);

    uint32_t width = xysize >> 16;
    uint32_t height = xysize & 0xFFFF;

    SceJpegColorSpace sceColorSpace = static_cast<SceJpegColorSpace>(SCE_JPEG_COLORSPACE_YUV | sampling);
    if (sceColorSpace != SCE_JPEG_COLORSPACE_GRAYSCALE && sceColorSpace != SCE_JPEG_COLORSPACE_YUV444) {
        // sceJpegCsc only supports YUV400 and YUV444
        return RET_ERROR(SCE_JPEG_ERROR_INVALID_COLOR_FORMAT);
    }

    SceJpegPitch yuv_pitch[4];
    auto colorSpace = convert_color_space_jpeg_to_decoder(static_cast<SceJpegColorSpace>(SCE_JPEG_COLORSPACE_YUV | sampling));

    calculate_pitch_info(width, height, 0, colorSpace, false, yuv_pitch);
    convert_yuv_to_rgb(pYCbCr, pRGBA, iFrameWidth, colorSpace, false, yuv_pitch);

    return 0;
}

EXPORT(int, sceJpegDecodeMJpeg, const unsigned char *pJpeg, SceSize isize, uint8_t *pRGBA, SceSize osize,
    int decodeMode, void *pTempBuffer, SceSize tempBufferSize, void *pCoefBuffer, SceSize coefBufferSize) {
    TRACY_FUNC(sceJpegDecodeMJpeg, pJpeg, isize, pRGBA, osize, decodeMode, pTempBuffer, tempBufferSize, pCoefBuffer, coefBufferSize);

    const auto state = emuenv.kernel.obj_store.get<MJpegState>();
    std::lock_guard<std::mutex> guard(state->decoderMutex);
    configure_decoder(state, decodeMode);

    // the yuv data will always be smaller than the rgba data, so osize is an upper bound
    std::vector<uint8_t> temporary(osize);
    DecoderSize size = {};

    state->decoder->send(pJpeg, isize);
    state->decoder->receive(temporary.data(), &size);

    SceJpegPitch yuv_pitch[4];
    state->decoder->get_pitch_info(yuv_pitch);

    if (is_unsupported_image_size(yuv_pitch[0].x, yuv_pitch[0].y)) {
        return RET_ERROR(SCE_JPEG_ERROR_UNSUPPORT_IMAGE_SIZE);
    }

    convert_yuv_to_rgb(temporary.data(), pRGBA, yuv_pitch[0].x, state->decoder->get_color_space(), false, yuv_pitch);

    // Top 16 bits = pitch_width, bottom 16 bits = pitch_height.
    return (yuv_pitch[0].x << 16u) | yuv_pitch[0].y;
}

EXPORT(int, sceJpegDecodeMJpegYCbCr, const uint8_t *pJpeg, SceSize isize,
    uint8_t *pYCbCr, SceSize osize, int decodeMode, void *pCoefBuffer, SceSize coefBufferSize) {
    TRACY_FUNC(sceJpegDecodeMJpegYCbCr, pJpeg, isize, pYCbCr, osize, decodeMode, pCoefBuffer, coefBufferSize);

    const auto state = emuenv.kernel.obj_store.get<MJpegState>();
    std::lock_guard<std::mutex> guard(state->decoderMutex);
    configure_decoder(state, decodeMode);

    DecoderSize size = {};

    state->decoder->send(pJpeg, isize);
    state->decoder->receive(pYCbCr, &size);

    SceJpegPitch yuv_pitch[4];
    state->decoder->get_pitch_info(yuv_pitch);

    // Top 16 bits = pitch_width, bottom 16 bits = pitch_height.
    return (yuv_pitch[0].x << 16u) | yuv_pitch[0].y;
}

EXPORT(int, sceJpegDeleteSplitDecoder) {
    TRACY_FUNC(sceJpegDeleteSplitDecoder);
    return UNIMPLEMENTED();
}

EXPORT(int, sceJpegFinishMJpeg) {
    TRACY_FUNC(sceJpegFinishMJpeg);
    emuenv.kernel.obj_store.erase<MJpegState>();

    return 0;
}

EXPORT(int, sceJpegGetOutputInfo, const uint8_t *pJpeg, SceSize isize,
    SceJpegFormat format, int decodeMode, SceJpegOutputInfo *output) {
    /* Note: This implementation matches the basic functionality of PS Vita's sceJpegGetOutputInfo,
     * but with the following limitations:
     * 1. YUV441 is currently not supported due to FFmpeg limitations. While no games are known to use
     *    YUV441, improvements may be necessary if such games are found.
     * 2. JPEG files with non-standard MCUs will be processed without errors in Vita3K, whereas they
     *    would cause decoding errors on the PS Vita.
     * 3. The calculation of coefBufferSize is not supported.
     */
    TRACY_FUNC(sceJpegGetOutputInfo, pJpeg, isize, format, decodeMode, output);

    if (!pJpeg || !output || !isize)
        return RET_ERROR(SCE_JPEG_ERROR_INVALID_POINTER);

    if (format != SCE_JPEG_NO_CSC_OUTPUT && format != SCE_JPEG_PIXEL_RGBA8888 && format != SCE_JPEG_PIXEL_BGRA8888)
        return RET_ERROR(SCE_JPEG_ERROR_INVALID_COLOR_FORMAT);

    const auto state = emuenv.kernel.obj_store.get<MJpegState>();
    std::lock_guard<std::mutex> guard(state->decoderMutex);
    configure_decoder(state, decodeMode);

    DecoderSize size = {};

    state->decoder->send(pJpeg, isize);
    state->decoder->receive(nullptr, &size);

    memset(output, 0, sizeof(SceJpegOutputInfo));
    output->color_space = convert_color_space_decoder_to_jpeg(state->decoder->get_color_space());

    SceJpegDHTMode dhtMode = get_DHT_mode(decodeMode);
    bool isStandardDecodingMode = is_standard_decoding(dhtMode);

    // Check for unsupported color spaces in standard decoding
    if (isStandardDecodingMode && output->color_space == SCE_JPEG_COLORSPACE_GRAYSCALE) {
        return RET_ERROR(SCE_JPEG_ERROR_UNSUPPORT_COLORSPACE);
    }

    if (isStandardDecodingMode && (output->color_space != SCE_JPEG_COLORSPACE_YUV420 && output->color_space != SCE_JPEG_COLORSPACE_YUV422)) {
        return RET_ERROR(SCE_JPEG_ERROR_UNSUPPORT_SAMPLING);
    }

    // Check for unsupported color conversion in extended decoding
    if (!isStandardDecodingMode && format != SCE_JPEG_NO_CSC_OUTPUT) {
        return RET_ERROR(SCE_JPEG_ERROR_UNSUPPORT_DOWNSCALE);
    }

    // Fill basic output information
    output->width = size.width;
    output->height = size.height;
    // Should be 0 most of the time but I believe it causes more problems
    // for it to be 0 when it shouldn't than the opposite
    output->coef_buffer_size = 0x100;

    state->decoder->get_pitch_info(output->pitch);

    int totalYuvSize = 0;

    // Calculate total YUV buffer size
    for (int i = 0; i < 4; i++) {
        totalYuvSize += output->pitch[i].x * output->pitch[i].y;
    }

    // Adjust output information based on format
    if (format != SCE_JPEG_NO_CSC_OUTPUT) {
        // Adjust for RGBA or BGRA format
        output->pitch[0].x *= 4;
        output->pitch[1] = output->pitch[2] = output->pitch[3] = { 0, 0 };

        output->temp_buffer_size = align(totalYuvSize, 256);
        output->output_size = align(output->pitch[0].x * output->pitch[0].y, 256);
    } else {
        // No color space conversion
        output->output_size = align(totalYuvSize, 256);
    }

    return 0;
}

EXPORT(int, sceJpegInitMJpeg, int maxSplitDecoder) {
    TRACY_FUNC(sceJpegInitMJpeg, maxSplitDecoder);
    if (maxSplitDecoder > 0)
        STUBBED("Ignoring non-zero maxSplitDecoder parameter");

    emuenv.kernel.obj_store.create<MJpegState>();
    const auto state = emuenv.kernel.obj_store.get<MJpegState>();
    state->initialized = true;
    state->decoder = std::make_shared<MjpegDecoderState>();

    return 0;
}

EXPORT(int, sceJpegInitMJpegWithParam, const SceJpegMJpegInitInfo *param) {
    TRACY_FUNC(sceJpegInitMJpegWithParam, param);
    return CALL_EXPORT(sceJpegInitMJpeg, param->maxSplitDecoder);
}

EXPORT(int, sceJpegMJpegCsc, uint8_t *pRGBA, const uint8_t *pYCbCr,
    uint32_t xysize, int iFrameWidth, int colorOption, int sampling) {
    TRACY_FUNC(sceJpegMJpegCsc, pRGBA, pYCbCr, xysize, iFrameWidth, colorOption, sampling);

    if (colorOption & SCE_JPEG_COLORSPACE_BT601) {
        STUBBED("Unhandled BT601 color conversion");
        colorOption &= ~SCE_JPEG_COLORSPACE_BT601;
    }

    if (colorOption != SCE_JPEG_PIXEL_RGBA8888 && colorOption != SCE_JPEG_PIXEL_BGRA8888)
        return RET_ERROR(SCE_JPEG_ERROR_INVALID_COLOR_FORMAT);

    uint32_t width = xysize >> 16;
    uint32_t height = xysize & 0xFFFF;

    if (is_unsupported_image_size(width, height)) {
        return RET_ERROR(SCE_JPEG_ERROR_UNSUPPORT_IMAGE_SIZE);
    }

    SceJpegPitch yuv_pitch[4];
    auto colorSpace = convert_color_space_jpeg_to_decoder(static_cast<SceJpegColorSpace>(SCE_JPEG_COLORSPACE_YUV | sampling));

    calculate_pitch_info(width, height, 0, colorSpace, true, yuv_pitch);
    convert_yuv_to_rgb(pYCbCr, pRGBA, iFrameWidth, colorSpace, colorOption == SCE_JPEG_PIXEL_BGRA8888, yuv_pitch);

    return 0;
}

EXPORT(int, sceJpegSplitDecodeMJpeg) {
    TRACY_FUNC(sceJpegSplitDecodeMJpeg);
    return UNIMPLEMENTED();
}
