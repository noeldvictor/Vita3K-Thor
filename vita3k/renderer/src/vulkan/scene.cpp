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

#include <renderer/vulkan/functions.h>

#include <gxm/functions.h>
#include <renderer/vulkan/gxm_to_vulkan.h>

#include <config/state.h>
#include <spdlog/fmt/bin_to_hex.h>

#include <util/float_to_half.h>
#include <util/log.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace renderer::vulkan {

struct RendererDebugRange {
    bool enabled = false;
    bool has_frame = false;
    bool has_scene = false;
    bool has_rt = false;
    bool has_color_addr = false;
    bool has_texture_addr = false;
    bool has_vhash = false;
    bool has_fhash = false;
    uint64_t frame = 0;
    uint64_t scene = 0;
    uint32_t rt_width = 0;
    uint32_t rt_height = 0;
    uint32_t first_draw = 0;
    uint32_t last_draw = 0;
    std::string vhash_prefix;
    std::string fhash_prefix;
    std::string color_addr_prefix;
    std::string texture_addr_prefix;
    std::string spec;
};

struct RendererDebugConfig {
    bool labels = false;
    bool trace = false;
    uint32_t trace_limit = 32;
    RendererDebugRange skip;
    RendererDebugRange stop_after;
    RendererDebugRange dump;
};

static bool renderer_debug_env_flag(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return false;

    const std::string_view text(value);
    return text != "0" && text != "false" && text != "False" && text != "FALSE" && text != "off" && text != "OFF" && text != "no" && text != "NO";
}

static bool parse_debug_bool(std::string_view text) {
    return text == "1" || text == "true" || text == "True" || text == "TRUE" || text == "on" || text == "ON" || text == "yes" || text == "YES";
}

static bool parse_u64_at(const char *begin, uint64_t &value, const char **end_out = nullptr) {
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(begin, &end, 10);
    if (begin == end)
        return false;

    value = static_cast<uint64_t>(parsed);
    if (end_out != nullptr)
        *end_out = end;
    return true;
}

static bool parse_u64_key(const std::string &spec, const char *key, uint64_t &value) {
    const size_t pos = spec.find(key);
    if (pos == std::string::npos)
        return false;

    return parse_u64_at(spec.c_str() + pos + std::strlen(key), value);
}

static std::string parse_text_key(const std::string &spec, const char *key) {
    const size_t pos = spec.find(key);
    if (pos == std::string::npos)
        return {};

    const size_t value_begin = pos + std::strlen(key);
    size_t value_end = spec.find(':', value_begin);
    if (value_end == std::string::npos)
        value_end = spec.size();

    return spec.substr(value_begin, value_end - value_begin);
}

static std::string normalize_renderer_debug_hex_prefix(std::string text) {
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text.erase(0, 2);

    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
        return ch == '_' || ch == ' ';
    }),
        text.end());

    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    return text;
}

static uint32_t clamp_draw_index(uint64_t value) {
    return static_cast<uint32_t>(std::min<uint64_t>(value, std::numeric_limits<uint32_t>::max()));
}

static RendererDebugRange parse_renderer_debug_range_spec(const std::string &spec) {
    RendererDebugRange range;
    if (spec.empty())
        return range;
    range.spec = spec;

    uint64_t parsed = 0;
    if (parse_u64_key(spec, "frame=", parsed)) {
        range.has_frame = true;
        range.frame = parsed;
    }
    if (parse_u64_key(spec, "scene=", parsed)) {
        range.has_scene = true;
        range.scene = parsed;
    }

    const std::string rt = parse_text_key(spec, "rt=");
    if (!rt.empty()) {
        const size_t separator = rt.find('x');
        if (separator != std::string::npos) {
            uint64_t parsed_width = 0;
            uint64_t parsed_height = 0;
            if (parse_u64_at(rt.c_str(), parsed_width) && parse_u64_at(rt.c_str() + separator + 1, parsed_height)) {
                range.has_rt = true;
                range.rt_width = clamp_draw_index(parsed_width);
                range.rt_height = clamp_draw_index(parsed_height);
            }
        }
    }

    range.vhash_prefix = parse_text_key(spec, "vhash=");
    range.has_vhash = !range.vhash_prefix.empty();
    range.fhash_prefix = parse_text_key(spec, "fhash=");
    range.has_fhash = !range.fhash_prefix.empty();
    range.color_addr_prefix = normalize_renderer_debug_hex_prefix(parse_text_key(spec, "addr="));
    if (range.color_addr_prefix.empty())
        range.color_addr_prefix = normalize_renderer_debug_hex_prefix(parse_text_key(spec, "color="));
    if (range.color_addr_prefix.empty())
        range.color_addr_prefix = normalize_renderer_debug_hex_prefix(parse_text_key(spec, "color_addr="));
    range.has_color_addr = !range.color_addr_prefix.empty();
    range.texture_addr_prefix = normalize_renderer_debug_hex_prefix(parse_text_key(spec, "tex="));
    if (range.texture_addr_prefix.empty())
        range.texture_addr_prefix = normalize_renderer_debug_hex_prefix(parse_text_key(spec, "texture="));
    if (range.texture_addr_prefix.empty())
        range.texture_addr_prefix = normalize_renderer_debug_hex_prefix(parse_text_key(spec, "sample="));
    if (range.texture_addr_prefix.empty())
        range.texture_addr_prefix = normalize_renderer_debug_hex_prefix(parse_text_key(spec, "sample_addr="));
    range.has_texture_addr = !range.texture_addr_prefix.empty();

    const size_t draw_pos = spec.find("draw=");
    const char *draw_begin = draw_pos == std::string::npos ? spec.c_str() : spec.c_str() + draw_pos + 5;
    const char *draw_end = nullptr;
    if (!parse_u64_at(draw_begin, parsed, &draw_end))
        return range;

    range.first_draw = clamp_draw_index(parsed);
    range.last_draw = range.first_draw;
    if (draw_end != nullptr && *draw_end == '-') {
        uint64_t parsed_last = 0;
        if (parse_u64_at(draw_end + 1, parsed_last))
            range.last_draw = clamp_draw_index(parsed_last);
    }

    if (range.first_draw > range.last_draw)
        std::swap(range.first_draw, range.last_draw);

    range.enabled = true;
    return range;
}

static RendererDebugRange parse_renderer_debug_range_env(const char *name) {
    const char *value = std::getenv(name);
    return value == nullptr ? RendererDebugRange{} : parse_renderer_debug_range_spec(value);
}

static uint32_t renderer_debug_trace_limit() {
    const char *value = std::getenv("VITA3K_RENDER_TRACE_LIMIT");
    if (value == nullptr || value[0] == '\0')
        return 32;

    uint64_t parsed = 0;
    if (!parse_u64_at(value, parsed) || parsed == 0)
        return 32;

    return clamp_draw_index(parsed);
}

static bool parse_debug_config_line(RendererDebugConfig &cfg, const std::string &line) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos)
        return false;

    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "labels") {
        cfg.labels = parse_debug_bool(value);
    } else if (key == "trace") {
        cfg.trace = parse_debug_bool(value);
    } else if (key == "trace_limit") {
        uint64_t parsed = 0;
        if (parse_u64_at(value.c_str(), parsed) && parsed != 0)
            cfg.trace_limit = clamp_draw_index(parsed);
    } else if (key == "skip") {
        cfg.skip = parse_renderer_debug_range_spec(value);
    } else if (key == "stop_after") {
        cfg.stop_after = parse_renderer_debug_range_spec(value);
    } else if (key == "dump") {
        cfg.dump = parse_renderer_debug_range_spec(value);
    } else {
        return false;
    }
    return true;
}

static void poll_renderer_debug_control_file(RendererDebugConfig &cfg) {
    const char *path = std::getenv("VITA3K_RENDER_CONTROL_FILE");
    if (path == nullptr || path[0] == '\0')
        return;

    static uint64_t poll_counter = 0;
    if ((poll_counter++ % 128) != 0)
        return;

    std::ifstream file(path, std::ios::in);
    if (!file.is_open())
        return;

    RendererDebugConfig next = cfg;
    std::string line;
    while (std::getline(file, line)) {
        const size_t comment = line.find('#');
        if (comment != std::string::npos)
            line.resize(comment);
        line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r';
        }),
            line.end());
        if (!line.empty())
            parse_debug_config_line(next, line);
    }

    if (next.labels != cfg.labels || next.trace != cfg.trace || next.trace_limit != cfg.trace_limit
        || next.skip.spec != cfg.skip.spec || next.stop_after.spec != cfg.stop_after.spec || next.dump.spec != cfg.dump.spec) {
        LOG_INFO("ThorRenderDebug live-control labels={} trace={} trace_limit={} skip={} stop_after={} dump={}",
            next.labels,
            next.trace,
            next.trace_limit,
            next.skip.spec,
            next.stop_after.spec,
            next.dump.spec);
    }
    cfg = next;
}

#ifdef __ANDROID__
static std::string android_debug_property(const char *name) {
    char value[PROP_VALUE_MAX] = {};
    if (__system_property_get(name, value) <= 0)
        return {};

    return value;
}

static bool parse_disabled_debug_value(const std::string &value) {
    return value.empty() || value == "0" || value == "false" || value == "False" || value == "FALSE" || value == "off" || value == "OFF" || value == "no" || value == "NO";
}

static void poll_renderer_debug_android_properties(RendererDebugConfig &cfg) {
    static uint64_t poll_counter = 0;
    if ((poll_counter++ % 128) != 0)
        return;

    RendererDebugConfig next = cfg;

    const std::string labels = android_debug_property("debug.vita3k.render_labels");
    if (!labels.empty())
        next.labels = parse_debug_bool(labels);

    const std::string trace = android_debug_property("debug.vita3k.render_trace");
    if (!trace.empty())
        next.trace = parse_debug_bool(trace);

    const std::string trace_limit = android_debug_property("debug.vita3k.render_trace_limit");
    if (!trace_limit.empty()) {
        uint64_t parsed = 0;
        if (parse_u64_at(trace_limit.c_str(), parsed) && parsed != 0)
            next.trace_limit = clamp_draw_index(parsed);
    }

    const std::string skip = android_debug_property("debug.vita3k.render_skip");
    if (!skip.empty())
        next.skip = parse_disabled_debug_value(skip) ? RendererDebugRange{} : parse_renderer_debug_range_spec(skip);

    const std::string stop_after = android_debug_property("debug.vita3k.render_stop_after");
    if (!stop_after.empty())
        next.stop_after = parse_disabled_debug_value(stop_after) ? RendererDebugRange{} : parse_renderer_debug_range_spec(stop_after);

    const std::string dump = android_debug_property("debug.vita3k.render_dump");
    if (!dump.empty())
        next.dump = parse_disabled_debug_value(dump) ? RendererDebugRange{} : parse_renderer_debug_range_spec(dump);

    if (next.labels != cfg.labels || next.trace != cfg.trace || next.trace_limit != cfg.trace_limit
        || next.skip.spec != cfg.skip.spec || next.stop_after.spec != cfg.stop_after.spec || next.dump.spec != cfg.dump.spec) {
        LOG_INFO("ThorRenderDebug android-props labels={} trace={} trace_limit={} skip={} stop_after={} dump={}",
            next.labels,
            next.trace,
            next.trace_limit,
            next.skip.spec,
            next.stop_after.spec,
            next.dump.spec);
    }

    cfg = next;
}
#endif

static std::string renderer_debug_setting(const char *env_name, const char *android_prop_name) {
    const char *env_value = std::getenv(env_name);
    if (env_value != nullptr && env_value[0] != '\0')
        return env_value;

#ifdef __ANDROID__
    return android_debug_property(android_prop_name);
#else
    (void)android_prop_name;
    return {};
#endif
}

static bool renderer_debug_setting_disabled(std::string_view value) {
    return value.empty() || value == "0" || value == "false" || value == "FALSE" || value == "off" || value == "OFF";
}

static RendererDebugConfig &renderer_debug_config() {
    static RendererDebugConfig config = [] {
        RendererDebugConfig cfg;
        cfg.labels = renderer_debug_env_flag("VITA3K_RENDER_DEBUG") || renderer_debug_env_flag("VITA3K_RENDER_LABELS");
        cfg.trace = renderer_debug_env_flag("VITA3K_RENDER_DEBUG") || renderer_debug_env_flag("VITA3K_RENDER_TRACE");
        cfg.trace_limit = renderer_debug_trace_limit();
        cfg.skip = parse_renderer_debug_range_env("VITA3K_RENDER_SKIP");
        cfg.stop_after = parse_renderer_debug_range_env("VITA3K_RENDER_STOP_AFTER");
        cfg.dump = parse_renderer_debug_range_env("VITA3K_RENDER_DUMP");

        if (cfg.labels || cfg.trace || cfg.skip.enabled || cfg.stop_after.enabled || cfg.dump.enabled || cfg.trace_limit != 32) {
            LOG_INFO("ThorRenderDebug labels={} trace={} trace_limit={} skip={} stop_after={} dump={}",
                cfg.labels,
                cfg.trace,
                cfg.trace_limit,
                cfg.skip.enabled ? std::getenv("VITA3K_RENDER_SKIP") : "",
                cfg.stop_after.enabled ? std::getenv("VITA3K_RENDER_STOP_AFTER") : "",
                cfg.dump.enabled ? std::getenv("VITA3K_RENDER_DUMP") : "");
        }
        return cfg;
    }();
    poll_renderer_debug_control_file(config);
#ifdef __ANDROID__
    poll_renderer_debug_android_properties(config);
#endif
    return config;
}

static bool renderer_debug_hash_prefix_matches(const std::string &hash, const std::string &prefix) {
    if (renderer_debug_setting_disabled(prefix))
        return false;

    return hash.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), hash.begin());
}

static bool renderer_debug_hash_prefix_or_all_matches(const Sha256Hash &hash, const std::string &value) {
    if (renderer_debug_setting_disabled(value))
        return false;

    if (value == "1" || value == "all" || value == "ALL")
        return true;

    return renderer_debug_hash_prefix_matches(hex_string(hash), value);
}

static bool renderer_debug_color_addr_prefix_matches(uint32_t color_addr, const std::string &prefix) {
    const std::string color_addr_text = fmt::format("{:08X}", color_addr);
    return color_addr_text.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), color_addr_text.begin());
}

static bool renderer_debug_texture_addr_prefix_matches(const SceGxmTexture *textures, const bool *valid, uint16_t texture_count, const std::string &prefix) {
    for (uint16_t texture_index = 0; texture_index < texture_count && texture_index < SCE_GXM_MAX_TEXTURE_UNITS; texture_index++) {
        if (!valid[texture_index])
            continue;
        if (renderer_debug_color_addr_prefix_matches(static_cast<uint32_t>(textures[texture_index].data_addr << 2), prefix))
            return true;
    }
    return false;
}

static bool renderer_debug_texture_addr_prefix_matches(const RendererDebugRange &range,
    const SceGxmTexture *vertex_textures, const bool *vertex_valid, uint16_t vertex_texture_count,
    const SceGxmTexture *fragment_textures, const bool *fragment_valid, uint16_t fragment_texture_count) {
    if (!range.has_texture_addr)
        return true;

    return renderer_debug_texture_addr_prefix_matches(vertex_textures, vertex_valid, vertex_texture_count, range.texture_addr_prefix)
        || renderer_debug_texture_addr_prefix_matches(fragment_textures, fragment_valid, fragment_texture_count, range.texture_addr_prefix);
}

static std::string renderer_debug_texture_addr_summary(const SceGxmTexture *textures, const bool *valid, uint16_t texture_count) {
    std::string summary;
    for (uint16_t texture_index = 0; texture_index < texture_count && texture_index < SCE_GXM_MAX_TEXTURE_UNITS; texture_index++) {
        if (!summary.empty())
            summary += ",";
        if (!valid[texture_index]) {
            summary += "invalid";
            continue;
        }
        summary += fmt::format("0x{:08X}", static_cast<uint32_t>(textures[texture_index].data_addr << 2));
    }
    return summary.empty() ? "-" : summary;
}

static bool renderer_debug_range_matches(const RendererDebugRange &range, uint64_t frame, uint64_t scene, uint32_t rt_width, uint32_t rt_height, uint32_t color_addr,
    const SceGxmTexture *vertex_textures, const bool *vertex_valid, uint16_t vertex_texture_count,
    const SceGxmTexture *fragment_textures, const bool *fragment_valid, uint16_t fragment_texture_count,
    const std::string &vhash, const std::string &fhash, uint32_t draw) {
    if (!range.enabled)
        return false;
    if (range.has_frame && range.frame != frame)
        return false;
    if (range.has_scene && range.scene != scene)
        return false;
    if (range.has_rt && (range.rt_width != rt_width || range.rt_height != rt_height))
        return false;
    if (range.has_color_addr && !renderer_debug_color_addr_prefix_matches(color_addr, range.color_addr_prefix))
        return false;
    if (!renderer_debug_texture_addr_prefix_matches(range, vertex_textures, vertex_valid, vertex_texture_count, fragment_textures, fragment_valid, fragment_texture_count))
        return false;
    if (range.has_vhash && !renderer_debug_hash_prefix_matches(vhash, range.vhash_prefix))
        return false;
    if (range.has_fhash && !renderer_debug_hash_prefix_matches(fhash, range.fhash_prefix))
        return false;
    return draw >= range.first_draw && draw <= range.last_draw;
}

static bool renderer_debug_stop_after_matches(const RendererDebugRange &range, uint64_t frame, uint64_t scene, uint32_t rt_width, uint32_t rt_height, uint32_t color_addr,
    const SceGxmTexture *vertex_textures, const bool *vertex_valid, uint16_t vertex_texture_count,
    const SceGxmTexture *fragment_textures, const bool *fragment_valid, uint16_t fragment_texture_count,
    const std::string &vhash, const std::string &fhash, uint32_t draw) {
    if (!range.enabled)
        return false;
    if (range.has_frame && range.frame != frame)
        return false;
    if (range.has_scene && range.scene != scene)
        return false;
    if (range.has_rt && (range.rt_width != rt_width || range.rt_height != rt_height))
        return false;
    if (range.has_color_addr && !renderer_debug_color_addr_prefix_matches(color_addr, range.color_addr_prefix))
        return false;
    if (!renderer_debug_texture_addr_prefix_matches(range, vertex_textures, vertex_valid, vertex_texture_count, fragment_textures, fragment_valid, fragment_texture_count))
        return false;
    if (range.has_vhash && !renderer_debug_hash_prefix_matches(vhash, range.vhash_prefix))
        return false;
    if (range.has_fhash && !renderer_debug_hash_prefix_matches(fhash, range.fhash_prefix))
        return false;
    return draw > range.last_draw;
}

static void insert_renderer_debug_label(VKContext &context, const std::string &label, const std::array<float, 4> &color) {
    const RendererDebugConfig &debug = renderer_debug_config();
    if (!debug.labels)
        return;

    static bool logged_missing_support = false;
    if (!context.state.support_debug_utils_labels) {
        if (!logged_missing_support) {
            LOG_WARN("VITA3K_RENDER_LABELS requested, but VK_EXT_debug_utils labels are not available in this Vulkan instance");
            logged_missing_support = true;
        }
        return;
    }

    vk::DebugUtilsLabelEXT label_info{
        .pLabelName = label.c_str()
    };
    label_info.color[0] = color[0];
    label_info.color[1] = color[1];
    label_info.color[2] = color[2];
    label_info.color[3] = color[3];
    context.render_cmd.insertDebugUtilsLabelEXT(label_info);
}

void set_uniform_buffer(VKContext &context, MemState &mem, const ShaderProgram *program, const bool vertex_shader, const int block_num, const int size, Ptr<uint8_t> data) {
    auto offset = program->uniform_buffer_data_offsets.at(block_num);
    if (offset == static_cast<std::uint32_t>(-1)) {
        return;
    }

    const uint32_t data_size_upload = std::min<uint32_t>(size, program->uniform_buffer_sizes.at(block_num) * 4);
    if (block_num >= 0 && block_num < SCE_GXM_REAL_MAX_UNIFORM_BUFFER) {
        uint32_t *addresses = vertex_shader ? context.vertex_uniform_addresses : context.fragment_uniform_addresses;
        uint32_t *sizes = vertex_shader ? context.vertex_uniform_sizes : context.fragment_uniform_sizes;
        bool *valid = vertex_shader ? context.vertex_uniform_valid : context.fragment_uniform_valid;
        addresses[block_num] = data.address();
        sizes[block_num] = data_size_upload;
        valid[block_num] = true;
    }

    if (context.state.features.enable_memory_mapping) {
        if (context.state.mapping_method == MappingMethod::DoubleBuffer) {
            // we must always cover everything as some small part of the buffer may get changed only
            context.state.buffer_trapping.access_buffer(data.address(), data_size_upload, mem, false, true);
        }

        const uint64_t buffer_address = context.state.get_matching_device_address(data.address());
        if (vertex_shader) {
            context.curr_vert_ublock.set_buffer_address(block_num, buffer_address);
        } else {
            context.curr_frag_ublock.set_buffer_address(block_num, buffer_address);
        }
    } else {
        const uint32_t offset_start_upload = offset * 4;

        if (vertex_shader) {
            if (!context.vertex_uniform_storage_allocated) {
                // Allocate a region for it. Don't worry though, when the shader program is changed
                context.vertex_uniform_stream_ring_buffer.allocate(program->max_total_uniform_buffer_storage * 4);
                context.vertex_uniform_storage_allocated = true;
            }

            context.vertex_uniform_stream_ring_buffer.copy(context.prerender_cmd, data_size_upload, data.get(mem), offset_start_upload);
        } else {
            if (!context.fragment_uniform_storage_allocated) {
                // Allocate a region for it. Don't worry though, when the shader program is changed
                context.fragment_uniform_stream_ring_buffer.allocate(program->max_total_uniform_buffer_storage * 4);
                context.fragment_uniform_storage_allocated = true;
            }

            context.fragment_uniform_stream_ring_buffer.copy(context.prerender_cmd, data_size_upload, data.get(mem), offset_start_upload);
        }
    }
}

void mid_scene_flush(VKContext &context, const SceGxmNotification notification) {
    // two cases :
    // notification.addr is 0: this means that the mid scene flush must be used as a barrier in the renderpass
    // notification.addr is not 0: this means the app is waiting for this part to be finished to re-use the resources

    // Note: however, when testing, the barrier inside a pipeline does not work (or not entirely, depending on the GPU)
    // maybe because I'm writing using buffer device addresses, not sure...
    // so for the time being always restart the render pass
    // const bool restart_render_pass = notification.address.address() != 0;
    const bool restart_render_pass = true;

    if (restart_render_pass && context.in_renderpass)
        context.stop_render_pass();

    // in case there is no notification, this will happen in the render pass
    vk::MemoryBarrier barrier{
        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
        .dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead,
    };
    context.render_cmd.pipelineBarrier(vk::PipelineStageFlagBits::eVertexShader, vk::PipelineStageFlagBits::eVertexInput,
        vk::DependencyFlags(), barrier, {}, {});

    if (restart_render_pass) {
        SceGxmNotification empty_notification = { Ptr<uint32_t>(0), 0 };
        const bool submit = notification.address.address() != 0;
        context.stop_recording(notification, empty_notification, submit);
        context.start_recording();
        context.scene_timestamp++;
    }
}

// restride vertex attribute binding strides to multiple of 4
// needed for Metal because it only allows multiples of 4; also useful as an
// Android debug path for odd Vita vertex strides.
static void restride_stream(const uint8_t *stream, uint32_t size, uint32_t stride, std::vector<uint8_t> &out) {
    const uint32_t new_stride = align(stride, 4);
    const uint32_t nb_vertex_input = ((size + stride - 1) / stride);

    out.assign(nb_vertex_input * new_stride, 0);
    for (uint32_t i = 0; i < nb_vertex_input; i++) {
        const uint32_t src_offset = stride * i;
        if (src_offset >= size)
            break;
        const uint32_t copy_size = std::min<uint32_t>(stride, size - src_offset);
        memcpy(out.data() + new_stride * i, stream + src_offset, copy_size);
    }
}

// when needed, how many descriptor of the given size we allocate for each frame at once
static constexpr uint32_t DESCRIPTOR_PACK_SIZE = 64;

static vk::DescriptorSet retrieve_descriptor(VKContext &context, bool is_vertex, uint16_t textures_count) {
    if (textures_count == 0)
        return context.empty_set;

    VKState &state = context.state;
    FrameDescriptor &frame_descriptor = is_vertex ? state.frame().vert_descriptors[textures_count - 1] : state.frame().frag_descriptors[textures_count - 1];
    if (frame_descriptor.descriptors_idx < frame_descriptor.sets.size())
        return frame_descriptor.sets[frame_descriptor.descriptors_idx++];

    // we have no more frame descriptor available, create a bunch of new one for this specific layout
    vk::DescriptorPoolSize pool_size{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = textures_count * DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING
    };

    vk::DescriptorPoolCreateInfo descriptor_pool_info{
        .maxSets = DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING
    };
    descriptor_pool_info.setPoolSizes(pool_size);

    vk::DescriptorPool descriptor_pool = state.device.createDescriptorPool(descriptor_pool_info);
    state.frame_descriptor_pools.push_back(descriptor_pool);

    // allocate all the descriptor sets
    const vk::DescriptorSetLayout set_layout = is_vertex ? state.pipeline_cache.vertex_textures_layout[textures_count] : state.pipeline_cache.fragment_textures_layout[textures_count];
    std::vector<vk::DescriptorSetLayout> layouts(DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING, set_layout);
    vk::DescriptorSetAllocateInfo descr_set_info{
        .descriptorPool = descriptor_pool
    };
    descr_set_info.setSetLayouts(layouts);
    auto descriptor_sets = state.device.allocateDescriptorSets(descr_set_info);

    // distribute them among all frames
    for (int frame_idx = 0; frame_idx < MAX_FRAMES_RENDERING; frame_idx++) {
        FrameObject &frame_object = state.frames[frame_idx];
        FrameDescriptor &frame_descr = is_vertex ? frame_object.vert_descriptors[textures_count - 1] : frame_object.frag_descriptors[textures_count - 1];

        // insert DESCRIPTOR_PACK_SIZE in each frame descriptor
        auto descr_it = descriptor_sets.begin() + frame_idx * DESCRIPTOR_PACK_SIZE;
        frame_descr.sets.insert(frame_descr.sets.end(), descr_it, descr_it + DESCRIPTOR_PACK_SIZE);
    }

    return frame_descriptor.sets[frame_descriptor.descriptors_idx++];
}

static void draw_bind_descriptors(VKContext &context, MemState &mem) {
    VKState &state = context.state;

    std::array<vk::DescriptorSet, 4> descriptors;
    descriptors[0] = context.global_set;
    descriptors[1] = context.rendertarget_set;

    const uint16_t vertex_textures_count = context.record.vertex_program.get(mem)->renderer_data->texture_count;
    const uint16_t fragment_texture_count = context.record.fragment_program.get(mem)->renderer_data->texture_count;

    vk::PipelineLayout pipeline_layout = state.pipeline_cache.pipeline_layouts[vertex_textures_count][fragment_texture_count];

    // try to use last descriptor if it still matches
    bool need_vert_descr = (vertex_textures_count != context.last_vert_texture_count);
    bool need_frag_descr = (fragment_texture_count != context.last_frag_texture_count);

    context.last_vert_texture_count = vertex_textures_count;
    context.last_frag_texture_count = fragment_texture_count;

    {
        if (need_vert_descr) {
            context.last_vert_texture_descriptor = retrieve_descriptor(context, true, vertex_textures_count);
        }
        descriptors[2] = context.last_vert_texture_descriptor;

        if (need_frag_descr) {
            context.last_frag_texture_descriptor = retrieve_descriptor(context, false, fragment_texture_count);
        }
        descriptors[3] = context.last_frag_texture_descriptor;
    }

    // bind textures
    std::array<vk::WriteDescriptorSet, 16> write_descrs;
    // some default sampler in case a slot has never been set and we read a slot with higher idx
    vk::DescriptorImageInfo default_image_info{
        .sampler = context.state.default_image.sampler,
        .imageView = context.state.default_image.view,
        .imageLayout = vk::ImageLayout::eGeneral
    };

    // vertex
    if (need_vert_descr) {
        for (uint32_t i = 0; i < vertex_textures_count; i++) {
            write_descrs[i] = vk::WriteDescriptorSet{
                .dstSet = descriptors[2],
                .dstBinding = i,
                .dstArrayElement = 0,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            };
            write_descrs[i].setImageInfo(context.vertex_textures[i].sampler ? context.vertex_textures[i] : default_image_info);
        }
        state.device.updateDescriptorSets(vertex_textures_count, write_descrs.data(), 0, nullptr);
    }

    // fragment
    if (need_frag_descr) {
        for (uint32_t i = 0; i < fragment_texture_count; i++) {
            write_descrs[i] = vk::WriteDescriptorSet{
                .dstSet = descriptors[3],
                .dstBinding = i,
                .dstArrayElement = 0,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            };
            write_descrs[i].setImageInfo(context.fragment_textures[i].sampler ? context.fragment_textures[i] : default_image_info);
        }
        state.device.updateDescriptorSets(fragment_texture_count, write_descrs.data(), 0, nullptr);
    }

    const uint32_t dynamic_offset_count = state.features.enable_memory_mapping ? 2U : 4U;
    const uint32_t dynamic_offsets[] = {
        // GXMRenderVertUniformBlock
        context.vertex_info_uniform_buffer.data_offset,
        // GXMRenderFragUniformBlock
        context.fragment_info_uniform_buffer.data_offset,
        // vertex ssbo
        context.vertex_uniform_stream_ring_buffer.data_offset,
        // fragment ssbo
        context.fragment_uniform_stream_ring_buffer.data_offset
    };

    context.render_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0,
        descriptors.size(), descriptors.data(), dynamic_offset_count, dynamic_offsets);
}

static size_t get_vulkan_attribute_byte_size(const SceGxmVertexAttribute &attribute, const shader::usse::AttributeInformation &info) {
    SceGxmAttributeFormat attribute_format = static_cast<SceGxmAttributeFormat>(attribute.format);
    uint8_t component_count = attribute.componentCount;

    if (info.regformat) {
        component_count = info.component_count;
        switch (info.gxm_type) {
        case SCE_GXM_PARAMETER_TYPE_U8:
        case SCE_GXM_PARAMETER_TYPE_S8:
        case SCE_GXM_PARAMETER_TYPE_C10:
            attribute_format = SCE_GXM_ATTRIBUTE_FORMAT_U8;
            break;
        case SCE_GXM_PARAMETER_TYPE_U16:
        case SCE_GXM_PARAMETER_TYPE_S16:
        case SCE_GXM_PARAMETER_TYPE_F16:
            attribute_format = SCE_GXM_ATTRIBUTE_FORMAT_U16;
            break;
        default:
            attribute_format = SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED;
            break;
        }

        if (info.gxm_type == SCE_GXM_PARAMETER_TYPE_C10)
            component_count = (component_count * 10 + 7) / 8;

        if (component_count > 4) {
            const uint32_t array_size = (component_count + 3) / 4;
            return array_size * 4 * gxm::attribute_format_size(attribute_format);
        }
    }

    return gxm::attribute_format_size(attribute_format) * component_count;
}

static uint32_t get_renderer_debug_max_index(Ptr<void> indices, size_t count, SceGxmIndexFormat format, MemState &mem) {
    if (count == 0)
        return 0;

    if (format == SCE_GXM_INDEX_FORMAT_U16) {
        const uint16_t *data = indices.cast<uint16_t>().get(mem);
        return *std::max_element(&data[0], &data[count]);
    }

    const uint32_t *data = indices.cast<uint32_t>().get(mem);
    return *std::max_element(&data[0], &data[count]);
}

static int get_used_vertex_stream_count(const SceGxmVertexProgram &vertex_program, const VertexProgram &vkvert) {
    int max_stream_idx = -1;

    for (const SceGxmVertexAttribute &attribute : vertex_program.attributes) {
        if (!vkvert.attribute_infos.contains(attribute.regIndex))
            continue;
        max_stream_idx = std::max<int>(max_stream_idx, attribute.streamIndex);
    }

    return max_stream_idx + 1;
}

static std::array<size_t, SCE_GXM_MAX_VERTEX_STREAMS> get_required_vertex_stream_sizes(const SceGxmVertexProgram &vertex_program, const VertexProgram &vkvert, uint32_t instance_count, uint32_t max_index) {
    std::array<size_t, SCE_GXM_MAX_VERTEX_STREAMS> required_sizes{};

    for (const SceGxmVertexAttribute &attribute : vertex_program.attributes) {
        if (!vkvert.attribute_infos.contains(attribute.regIndex))
            continue;

        const size_t attribute_size = get_vulkan_attribute_byte_size(attribute, vkvert.attribute_infos.at(attribute.regIndex));
        const SceGxmVertexStream &stream = vertex_program.streams[attribute.streamIndex];
        const SceGxmIndexSource index_source = static_cast<SceGxmIndexSource>(stream.indexSource);
        const size_t data_passed_length = gxm::is_stream_instancing(index_source) ? ((instance_count - 1) * stream.stride) : (max_index * stream.stride);
        const size_t data_length = attribute.offset + data_passed_length + attribute_size;
        required_sizes[attribute.streamIndex] = std::max(required_sizes[attribute.streamIndex], data_length);
    }

    return required_sizes;
}

static void log_renderer_debug_textures(VKContext &context, const char *stage, const SceGxmTexture *textures, const bool *valid, uint16_t texture_count, uint32_t debug_draw_index) {
    for (uint16_t texture_index = 0; texture_index < texture_count && texture_index < SCE_GXM_MAX_TEXTURE_UNITS; texture_index++) {
        if (!valid[texture_index]) {
            LOG_INFO("ThorRenderDump texture frame={} scene={} draw={} stage={} slot={} valid=0",
                context.frame_timestamp,
                context.scene_timestamp,
                debug_draw_index,
                stage,
                texture_index);
            continue;
        }

        const SceGxmTexture &texture = textures[texture_index];
        const SceGxmTextureFormat format = gxm::get_format(texture);
        const SceGxmTextureBaseFormat base_format = gxm::get_base_format(format);
        const uint32_t stride = texture.texture_type() == SCE_GXM_TEXTURE_LINEAR_STRIDED ? gxm::get_stride_in_bytes(texture) : 0;
        LOG_INFO("ThorRenderDump texture frame={} scene={} draw={} stage={} slot={} valid=1 addr=0x{:08X} type={} fmt=0x{:08X} base=0x{:08X} size={}x{} stride={} mips={} palette=0x{:08X} filters={}/{} mip_filter={} addr_mode={}/{} gamma={} normalize={}",
            context.frame_timestamp,
            context.scene_timestamp,
            debug_draw_index,
            stage,
            texture_index,
            static_cast<uint32_t>(texture.data_addr << 2),
            static_cast<uint32_t>(texture.texture_type()),
            static_cast<uint32_t>(format),
            static_cast<uint32_t>(base_format),
            gxm::get_width(texture),
            gxm::get_height(texture),
            stride,
            texture.true_mip_count(),
            static_cast<uint32_t>(texture.palette_addr << 6),
            static_cast<uint32_t>(texture.mag_filter),
            static_cast<uint32_t>(texture.min_filter),
            static_cast<uint32_t>(texture.mip_filter),
            static_cast<uint32_t>(texture.uaddr_mode),
            static_cast<uint32_t>(texture.vaddr_mode),
            static_cast<uint32_t>(texture.gamma_mode),
            static_cast<uint32_t>(texture.normalize_mode));
    }
}

static uint32_t get_renderer_debug_index_value(const void *indices, size_t index, SceGxmIndexFormat format) {
    if (format == SCE_GXM_INDEX_FORMAT_U16)
        return reinterpret_cast<const uint16_t *>(indices)[index];
    return reinterpret_cast<const uint32_t *>(indices)[index];
}

template <typename T>
static T read_renderer_debug_unaligned(const uint8_t *data) {
    T value;
    std::memcpy(&value, data, sizeof(T));
    return value;
}

static bool read_renderer_debug_attribute_component(const uint8_t *data, SceGxmAttributeFormat format, uint8_t component, float &value) {
    switch (format) {
    case SCE_GXM_ATTRIBUTE_FORMAT_U8:
        value = static_cast<float>(read_renderer_debug_unaligned<uint8_t>(data + component));
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_S8:
        value = static_cast<float>(read_renderer_debug_unaligned<int8_t>(data + component));
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_U16:
        value = static_cast<float>(read_renderer_debug_unaligned<uint16_t>(data + component * sizeof(uint16_t)));
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_S16:
        value = static_cast<float>(read_renderer_debug_unaligned<int16_t>(data + component * sizeof(int16_t)));
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_U8N:
        value = static_cast<float>(read_renderer_debug_unaligned<uint8_t>(data + component)) / 255.0f;
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_S8N:
        value = std::max(-1.0f, static_cast<float>(read_renderer_debug_unaligned<int8_t>(data + component)) / 127.0f);
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_U16N:
        value = static_cast<float>(read_renderer_debug_unaligned<uint16_t>(data + component * sizeof(uint16_t))) / 65535.0f;
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_S16N:
        value = std::max(-1.0f, static_cast<float>(read_renderer_debug_unaligned<int16_t>(data + component * sizeof(int16_t))) / 32767.0f);
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_F16:
        value = util::decode_flt16<float>(read_renderer_debug_unaligned<uint16_t>(data + component * sizeof(uint16_t)));
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_F32:
        value = read_renderer_debug_unaligned<float>(data + component * sizeof(float));
        return true;
    case SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED:
        value = static_cast<float>(read_renderer_debug_unaligned<uint32_t>(data + component * sizeof(uint32_t)));
        return true;
    default:
        return false;
    }
}

static void log_renderer_debug_attribute_values(VKContext &context, MemState &mem, SceGxmIndexFormat index_format,
    Ptr<void> indices, size_t index_count, uint32_t debug_draw_index, const SceGxmVertexAttribute &attribute,
    const shader::usse::AttributeInformation &info, const SceGxmVertexStream &stream, size_t required_size) {
    const GXMStreamInfo &stream_info = context.record.vertex_streams[attribute.streamIndex];
    if (!stream_info.data || index_count == 0 || attribute.componentCount == 0)
        return;

    const uint8_t *stream_data = stream_info.data.get(mem);
    const void *indices_data = indices.get(mem);
    const uint8_t component_count = std::min<uint8_t>(attribute.componentCount, 4);
    std::array<float, 4> min_values = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
    std::array<float, 4> max_values = { -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    std::array<float, 4> first_values = {};
    std::array<uint32_t, 4> first_indices = {};
    uint32_t sampled = 0;
    uint32_t nonfinite = 0;
    uint32_t out_of_required_bounds = 0;
    uint32_t huge = 0;

    const size_t attr_size = get_vulkan_attribute_byte_size(attribute, info);
    for (size_t i = 0; i < index_count; i++) {
        const uint32_t vertex_index = get_renderer_debug_index_value(indices_data, i, index_format);
        const size_t vertex_offset = static_cast<size_t>(vertex_index) * stream.stride + attribute.offset;
        if (vertex_offset + attr_size > required_size) {
            out_of_required_bounds++;
            continue;
        }

        const uint8_t *attribute_data = stream_data + vertex_offset;
        for (uint8_t component = 0; component < component_count; component++) {
            float value = 0.0f;
            if (!read_renderer_debug_attribute_component(attribute_data, static_cast<SceGxmAttributeFormat>(attribute.format), component, value))
                continue;

            if (sampled == 0) {
                first_values[component] = value;
                first_indices[0] = vertex_index;
            } else if (sampled < first_indices.size()) {
                first_indices[sampled] = vertex_index;
            }

            if (!std::isfinite(value)) {
                nonfinite++;
                continue;
            }
            if (std::abs(value) > 100000.0f)
                huge++;
            min_values[component] = std::min(min_values[component], value);
            max_values[component] = std::max(max_values[component], value);
        }
        sampled++;
    }

    LOG_INFO("ThorRenderDump attr-values frame={} scene={} draw={} reg={} loc={} stream={} format={} components={} sampled={} first_indices={},{},{},{} first={},{},{},{} min={},{},{},{} max={},{},{},{} nonfinite={} huge={} out_of_required_bounds={}",
        context.frame_timestamp,
        context.scene_timestamp,
        debug_draw_index,
        attribute.regIndex,
        info.location,
        attribute.streamIndex,
        static_cast<uint32_t>(attribute.format),
        attribute.componentCount,
        sampled,
        first_indices[0],
        first_indices[1],
        first_indices[2],
        first_indices[3],
        first_values[0],
        first_values[1],
        first_values[2],
        first_values[3],
        min_values[0],
        min_values[1],
        min_values[2],
        min_values[3],
        max_values[0],
        max_values[1],
        max_values[2],
        max_values[3],
        nonfinite,
        huge,
        out_of_required_bounds);
}

static void log_renderer_debug_uniform_buffers(VKContext &context, MemState &mem, const char *stage,
    const ShaderProgram &program, const uint32_t *addresses, const uint32_t *sizes, const bool *valid,
    uint32_t debug_draw_index) {
    for (uint32_t block = 0; block < program.buffer_count && block < SCE_GXM_REAL_MAX_UNIFORM_BUFFER; block++) {
        const uint32_t declared_size = program.uniform_buffer_sizes[block] * 4;
        if (!valid[block]) {
            LOG_INFO("ThorRenderDump uniform frame={} scene={} draw={} stage={} block={} valid=0 declared_size={} offset_words={}",
                context.frame_timestamp,
                context.scene_timestamp,
                debug_draw_index,
                stage,
                block,
                declared_size,
                program.uniform_buffer_data_offsets[block]);
            continue;
        }

        const uint32_t data_size = std::min(sizes[block], declared_size);
        const uint8_t *data = Ptr<const uint8_t>(addresses[block]).get(mem);
        const uint32_t float_count = data_size / sizeof(float);
        std::array<float, 8> first_values = {};
        float min_value = std::numeric_limits<float>::infinity();
        float max_value = -std::numeric_limits<float>::infinity();
        uint32_t nonfinite = 0;
        uint32_t huge = 0;
        uint32_t nonzero_bytes = 0;

        for (uint32_t byte = 0; byte < data_size; byte++) {
            if (data[byte] != 0)
                nonzero_bytes++;
        }

        for (uint32_t index = 0; index < float_count; index++) {
            const float value = read_renderer_debug_unaligned<float>(data + index * sizeof(float));
            if (index < first_values.size())
                first_values[index] = value;
            if (!std::isfinite(value)) {
                nonfinite++;
                continue;
            }
            if (std::abs(value) > 100000.0f)
                huge++;
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }

        LOG_INFO("ThorRenderDump uniform frame={} scene={} draw={} stage={} block={} valid=1 addr=0x{:08X} size={} declared_size={} offset_words={} nonzero_bytes={} float_count={} first8={},{},{},{},{},{},{},{} min={} max={} nonfinite={} huge={}",
            context.frame_timestamp,
            context.scene_timestamp,
            debug_draw_index,
            stage,
            block,
            addresses[block],
            data_size,
            declared_size,
            program.uniform_buffer_data_offsets[block],
            nonzero_bytes,
            float_count,
            first_values[0],
            first_values[1],
            first_values[2],
            first_values[3],
            first_values[4],
            first_values[5],
            first_values[6],
            first_values[7],
            min_value,
            max_value,
            nonfinite,
            huge);
    }
}

static void log_renderer_debug_draw_dump(VKContext &context, MemState &mem, SceGxmPrimitiveType type, SceGxmIndexFormat format,
    Ptr<void> indices, size_t count, uint32_t instance_count, uint32_t max_index, uint32_t debug_draw_index,
    uint32_t debug_rt_width, uint32_t debug_rt_height, const std::string &hash_text_v, const std::string &hash_text_f) {
    const SceGxmVertexProgram &vertex_program = *context.record.vertex_program.get(mem);
    const SceGxmFragmentProgram &fragment_program = *context.record.fragment_program.get(mem);
    const SceGxmProgram &fragment_program_gxp = *fragment_program.program.get(mem);
    const VertexProgram &vkvert = *vertex_program.renderer_data.get();
    const auto &vertex_data = context.record.vertex_program.get(mem)->renderer_data;
    const auto &fragment_data = context.record.fragment_program.get(mem)->renderer_data;
    const VKFragmentProgram &vkfrag = *reinterpret_cast<const VKFragmentProgram *>(fragment_data.get());
    const std::array<size_t, SCE_GXM_MAX_VERTEX_STREAMS> required_sizes = get_required_vertex_stream_sizes(vertex_program, vkvert, instance_count, max_index);
    const int max_stream_idx = get_used_vertex_stream_count(vertex_program, vkvert);
    const SceGxmBlendInfo &blend_info = vkfrag.blend_info;
    const uint32_t vk_write_mask = (static_cast<bool>(vkfrag.blending.colorWriteMask & vk::ColorComponentFlagBits::eR) ? 1U : 0U)
        | (static_cast<bool>(vkfrag.blending.colorWriteMask & vk::ColorComponentFlagBits::eG) ? 2U : 0U)
        | (static_cast<bool>(vkfrag.blending.colorWriteMask & vk::ColorComponentFlagBits::eB) ? 4U : 0U)
        | (static_cast<bool>(vkfrag.blending.colorWriteMask & vk::ColorComponentFlagBits::eA) ? 8U : 0U);

    LOG_INFO("ThorRenderDump draw frame={} scene={} rt={}x{} draw={} prim={} index_fmt={} index_addr=0x{:08X} count={} max_index={} instances={} memory_mapping={} mapping={} vhash={} fhash={} streams={} attrs={} vtex={} ftex={} color_addr=0x{:08X} depth_addr=0x{:08X} stencil_addr=0x{:08X}",
        context.frame_timestamp,
        context.scene_timestamp,
        debug_rt_width,
        debug_rt_height,
        debug_draw_index,
        static_cast<uint32_t>(type),
        static_cast<uint32_t>(format),
        indices.address(),
        count,
        max_index,
        instance_count,
        context.state.features.enable_memory_mapping,
        static_cast<uint32_t>(context.state.mapping_method),
        hash_text_v,
        hash_text_f,
        max_stream_idx,
        vertex_program.attributes.size(),
        vertex_data->texture_count,
        fragment_data->texture_count,
        context.record.color_surface.data.address(),
        context.record.depth_stencil_surface.depth_data.address(),
        context.record.depth_stencil_surface.stencil_data.address());

    LOG_INFO("ThorRenderDump state frame={} scene={} draw={} depth_func={}/{} depth_write={}/{} depth_bias={}/{} stencil_func={}/{} cull={} two_sided={} vp_flat={} vp_flip={},{},{},{} z_offset={} z_scale={} writing_mask={}",
        context.frame_timestamp,
        context.scene_timestamp,
        debug_draw_index,
        static_cast<uint32_t>(context.record.front_depth_func),
        static_cast<uint32_t>(context.record.back_depth_func),
        static_cast<uint32_t>(context.record.front_depth_write_mode),
        static_cast<uint32_t>(context.record.back_depth_write_mode),
        context.record.depth_bias_unit,
        context.record.depth_bias_slope,
        static_cast<uint32_t>(context.record.front_stencil_state_op.func),
        static_cast<uint32_t>(context.record.back_stencil_state_op.func),
        static_cast<uint32_t>(context.record.cull_mode),
        static_cast<uint32_t>(context.record.two_sided),
        context.record.viewport_flat,
        context.record.viewport_flip[0],
        context.record.viewport_flip[1],
        context.record.viewport_flip[2],
        context.record.viewport_flip[3],
        context.record.z_offset,
        context.record.z_scale,
        context.record.writing_mask);

    LOG_INFO("ThorRenderDump fragment frame={} scene={} draw={} program_flags=0x{:08X} discard={} depth_replace={} sprite_coord={} native_color={} frag_color={} output_undefined={} no_effect={} maskupdate={} fp_mode={}/{} has_blend_info={} gxm_mask={} gxm_color_func={} gxm_alpha_func={} gxm_color_src={} gxm_color_dst={} gxm_alpha_src={} gxm_alpha_dst={} vk_blend={} vk_mask_rgba_bits={} vk_color_src={} vk_color_dst={} vk_color_op={} vk_alpha_src={} vk_alpha_dst={} vk_alpha_op={} blend_hash=0x{:016X}",
        context.frame_timestamp,
        context.scene_timestamp,
        debug_draw_index,
        fragment_program_gxp.program_flags,
        fragment_program_gxp.is_discard_used(),
        fragment_program_gxp.is_depth_replace_used(),
        fragment_program_gxp.is_sprite_coord_used(),
        fragment_program_gxp.is_native_color(),
        fragment_program_gxp.is_frag_color_used(),
        static_cast<bool>(fragment_program_gxp.program_flags & SCE_GXM_PROGRAM_FLAG_OUTPUT_UNDEFINED),
        fragment_program_gxp.has_no_effect(),
        fragment_program.is_maskupdate,
        static_cast<uint32_t>(context.record.front_side_fragment_program_mode),
        static_cast<uint32_t>(context.record.back_side_fragment_program_mode),
        vkfrag.has_blend_info,
        vkfrag.has_blend_info ? static_cast<uint32_t>(blend_info.colorMask) : 0,
        vkfrag.has_blend_info ? static_cast<uint32_t>(blend_info.colorFunc) : 0,
        vkfrag.has_blend_info ? static_cast<uint32_t>(blend_info.alphaFunc) : 0,
        vkfrag.has_blend_info ? static_cast<uint32_t>(blend_info.colorSrc) : 0,
        vkfrag.has_blend_info ? static_cast<uint32_t>(blend_info.colorDst) : 0,
        vkfrag.has_blend_info ? static_cast<uint32_t>(blend_info.alphaSrc) : 0,
        vkfrag.has_blend_info ? static_cast<uint32_t>(blend_info.alphaDst) : 0,
        static_cast<bool>(vkfrag.blending.blendEnable),
        vk_write_mask,
        static_cast<uint32_t>(vkfrag.blending.srcColorBlendFactor),
        static_cast<uint32_t>(vkfrag.blending.dstColorBlendFactor),
        static_cast<uint32_t>(vkfrag.blending.colorBlendOp),
        static_cast<uint32_t>(vkfrag.blending.srcAlphaBlendFactor),
        static_cast<uint32_t>(vkfrag.blending.dstAlphaBlendFactor),
        static_cast<uint32_t>(vkfrag.blending.alphaBlendOp),
        vkfrag.blending_hash);

    for (int stream_index = 0; stream_index < max_stream_idx; stream_index++) {
        const SceGxmVertexStream &stream = vertex_program.streams[stream_index];
        const SceGxmIndexSource index_source = static_cast<SceGxmIndexSource>(stream.indexSource);
        const GXMStreamInfo &stream_info = context.record.vertex_streams[stream_index];
        LOG_INFO("ThorRenderDump stream frame={} scene={} draw={} slot={} data=0x{:08X} record_size={} required_size={} stride={} index_source={} instanced={}",
            context.frame_timestamp,
            context.scene_timestamp,
            debug_draw_index,
            stream_index,
            stream_info.data.address(),
            stream_info.size,
            required_sizes[stream_index],
            stream.stride,
            static_cast<uint32_t>(stream.indexSource),
            gxm::is_stream_instancing(index_source));
    }

    for (const SceGxmVertexAttribute &attribute : vertex_program.attributes) {
        if (!vkvert.attribute_infos.contains(attribute.regIndex)) {
            LOG_INFO("ThorRenderDump attr frame={} scene={} draw={} reg={} stream={} offset={} format={} components={} used=0",
                context.frame_timestamp,
                context.scene_timestamp,
                debug_draw_index,
                attribute.regIndex,
                attribute.streamIndex,
                attribute.offset,
                static_cast<uint32_t>(attribute.format),
                attribute.componentCount);
            continue;
        }

        const shader::usse::AttributeInformation &info = vkvert.attribute_infos.at(attribute.regIndex);
        const SceGxmVertexStream &stream = vertex_program.streams[attribute.streamIndex];
        LOG_INFO("ThorRenderDump attr frame={} scene={} draw={} reg={} loc={} stream={} stream_stride={} offset={} format={} components={} attr_bytes={} shader_type={} shader_components={} integer={} signed={} regformat={}",
            context.frame_timestamp,
            context.scene_timestamp,
            debug_draw_index,
            attribute.regIndex,
            info.location,
            attribute.streamIndex,
            stream.stride,
            attribute.offset,
            static_cast<uint32_t>(attribute.format),
            attribute.componentCount,
            get_vulkan_attribute_byte_size(attribute, info),
            static_cast<uint32_t>(info.gxm_type),
            info.component_count,
            info.is_integer,
            info.is_signed,
            info.regformat);
        log_renderer_debug_attribute_values(context, mem, format, indices, count, debug_draw_index, attribute, info, stream, required_sizes[attribute.streamIndex]);
    }

    log_renderer_debug_textures(context, "vertex", context.vertex_gxm_textures, context.vertex_gxm_texture_valid, vertex_data->texture_count, debug_draw_index);
    log_renderer_debug_textures(context, "fragment", context.fragment_gxm_textures, context.fragment_gxm_texture_valid, fragment_data->texture_count, debug_draw_index);
    log_renderer_debug_uniform_buffers(context, mem, "vertex", *vertex_data, context.vertex_uniform_addresses, context.vertex_uniform_sizes, context.vertex_uniform_valid, debug_draw_index);
    log_renderer_debug_uniform_buffers(context, mem, "fragment", *fragment_data, context.fragment_uniform_addresses, context.fragment_uniform_sizes, context.fragment_uniform_valid, debug_draw_index);
}

// vertex count is only used with double buffer mapping
static void bind_vertex_streams(VKContext &context, MemState &mem, uint32_t instance_count, uint32_t max_index) {
    GxmRecordState &state = context.record;
    const SceGxmVertexProgram &vertex_program = *state.vertex_program.get(mem);
    VertexProgram *vkvert = vertex_program.renderer_data.get();
    const bool force_stream_copy = renderer_debug_hash_prefix_or_all_matches(vkvert->hash, renderer_debug_setting("VITA3K_RENDER_FORCE_VERTEX_STREAM_COPY_VHASH", "debug.vita3k.render_force_vertex_stream_copy_vhash"));
    const bool align_stride4 = renderer_debug_hash_prefix_or_all_matches(vkvert->hash, renderer_debug_setting("VITA3K_RENDER_ALIGN_VERTEX_STRIDE4_VHASH", "debug.vita3k.render_align_vertex_stride4_vhash"));

    int max_stream_idx = get_used_vertex_stream_count(vertex_program, *vkvert);

    if (context.state.mapping_method == MappingMethod::DoubleBuffer) {
        for (int i = 0; i < max_stream_idx; i++)
            state.vertex_streams[i].size = 0;

        // same as in SceGxm.cpp
        const std::array<size_t, SCE_GXM_MAX_VERTEX_STREAMS> required_sizes = get_required_vertex_stream_sizes(vertex_program, *vkvert, instance_count, max_index);
        for (int i = 0; i < max_stream_idx; i++)
            state.vertex_streams[i].size = required_sizes[i];

        for (int i = 0; i < max_stream_idx; i++) {
            if (state.vertex_streams[i].data)
                // on the PS Vita, shader stores are used most of the time to write to a vertex buffer
                // Vertex buffers are often rewritten at unaligned offsets. Track the
                // whole page range so double-buffer mapping cannot keep stale vertices
                // in the unprotected head/tail of a trapped buffer.
                context.state.buffer_trapping.access_buffer(state.vertex_streams[i].data.address(), static_cast<uint32_t>(state.vertex_streams[i].size), mem, context.state.has_shader_store, true);
        }
    }

    if (max_stream_idx == 0)
        return;

    for (int i = 0; i < max_stream_idx; i++) {
        if (state.vertex_streams[i].data) {
            if (context.state.features.enable_memory_mapping && !force_stream_copy && !align_stride4) {
                auto [buffer, offset] = context.state.get_matching_mapping(state.vertex_streams[i].data.cast<void>());

                context.vertex_stream_offsets[i] = offset;
                context.vertex_stream_buffers[i] = buffer;
            } else {
                const uint8_t *stream = state.vertex_streams[i].data.get(mem);
                uint32_t stream_size = state.vertex_streams[i].size;
                std::vector<uint8_t> restrided_stream;
#ifdef __APPLE__
                // Vulkan allows any stride, but Metal only allows multiples of 4.
                const bool restride = vertex_program.streams[i].stride % 4 != 0;
#else
                const bool restride = align_stride4 && vertex_program.streams[i].stride % 4 != 0;
#endif
                if (restride) {
                    restride_stream(stream, stream_size, vertex_program.streams[i].stride, restrided_stream);
                    stream = restrided_stream.data();
                    stream_size = static_cast<uint32_t>(restrided_stream.size());
                }
                context.vertex_stream_ring_buffer.allocate(context.prerender_cmd, stream_size, stream);
                context.vertex_stream_offsets[i] = context.vertex_stream_ring_buffer.data_offset;
                context.vertex_stream_buffers[i] = context.vertex_stream_ring_buffer.handle();
            }

            state.vertex_streams[i].data = nullptr;
            state.vertex_streams[i].size = 0;
        }
    }

    context.render_cmd.bindVertexBuffers(0, max_stream_idx, context.vertex_stream_buffers, context.vertex_stream_offsets);
}

void draw(VKContext &context, SceGxmPrimitiveType type, SceGxmIndexFormat format,
    Ptr<void> indices, size_t count, uint32_t instance_count, MemState &mem, const Config &config) {
    void *indices_ptr = indices.get(mem);

    context.check_for_macroblock_change(true);

    if (!context.in_renderpass)
        context.start_render_pass();

    // when we do multiple render pass for one scene (shader interlock or slow macroblock),
    // we need to always load the depth-stencil after the first draw
    if (context.is_first_scene_draw && (context.state.features.support_shader_interlock || context.ignore_macroblock)) {
        // update the render pass to load and store the depth and stencil
        context.current_render_pass = context.state.pipeline_cache.retrieve_render_pass(context.current_color_format, true, true, !context.record.color_surface.data);
        context.is_first_scene_draw = false;
    }

    const SceGxmFragmentProgram &gxm_fragment_program = *context.record.fragment_program.get(mem);
    const SceGxmProgram &fragment_program_gxp = *gxm_fragment_program.program.get(mem);
    if (context.state.features.direct_fragcolor && fragment_program_gxp.is_frag_color_used()) {
        // the fragment shader is using programmable blending with a subpass input
        vk::ImageMemoryBarrier barrier{
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = context.current_color_base_image->image,
            .subresourceRange = vkutil::color_subresource_range
        };
        context.render_cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlagBits::eByRegion, {}, {}, barrier);
    } else if (context.state.features.support_shader_interlock
        && fragment_program_gxp.is_frag_color_used() != context.last_draw_was_framebuffer_fetch) {
        // restart the render pass to act as a barrier
        context.render_cmd.endRenderPass();

        if (fragment_program_gxp.is_frag_color_used()) {
            context.curr_renderpass_info.framebuffer = context.current_shader_interlock_framebuffer;
            context.curr_renderpass_info.renderPass = context.current_shader_interlock_pass;
        } else {
            context.curr_renderpass_info.framebuffer = context.current_framebuffer;
            context.curr_renderpass_info.renderPass = context.current_render_pass;
        }

        context.render_cmd.beginRenderPass(context.curr_renderpass_info, vk::SubpassContents::eInline);
        context.last_draw_was_framebuffer_fetch = fragment_program_gxp.is_frag_color_used();
    }

    const RendererDebugConfig &renderer_debug = renderer_debug_config();
    const uint32_t debug_draw_index = context.debug_scene_draw_count++;
    const auto &vertex_data = context.record.vertex_program.get(mem)->renderer_data;
    const auto &fragment_data = context.record.fragment_program.get(mem)->renderer_data;
    const std::string hash_text_v = hex_string(vertex_data->hash);
    const std::string hash_text_f = hex_string(fragment_data->hash);
    const uint32_t debug_rt_width = context.render_target != nullptr ? context.render_target->width : 0;
    const uint32_t debug_rt_height = context.render_target != nullptr ? context.render_target->height : 0;
    const uint32_t debug_color_addr = context.record.color_surface.data.address();
    const bool renderer_debug_skip = renderer_debug_range_matches(renderer_debug.skip, context.frame_timestamp, context.scene_timestamp, debug_rt_width, debug_rt_height, debug_color_addr,
        context.vertex_gxm_textures, context.vertex_gxm_texture_valid, vertex_data->texture_count,
        context.fragment_gxm_textures, context.fragment_gxm_texture_valid, fragment_data->texture_count,
        hash_text_v, hash_text_f, debug_draw_index);
    const bool renderer_debug_stop_after = renderer_debug_stop_after_matches(renderer_debug.stop_after, context.frame_timestamp, context.scene_timestamp, debug_rt_width, debug_rt_height, debug_color_addr,
        context.vertex_gxm_textures, context.vertex_gxm_texture_valid, vertex_data->texture_count,
        context.fragment_gxm_textures, context.fragment_gxm_texture_valid, fragment_data->texture_count,
        hash_text_v, hash_text_f, debug_draw_index);
    const bool renderer_debug_dump = renderer_debug_range_matches(renderer_debug.dump, context.frame_timestamp, context.scene_timestamp, debug_rt_width, debug_rt_height, debug_color_addr,
        context.vertex_gxm_textures, context.vertex_gxm_texture_valid, vertex_data->texture_count,
        context.fragment_gxm_textures, context.fragment_gxm_texture_valid, fragment_data->texture_count,
        hash_text_v, hash_text_f, debug_draw_index);
    if (renderer_debug_skip || context.debug_scene_stop_after_active) {
        const char *reason = renderer_debug_skip ? "skip" : "stopped-after";
        if (context.state.renderer_trace_gxm_state || renderer_debug.trace) {
            LOG_INFO("ThorRenderDebug {} frame={} scene={} rt={}x{} color_addr=0x{:08X} draw={} prim={} index_fmt={} count={} instances={} vhash={} fhash={}",
                reason,
                context.frame_timestamp,
                context.scene_timestamp,
                debug_rt_width,
                debug_rt_height,
                debug_color_addr,
                debug_draw_index,
                static_cast<uint32_t>(type),
                static_cast<uint32_t>(format),
                count,
                instance_count,
                hash_text_v,
                hash_text_f);
        }

        insert_renderer_debug_label(context,
            fmt::format("{} frame={} scene={} draw={} {} prim={} count={}",
                context.state.game_id.empty() ? "unknown" : context.state.game_id,
                context.frame_timestamp,
                context.scene_timestamp,
                debug_draw_index,
                reason,
                static_cast<uint32_t>(type),
                count),
            { 1.0f, 0.25f, 0.1f, 1.0f });

        context.vertex_uniform_storage_allocated = false;
        context.fragment_uniform_storage_allocated = false;
        return;
    }

    if (context.current_visibility_buffer != nullptr && context.current_query_idx != -1 && !context.is_in_query) {
        if (context.current_visibility_buffer->queries_used[context.current_query_idx]) {
            LOG_WARN_ONCE("Visibility buffer entry is used more than once in a scene");
            // still let this happen, this is a validation error but I think most GPUs should be fine with it
        }
        context.current_visibility_buffer->queries_used[context.current_query_idx] = true;

        context.visibility_max_used_idx = std::max(context.visibility_max_used_idx, context.current_query_idx);

        const vk::QueryControlFlags control_flags = (context.is_query_op_increment && context.state.physical_device_features.occlusionQueryPrecise) ? vk::QueryControlFlagBits::ePrecise : vk::QueryControlFlags();
        context.render_cmd.beginQuery(context.current_visibility_buffer->query_pool, context.current_query_idx, control_flags);
        context.is_in_query = true;
    }

    // do we need to check for a pipeline change?
    if (context.refresh_pipeline || type != context.last_primitive) {
        context.refresh_pipeline = false;
        context.last_primitive = type;

        // We don't want to defer cases where we draw a whole quad over the screen as these draws could be necessary
        // to be able to see anything
        bool can_be_whole_quad = instance_count == 1 && count <= 6;
        vk::Pipeline new_pipeline = context.state.pipeline_cache.retrieve_pipeline(context, type, !can_be_whole_quad, mem);

        if (new_pipeline != context.current_pipeline) {
            context.current_pipeline = new_pipeline;

            if (new_pipeline != nullptr)
                context.render_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, context.current_pipeline);
        }
    }

    // can happen with asynchronous pipeline compilation
    if (context.current_pipeline == nullptr)
        return;

    if (context.state.renderer_trace_gxm_state || renderer_debug.trace) {
        const uint32_t trace_draw_limit = renderer_debug.trace_limit;
        if (debug_draw_index < trace_draw_limit) {
            float debug_frag_res_multiplier = context.state.res_multiplier;
            const bool debug_has_msaa = context.render_target != nullptr && context.render_target->multisample_mode;
            const bool debug_has_downscale = context.record.color_surface.downscale;
            if (debug_has_msaa && !debug_has_downscale)
                debug_frag_res_multiplier *= 2;
            else if (!debug_has_msaa && debug_has_downscale)
                debug_frag_res_multiplier /= 2;

            const std::string vertex_texture_addrs = renderer_debug_texture_addr_summary(context.vertex_gxm_textures, context.vertex_gxm_texture_valid, vertex_data->texture_count);
            const std::string fragment_texture_addrs = renderer_debug_texture_addr_summary(context.fragment_gxm_textures, context.fragment_gxm_texture_valid, fragment_data->texture_count);

            LOG_INFO("ThorRenderTrace draw frame={} scene={} color_addr=0x{:08X} draw={} prim={} index_fmt={} count={} instances={} pipeline={} framebuffer_fetch={} vhash={} fhash={} vtex={} ftex={} vtex_addrs={} ftex_addrs={} vbufs={} fbufs={} depth_func={}/{} depth_write={}/{} depth_bias={}/{} stencil_func={}/{} cull={} two_sided={} vp_flat={} z_offset={} z_scale={} writing_mask={} viewport={},{},{},{} scissor={},{},{},{} frag_res_multiplier={} color_downscale={}",
                context.frame_timestamp,
                context.scene_timestamp,
                debug_color_addr,
                debug_draw_index,
                static_cast<uint32_t>(type),
                static_cast<uint32_t>(format),
                count,
                instance_count,
                context.current_pipeline != nullptr,
                fragment_program_gxp.is_frag_color_used(),
                hash_text_v,
                hash_text_f,
                vertex_data->texture_count,
                fragment_data->texture_count,
                vertex_texture_addrs,
                fragment_texture_addrs,
                vertex_data->buffer_count,
                fragment_data->buffer_count,
                static_cast<uint32_t>(context.record.front_depth_func),
                static_cast<uint32_t>(context.record.back_depth_func),
                static_cast<uint32_t>(context.record.front_depth_write_mode),
                static_cast<uint32_t>(context.record.back_depth_write_mode),
                context.record.depth_bias_unit,
                context.record.depth_bias_slope,
                static_cast<uint32_t>(context.record.front_stencil_state_op.func),
                static_cast<uint32_t>(context.record.back_stencil_state_op.func),
                static_cast<uint32_t>(context.record.cull_mode),
                static_cast<uint32_t>(context.record.two_sided),
                context.record.viewport_flat,
                context.record.z_offset,
                context.record.z_scale,
                context.record.writing_mask,
                context.viewport.x,
                context.viewport.y,
                context.viewport.width,
                context.viewport.height,
                context.scissor.offset.x,
                context.scissor.offset.y,
                context.scissor.extent.width,
                context.scissor.extent.height,
                debug_frag_res_multiplier,
                debug_has_downscale);
        } else if (debug_draw_index == trace_draw_limit) {
            LOG_INFO("ThorRenderTrace draw frame={} scene={} draw_limit={} reached; suppressing remaining draws for this scene",
                context.frame_timestamp,
                context.scene_timestamp,
                trace_draw_limit);
        }
    }

    if (config.log_active_shaders) {
        const std::string hash_text_f = hex_string(context.record.fragment_program.get(mem)->renderer_data->hash);
        const std::string hash_text_v = hex_string(context.record.vertex_program.get(mem)->renderer_data->hash);

        LOG_DEBUG("\nVertex  : {}\nFragment: {}", hash_text_v, hash_text_f);
        LOG_DEBUG("Vertex default uniform buffer: {}\n", spdlog::to_hex(context.ubo_data[0], 16));
        LOG_DEBUG("Fragment default uniform buffer: {}\n", spdlog::to_hex(context.ubo_data[SCE_GXM_REAL_MAX_UNIFORM_BUFFER], 16));
    }

    const bool use_memory_mapping = context.state.features.enable_memory_mapping;

    // update uniforms if needed
    // first update the buffer and texture count
    auto &vert_render_data = context.record.vertex_program.get(mem)->renderer_data;
    auto &frag_render_data = context.record.fragment_program.get(mem)->renderer_data;

    if (use_memory_mapping) {
        context.curr_vert_ublock.set_buffer_count(vert_render_data->buffer_count);
        context.curr_frag_ublock.set_buffer_count(frag_render_data->buffer_count);
    }

    if (context.state.features.use_texture_viewport) {
        context.curr_vert_ublock.set_texture_count(vert_render_data->texture_count);
        context.curr_frag_ublock.set_texture_count(frag_render_data->texture_count);
    }

    auto &vert_ublock = context.curr_vert_ublock.base_block;
    vert_ublock.viewport_flip = context.record.viewport_flip;
    vert_ublock.viewport_flag = (context.record.viewport_flat) ? 0.0f : 1.0f;
    vert_ublock.z_offset = context.record.z_offset;
    vert_ublock.z_scale = context.record.z_scale;
    vert_ublock.screen_width = context.render_target->width / context.state.res_multiplier;
    vert_ublock.screen_height = context.render_target->height / context.state.res_multiplier;

    if (context.curr_vert_ublock.changed || memcmp(&context.prev_vert_ublock, &vert_ublock, sizeof(vert_ublock)) != 0) {
        // TODO: this intermediate step can be avoided
        context.curr_vert_ublock.copy_to(context.shader_info_temp);
        context.vertex_info_uniform_buffer.allocate(context.prerender_cmd, context.curr_vert_ublock.get_size(), context.shader_info_temp);
        memcpy(&context.prev_vert_ublock, &vert_ublock, sizeof(vert_ublock));
    }

    auto &frag_ublock = context.curr_frag_ublock.base_block;
    frag_ublock.writing_mask = context.record.writing_mask;
    frag_ublock.res_multiplier = context.state.res_multiplier;
    const bool has_msaa = context.render_target->multisample_mode;
    const bool has_downscale = context.record.color_surface.downscale;
    if (has_msaa && !has_downscale)
        frag_ublock.res_multiplier *= 2;
    else if (!has_msaa && has_downscale)
        frag_ublock.res_multiplier /= 2;

    if (context.curr_frag_ublock.changed || memcmp(&context.prev_frag_ublock, &frag_ublock, sizeof(frag_ublock)) != 0) {
        // TODO: this intermediate step can be avoided
        context.curr_frag_ublock.copy_to(context.shader_info_temp);
        context.fragment_info_uniform_buffer.allocate(context.prerender_cmd, context.curr_frag_ublock.get_size(), context.shader_info_temp);
        memcpy(&context.prev_frag_ublock, &frag_ublock, sizeof(frag_ublock));
    }

    // create, update and bind descriptors (uniforms and textures)
    draw_bind_descriptors(context, mem);

    // Upload index data.
    vk::IndexType index_type = (format == SCE_GXM_INDEX_FORMAT_U16) ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
    const size_t index_size = (format == SCE_GXM_INDEX_FORMAT_U16) ? 2 : 4;

    uint32_t max_index = 0;
    if (use_memory_mapping) {
        auto [buffer, offset] = context.state.get_matching_mapping(indices);
        if (context.state.mapping_method == MappingMethod::DoubleBuffer) {
            TrappedBuffer *trapped_buffer = context.state.buffer_trapping.access_buffer(indices.address(), count * index_size, mem, false, true);
            if (count != 0 && trapped_buffer->extra == ~0) {
                // store the max element in extra
                if (format == SCE_GXM_INDEX_FORMAT_U16) {
                    uint16_t *data = indices.cast<uint16_t>().get(mem);
                    trapped_buffer->extra = *std::max_element(&data[0], &data[count]);
                } else {
                    uint32_t *data = indices.cast<uint32_t>().get(mem);
                    trapped_buffer->extra = *std::max_element(&data[0], &data[count]);
                }
            }
            max_index = (count == 0) ? 0 : trapped_buffer->extra;
        }
        context.render_cmd.bindIndexBuffer(buffer, offset, index_type);
    } else {
        const size_t index_buffer_size = index_size * count;
        context.index_stream_ring_buffer.allocate(context.prerender_cmd, index_buffer_size, indices_ptr);
        context.render_cmd.bindIndexBuffer(context.index_stream_ring_buffer.handle(), context.index_stream_ring_buffer.data_offset, index_type);
        if (renderer_debug_dump)
            max_index = get_renderer_debug_max_index(indices, count, format, mem);
    }

    if (renderer_debug_dump) {
        log_renderer_debug_draw_dump(context, mem, type, format, indices, count, instance_count, max_index, debug_draw_index, debug_rt_width, debug_rt_height, hash_text_v, hash_text_f);
    }

    // bind the vertex streams
    bind_vertex_streams(context, mem, instance_count, max_index);

    if (renderer_debug.labels) {
        insert_renderer_debug_label(context,
            fmt::format("{} frame={} scene={} draw={} prim={} count={} vhash={} fhash={}",
                context.state.game_id.empty() ? "unknown" : context.state.game_id,
                context.frame_timestamp,
                context.scene_timestamp,
                debug_draw_index,
                static_cast<uint32_t>(type),
                count,
                hash_text_v,
                hash_text_f),
            { 0.2f, 0.7f, 1.0f, 1.0f });
    }

    context.render_cmd.drawIndexed(count, instance_count, 0, 0, 0);

    if (renderer_debug_stop_after) {
        context.debug_scene_stop_after_active = true;
        if (context.state.renderer_trace_gxm_state || renderer_debug.trace) {
            LOG_INFO("ThorRenderDebug stop-after armed frame={} scene={} rt={}x{} color_addr=0x{:08X} draw={} vhash={} fhash={}",
                context.frame_timestamp,
                context.scene_timestamp,
                debug_rt_width,
                debug_rt_height,
                debug_color_addr,
                debug_draw_index,
                hash_text_v,
                hash_text_f);
        }
    }

    context.vertex_uniform_storage_allocated = false;
    context.fragment_uniform_storage_allocated = false;
}

} // namespace renderer::vulkan
