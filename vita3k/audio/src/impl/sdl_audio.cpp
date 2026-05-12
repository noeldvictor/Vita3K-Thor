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

#include "audio/impl/sdl_audio.h"
#include "util/log.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
}

#define SDL_CHECK_EXT(condition, ret)                         \
    do {                                                      \
        if (!(condition)) {                                   \
            LOG_ERROR("SDL audio error: {}", SDL_GetError()); \
            return ret;                                       \
        }                                                     \
    } while (0)

#define SDL_CHECK(f_call) SDL_CHECK_EXT(f_call, {})
#define SDL_CHECK_VOID(f_call) SDL_CHECK_EXT(f_call, )
#define SDL_CHECK_NEG(f_call) SDL_CHECK_EXT((f_call) >= 0, {})

static int get_threshold_samples(const int device_buffer_samples) {
    return 4 * device_buffer_samples;
}

static void log_av_error(const char *operation, const int ret) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR("FFmpeg atempo {} failed: {}", operation, errbuf);
}

static void reset_tempo_filter(SDLAudioOutPort &port) {
    if (port.tempo_input_frame) {
        av_frame_free(&port.tempo_input_frame);
    }
    if (port.tempo_output_frame) {
        av_frame_free(&port.tempo_output_frame);
    }
    if (port.tempo_graph) {
        avfilter_graph_free(&port.tempo_graph);
    }
    port.tempo_source = nullptr;
    port.tempo_sink = nullptr;
    port.tempo_speed_percent = 100;
    port.tempo_buffer.clear();
    port.tempo_fallback_credit = 0.0;
    port.tempo_fallback_tail.clear();
}

SDLAudioOutPort::~SDLAudioOutPort() {
    reset_tempo_filter(*this);
}

static std::string make_atempo_filter_chain(const uint32_t speed_percent) {
    double tempo = std::clamp(speed_percent / 100.0, 0.5, 100.0);
    std::ostringstream filter;
    bool first = true;
    while (tempo > 2.0) {
        if (!first)
            filter << ',';
        filter << "atempo=2.0";
        tempo /= 2.0;
        first = false;
    }

    if (!first)
        filter << ',';
    filter << "atempo=" << tempo;
    return filter.str();
}

static bool init_tempo_filter(SDLAudioOutPort &port, const uint32_t speed_percent) {
    reset_tempo_filter(port);
    port.tempo_filter_failed = false;

    const AVFilter *source_filter = avfilter_get_by_name("abuffer");
    const AVFilter *sink_filter = avfilter_get_by_name("abuffersink");
    const AVFilter *tempo_filter = avfilter_get_by_name("atempo");
    if (!source_filter || !sink_filter || !tempo_filter) {
        LOG_ERROR("FFmpeg atempo filter is unavailable; falling back to SDL fast-forward audio");
        port.tempo_filter_failed = true;
        return false;
    }

    port.tempo_graph = avfilter_graph_alloc();
    if (!port.tempo_graph) {
        LOG_ERROR("Failed to allocate FFmpeg atempo graph");
        port.tempo_filter_failed = true;
        return false;
    }

    int ret = avfilter_graph_create_filter(&port.tempo_source, source_filter, "thor_tempo_in", nullptr, nullptr, port.tempo_graph);
    if (ret < 0) {
        log_av_error("create abuffer", ret);
        port.tempo_filter_failed = true;
        reset_tempo_filter(port);
        return false;
    }

    AVBufferSrcParameters *params = av_buffersrc_parameters_alloc();
    if (!params) {
        LOG_ERROR("Failed to allocate FFmpeg atempo source parameters");
        port.tempo_filter_failed = true;
        reset_tempo_filter(port);
        return false;
    }

    params->format = AV_SAMPLE_FMT_S16;
    params->time_base = { 1, port.freq };
    params->sample_rate = port.freq;
    av_channel_layout_default(&params->ch_layout, port.channels);
    ret = av_buffersrc_parameters_set(port.tempo_source, params);
    av_channel_layout_uninit(&params->ch_layout);
    av_free(params);
    if (ret < 0) {
        log_av_error("set abuffer parameters", ret);
        port.tempo_filter_failed = true;
        reset_tempo_filter(port);
        return false;
    }

    ret = avfilter_graph_create_filter(&port.tempo_sink, sink_filter, "thor_tempo_out", nullptr, nullptr, port.tempo_graph);
    if (ret < 0) {
        log_av_error("create abuffersink", ret);
        port.tempo_filter_failed = true;
        reset_tempo_filter(port);
        return false;
    }

    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();
    if (!inputs || !outputs) {
        LOG_ERROR("Failed to allocate FFmpeg atempo graph endpoints");
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        port.tempo_filter_failed = true;
        reset_tempo_filter(port);
        return false;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = port.tempo_source;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = port.tempo_sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    const std::string filter_chain = make_atempo_filter_chain(speed_percent);
    ret = avfilter_graph_parse_ptr(port.tempo_graph, filter_chain.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) {
        log_av_error("parse filter chain", ret);
        port.tempo_filter_failed = true;
        reset_tempo_filter(port);
        return false;
    }

    ret = avfilter_graph_config(port.tempo_graph, nullptr);
    if (ret < 0) {
        log_av_error("configure graph", ret);
        port.tempo_filter_failed = true;
        reset_tempo_filter(port);
        return false;
    }

    port.tempo_input_frame = av_frame_alloc();
    port.tempo_output_frame = av_frame_alloc();
    if (!port.tempo_input_frame || !port.tempo_output_frame) {
        LOG_ERROR("Failed to allocate FFmpeg atempo frames");
        port.tempo_filter_failed = true;
        reset_tempo_filter(port);
        return false;
    }

    port.tempo_speed_percent = speed_percent;
    LOG_INFO("Using FFmpeg atempo fast-forward audio filter: {}", filter_chain);
    return true;
}

static bool ensure_tempo_filter(SDLAudioOutPort &port, const uint32_t speed_percent) {
    if (speed_percent <= 100 || port.channels <= 0 || port.len <= 0 || port.freq <= 0) {
        reset_tempo_filter(port);
        port.tempo_filter_failed = false;
        return false;
    }

    if (port.tempo_filter_failed && !port.tempo_graph)
        return false;

    if (port.tempo_graph && port.tempo_speed_percent == speed_percent)
        return true;

    return init_tempo_filter(port, speed_percent);
}

static const void *tempo_correct_fast_forward(SDLAudioOutPort &port, const void *buffer, int &bytes_to_write, const uint32_t speed_percent) {
    bytes_to_write = port.len_bytes;
    if (!ensure_tempo_filter(port, speed_percent))
        return buffer;

    AVFrame *input_frame = port.tempo_input_frame;
    av_frame_unref(input_frame);
    input_frame->format = AV_SAMPLE_FMT_S16;
    input_frame->nb_samples = port.len;
    input_frame->sample_rate = port.freq;
    av_channel_layout_default(&input_frame->ch_layout, port.channels);

    int ret = av_frame_get_buffer(input_frame, 0);
    if (ret < 0) {
        log_av_error("allocate input frame", ret);
        return buffer;
    }

    const int input_bytes = port.len * port.channels * static_cast<int>(sizeof(int16_t));
    std::memcpy(input_frame->data[0], buffer, static_cast<size_t>(input_bytes));
    ret = av_buffersrc_add_frame_flags(port.tempo_source, input_frame, 0);
    if (ret < 0) {
        log_av_error("push input frame", ret);
        av_frame_unref(input_frame);
        return buffer;
    }

    auto &output = port.tempo_buffer;
    output.clear();
    AVFrame *output_frame = port.tempo_output_frame;
    while (true) {
        av_frame_unref(output_frame);
        ret = av_buffersink_get_frame(port.tempo_sink, output_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            log_av_error("pull output frame", ret);
            break;
        }

        const int bytes_per_sample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(output_frame->format));
        if (output_frame->format != AV_SAMPLE_FMT_S16 || bytes_per_sample != static_cast<int>(sizeof(int16_t)) || output_frame->ch_layout.nb_channels != port.channels) {
            LOG_ERROR("Unexpected FFmpeg atempo output format");
            av_frame_unref(output_frame);
            return buffer;
        }

        const size_t existing = output.size();
        const size_t sample_count = static_cast<size_t>(output_frame->nb_samples) * port.channels;
        output.resize(existing + sample_count);
        std::memcpy(&output[existing], output_frame->data[0], sample_count * sizeof(int16_t));
    }

    bytes_to_write = static_cast<int>(output.size() * sizeof(int16_t));
    return output.data();
}

static int16_t blend_sample(const int16_t a, const int16_t b, const int index, const int count) {
    const int weight_b = index + 1;
    const int weight_a = count - weight_b + 1;
    return static_cast<int16_t>((static_cast<int32_t>(a) * weight_a + static_cast<int32_t>(b) * weight_b) / (count + 1));
}

static const void *normal_pitch_skip_fast_forward(SDLAudioOutPort &port, const void *buffer, int &bytes_to_write, const uint32_t speed_percent) {
    bytes_to_write = port.len_bytes;
    if (speed_percent <= 100 || port.channels <= 0 || port.len <= 0 || port.freq <= 0) {
        port.tempo_fallback_credit = 0.0;
        port.tempo_fallback_tail.clear();
        return buffer;
    }

    port.tempo_fallback_credit += 100.0;
    if (port.tempo_fallback_credit + 0.001 < speed_percent) {
        bytes_to_write = 0;
        return nullptr;
    }

    while (port.tempo_fallback_credit >= speed_percent)
        port.tempo_fallback_credit -= speed_percent;

    const auto *input = static_cast<const int16_t *>(buffer);
    auto &output = port.tempo_buffer;
    const size_t sample_count = static_cast<size_t>(port.len) * port.channels;
    output.assign(input, input + sample_count);

    const int overlap_frames = std::min({ port.len, std::max(16, port.freq / 400), static_cast<int>(port.tempo_fallback_tail.size() / port.channels) });
    if (overlap_frames > 0) {
        for (int frame = 0; frame < overlap_frames; frame++) {
            for (int channel = 0; channel < port.channels; channel++) {
                const int index = frame * port.channels + channel;
                output[index] = blend_sample(port.tempo_fallback_tail[index], output[index], frame, overlap_frames);
            }
        }
    }

    const int tail_frames = std::min(port.len, std::max(16, port.freq / 400));
    port.tempo_fallback_tail.assign(
        output.end() - static_cast<ptrdiff_t>(tail_frames * port.channels),
        output.end());

    return output.data();
}

void SDLCALL SDLAudioAdapter::thread_wakeup_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    assert(userdata != nullptr);
    assert(stream != nullptr);
    SDLAudioOutPort *port = static_cast<SDLAudioOutPort *>(userdata);
    const int samples_available = port->adapter.get_rest_sample(*port);
    if (samples_available < get_threshold_samples(port->adapter.device_buffer_samples) || additional_amount > 0) {
        port->cond_var.notify_one();
    }
}

SDLAudioAdapter::SDLAudioAdapter(AudioState &audio_state)
    : AudioAdapter(audio_state) {}

SDLAudioAdapter::~SDLAudioAdapter() {
    if (device_id > 0) {
        SDL_CloseAudioDevice(device_id);
    }
}

bool SDLAudioAdapter::init() {
    // SDL3 default is 1024 sample frames for 48kHz audio, which is higher than cubeb.
    // Request smaller device buffer for lower latency callbacks.
    // 512 sample frames = 2048 bytes for stereo 16-bit, matching cubeb's callback size.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    SDL_CHECK_EXT(device_id > 0, false);
    return true;
}

void SDLAudioAdapter::switch_state(const bool pause) {
    if (pause)
        SDL_CHECK_VOID(SDL_PauseAudioDevice(device_id));
    else
        SDL_CHECK_VOID(SDL_ResumeAudioDevice(device_id));
}

AudioOutPortPtr SDLAudioAdapter::open_port(int nb_channels, int freq, int nb_sample) {
    SDL_AudioSpec src_spec = {
        .format = SDL_AUDIO_S16LE,
        .channels = nb_channels,
        .freq = freq
    };
    SDL_CHECK(SDL_GetAudioDeviceFormat(device_id, &dst_spec, &device_buffer_samples));
    const AudioStreamPtr stream(SDL_CreateAudioStream(&src_spec, &dst_spec), SDL_DestroyAudioStream);
    SDL_CHECK(stream);
    SDL_CHECK(SDL_BindAudioStream(device_id, stream.get()));
    auto port = std::make_shared<SDLAudioOutPort>(stream, *this);
    SDL_CHECK(SDL_SetAudioStreamGetCallback(stream.get(), SDLAudioAdapter::thread_wakeup_callback, port.get()));
    port->channels = nb_channels;
    port->len_microseconds = (nb_sample * 1'000'000ULL) / freq;
    port->len_bytes = nb_sample * nb_channels * sizeof(int16_t);
    switch_state(false);
    return port;
}

void SDLAudioAdapter::audio_output(AudioOutPort &out_port, const void *buffer) {
    //  Put audio to the port's stream and see how much is left to play.
    SDLAudioOutPort &port = static_cast<SDLAudioOutPort &>(out_port);
    const uint32_t speed_percent = std::max<uint32_t>(state.speed_percent.load(), 1);
    const bool use_tempo_filter = ensure_tempo_filter(port, speed_percent);
    const float speed_ratio = 1.f;
    if (std::abs(port.speed_ratio - speed_ratio) > 0.001f) {
        SDL_CHECK_VOID(SDL_SetAudioStreamFrequencyRatio(port.stream.get(), speed_ratio));
        port.speed_ratio = speed_ratio;
    }

    // If there's lots of audio left to play, stop this thread.
    // The audio callback will wake it up later when it's running out of data.
    const int samples_available = get_rest_sample(port);
    const int speed_scaled_threshold = std::max(1, (get_threshold_samples(device_buffer_samples) * static_cast<int>(speed_percent)) / 100);
    if (samples_available > speed_scaled_threshold) {
        std::unique_lock<std::mutex> lock(port.mutex);
        const uint64_t scaled_wait = std::max<uint64_t>(1, (port.len_microseconds * 200) / speed_percent);
        port.cond_var.wait_for(lock, std::chrono::microseconds(scaled_wait));
    }
    int bytes_to_write = out_port.len_bytes;
    const void *audio_data = use_tempo_filter
        ? tempo_correct_fast_forward(port, buffer, bytes_to_write, speed_percent)
        : normal_pitch_skip_fast_forward(port, buffer, bytes_to_write, speed_percent);
    if (bytes_to_write > 0) {
        SDL_CHECK_VOID(SDL_PutAudioStreamData(port.stream.get(), audio_data, bytes_to_write));
    }
}

void SDLAudioAdapter::set_volume(AudioOutPort &out_port, float volume) {
    SDL_CHECK_VOID(SDL_SetAudioStreamGain(static_cast<SDLAudioOutPort &>(out_port).stream.get(), volume));
}

int SDLAudioAdapter::get_rest_sample(AudioOutPort &out_port) {
    auto &port = static_cast<SDLAudioOutPort &>(out_port);
    const int bytes_available = SDL_GetAudioStreamAvailable(port.stream.get());
    SDL_CHECK_NEG(bytes_available);
    // we have the number of bytes left, we can convert it back to the number of samples left
    return bytes_available / SDL_AUDIO_FRAMESIZE(dst_spec);
}
