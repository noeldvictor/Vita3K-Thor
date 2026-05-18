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

#include <codec/state.h>

#include <util/fs.h>
#include <util/log.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include <cassert>
#include <limits>

#include <fmt/format.h>

uint64_t PlayerState::get_framerate_microseconds() {
    if (!format || video_stream_id < 0 || !format->streams[video_stream_id])
        return 16667;

    AVRational rational = format->streams[video_stream_id]->avg_frame_rate;
    if (rational.num == 0)
        return 16667;

    return 1000000ull * rational.den / rational.num;
}

DecoderSize PlayerState::get_size() {
    if (video_context)
        return { { static_cast<uint32_t>(video_context->width), static_cast<uint32_t>(video_context->height) } };

    return {};
}

uint64_t PlayerState::stream_timestamp_to_microseconds(const int32_t stream_id, const uint64_t timestamp) const {
    if (!format || stream_id < 0 || static_cast<unsigned int>(stream_id) >= format->nb_streams || timestamp > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return 0;

    const int64_t scaled = av_rescale_q(static_cast<int64_t>(timestamp), format->streams[stream_id]->time_base, AV_TIME_BASE_Q);
    return scaled > 0 ? static_cast<uint64_t>(scaled) : 0;
}

void PlayerState::pop_video() {
    if (videos_queue.empty())
        return;

    if (!switch_video(videos_queue.front()))
        LOG_WARN("Failed to switch queued video '{}'.", videos_queue.front());
    videos_queue.pop();
}

void PlayerState::free_video() {
    if (video_context)
        avcodec_free_context(&video_context);

    if (audio_context)
        avcodec_free_context(&audio_context);

    if (format)
        avformat_close_input(&format);

    while (!video_packets.empty()) {
        AVPacket *packet = video_packets.front();
        av_packet_free(&packet);
        video_packets.pop();
    }

    while (!audio_packets.empty()) {
        AVPacket *packet = audio_packets.front();
        av_packet_free(&packet);
        audio_packets.pop();
    }

    video_playing.clear();
    video_stream_id = -1;
    audio_stream_id = -1;
    time_of_last_frame = 0;
    framerate_microseconds = 0;
    last_timestamp = 0;
    last_video_time_us = 0;
    last_audio_timestamp = 0;
    last_audio_time_us = 0;
    restored_video_frame_ready = false;
    restored_video_frame_timestamp = 0;
    restored_video_frame_time_us = 0;
    restored_video_frame.clear();
}

bool PlayerState::switch_video(const std::string &path) {
    free_video();
    video_playing = path;

    int error = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
    if (error != 0) {
        LOG_WARN("Failed to open video '{}': {}.", path, codec_error_name(error));
        video_playing.clear();
        return false;
    }

    // Load stream info.
    error = avformat_find_stream_info(format, nullptr);
    if (error < 0) {
        LOG_WARN("Failed to read stream info for video '{}': {}.", path, codec_error_name(error));
        free_video();
        return false;
    }

    video_stream_id = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_stream_id = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (video_stream_id >= 0) {
        AVStream *video_stream = format->streams[video_stream_id];
        const AVCodec *video_codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
        video_context = avcodec_alloc_context3(video_codec);
        avcodec_parameters_to_context(video_context, video_stream->codecpar);
        avcodec_open2(video_context, video_codec, nullptr);
    }

    if (audio_stream_id >= 0) {
        AVStream *audio_stream = format->streams[audio_stream_id];
        const AVCodec *audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
        audio_context = avcodec_alloc_context3(audio_codec);
        avcodec_parameters_to_context(audio_context, audio_stream->codecpar);
        avcodec_open2(audio_context, audio_codec, nullptr);
    }

    return true;
}

bool PlayerState::next_packet(int32_t stream_id) {
    std::queue<AVPacket *> &this_queue = stream_id == video_stream_id ? video_packets : audio_packets;
    std::queue<AVPacket *> &other_queue = stream_id != video_stream_id ? video_packets : audio_packets;

    while (true) {
        if (!this_queue.empty()) {
            AVPacket *this_packet = this_queue.front();
            this_queue.pop();

            if (stream_id == video_stream_id) {
                int err = avcodec_send_packet(video_context, this_packet);
                assert(err == 0);
            }

            if (stream_id == audio_stream_id) {
                int err = avcodec_send_packet(audio_context, this_packet);
                assert(err == 0);
            }

            av_packet_free(&this_packet);
            return true;
        }

        AVPacket *packet = av_packet_alloc();
        if (av_read_frame(format, packet) != 0) {
            av_packet_free(&packet);
            return false;
        }

        if (packet->stream_index == stream_id) {
            this_queue.push(packet);
        } else {
            other_queue.push(packet);
        }
    }
}

bool PlayerState::prime_video_frame_at_or_after(const uint64_t target_timestamp, std::string *detail) {
    restored_video_frame_ready = false;
    restored_video_frame_timestamp = 0;
    restored_video_frame_time_us = 0;
    restored_video_frame.clear();

    if (!format || !video_context || video_stream_id < 0)
        return true;

    const DecoderSize size = get_size();
    const uint32_t output_size = H264DecoderState::buffer_size(size);
    if (output_size == 0) {
        if (detail)
            *detail = fmt::format("video '{}' has no decodable frame size after seek", video_playing);
        return false;
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        if (detail)
            *detail = fmt::format("video '{}' could not allocate a restore-prime frame", video_playing);
        return false;
    }

    constexpr uint32_t MAX_PRIME_FRAMES = 600;
    for (uint32_t decoded_frames = 0; decoded_frames < MAX_PRIME_FRAMES;) {
        const int error = avcodec_receive_frame(video_context, frame);
        if (error == AVERROR(EAGAIN)) {
            if (next_packet(video_stream_id))
                continue;

            if (detail)
                *detail = fmt::format("video '{}' ended before timestamp {} could be restored", video_playing, target_timestamp);
            av_frame_free(&frame);
            return false;
        }
        if (error == AVERROR_EOF) {
            if (detail)
                *detail = fmt::format("video '{}' reached EOF before timestamp {} could be restored", video_playing, target_timestamp);
            av_frame_free(&frame);
            return false;
        }
        if (error != 0) {
            if (detail)
                *detail = fmt::format("video '{}' decode failed while restoring timestamp {}: {}", video_playing, target_timestamp, codec_error_name(error));
            av_frame_free(&frame);
            return false;
        }

        decoded_frames++;
        const bool has_timestamp = frame->best_effort_timestamp != AV_NOPTS_VALUE && frame->best_effort_timestamp >= 0;
        const uint64_t frame_timestamp = has_timestamp ? static_cast<uint64_t>(frame->best_effort_timestamp) : 0;
        if (!has_timestamp || target_timestamp == 0 || frame_timestamp >= target_timestamp) {
            restored_video_frame.resize(output_size);
            copy_yuv_data_from_frame(frame, restored_video_frame.data(), frame->width, frame->height, false);
            restored_video_frame_ready = true;
            restored_video_frame_timestamp = frame_timestamp;
            restored_video_frame_time_us = has_timestamp ? stream_timestamp_to_microseconds(video_stream_id, frame_timestamp) : 0;
            last_timestamp = frame_timestamp;
            last_video_time_us = restored_video_frame_time_us;
            av_frame_free(&frame);
            return true;
        }

        av_frame_unref(frame);
    }

    if (detail)
        *detail = fmt::format("video '{}' did not reach timestamp {} within {} decoded frames after seek", video_playing, target_timestamp, MAX_PRIME_FRAMES);
    av_frame_free(&frame);
    return false;
}

std::vector<int16_t> PlayerState::receive_audio() {
    if (audio_stream_id < 0)
        return {};

    if (video_playing.empty())
        return {};

    AVFrame *frame = av_frame_alloc();
    std::vector<int16_t> data;
    while (true) {
        int error = avcodec_receive_frame(audio_context, frame);

        if (error == AVERROR(EAGAIN) && next_packet(audio_stream_id))
            continue;

        if (error != 0) {
            if (videos_queue.empty()) {
                // Stop playing videos or
                video_playing.clear();
                break;
            } else {
                // Play the next video (if there is any).
                if (!switch_video(videos_queue.front()))
                    LOG_WARN("Failed to switch queued audio/video source '{}'.", videos_queue.front());
                videos_queue.pop();
                continue;
            }
        }

        LOG_WARN_IF(frame->format != AV_SAMPLE_FMT_FLTP, "Unknown audio format {}.", frame->format);

        if (frame->best_effort_timestamp != AV_NOPTS_VALUE && frame->best_effort_timestamp >= 0) {
            last_audio_timestamp = static_cast<uint64_t>(frame->best_effort_timestamp);
            last_audio_time_us = stream_timestamp_to_microseconds(audio_stream_id, last_audio_timestamp);
        }

        last_channels = frame->ch_layout.nb_channels;
        last_sample_count = frame->nb_samples;
        last_sample_rate = frame->sample_rate;

        data.resize(frame->nb_samples * frame->ch_layout.nb_channels);

        for (int a = 0; a < frame->nb_samples; a++) {
            for (int b = 0; b < frame->ch_layout.nb_channels; b++) {
                auto *frame_data = reinterpret_cast<float *>(frame->data[b]);
                float current_sample = frame_data[a];
                int16_t pcm_sample = current_sample * INT16_MAX;

                data[a * frame->ch_layout.nb_channels + b] = pcm_sample;
            }
        }

        break;
    }

    av_frame_free(&frame);
    return data;
}

std::vector<uint8_t> PlayerState::receive_video() {
    if (video_stream_id < 0)
        return {};

    if (video_playing.empty())
        return {};

    if (restored_video_frame_ready) {
        restored_video_frame_ready = false;
        last_timestamp = restored_video_frame_timestamp;
        last_video_time_us = restored_video_frame_time_us;
        return std::move(restored_video_frame);
    }

    AVFrame *frame = av_frame_alloc();
    std::vector<uint8_t> data;
    while (true) {
        int error = avcodec_receive_frame(video_context, frame);

        if (error == AVERROR(EAGAIN) && next_packet(video_stream_id))
            continue;

        if (error != 0) {
            if (videos_queue.empty()) {
                // Stop playing videos or
                video_playing.clear();
                break;
            } else {
                // Play the next video (if there is any).
                if (!switch_video(videos_queue.front()))
                    LOG_WARN("Failed to switch queued video source '{}'.", videos_queue.front());
                videos_queue.pop();
                continue;
            }
        }

        if (frame->best_effort_timestamp != AV_NOPTS_VALUE && frame->best_effort_timestamp >= 0)
            last_timestamp = static_cast<uint64_t>(frame->best_effort_timestamp);
        last_video_time_us = stream_timestamp_to_microseconds(video_stream_id, last_timestamp);

        data.resize(H264DecoderState::buffer_size(
            { { static_cast<uint32_t>(video_context->width), static_cast<uint32_t>(video_context->height) } }));
        copy_yuv_data_from_frame(frame, data.data(), frame->width, frame->height, false);

        break;
    }

    av_frame_free(&frame);
    return data;
}

void PlayerState::queue(const std::string &path) {
    if (fs::exists(path)) {
        LOG_INFO("Queued video: '{}'.", path);
        if (video_playing.empty()) {
            if (!switch_video(path))
                LOG_WARN("Failed to start queued video '{}'.", path);
        } else {
            videos_queue.push(path);
        }
    } else {
        LOG_INFO("Cannot find video: {}", path);
    }
}

PlayerQuickState PlayerState::export_quick_state() const {
    PlayerQuickState state;
    state.video_playing = video_playing;
    std::queue<std::string> queued = videos_queue;
    while (!queued.empty()) {
        state.videos_queue.push_back(queued.front());
        queued.pop();
    }
    state.time_of_last_frame = time_of_last_frame;
    state.framerate_microseconds = framerate_microseconds;
    state.last_timestamp = last_timestamp;
    state.last_video_time_us = last_video_time_us;
    state.last_audio_timestamp = last_audio_timestamp;
    state.last_audio_time_us = last_audio_time_us;
    state.last_channels = last_channels;
    state.last_sample_rate = last_sample_rate;
    state.last_sample_count = last_sample_count;
    state.video_packet_count = static_cast<uint32_t>(std::min<size_t>(video_packets.size(), std::numeric_limits<uint32_t>::max()));
    state.audio_packet_count = static_cast<uint32_t>(std::min<size_t>(audio_packets.size(), std::numeric_limits<uint32_t>::max()));
    return state;
}

bool PlayerState::restore_quick_state(const PlayerQuickState &state, std::string *detail) {
    free_video();
    videos_queue = {};
    for (const std::string &queued : state.videos_queue)
        videos_queue.push(queued);

    time_of_last_frame = state.time_of_last_frame;
    framerate_microseconds = state.framerate_microseconds;
    last_timestamp = state.last_timestamp;
    last_video_time_us = state.last_video_time_us;
    last_audio_timestamp = state.last_audio_timestamp;
    last_audio_time_us = state.last_audio_time_us;
    last_channels = state.last_channels;
    last_sample_rate = state.last_sample_rate;
    last_sample_count = state.last_sample_count;

    if (state.video_playing.empty())
        return true;

    if (!fs::exists(state.video_playing)) {
        if (detail)
            *detail = fmt::format("video '{}' is missing", state.video_playing);
        return false;
    }

    if (!switch_video(state.video_playing)) {
        if (detail)
            *detail = fmt::format("video '{}' could not be reopened", state.video_playing);
        return false;
    }
    time_of_last_frame = state.time_of_last_frame;
    framerate_microseconds = state.framerate_microseconds;
    last_timestamp = state.last_timestamp;
    last_video_time_us = state.last_video_time_us;
    last_audio_timestamp = state.last_audio_timestamp;
    last_audio_time_us = state.last_audio_time_us;
    last_channels = state.last_channels;
    last_sample_rate = state.last_sample_rate;
    last_sample_count = state.last_sample_count;

    if (state.last_timestamp == 0 || video_stream_id < 0)
        return true;

    const int seek_error = av_seek_frame(format, video_stream_id, static_cast<int64_t>(state.last_timestamp), AVSEEK_FLAG_BACKWARD);
    if (seek_error < 0) {
        if (detail)
            *detail = fmt::format("video '{}' could not seek to timestamp {}: {}", state.video_playing, state.last_timestamp, codec_error_name(seek_error));
        return false;
    }

    if (video_context)
        avcodec_flush_buffers(video_context);
    if (audio_context)
        avcodec_flush_buffers(audio_context);

    if (!prime_video_frame_at_or_after(state.last_timestamp, detail))
        return false;

    last_audio_timestamp = state.last_audio_timestamp;
    last_audio_time_us = state.last_audio_time_us;
    return true;
}

PlayerState::~PlayerState() {
    free_video();

    video_playing.clear();
    videos_queue = {};
}
