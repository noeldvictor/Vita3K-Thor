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

#include "SceAudiodecUser.h"

#include <audio/state.h>
#include <codec/state.h>
#include <kernel/state.h>
#include <modules/SceAudiodec/quick_state.h>
#include <util/lock_and_find.h>
#include <util/tracy.h>

#include <cstring>
#include <fmt/format.h>
#include <iomanip>
#include <limits>
#include <sstream>

TRACY_MODULE_NAME(SceAudiodecUser);

enum {
    SCE_AUDIODEC_ERROR_API_FAIL = 0x807F0000,
    SCE_AUDIODEC_ERROR_NOT_INITIALIZED = 0x807F0005,
    SCE_AUDIODEC_ERROR_INVALID_HANDLE = 0x807F0009,
    SCE_AUDIODEC_ERROR_NOT_HANDLE_IN_USE = 0x807F000A,
    SCE_AUDIODEC_MP3_ERROR_INVALID_MPEG_VERSION = 0x807F2801,
};

enum {
    SCE_AUDIODEC_MP3_MPEG_VERSION_2_5,
    SCE_AUDIODEC_MP3_MPEG_VERSION_RESERVED,
    SCE_AUDIODEC_MP3_MPEG_VERSION_2,
    SCE_AUDIODEC_MP3_MPEG_VERSION_1,
};

typedef std::shared_ptr<DecoderState> DecoderPtr;
typedef std::map<SceUID, DecoderPtr> DecoderStates;
typedef std::set<SceUID> CodecDecoders;
typedef std::map<SceAudiodecCodec, CodecDecoders> CodecDecodersMap;

struct AudiodecState {
    std::mutex mutex;
    DecoderStates decoders;
    CodecDecodersMap codecs;
};

struct SceAudiodecInfoAt9 {
    uint32_t config_data;
    uint32_t channels;
    uint32_t bit_rate;
    uint32_t sample_rate;
    uint32_t super_frame_size;
    uint32_t frames_in_super_frame;
};

struct SceAudiodecInfoMp3 {
    uint32_t channels;
    uint32_t version;
};

struct SceAudiodecInfoAac {
    uint32_t is_adts;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t is_sbr;
};

struct SceAudiodecInfoCelp {
    uint32_t excitation_mode;
    uint32_t sample_rate;
    uint32_t bit_rate;
    uint32_t lost_count;
};

struct SceAudiodecInfo {
    uint32_t size;
    union {
        SceAudiodecInfoAt9 at9;
        SceAudiodecInfoMp3 mp3;
        SceAudiodecInfoAac aac;
        SceAudiodecInfoCelp celp;
    };
};

struct SceAudiodecCtrl {
    uint32_t size;
    SceUID handle;
    Ptr<uint8_t> es_data;
    uint32_t es_size_used;
    uint32_t es_size_max;
    Ptr<uint8_t> pcm_data;
    uint32_t pcm_size_given;
    uint32_t pcm_size_max;
    uint32_t word_length;
    Ptr<SceAudiodecInfo> info;
};

namespace sce_audiodec {

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

static bool quick_state_parse_codec_text(const std::string &text, SceAudiodecCodec &codec) {
    uint32_t parsed = 0;
    if (!quick_state_parse_u32_text(text, parsed))
        return false;

    switch (static_cast<SceAudiodecCodec>(parsed)) {
    case SCE_AUDIODEC_TYPE_AT9:
    case SCE_AUDIODEC_TYPE_MP3:
    case SCE_AUDIODEC_TYPE_AAC:
    case SCE_AUDIODEC_TYPE_CELP:
        codec = static_cast<SceAudiodecCodec>(parsed);
        return true;
    default:
        return false;
    }
}

static const char *quick_state_decoder_kind(const DecoderPtr &decoder) {
    if (std::dynamic_pointer_cast<Atrac9DecoderState>(decoder))
        return "at9";
    if (std::dynamic_pointer_cast<Mp3DecoderState>(decoder))
        return "mp3";
    if (std::dynamic_pointer_cast<AacDecoderState>(decoder))
        return "aac";
    return "unknown";
}

static bool quick_state_decoder_exact(const DecoderPtr &decoder) {
    return std::dynamic_pointer_cast<Atrac9DecoderState>(decoder)
        || std::dynamic_pointer_cast<Mp3DecoderState>(decoder)
        || std::dynamic_pointer_cast<AacDecoderState>(decoder);
}

static bool quick_state_codec_for_handle(const AudiodecState &state, const SceUID handle, SceAudiodecCodec &codec) {
    for (const auto &[candidate, handles] : state.codecs) {
        if (handles.contains(handle)) {
            codec = candidate;
            return true;
        }
    }
    return false;
}

static std::string quick_state_hex_bytes(const void *data, const size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; i++)
        text << std::setw(2) << static_cast<unsigned>(bytes[i]);
    return text.str();
}

static bool quick_state_unhex_bytes(const std::string &text, std::vector<uint8_t> &out) {
    if ((text.size() % 2) != 0)
        return false;

    auto hex_value = [](const char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    out.clear();
    out.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const int high = hex_value(text[i]);
        const int low = hex_value(text[i + 1]);
        if (high < 0 || low < 0)
            return false;
        out.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return true;
}

static bool quick_state_decoder_fields_valid(const std::map<std::string, std::string> &fields, std::string *detail) {
    static constexpr const char *required_fields[] = {
        "handle",
        "codec",
        "kind",
        "exact",
    };
    for (const char *field : required_fields) {
        if (!fields.contains(field)) {
            if (detail)
                *detail = std::string("Audiodec decoder field is missing: ") + field;
            return false;
        }
    }

    uint32_t parsed_u32 = 0;
    SceAudiodecCodec codec = SCE_AUDIODEC_TYPE_AT9;
    bool parsed_bool = false;
    if (!quick_state_parse_u32_text(fields.at("handle"), parsed_u32) || parsed_u32 == 0
        || !quick_state_parse_codec_text(fields.at("codec"), codec)
        || !quick_state_parse_bool_text(fields.at("exact"), parsed_bool)) {
        if (detail)
            *detail = "Audiodec decoder metadata is invalid";
        return false;
    }

    if (fields.at("kind") == "at9") {
        static constexpr const char *at9_fields[] = {
            "config_data",
            "es_size_used",
            "superframe_frame_idx",
            "superframe_data_left",
            "saved",
        };
        for (const char *field : at9_fields) {
            if (!fields.contains(field)) {
                if (detail)
                    *detail = std::string("Audiodec AT9 decoder field is missing: ") + field;
                return false;
            }
        }

        std::vector<uint8_t> saved_state;
        if (!quick_state_parse_u32_text(fields.at("config_data"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("es_size_used"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("superframe_frame_idx"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("superframe_data_left"), parsed_u32)
            || !quick_state_unhex_bytes(fields.at("saved"), saved_state)
            || saved_state.size() != sizeof(Atrac9DecoderSavedState)) {
            if (detail)
                *detail = "Audiodec AT9 decoder metadata is invalid";
            return false;
        }
    } else if (fields.at("kind") == "mp3") {
        if (!fields.contains("channels") || !fields.contains("es_size_used")
            || !quick_state_parse_u32_text(fields.at("channels"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("es_size_used"), parsed_u32)) {
            if (detail)
                *detail = "Audiodec MP3 decoder metadata is invalid";
            return false;
        }
    } else if (fields.at("kind") == "aac") {
        if (!fields.contains("channels") || !fields.contains("sample_rate") || !fields.contains("es_size_used")
            || !quick_state_parse_u32_text(fields.at("channels"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("sample_rate"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("es_size_used"), parsed_u32)) {
            if (detail)
                *detail = "Audiodec AAC decoder metadata is invalid";
            return false;
        }
    } else if (fields.at("kind") == "unknown") {
        if (parsed_bool) {
            if (detail)
                *detail = "Audiodec unknown decoder kind cannot be marked exact";
            return false;
        }
    } else {
        if (detail)
            *detail = "Audiodec decoder kind is invalid";
        return false;
    }

    return true;
}

std::string quick_state_snapshot_text(EmuEnvState &emuenv) {
    std::ostringstream text;
    text << "schema=thor.audiodec.v1\n";

    AudiodecState *state = emuenv.kernel.obj_store.get_if<AudiodecState>();
    if (!state) {
        text << "libraries=0\n";
        text << "decoders=0\n";
        text << "exact=1\n";
        return text.str();
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    text << "libraries=" << state->codecs.size() << "\n";
    size_t library_index = 0;
    for (const auto &[codec, handles] : state->codecs)
        text << "library." << library_index++ << "=codec=" << static_cast<uint32_t>(codec) << "\n";

    size_t decoder_count = 0;
    bool exact_restore = true;
    for (const auto &[handle, decoder] : state->decoders) {
        if (!decoder)
            continue;
        decoder_count++;
        exact_restore = exact_restore && quick_state_decoder_exact(decoder);
    }

    text << "decoders=" << decoder_count << "\n";
    text << "exact=" << (exact_restore ? 1 : 0) << "\n";
    size_t decoder_index = 0;
    for (const auto &[handle, decoder] : state->decoders) {
        if (!decoder)
            continue;

        SceAudiodecCodec codec = SCE_AUDIODEC_TYPE_AT9;
        quick_state_codec_for_handle(*state, handle, codec);
        const char *kind = quick_state_decoder_kind(decoder);
        const bool exact = quick_state_decoder_exact(decoder);

        text << "decoder." << decoder_index++
             << "=handle=" << handle
             << ";codec=" << static_cast<uint32_t>(codec)
             << ";kind=" << kind
             << ";exact=" << (exact ? 1 : 0);

        std::lock_guard<std::mutex> decoder_lock(decoder->codec_mutex);
        if (auto at9 = std::dynamic_pointer_cast<Atrac9DecoderState>(decoder)) {
            Atrac9DecoderSavedState saved = {};
            at9->export_state(&saved);
            text << ";config_data=" << at9->config_data
                 << ";es_size_used=" << at9->es_size_used
                 << ";superframe_frame_idx=" << at9->superframe_frame_idx
                 << ";superframe_data_left=" << at9->superframe_data_left
                 << ";saved=" << quick_state_hex_bytes(&saved, sizeof(saved));
        } else if (auto mp3 = std::dynamic_pointer_cast<Mp3DecoderState>(decoder)) {
            text << ";channels=" << mp3->get(DecoderQuery::CHANNELS)
                 << ";es_size_used=" << mp3->es_size_used;
        } else if (auto aac = std::dynamic_pointer_cast<AacDecoderState>(decoder)) {
            text << ";channels=" << aac->get(DecoderQuery::CHANNELS)
                 << ";sample_rate=" << aac->get(DecoderQuery::SAMPLE_RATE)
                 << ";es_size_used=" << aac->es_size_used;
        }
        text << "\n";
    }

    return text.str();
}

bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, size_t *decoder_count, size_t *library_count, bool *exact_restore, std::string *detail) {
    const auto schema = values.find("schema");
    if (schema == values.end() || schema->second != "thor.audiodec.v1") {
        if (detail)
            *detail = "Audiodec section schema is invalid";
        return false;
    }

    uint64_t parsed_libraries = 0;
    uint64_t parsed_decoders = 0;
    bool parsed_exact = false;
    if (!values.contains("libraries") || !quick_state_parse_u64_text(values.at("libraries"), parsed_libraries) || parsed_libraries > 16
        || !values.contains("decoders") || !quick_state_parse_u64_text(values.at("decoders"), parsed_decoders) || parsed_decoders > 64
        || !values.contains("exact") || !quick_state_parse_bool_text(values.at("exact"), parsed_exact)) {
        if (detail)
            *detail = "Audiodec section header is invalid";
        return false;
    }

    if (library_count)
        *library_count = static_cast<size_t>(parsed_libraries);
    if (decoder_count)
        *decoder_count = static_cast<size_t>(parsed_decoders);

    for (size_t index = 0; index < static_cast<size_t>(parsed_libraries); index++) {
        const auto value = values.find(fmt::format("library.{}", index));
        if (value == values.end()) {
            if (detail)
                *detail = "Audiodec library entry is missing";
            return false;
        }

        const auto fields = quick_state_parse_fields(value->second);
        SceAudiodecCodec codec = SCE_AUDIODEC_TYPE_AT9;
        if (!fields.contains("codec") || !quick_state_parse_codec_text(fields.at("codec"), codec)) {
            if (detail)
                *detail = "Audiodec library entry is invalid";
            return false;
        }
    }

    bool all_decoders_exact = true;
    for (size_t index = 0; index < static_cast<size_t>(parsed_decoders); index++) {
        const auto value = values.find(fmt::format("decoder.{}", index));
        if (value == values.end()) {
            if (detail)
                *detail = "Audiodec decoder entry is missing";
            return false;
        }

        const auto fields = quick_state_parse_fields(value->second);
        if (!quick_state_decoder_fields_valid(fields, detail))
            return false;

        bool decoder_exact = false;
        quick_state_parse_bool_text(fields.at("exact"), decoder_exact);
        all_decoders_exact = all_decoders_exact && decoder_exact;
    }

    if (exact_restore)
        *exact_restore = parsed_exact && all_decoders_exact;
    return true;
}

bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail) {
    size_t decoder_count = 0;
    size_t library_count = 0;
    bool exact_restore = false;
    if (!quick_state_validate_snapshot_values(values, &decoder_count, &library_count, &exact_restore, detail))
        return false;
    if (decoder_count > 0 && !exact_restore) {
        if (detail)
            *detail = "Audiodec section contains decoder state that is not exact-restorable";
        return false;
    }

    AudiodecState *state = emuenv.kernel.obj_store.get_if<AudiodecState>();
    if (!state) {
        emuenv.kernel.obj_store.create<AudiodecState>();
        state = emuenv.kernel.obj_store.get_if<AudiodecState>();
    }
    if (!state) {
        if (detail)
            *detail = "Audiodec state object could not be created";
        return false;
    }

    DecoderStates restored_decoders;
    CodecDecodersMap restored_codecs;
    for (size_t index = 0; index < library_count; index++) {
        const auto fields = quick_state_parse_fields(values.at(fmt::format("library.{}", index)));
        SceAudiodecCodec codec = SCE_AUDIODEC_TYPE_AT9;
        quick_state_parse_codec_text(fields.at("codec"), codec);
        restored_codecs[codec] = CodecDecoders();
    }

    for (size_t index = 0; index < decoder_count; index++) {
        const auto fields = quick_state_parse_fields(values.at(fmt::format("decoder.{}", index)));
        uint32_t handle = 0;
        SceAudiodecCodec codec = SCE_AUDIODEC_TYPE_AT9;
        quick_state_parse_u32_text(fields.at("handle"), handle);
        quick_state_parse_codec_text(fields.at("codec"), codec);

        if (fields.at("kind") == "at9") {
            uint32_t config_data = 0;
            uint32_t es_size_used = 0;
            uint32_t superframe_frame_idx = 0;
            uint32_t superframe_data_left = 0;
            std::vector<uint8_t> saved_bytes;
            if (!quick_state_parse_u32_text(fields.at("config_data"), config_data)
                || !quick_state_parse_u32_text(fields.at("es_size_used"), es_size_used)
                || !quick_state_parse_u32_text(fields.at("superframe_frame_idx"), superframe_frame_idx)
                || !quick_state_parse_u32_text(fields.at("superframe_data_left"), superframe_data_left)
                || !quick_state_unhex_bytes(fields.at("saved"), saved_bytes)
                || saved_bytes.size() != sizeof(Atrac9DecoderSavedState)) {
                if (detail)
                    *detail = "Audiodec AT9 decoder restore metadata is invalid";
                return false;
            }

            auto decoder = std::make_shared<Atrac9DecoderState>(config_data);
            decoder->es_size_used = es_size_used;
            decoder->superframe_frame_idx = static_cast<int>(superframe_frame_idx);
            decoder->superframe_data_left = static_cast<int>(superframe_data_left);
            Atrac9DecoderSavedState saved_state = {};
            std::memcpy(&saved_state, saved_bytes.data(), sizeof(saved_state));
            decoder->load_state(&saved_state);
            restored_decoders.emplace(static_cast<SceUID>(handle), decoder);
        } else if (fields.at("kind") == "mp3") {
            uint32_t channels = 0;
            uint32_t es_size_used = 0;
            if (!quick_state_parse_u32_text(fields.at("channels"), channels)
                || !quick_state_parse_u32_text(fields.at("es_size_used"), es_size_used)
                || channels == 0) {
                if (detail)
                    *detail = "Audiodec MP3 decoder restore metadata is invalid";
                return false;
            }

            auto decoder = std::make_shared<Mp3DecoderState>(channels);
            decoder->es_size_used = es_size_used;
            restored_decoders.emplace(static_cast<SceUID>(handle), decoder);
        } else if (fields.at("kind") == "aac") {
            uint32_t channels = 0;
            uint32_t sample_rate = 0;
            uint32_t es_size_used = 0;
            if (!quick_state_parse_u32_text(fields.at("channels"), channels)
                || !quick_state_parse_u32_text(fields.at("sample_rate"), sample_rate)
                || !quick_state_parse_u32_text(fields.at("es_size_used"), es_size_used)
                || channels == 0
                || sample_rate == 0) {
                if (detail)
                    *detail = "Audiodec AAC decoder restore metadata is invalid";
                return false;
            }

            auto decoder = std::make_shared<AacDecoderState>(sample_rate, channels);
            decoder->es_size_used = es_size_used;
            restored_decoders.emplace(static_cast<SceUID>(handle), decoder);
        } else {
            if (detail)
                *detail = "Audiodec restore supports AT9, MP3, and AAC decoder state only";
            return false;
        }
        restored_codecs[codec].insert(static_cast<SceUID>(handle));
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->decoders.swap(restored_decoders);
        state->codecs.swap(restored_codecs);
    }
    return true;
}

} // namespace sce_audiodec

constexpr uint32_t SCE_AUDIODEC_AT9_MAX_ES_SIZE = 1024;
constexpr uint32_t SCE_AUDIODEC_MP3_MAX_ES_SIZE = 1441;
// max size is 1792 for AAC ES if adts is enabled
constexpr uint32_t SCE_AUDIODEC_AAC_MAX_ES_SIZE = 1536;
constexpr uint32_t SCE_AUDIODEC_CELP_MAX_ES_SIZE = 27;

// this value is multiplied by 2 if sbr is enabled
constexpr uint32_t SCE_AUDIODEC_AAC_MAX_PCM_SIZE = KiB(2);
constexpr uint32_t SCE_AUDIODEC_MP3_V1_MAX_PCM_SIZE = 2304;
constexpr uint32_t SCE_AUDIODEC_MP3_V2_MAX_PCM_SIZE = 1152;

LIBRARY_INIT(SceAudiodec) {
    emuenv.kernel.obj_store.create<AudiodecState>();
}

EXPORT(int, sceAudiodecClearContext, SceAudiodecCtrl *ctrl) {
    TRACY_FUNC(sceAudiodecClearContext, ctrl)
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    if (state->codecs.empty()) {
        return SCE_AUDIODEC_ERROR_NOT_INITIALIZED;
    }
    if (!ctrl->handle) {
        return SCE_AUDIODEC_ERROR_INVALID_HANDLE;
    }

    const DecoderPtr &decoder = lock_and_find(ctrl->handle, state->decoders, state->mutex);

    if (decoder) {
        decoder->flush();
    } else {
        return SCE_AUDIODEC_ERROR_NOT_HANDLE_IN_USE;
    }

    return 0;
}

static int create_decoder(EmuEnvState &emuenv, SceAudiodecCtrl *ctrl, SceAudiodecCodec codec) {
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);

    SceUID handle = emuenv.kernel.get_next_uid();
    ctrl->handle = handle;
    state->codecs[codec].insert(handle);

    switch (codec) {
    case SCE_AUDIODEC_TYPE_AT9: {
        SceAudiodecInfoAt9 &info = ctrl->info.get(emuenv.mem)->at9;
        DecoderPtr decoder = std::make_shared<Atrac9DecoderState>(info.config_data);
        state->decoders[handle] = decoder;

        info.channels = decoder->get(DecoderQuery::CHANNELS);
        info.bit_rate = decoder->get(DecoderQuery::BIT_RATE);
        info.sample_rate = decoder->get(DecoderQuery::SAMPLE_RATE);
        info.super_frame_size = decoder->get(DecoderQuery::AT9_SUPERFRAME_SIZE);
        info.frames_in_super_frame = decoder->get(DecoderQuery::AT9_FRAMES_IN_SUPERFRAME);
        ctrl->es_size_max = std::min(info.super_frame_size, SCE_AUDIODEC_AT9_MAX_ES_SIZE);
        ctrl->pcm_size_max = decoder->get(DecoderQuery::AT9_SAMPLE_PER_FRAME)
            * decoder->get(DecoderQuery::CHANNELS) * sizeof(int16_t);
        return 0;
    }
    case SCE_AUDIODEC_TYPE_AAC: {
        SceAudiodecInfoAac &info = ctrl->info.get(emuenv.mem)->aac;
        DecoderPtr decoder = std::make_shared<AacDecoderState>(info.sample_rate, info.channels);
        state->decoders[handle] = decoder;

        ctrl->es_size_max = SCE_AUDIODEC_AAC_MAX_ES_SIZE;
        if (info.is_adts)
            ctrl->es_size_max += 0x100;
        ctrl->pcm_size_max = info.channels * SCE_AUDIODEC_AAC_MAX_PCM_SIZE;
        if (info.is_sbr)
            ctrl->pcm_size_max *= 2;

        LOG_WARN_IF(info.is_adts || info.is_sbr, "report it to dev, is_adts: {}, is_sbr: {}", info.is_adts, info.is_sbr);

        return 0;
    }
    case SCE_AUDIODEC_TYPE_MP3: {
        SceAudiodecInfoMp3 &info = ctrl->info.get(emuenv.mem)->mp3;
        DecoderPtr decoder = std::make_shared<Mp3DecoderState>(info.channels);
        state->decoders[handle] = decoder;

        ctrl->es_size_max = SCE_AUDIODEC_MP3_MAX_ES_SIZE;

        switch (info.version) {
        case SCE_AUDIODEC_MP3_MPEG_VERSION_1:
            ctrl->pcm_size_max = info.channels * SCE_AUDIODEC_MP3_V1_MAX_PCM_SIZE;
            return 0;
        case SCE_AUDIODEC_MP3_MPEG_VERSION_2:
        case SCE_AUDIODEC_MP3_MPEG_VERSION_2_5:
            ctrl->pcm_size_max = info.channels * SCE_AUDIODEC_MP3_V2_MAX_PCM_SIZE;
            return 0;
        default:
            LOG_ERROR("Invalid MPEG version {}.", info.version);
            return SCE_AUDIODEC_MP3_ERROR_INVALID_MPEG_VERSION;
        }
    }
    default: {
        LOG_ERROR("Unimplemented audio decoder {}.", codec);
        return -1;
    }
    }
}

EXPORT(int, sceAudiodecCreateDecoder, SceAudiodecCtrl *ctrl, SceAudiodecCodec codec) {
    TRACY_FUNC(sceAudiodecCreateDecoder, ctrl, codec);
    return create_decoder(emuenv, ctrl, codec);
}

EXPORT(int, sceAudiodecCreateDecoderExternal, SceAudiodecCtrl *ctrl, SceAudiodecCodec codec, void *context, uint32_t size) {
    TRACY_FUNC(sceAudiodecCreateDecoderExternal, ctrl, codec, context, size);
    // I think context is supposed to be just extra memory where I can allocate my context.
    // I'm just going to allocate like regular sceAudiodecCreateDecoder and see how it goes.
    // Almost sure zang has already tried this so :/ - desgroup
    return create_decoder(emuenv, ctrl, codec);
}

EXPORT(int, sceAudiodecCreateDecoderResident) {
    TRACY_FUNC(sceAudiodecCreateDecoderResident);
    return UNIMPLEMENTED();
}

static int decode_audio_frames(EmuEnvState &emuenv, const char *export_name, SceAudiodecCtrl *ctrl, SceUInt32 nb_frames) {
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    const DecoderPtr &decoder = lock_and_find(ctrl->handle, state->decoders, state->mutex);

    uint8_t *es_data = ctrl->es_data.get(emuenv.mem);
    uint8_t *pcm_data = ctrl->pcm_data.get(emuenv.mem);

    ctrl->es_size_used = 0;
    ctrl->pcm_size_given = 0;

    for (uint32_t frame = 0; frame < nb_frames; frame++) {
        DecoderSize size;
        if (!decoder->send(es_data, ctrl->es_size_max)
            || !decoder->receive(pcm_data, &size)) {
            return RET_ERROR(SCE_AUDIODEC_ERROR_API_FAIL);
        }

        uint32_t es_size_used = std::min(decoder->get_es_size(), ctrl->es_size_max);
        assert(es_size_used <= ctrl->es_size_max);
        ctrl->es_size_used += es_size_used;
        es_data += es_size_used;

        uint32_t pcm_size_given = size.samples * decoder->get(DecoderQuery::CHANNELS) * sizeof(int16_t);
        assert(pcm_size_given <= ctrl->pcm_size_max);
        ctrl->pcm_size_given += pcm_size_given;
        pcm_data += pcm_size_given;
    }

    return 0;
}

EXPORT(int, sceAudiodecDecode, SceAudiodecCtrl *ctrl) {
    TRACY_FUNC(sceAudiodecDecode, ctrl);
    return decode_audio_frames(emuenv, export_name, ctrl, 1);
}

EXPORT(int, sceAudiodecDecodeNFrames, SceAudiodecCtrl *ctrl, SceUInt32 nFrames) {
    TRACY_FUNC(sceAudiodecDecodeNFrames, ctrl, nFrames);
    return decode_audio_frames(emuenv, export_name, ctrl, nFrames);
}

EXPORT(int, sceAudiodecDecodeNStreams, Ptr<SceAudiodecCtrl> *pCtrls, SceUInt32 nStreams) {
    TRACY_FUNC(sceAudiodecDecodeNStreams, pCtrls, nStreams);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudiodecDeleteDecoder, SceAudiodecCtrl *ctrl) {
    TRACY_FUNC(sceAudiodecDeleteDecoder, ctrl);
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    state->decoders.erase(ctrl->handle);

    // there are at most 4 different codecs, we can afford to look
    // at all of them (the handle is in one of them)
    for (auto &codec : state->codecs) {
        codec.second.erase(ctrl->handle);
    }

    return 0;
}

EXPORT(int, sceAudiodecDeleteDecoderExternal, SceAudiodecCtrl *ctrl, void *context) {
    TRACY_FUNC(sceAudiodecDeleteDecoderExternal, ctrl, context);
    return CALL_EXPORT(sceAudiodecDeleteDecoder, ctrl);
}

EXPORT(int, sceAudiodecDeleteDecoderResident) {
    TRACY_FUNC(sceAudiodecDeleteDecoderResident);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudiodecGetContextSize, SceAudiodecCtrl *pCtrl, SceAudiodecCodec codecType) {
    TRACY_FUNC(sceAudiodecGetContextSize, pCtrl, codecType);
    STUBBED("fake size");
    return 53;
}

EXPORT(int, sceAudiodecGetInternalError) {
    TRACY_FUNC(sceAudiodecGetInternalError);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceAudiodecInitLibrary, SceAudiodecCodec codecType, SceAudiodecInitParam *pInitParam) {
    TRACY_FUNC(sceAudiodecInitLibrary, codecType, pInitParam);
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);

    state->codecs[codecType] = CodecDecoders();
    return 0;
}

EXPORT(int, sceAudiodecPartlyDecode, SceAudiodecCtrl *ctrl, SceUInt32 samples_offset, SceUInt32 samples_to_decode) {
    TRACY_FUNC(sceAudiodecPartlyDecode, ctrl, samples_offset, samples_to_decode);
    // this function is only called by libatrac
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    if (!state->codecs[SCE_AUDIODEC_TYPE_AT9].contains(ctrl->handle)) {
        STUBBED("Call to sceAudiodecPartlyDecode with a codec other than Atrac9, report it to the devs");
    }

    const std::shared_ptr<DecoderState> &decoder = lock_and_find(ctrl->handle, state->decoders, state->mutex);

    uint8_t *es_data = ctrl->es_data.get(emuenv.mem);
    uint8_t *pcm_data = ctrl->pcm_data.get(emuenv.mem);

    // TODO: if the offset is too big, do not decode the first superframes (doesn't seem to happen with libatrac)
    const uint32_t bytes_per_sample = decoder->get(DecoderQuery::CHANNELS) * sizeof(int16_t);
    ctrl->es_size_used = 0;
    ctrl->pcm_size_given = 0;
    std::vector<uint8_t> temp_storage;
    temp_storage.reserve((samples_offset + samples_to_decode) * bytes_per_sample);

    while (ctrl->pcm_size_given < (samples_offset + samples_to_decode) * bytes_per_sample) {
        DecoderSize size;
        if (!decoder->send(es_data, ctrl->es_size_max)) {
            return RET_ERROR(SCE_AUDIODEC_ERROR_API_FAIL);
        }
        const uint32_t es_size_used = decoder->get_es_size();
        assert(es_size_used <= ctrl->es_size_max);
        ctrl->es_size_used += es_size_used;
        es_data += es_size_used;

        decoder->receive(nullptr, &size);
        const uint32_t pcm_size_given = size.samples * bytes_per_sample;
        ctrl->pcm_size_given += pcm_size_given;
        const uint32_t old_size = temp_storage.size();
        temp_storage.resize(old_size + pcm_size_given);
        decoder->receive(temp_storage.data() + old_size, &size);
    }

    memcpy(pcm_data + samples_offset * bytes_per_sample, temp_storage.data() + samples_offset * bytes_per_sample, samples_to_decode * bytes_per_sample);

    return 0;
}

EXPORT(SceInt32, sceAudiodecTermLibrary, SceAudiodecCodec codecType) {
    TRACY_FUNC(sceAudiodecTermLibrary, codecType);
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);

    // remove decoders associated with codecType
    for (auto &handle : state->codecs[codecType]) {
        state->decoders.erase(handle);
    }
    state->codecs.erase(codecType);
    return 0;
}
