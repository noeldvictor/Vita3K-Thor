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

static int16_t blend_sample(const int16_t a, const int16_t b, const int index, const int count) {
    const int weight_b = index + 1;
    const int weight_a = count - weight_b + 1;
    return static_cast<int16_t>((static_cast<int32_t>(a) * weight_a + static_cast<int32_t>(b) * weight_b) / (count + 1));
}

static const void *pitch_correct_fast_forward(SDLAudioOutPort &port, const void *buffer, int &bytes_to_write, const uint32_t speed_percent) {
    if (speed_percent <= 100 || port.channels <= 0 || port.len <= 0)
        return buffer;

    const int input_frames = port.len;
    const int target_frames = std::max(1, (input_frames * 100) / static_cast<int>(speed_percent));
    if (target_frames >= input_frames)
        return buffer;

    const auto *input = static_cast<const int16_t *>(buffer);
    auto &output = port.pitch_correct_buffer;
    output.assign(static_cast<size_t>(target_frames) * port.channels, 0);

    const int grain_frames = std::clamp(port.freq / 100, 96, std::max(96, input_frames));
    const int overlap_frames = std::clamp(grain_frames / 4, 16, std::max(16, grain_frames / 2));
    int write_frame = 0;

    while (write_frame < target_frames) {
        const int input_frame = std::min(input_frames - 1, (write_frame * static_cast<int>(speed_percent)) / 100);
        const int copy_frames = std::min({ grain_frames, target_frames - write_frame, input_frames - input_frame });
        if (copy_frames <= 0)
            break;

        if (write_frame == 0) {
            std::copy_n(&input[input_frame * port.channels], static_cast<size_t>(copy_frames) * port.channels, output.data());
            write_frame += copy_frames;
            continue;
        }

        const int overlap = std::min({ overlap_frames, write_frame, copy_frames });
        for (int frame = 0; frame < overlap; frame++) {
            const int dst_frame = write_frame - overlap + frame;
            const int src_frame = input_frame + frame;
            for (int channel = 0; channel < port.channels; channel++) {
                const int dst = dst_frame * port.channels + channel;
                output[dst] = blend_sample(output[dst], input[src_frame * port.channels + channel], frame, overlap);
            }
        }

        const int append_frames = copy_frames - overlap;
        if (append_frames > 0) {
            std::copy_n(
                &input[(input_frame + overlap) * port.channels],
                static_cast<size_t>(append_frames) * port.channels,
                &output[write_frame * port.channels]);
            write_frame += append_frames;
        } else {
            write_frame++;
        }
    }

    bytes_to_write = std::max(1, write_frame) * port.channels * static_cast<int>(sizeof(int16_t));
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
    const void *audio_data = pitch_correct_fast_forward(port, buffer, bytes_to_write, speed_percent);
    SDL_CHECK_VOID(SDL_PutAudioStreamData(port.stream.get(), audio_data, bytes_to_write));
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
