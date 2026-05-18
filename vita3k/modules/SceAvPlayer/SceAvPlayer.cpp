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

#include <modules/SceAvPlayer/quick_state.h>

#include <codec/state.h>
#include <io/functions.h>
#include <kernel/state.h>

#include <util/lock_and_find.h>
#include <util/log.h>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

// Defines stop/pause behaviour. If true, GetVideo/AudioData will return false when stopped.
constexpr bool REJECT_DATA_ON_PAUSE = true;

// Uses a catchup video style if lag causes the video to go behind.
constexpr bool CATCHUP_VIDEO_PLAYBACK = true;

/*
typedef Ptr<void> (*SceAvPlayerAllocator)(uint32_t arguments, uint32_t alignment, uint32_t size);
typedef void (*SceAvPlayerDeallocator)(uint32_t arguments, Ptr<void> memory);

typedef int32_t (*SceAvPlayerOpenFile)(uint32_t arguments, Ptr<const char> filename);
typedef int32_t (*SceAvPlayerCloseFile)(uint32_t arguments);
typedef int32_t (*SceAvPlayerReadFile)(uint32_t arguments, Ptr<uint8_t> buffer, uint64_t offset, uint32_t size);
typedef uint64_t (*SceAvPlayerGetFileSize)(uint32_t arguments);

typedef void (*SceAvPlayerEventCallback)(uint32_t arguments, int32_t event_id, int32_t source_id, Ptr<void> event_data);
*/

struct PlayerInfoState;
typedef std::shared_ptr<PlayerInfoState> PlayerPtr;
typedef std::map<SceUID, PlayerPtr> PlayerStates;

struct AvPlayerState {
    std::mutex mutex;
    PlayerStates players;
};

struct SceAvPlayerMemoryAllocator {
    uint32_t user_data = 0;

    // All of these should be cast to SceAvPlayerAllocator or SceAvPlayerDeallocator types.
    Ptr<void> general_allocator;
    Ptr<void> general_deallocator;
    Ptr<void> texture_allocator;
    Ptr<void> texture_deallocator;
};

struct SceAvPlayerFileManager {
    uint32_t user_data = 0;

    // Cast to SceAvPlayerOpenFile, SceAvPlayerCloseFile, SceAvPlayerReadFile and SceAvPlayerGetFileSize.
    Ptr<void> open_file;
    Ptr<void> close_file;
    Ptr<void> read_file;
    Ptr<void> file_size;
};

struct SceAvPlayerEventManager {
    uint32_t user_data = 0;

    // Cast to SceAvPlayerEventCallback.
    Ptr<void> event_callback;
};

struct PlayerInfoState {
    PlayerState player;

    // Framebuffer count is defined in info. I'm being safe now and forcing it to 4 (even though its usually 2).
    constexpr static uint32_t RING_BUFFER_COUNT = 4;

    uint32_t video_buffer_ring_index = 0;
    uint32_t video_buffer_size = 0;
    std::array<Ptr<uint8_t>, RING_BUFFER_COUNT> video_buffer;

    uint32_t audio_buffer_ring_index = 0;
    uint32_t audio_buffer_size = 0;
    std::array<Ptr<uint8_t>, RING_BUFFER_COUNT> audio_buffer;

    bool do_loop = false;
    bool paused = false;

    uint64_t last_frame_time = 0;
    SceAvPlayerMemoryAllocator memory_allocator;
    SceAvPlayerFileManager file_manager;
    SceAvPlayerEventManager event_manager;
};

enum class DebugLevel {
    NONE,
    INFO,
    WARNINGS,
    ALL,
};

struct SceAvPlayerInfo {
    SceAvPlayerMemoryAllocator memory_allocator;
    SceAvPlayerFileManager file_manager;
    SceAvPlayerEventManager event_manager;
    DebugLevel debug_level;
    uint32_t base_priority;
    int32_t frame_buffer_count;
    int32_t auto_start;
    uint32_t unknown0;
};

struct SceAvPlayerAudio {
    uint16_t channels;
    uint16_t unknown;
    uint32_t sample_rate;
    uint32_t size;
    char language[4];
};

struct SceAvPlayerVideo {
    uint32_t width;
    uint32_t height;
    float aspect_ratio;
    char language[4];
};

struct SceAvPlayerTextPosition {
    uint16_t top;
    uint16_t left;
    uint16_t bottom;
    uint16_t right;
};

struct SceAvPlayerTimedText {
    char language[4];
    uint16_t text_size;
    uint16_t font_size;
    SceAvPlayerTextPosition position;
};

union SceAvPlayerStreamDetails {
    SceAvPlayerAudio audio;
    SceAvPlayerVideo video;
    SceAvPlayerTimedText text;
};

struct SceAvPlayerFrameInfo {
    Ptr<uint8_t> data;
    uint32_t unknown;
    uint64_t timestamp;
    SceAvPlayerStreamDetails stream_details;
};

enum class MediaType {
    VIDEO,
    AUDIO,
};

struct SceAvPlayerStreamInfo {
    MediaType stream_type;
    uint32_t unknown;
    SceAvPlayerStreamDetails stream_details;
};

enum SceAvPlayerErrorCode : uint32_t {
    SCE_AVPLAYER_ERROR_ILLEGAL_ADDR = 0x806a0001,
    SCE_AVPLAYER_ERROR_INVALID_ARGUMENT = 0x806a0002,
    SCE_AVPLAYER_ERROR_NOT_ENOUGH_MEMORY = 0x806a0003,
    SCE_AVPLAYER_ERROR_INVALID_EVENT = 0x806a0004,
    SCE_AVPLAYER_WAR_FILE_NONINTERLEAVED = 0x806a00a0,
    SCE_AVPLAYER_WAR_LOOPING_BACK = 0x806a00a1,
    SCE_AVPLAYER_WAR_JUMP_COMPLETE = 0x806a00a3
};

enum SceAvPlayerState {
    SCE_AVPLAYER_STATE_UNKNOWN = 0,
    SCE_AVPLAYER_STATE_STOP = 1,
    SCE_AVPLAYER_STATE_READY = 2,
    SCE_AVPLAYER_STATE_PLAY = 3,
    SCE_AVPLAYER_STATE_PAUSE = 4,
    SCE_AVPLAYER_STATE_BUFFERING = 5,
    SCE_AVPLAYER_TIMED_TEXT_DELIVERY = 16,
    SCE_AVPLAYER_WARNING_ID = 32,
    SCE_AVPLAYER_ENCRYPTION = 48
};

static inline uint64_t current_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

static uint64_t scaled_video_frame_interval(const EmuEnvState &emuenv, const uint64_t frame_interval_us) {
    const uint64_t speed_percent = std::max<uint32_t>(emuenv.kernel.speed_percent.load(), 1);
    return std::max<uint64_t>(1, (frame_interval_us * 100) / speed_percent);
}

namespace sce_avplayer {

static std::string quick_state_hex_string(const std::string &value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text;
    text.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0xF]);
    }
    return text;
}

static bool quick_state_unhex_string(const std::string &text, std::string &value) {
    if (text.size() % 2 != 0)
        return false;

    const auto hex_value = [](const char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    value.clear();
    value.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const int hi = hex_value(text[i]);
        const int lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        value.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
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
    long parsed = 0;
    try {
        parsed = std::stol(text, &consumed, 10);
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

static bool quick_state_parse_address_text(const std::string &text, Address &out) {
    uint64_t parsed = 0;
    if (!quick_state_parse_u64_text(text, parsed, 0) || parsed > std::numeric_limits<Address>::max())
        return false;
    out = static_cast<Address>(parsed);
    return true;
}

static bool quick_state_parse_indexed_player(const std::map<std::string, std::string> &values, const size_t index, const bool require_v2_fields, std::map<std::string, std::string> &fields, std::string *detail) {
    const std::string key = fmt::format("player.{}", index);
    const auto value = values.find(key);
    if (value == values.end()) {
        if (detail)
            *detail = fmt::format("player {} metadata is missing", index);
        return false;
    }

    fields = quick_state_parse_fields(value->second);
    static constexpr const char *required_fields[] = {
        "handle",
        "active",
        "playing",
        "queued",
        "video_ring",
        "video_size",
        "audio_ring",
        "audio_size",
        "loop",
        "paused",
        "last_frame_age_us",
        "last_timestamp",
        "last_channels",
        "last_sample_rate",
        "last_sample_count",
        "general_user",
        "general_allocator",
        "general_deallocator",
        "texture_user",
        "texture_allocator",
        "texture_deallocator",
        "file_user",
        "open_file",
        "close_file",
        "read_file",
        "file_size",
        "event_user",
        "event_callback",
    };
    for (const char *field : required_fields) {
        if (!fields.contains(field)) {
            if (detail)
                *detail = fmt::format("player {} field '{}' is missing", index, field);
            return false;
        }
    }

    if (require_v2_fields) {
        static constexpr const char *required_v2_fields[] = {
            "last_video_time_us",
            "last_audio_timestamp",
            "last_audio_time_us",
            "video_packets",
            "audio_packets",
            "cursor_restore",
        };
        for (const char *field : required_v2_fields) {
            if (!fields.contains(field)) {
                if (detail)
                    *detail = fmt::format("player {} v2 field '{}' is missing", index, field);
                return false;
            }
        }
    }

    return true;
}

std::string quick_state_snapshot_text(EmuEnvState &emuenv) {
    std::ostringstream text;
    text << "schema=thor.avplayer.v2\n";

    AvPlayerState *state = emuenv.kernel.obj_store.get_if<AvPlayerState>();
    if (!state) {
        text << "players=0\n";
        text << "active_players=0\n";
        return text.str();
    }

    const uint64_t now = current_time();
    std::lock_guard<std::mutex> lock(state->mutex);
    std::vector<std::pair<SceUID, PlayerPtr>> players;
    players.reserve(state->players.size());
    for (const auto &[handle, player] : state->players) {
        if (player)
            players.push_back({ handle, player });
    }

    size_t active_players = 0;
    for (const auto &[_, player_info] : players) {
        if (!player_info->player.video_playing.empty())
            active_players++;
    }

    text << "players=" << players.size() << "\n";
    text << "active_players=" << active_players << "\n";
    for (size_t index = 0; index < players.size(); index++) {
        const auto &[handle, player_info] = players[index];
        const PlayerQuickState player_state = player_info->player.export_quick_state();
        const uint64_t last_frame_age_us = now > player_info->last_frame_time ? now - player_info->last_frame_time : 0;

        text << "player." << index
             << "=handle=" << handle
             << ";active=" << !player_state.video_playing.empty()
             << ";playing=" << quick_state_hex_string(player_state.video_playing)
             << ";queued=" << player_state.videos_queue.size()
             << ";video_ring=" << player_info->video_buffer_ring_index
             << ";video_size=" << player_info->video_buffer_size
             << ";audio_ring=" << player_info->audio_buffer_ring_index
             << ";audio_size=" << player_info->audio_buffer_size
             << ";loop=" << player_info->do_loop
             << ";paused=" << player_info->paused
             << ";last_frame_age_us=" << last_frame_age_us
             << ";last_timestamp=" << player_state.last_timestamp
             << ";last_video_time_us=" << player_state.last_video_time_us
             << ";last_audio_timestamp=" << player_state.last_audio_timestamp
             << ";last_audio_time_us=" << player_state.last_audio_time_us
             << ";last_channels=" << player_state.last_channels
             << ";last_sample_rate=" << player_state.last_sample_rate
             << ";last_sample_count=" << player_state.last_sample_count
             << ";video_packets=" << player_state.video_packet_count
             << ";audio_packets=" << player_state.audio_packet_count
             << ";cursor_restore=seek-prime"
             << ";general_user=" << player_info->memory_allocator.user_data
             << ";general_allocator=0x" << std::hex << player_info->memory_allocator.general_allocator.address()
             << ";general_deallocator=0x" << player_info->memory_allocator.general_deallocator.address()
             << ";texture_user=" << std::dec << player_info->memory_allocator.user_data
             << ";texture_allocator=0x" << std::hex << player_info->memory_allocator.texture_allocator.address()
             << ";texture_deallocator=0x" << player_info->memory_allocator.texture_deallocator.address()
             << ";file_user=" << std::dec << player_info->file_manager.user_data
             << ";open_file=0x" << std::hex << player_info->file_manager.open_file.address()
             << ";close_file=0x" << player_info->file_manager.close_file.address()
             << ";read_file=0x" << player_info->file_manager.read_file.address()
             << ";file_size=0x" << player_info->file_manager.file_size.address()
             << ";event_user=" << std::dec << player_info->event_manager.user_data
             << ";event_callback=0x" << std::hex << player_info->event_manager.event_callback.address()
             << std::dec
             << "\n";
        for (size_t queue_index = 0; queue_index < player_state.videos_queue.size(); queue_index++) {
            text << "player." << index << ".queue." << queue_index << "=" << quick_state_hex_string(player_state.videos_queue[queue_index]) << "\n";
        }
        for (size_t buffer_index = 0; buffer_index < PlayerInfoState::RING_BUFFER_COUNT; buffer_index++) {
            text << "player." << index << ".video_buffer." << buffer_index << "=0x" << std::hex << player_info->video_buffer[buffer_index].address() << std::dec << "\n";
            text << "player." << index << ".audio_buffer." << buffer_index << "=0x" << std::hex << player_info->audio_buffer[buffer_index].address() << std::dec << "\n";
        }
    }

    return text.str();
}

bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, size_t *player_count, size_t *active_player_count, std::string *detail) {
    const auto schema = values.find("schema");
    if (schema == values.end() || (schema->second != "thor.avplayer.v1" && schema->second != "thor.avplayer.v2")) {
        if (detail)
            *detail = "AVPlayer section schema is invalid";
        return false;
    }
    const bool schema_v2 = schema->second == "thor.avplayer.v2";

    uint64_t parsed_players = 0;
    uint64_t parsed_active = 0;
    if (!values.contains("players") || !values.contains("active_players")
        || !quick_state_parse_u64_text(values.at("players"), parsed_players)
        || !quick_state_parse_u64_text(values.at("active_players"), parsed_active)
        || parsed_players > 64
        || parsed_active > parsed_players) {
        if (detail)
            *detail = "AVPlayer section header is invalid";
        return false;
    }

    if (player_count)
        *player_count = static_cast<size_t>(parsed_players);
    if (active_player_count)
        *active_player_count = static_cast<size_t>(parsed_active);

    size_t actual_active_players = 0;
    for (size_t index = 0; index < static_cast<size_t>(parsed_players); index++) {
        std::map<std::string, std::string> fields;
        if (!quick_state_parse_indexed_player(values, index, schema_v2, fields, detail))
            return false;

        uint32_t parsed_u32 = 0;
        uint64_t parsed_u64 = 0;
        Address parsed_address = 0;
        bool parsed_bool = false;
        bool active = false;
        std::string decoded_path;
        if (!quick_state_parse_u32_text(fields.at("handle"), parsed_u32) || parsed_u32 == 0
            || !quick_state_parse_bool_text(fields.at("active"), active)
            || !quick_state_unhex_string(fields.at("playing"), decoded_path)
            || !quick_state_parse_u64_text(fields.at("queued"), parsed_u64) || parsed_u64 > 64
            || !quick_state_parse_u32_text(fields.at("video_ring"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("video_size"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("audio_ring"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("audio_size"), parsed_u32)
            || !quick_state_parse_bool_text(fields.at("loop"), parsed_bool)
            || !quick_state_parse_bool_text(fields.at("paused"), parsed_bool)
            || !quick_state_parse_u64_text(fields.at("last_frame_age_us"), parsed_u64)
            || !quick_state_parse_u64_text(fields.at("last_timestamp"), parsed_u64)
            || !quick_state_parse_u32_text(fields.at("last_channels"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("last_sample_rate"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("last_sample_count"), parsed_u32)
            || !quick_state_parse_u32_text(fields.at("general_user"), parsed_u32)
            || !quick_state_parse_address_text(fields.at("general_allocator"), parsed_address)
            || !quick_state_parse_address_text(fields.at("general_deallocator"), parsed_address)
            || !quick_state_parse_u32_text(fields.at("texture_user"), parsed_u32)
            || !quick_state_parse_address_text(fields.at("texture_allocator"), parsed_address)
            || !quick_state_parse_address_text(fields.at("texture_deallocator"), parsed_address)
            || !quick_state_parse_u32_text(fields.at("file_user"), parsed_u32)
            || !quick_state_parse_address_text(fields.at("open_file"), parsed_address)
            || !quick_state_parse_address_text(fields.at("close_file"), parsed_address)
            || !quick_state_parse_address_text(fields.at("read_file"), parsed_address)
            || !quick_state_parse_address_text(fields.at("file_size"), parsed_address)
            || !quick_state_parse_u32_text(fields.at("event_user"), parsed_u32)
            || !quick_state_parse_address_text(fields.at("event_callback"), parsed_address)) {
            if (detail)
                *detail = fmt::format("AVPlayer player {} metadata is invalid", index);
            return false;
        }

        if (active != !decoded_path.empty()) {
            if (detail)
                *detail = fmt::format("AVPlayer player {} active flag does not match playing path", index);
            return false;
        }
        if (active)
            actual_active_players++;
        if (!schema_v2 && active) {
            if (detail)
                *detail = fmt::format("AVPlayer player {} uses old v1 active movie cursor without exact restore metadata", index);
            return false;
        }
        if (schema_v2) {
            if (!quick_state_parse_u64_text(fields.at("last_video_time_us"), parsed_u64)
                || !quick_state_parse_u64_text(fields.at("last_audio_timestamp"), parsed_u64)
                || !quick_state_parse_u64_text(fields.at("last_audio_time_us"), parsed_u64)
                || !quick_state_parse_u32_text(fields.at("video_packets"), parsed_u32)
                || !quick_state_parse_u32_text(fields.at("audio_packets"), parsed_u32)
                || fields.at("cursor_restore") != "seek-prime") {
                if (detail)
                    *detail = fmt::format("AVPlayer player {} v2 cursor metadata is invalid", index);
                return false;
            }
        }

        uint64_t queued = 0;
        quick_state_parse_u64_text(fields.at("queued"), queued);
        for (size_t queue_index = 0; queue_index < static_cast<size_t>(queued); queue_index++) {
            const std::string queue_key = fmt::format("player.{}.queue.{}", index, queue_index);
            if (!values.contains(queue_key) || !quick_state_unhex_string(values.at(queue_key), decoded_path)) {
                if (detail)
                    *detail = fmt::format("AVPlayer player {} queue {} is invalid", index, queue_index);
                return false;
            }
        }
        for (size_t buffer_index = 0; buffer_index < PlayerInfoState::RING_BUFFER_COUNT; buffer_index++) {
            const std::string video_key = fmt::format("player.{}.video_buffer.{}", index, buffer_index);
            const std::string audio_key = fmt::format("player.{}.audio_buffer.{}", index, buffer_index);
            if (!values.contains(video_key) || !quick_state_parse_address_text(values.at(video_key), parsed_address)
                || !values.contains(audio_key) || !quick_state_parse_address_text(values.at(audio_key), parsed_address)) {
                if (detail)
                    *detail = fmt::format("AVPlayer player {} ring buffer {} is invalid", index, buffer_index);
                return false;
            }
        }
    }

    if (actual_active_players != static_cast<size_t>(parsed_active)) {
        if (detail)
            *detail = fmt::format("AVPlayer active player count {} does not match header {}", actual_active_players, parsed_active);
        return false;
    }

    return true;
}

bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail) {
    size_t player_count = 0;
    if (!quick_state_validate_snapshot_values(values, &player_count, nullptr, detail))
        return false;
    const bool schema_v2 = values.at("schema") == "thor.avplayer.v2";

    AvPlayerState *state = emuenv.kernel.obj_store.get_if<AvPlayerState>();
    if (!state) {
        emuenv.kernel.obj_store.create<AvPlayerState>();
        state = emuenv.kernel.obj_store.get_if<AvPlayerState>();
    }
    if (!state) {
        if (detail)
            *detail = "AVPlayer state object could not be created";
        return false;
    }

    const uint64_t now = current_time();
    PlayerStates restored_players;
    for (size_t index = 0; index < player_count; index++) {
        std::map<std::string, std::string> fields;
        if (!quick_state_parse_indexed_player(values, index, schema_v2, fields, detail))
            return false;

        uint32_t handle = 0;
        uint32_t parsed_u32 = 0;
        uint64_t parsed_u64 = 0;
        Address parsed_address = 0;
        bool parsed_bool = false;
        std::string decoded_path;
        if (!quick_state_parse_u32_text(fields.at("handle"), handle) || handle == 0)
            return false;

        PlayerPtr player_info = std::make_shared<PlayerInfoState>();
        PlayerQuickState player_state;
        if (!quick_state_unhex_string(fields.at("playing"), player_state.video_playing))
            return false;
        if (!quick_state_parse_u64_text(fields.at("queued"), parsed_u64) || parsed_u64 > 64)
            return false;
        player_state.videos_queue.reserve(static_cast<size_t>(parsed_u64));
        for (size_t queue_index = 0; queue_index < static_cast<size_t>(parsed_u64); queue_index++) {
            const std::string queue_key = fmt::format("player.{}.queue.{}", index, queue_index);
            if (!quick_state_unhex_string(values.at(queue_key), decoded_path))
                return false;
            player_state.videos_queue.push_back(decoded_path);
        }

        if (!quick_state_parse_u32_text(fields.at("video_ring"), player_info->video_buffer_ring_index)
            || !quick_state_parse_u32_text(fields.at("video_size"), player_info->video_buffer_size)
            || !quick_state_parse_u32_text(fields.at("audio_ring"), player_info->audio_buffer_ring_index)
            || !quick_state_parse_u32_text(fields.at("audio_size"), player_info->audio_buffer_size)
            || !quick_state_parse_bool_text(fields.at("loop"), player_info->do_loop)
            || !quick_state_parse_bool_text(fields.at("paused"), player_info->paused)
            || !quick_state_parse_u64_text(fields.at("last_frame_age_us"), parsed_u64)
            || !quick_state_parse_u64_text(fields.at("last_timestamp"), player_state.last_timestamp)
            || !quick_state_parse_u32_text(fields.at("last_channels"), player_state.last_channels)
            || !quick_state_parse_u32_text(fields.at("last_sample_rate"), player_state.last_sample_rate)
            || !quick_state_parse_u32_text(fields.at("last_sample_count"), player_state.last_sample_count)) {
            if (detail)
                *detail = fmt::format("AVPlayer player {} scalar restore fields are invalid", index);
            return false;
        }
        player_info->last_frame_time = now > parsed_u64 ? now - parsed_u64 : now;
        if (schema_v2) {
            if (!quick_state_parse_u64_text(fields.at("last_video_time_us"), player_state.last_video_time_us)
                || !quick_state_parse_u64_text(fields.at("last_audio_timestamp"), player_state.last_audio_timestamp)
                || !quick_state_parse_u64_text(fields.at("last_audio_time_us"), player_state.last_audio_time_us)) {
                if (detail)
                    *detail = fmt::format("AVPlayer player {} v2 cursor restore fields are invalid", index);
                return false;
            }
        }

        if (!quick_state_parse_u32_text(fields.at("general_user"), player_info->memory_allocator.user_data)
            || !quick_state_parse_address_text(fields.at("general_allocator"), parsed_address)) {
            return false;
        }
        player_info->memory_allocator.general_allocator = Ptr<void>(parsed_address);
        if (!quick_state_parse_address_text(fields.at("general_deallocator"), parsed_address)
            || !quick_state_parse_u32_text(fields.at("texture_user"), parsed_u32)) {
            return false;
        }
        player_info->memory_allocator.general_deallocator = Ptr<void>(parsed_address);
        player_info->memory_allocator.user_data = parsed_u32;
        if (!quick_state_parse_address_text(fields.at("texture_allocator"), parsed_address))
            return false;
        player_info->memory_allocator.texture_allocator = Ptr<void>(parsed_address);
        if (!quick_state_parse_address_text(fields.at("texture_deallocator"), parsed_address)
            || !quick_state_parse_u32_text(fields.at("file_user"), player_info->file_manager.user_data)) {
            return false;
        }
        player_info->memory_allocator.texture_deallocator = Ptr<void>(parsed_address);
        if (!quick_state_parse_address_text(fields.at("open_file"), parsed_address))
            return false;
        player_info->file_manager.open_file = Ptr<void>(parsed_address);
        if (!quick_state_parse_address_text(fields.at("close_file"), parsed_address))
            return false;
        player_info->file_manager.close_file = Ptr<void>(parsed_address);
        if (!quick_state_parse_address_text(fields.at("read_file"), parsed_address))
            return false;
        player_info->file_manager.read_file = Ptr<void>(parsed_address);
        if (!quick_state_parse_address_text(fields.at("file_size"), parsed_address)
            || !quick_state_parse_u32_text(fields.at("event_user"), player_info->event_manager.user_data)) {
            return false;
        }
        player_info->file_manager.file_size = Ptr<void>(parsed_address);
        if (!quick_state_parse_address_text(fields.at("event_callback"), parsed_address))
            return false;
        player_info->event_manager.event_callback = Ptr<void>(parsed_address);

        for (size_t buffer_index = 0; buffer_index < PlayerInfoState::RING_BUFFER_COUNT; buffer_index++) {
            const std::string video_key = fmt::format("player.{}.video_buffer.{}", index, buffer_index);
            const std::string audio_key = fmt::format("player.{}.audio_buffer.{}", index, buffer_index);
            if (!quick_state_parse_address_text(values.at(video_key), parsed_address))
                return false;
            player_info->video_buffer[buffer_index] = Ptr<uint8_t>(parsed_address);
            if (!quick_state_parse_address_text(values.at(audio_key), parsed_address))
                return false;
            player_info->audio_buffer[buffer_index] = Ptr<uint8_t>(parsed_address);
        }

        if (!player_info->player.restore_quick_state(player_state, detail))
            return false;

        restored_players.emplace(static_cast<SceUID>(handle), std::move(player_info));
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->players.swap(restored_players);
    }
    return true;
}

} // namespace sce_avplayer

static Ptr<uint8_t> get_buffer(const PlayerPtr &player, MediaType media_type,
    MemState &mem, uint32_t size, bool new_frame = true) {
    uint32_t &buffer_size = media_type == MediaType::VIDEO ? player->video_buffer_size : player->audio_buffer_size;
    uint32_t &ring_index = media_type == MediaType::VIDEO ? player->video_buffer_ring_index : player->audio_buffer_ring_index;
    auto &buffers = media_type == MediaType::VIDEO ? player->video_buffer : player->audio_buffer;

    if (buffer_size < size) {
        buffer_size = size;
        for (uint32_t a = 0; a < PlayerInfoState::RING_BUFFER_COUNT; a++) {
            if (buffers[a])
                free(mem, buffers[a]);
            std::string alloc_name = fmt::format("AvPlayer {} Media Ring {}",
                media_type == MediaType::VIDEO ? "Video" : "Audio", a);

            buffers[a] = alloc(mem, size, alloc_name.c_str());
        }
    }

    if (new_frame)
        ring_index++;
    Ptr<uint8_t> buffer = buffers[ring_index % PlayerInfoState::RING_BUFFER_COUNT];
    return buffer;
}

static void run_event_callback(EmuEnvState &emuenv, const ThreadStatePtr &thread, const PlayerPtr &player_info, uint32_t event_id, uint32_t source_id, Ptr<void> event_data) {
    if (player_info->event_manager.event_callback) {
        thread->run_callback(player_info->event_manager.event_callback.address(), { player_info->event_manager.user_data, event_id, source_id, event_data.address() });
    }
}

EXPORT(int32_t, sceAvPlayerAddSource, SceUID player_handle, Ptr<const char> path) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);

    if (!player_info) {
        return RET_ERROR(SCE_AVPLAYER_ERROR_INVALID_ARGUMENT);
    }

    const auto thread = emuenv.kernel.get_thread(thread_id);

    auto file_path = expand_path(emuenv.io, path.get(emuenv.mem), emuenv.pref_path);
    if (!fs::exists(file_path) && player_info->file_manager.open_file && player_info->file_manager.close_file && player_info->file_manager.read_file && player_info->file_manager.file_size) {
        fs::create_directories(emuenv.cache_path);

        // Create temp media file
        const auto temp_file_path = emuenv.cache_path / "temp_vita_media.mp4";
        fs::ofstream temp_file(temp_file_path, std::ios::out | std::ios::binary);

        const Address buf = alloc(emuenv.mem, KiB(512), "AvPlayer buffer");
        const auto buf_ptr = Ptr<char>(buf).get(emuenv.mem);
        thread->run_callback(player_info->file_manager.open_file.address(), { player_info->file_manager.user_data, path.address() });
        // TODO: support file_size > 4GB (callback function returns uint64_t, but I dont know how to get high dword of uint64_t)
        const uint32_t file_size = thread->run_callback(player_info->file_manager.file_size.address(), { player_info->file_manager.user_data });
        auto remaining = file_size;
        uint32_t offset = 0;
        while (remaining) {
            const auto buf_size = std::min((uint32_t)KiB(512), remaining);
            // zero in 5 parameter means high dword of uint64_t parameter. see previous todo
            thread->run_callback(player_info->file_manager.read_file.address(), { player_info->file_manager.user_data, buf, offset, 0, buf_size });
            temp_file.write(buf_ptr, buf_size);
            offset += buf_size;
            remaining -= buf_size;
        }
        free(emuenv.mem, buf);
        temp_file.close();
        thread->run_callback(player_info->file_manager.close_file.address(), { player_info->file_manager.user_data });
        if (fs::file_size(temp_file_path) != file_size) {
            LOG_ERROR("File is corrupted or incomplete: {}", temp_file_path);
            return -1;
        }
        file_path = temp_file_path;
    }

    player_info->player.queue(file_path.string());
    run_event_callback(emuenv, thread, player_info, SCE_AVPLAYER_STATE_BUFFERING, 0, Ptr<void>(0)); // may be important for sound
    run_event_callback(emuenv, thread, player_info, SCE_AVPLAYER_STATE_READY, 0, Ptr<void>(0));
    return 0;
}

EXPORT(int, sceAvPlayerClose, SceUID player_handle) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    const auto thread = emuenv.kernel.get_thread(thread_id);
    run_event_callback(emuenv, thread, player_info, SCE_AVPLAYER_STATE_STOP, 0, Ptr<void>(0));
    std::lock_guard<std::mutex> lock(state->mutex);
    state->players.erase(player_handle);
    return 0;
}

EXPORT(uint64_t, sceAvPlayerCurrentTime, SceUID player_handle) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);

    return player_info->player.last_timestamp;
}

EXPORT(int, sceAvPlayerDisableStream) {
    return UNIMPLEMENTED();
}

EXPORT(int32_t, sceAvPlayerStreamCount, SceUID player_handle) {
    STUBBED("ALWAYS RETURN 2 (VIDEO AND AUDIO)");
    return 2;
}

EXPORT(int32_t, sceAvPlayerEnableStream, SceUID player_handle, uint32_t stream_no) {
    if (player_handle == 0) {
        return SCE_AVPLAYER_ERROR_ILLEGAL_ADDR;
    }
    if (stream_no > (uint32_t)(CALL_EXPORT(sceAvPlayerStreamCount, player_handle))) {
        return SCE_AVPLAYER_ERROR_INVALID_ARGUMENT;
    }
    return UNIMPLEMENTED();
}

EXPORT(bool, sceAvPlayerGetAudioData, SceUID player_handle, SceAvPlayerFrameInfo *frame_info) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    if (!player_info) {
        return false;
    }
    Ptr<uint8_t> buffer;

    if (player_info->paused) {
        if (REJECT_DATA_ON_PAUSE) {
            return false;
        } else {
            // This is probably incorrect and will make weird noises :P
            buffer = get_buffer(player_info, MediaType::AUDIO, emuenv.mem,
                player_info->player.last_sample_count * sizeof(int16_t) * player_info->player.last_channels, true);
        }
    } else {
        std::vector<int16_t> data = player_info->player.receive_audio();

        if (data.empty())
            return false;

        buffer = get_buffer(player_info, MediaType::AUDIO, emuenv.mem, (uint32_t)data.size() * sizeof(int16_t), false);
        std::memcpy(buffer.get(emuenv.mem), data.data(), data.size() * sizeof(int16_t));
    }

    frame_info->timestamp = player_info->player.last_timestamp;
    frame_info->stream_details.audio.channels = player_info->player.last_channels;
    frame_info->stream_details.audio.sample_rate = player_info->player.last_sample_rate;
    frame_info->stream_details.audio.size = player_info->player.last_channels * player_info->player.last_sample_count * sizeof(int16_t);
    frame_info->data = buffer;

    strcpy(frame_info->stream_details.audio.language, "ENG");
    return true;
}

EXPORT(uint32_t, sceAvPlayerGetStreamInfo, SceUID player_handle, SceUInt32 stream_no, SceAvPlayerStreamInfo *stream_info) {
    if (!stream_info) {
        return SCE_AVPLAYER_ERROR_ILLEGAL_ADDR;
    }
    if (player_handle == 0) {
        return SCE_AVPLAYER_ERROR_ILLEGAL_ADDR;
    }
    STUBBED("ALWAYS SUSPECTS 2 STREAMS: VIDEO AND AUDIO");
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    if (stream_no == 0) { // suspect always two streams: audio and video //first is video
        DecoderSize size = player_info->player.get_size();
        stream_info->stream_type = MediaType::VIDEO;
        stream_info->stream_details.video.width = size.width;
        stream_info->stream_details.video.height = size.height;
        stream_info->stream_details.video.aspect_ratio = static_cast<float>(size.width) / static_cast<float>(size.height);
        strcpy(stream_info->stream_details.video.language, "ENG");
    } else if (stream_no == 1) { // audio
        player_info->player.receive_audio(); // TODO: Get audio info without skipping data frames
        stream_info->stream_type = MediaType::AUDIO;
        stream_info->stream_details.audio.channels = player_info->player.last_channels;
        stream_info->stream_details.audio.sample_rate = player_info->player.last_sample_rate;
        stream_info->stream_details.audio.size = player_info->player.last_channels * player_info->player.last_sample_count * sizeof(int16_t);
        strcpy(stream_info->stream_details.audio.language, "ENG");
    } else {
        return SCE_AVPLAYER_ERROR_INVALID_ARGUMENT;
    }
    return 0;
}

EXPORT(bool, sceAvPlayerGetVideoData, SceUID player_handle, SceAvPlayerFrameInfo *frame_info) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    if (!player_info) {
        return false;
    }

    Ptr<uint8_t> buffer;

    DecoderSize size = player_info->player.get_size();

    uint64_t framerate = scaled_video_frame_interval(emuenv, player_info->player.get_framerate_microseconds());

    // needs new frame
    if (player_info->last_frame_time + framerate < current_time()) {
        if (CATCHUP_VIDEO_PLAYBACK)
            player_info->last_frame_time += framerate;
        else
            player_info->last_frame_time = current_time();

        if (player_info->paused) {
            if (REJECT_DATA_ON_PAUSE)
                return false;
            else
                buffer = get_buffer(player_info, MediaType::VIDEO, emuenv.mem, H264DecoderState::buffer_size(size), false);
        } else {
            buffer = get_buffer(player_info, MediaType::VIDEO, emuenv.mem, H264DecoderState::buffer_size(size), true);

            std::vector<uint8_t> data = player_info->player.receive_video();
            std::memcpy(buffer.get(emuenv.mem), data.data(), data.size());
        }
    } else {
        buffer = get_buffer(player_info, MediaType::VIDEO, emuenv.mem, H264DecoderState::buffer_size(size), false);
    }
    // TODO: catch eof error and call
    // uint32_t buf = SCE_AVPLAYER_ERROR_MAYBE_EOF;
    // run_event_callback(emuenv, thread_id, player_info, SCE_AVPLAYER_STATE_ERROR, 0, &buf);

    frame_info->timestamp = player_info->player.last_timestamp;
    frame_info->stream_details.video.width = size.width;
    frame_info->stream_details.video.height = size.height;
    frame_info->stream_details.video.aspect_ratio = static_cast<float>(size.width) / static_cast<float>(size.height);
    strcpy(frame_info->stream_details.video.language, "ENG");
    frame_info->data = buffer;
    return true;
}

EXPORT(bool, sceAvPlayerGetVideoDataEx, SceUID player_handle, SceAvPlayerFrameInfo *frame_info) {
    STUBBED("Use GetVideoData");
    return CALL_EXPORT(sceAvPlayerGetVideoData, player_handle, frame_info);
}

EXPORT(SceUID, sceAvPlayerInit, SceAvPlayerInfo *info) {
    emuenv.kernel.obj_store.create<AvPlayerState>();
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    SceUID player_handle = emuenv.kernel.get_next_uid();
    PlayerPtr player = std::make_shared<PlayerInfoState>();
    state->players[player_handle] = player;

    player->last_frame_time = current_time();
    player->memory_allocator = info->memory_allocator;
    player->file_manager = info->file_manager;
    player->event_manager = info->event_manager;

    // Result is defined as a void *, but I just call it SceUID because it is easier to deal with. Same size.
    return player_handle;
}

EXPORT(bool, sceAvPlayerIsActive, SceUID player_handle) {
    if (player_handle == 0) {
        return false;
    }

    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);

    return !player_info->player.video_playing.empty();
}

EXPORT(int, sceAvPlayerJumpToTime) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvPlayerPause, SceUID player_handle) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    player_info->paused = true;
    const auto thread = emuenv.kernel.get_thread(thread_id);
    run_event_callback(emuenv, thread, player_info, SCE_AVPLAYER_STATE_PAUSE, 0, Ptr<void>(0));
    return 0;
}

EXPORT(int, sceAvPlayerPostInit) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvPlayerResume, SceUID player_handle) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    if (!player_info->paused) {
        const auto thread = emuenv.kernel.get_thread(thread_id);
        run_event_callback(emuenv, thread, player_info, SCE_AVPLAYER_STATE_PLAY, 0, Ptr<void>(0));
    }
    player_info->paused = false;
    return 0;
}

EXPORT(int, sceAvPlayerSetLooping, SceUID player_handle, bool do_loop) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    player_info->do_loop = do_loop;

    return STUBBED("LOOPING NOT IMPLEMENTED");
}

EXPORT(int, sceAvPlayerSetTrickSpeed) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvPlayerStart, SceUID player_handle) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    if (!player_info->player.videos_queue.empty()) {
        player_info->player.pop_video();
    }
    const auto thread = emuenv.kernel.get_thread(thread_id);
    run_event_callback(emuenv, thread, player_info, SCE_AVPLAYER_STATE_PLAY, 0, Ptr<void>(0));
    return 0;
}

EXPORT(int, sceAvPlayerStop, SceUID player_handle) {
    const auto state = emuenv.kernel.obj_store.get<AvPlayerState>();
    const PlayerPtr &player_info = lock_and_find(player_handle, state->players, state->mutex);
    player_info->player.free_video();
    const auto thread = emuenv.kernel.get_thread(thread_id);
    run_event_callback(emuenv, thread, player_info, SCE_AVPLAYER_STATE_STOP, 0, Ptr<void>(0));
    return 0;
}
