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

#include "interface.h"

#include <app/functions.h>
#include <bgm_player/functions.h>
#include <config/functions.h>
#include <config/version.h>
#include <dialog/state.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <gui/functions.h>
#include <gui/state.h>
#include <include/cpu.h>
#include <include/environment.h>
#include <io/state.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <mem/ptr.h>
#include <modules/module_parent.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <packages/sfo.h>
#include <renderer/functions.h>
#include <renderer/shaders.h>
#include <renderer/state.h>
#include <shader/spirv_recompiler.h>
#include <util/cheat_paths.h>
#include <util/fs.h>
#include <util/log.h>
#include <util/string_utils.h>

#if USE_DISCORD
#include <app/discord.h>
#endif

#ifdef _WIN32
#include <combaseapi.h>
#include <process.h>
#define SDL_MAIN_HANDLED
#endif

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#include <unistd.h>
#include <xxh3.h>
#endif

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

namespace {

enum class RuntimeCheatWriteKind {
    Direct,
    PointerLevel1,
};

struct RuntimeCheatWrite {
    RuntimeCheatWriteKind kind = RuntimeCheatWriteKind::Direct;
    Address address = 0;
    uint32_t value = 0;
    uint8_t width = 0;
    int relative_segment = -1;
    uint32_t pointer_offset = 0;
    uint32_t final_offset = 0;
    uint32_t line = 0;
    bool code_patch = false;
    bool warned_invalid = false;
};

struct RuntimeCheat {
    std::string name;
    bool enabled = false;
    std::vector<RuntimeCheatWrite> writes;
    uint32_t unsupported_lines = 0;
};

struct RuntimeCheats {
    fs::path source;
    std::array<Address, MODULE_INFO_NUM_SEGMENTS> segment_bases{};
    std::vector<RuntimeCheat> cheats;
    uint32_t enabled_write_count = 0;
    uint32_t code_patch_write_count = 0;
    uint32_t pointer_write_count = 0;
};

struct PendingPointerWrite {
    Address base_address = 0;
    uint32_t first_offset = 0;
    uint8_t width = 0;
    int relative_segment = -1;
    uint32_t line = 0;
};

static std::string trim_copy(std::string value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](const unsigned char c) {
        return std::isspace(c);
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](const unsigned char c) {
        return std::isspace(c);
    }).base();

    if (begin >= end)
        return {};

    return std::string(begin, end);
}

static std::optional<uint32_t> parse_hex_word(std::string token) {
    token = trim_copy(token);
    if (!token.empty() && token.front() == '$')
        token.erase(token.begin());
    if ((token.size() > 2) && (token[0] == '0') && ((token[1] == 'x') || (token[1] == 'X')))
        token.erase(0, 2);
    if (token.empty())
        return std::nullopt;

    errno = 0;
    char *end = nullptr;
    const auto value = std::strtoul(token.c_str(), &end, 16);
    if ((errno != 0) || !end || (*end != '\0') || (value > std::numeric_limits<uint32_t>::max()))
        return std::nullopt;

    return static_cast<uint32_t>(value);
}

static std::vector<std::string> split_fields(const std::string &line) {
    std::istringstream stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field)
        fields.push_back(field);
    return fields;
}

static bool vita_cheat_static_write_width(const uint32_t code_type, uint8_t &width) {
    switch (code_type) {
    case 0x0000:
        width = 1;
        return true;
    case 0x0100:
        width = 2;
        return true;
    case 0x0200:
        width = 4;
        return true;
    default:
        return false;
    }
}

static bool vita_cheat_arm_write_width(const uint32_t code_type, uint8_t &width) {
    switch (code_type) {
    case 0xA000:
        width = 1;
        return true;
    case 0xA100:
        width = 2;
        return true;
    case 0xA200:
        width = 4;
        return true;
    default:
        return false;
    }
}

static bool vita_cheat_pointer_header(const uint32_t code_type, uint8_t &width, uint8_t &level) {
    if ((code_type & 0xF000) != 0x3000)
        return false;

    const uint8_t width_type = static_cast<uint8_t>((code_type >> 8) & 0xF);
    switch (width_type) {
    case 0:
        width = 1;
        break;
    case 1:
        width = 2;
        break;
    case 2:
        width = 4;
        break;
    default:
        return false;
    }

    level = static_cast<uint8_t>(code_type & 0xFF);
    return level > 0;
}

static std::array<Address, MODULE_INFO_NUM_SEGMENTS> get_main_module_segment_bases(const EmuEnvState &emuenv, const int32_t main_module_id) {
    std::array<Address, MODULE_INFO_NUM_SEGMENTS> bases{};
    const auto module = emuenv.kernel.loaded_modules.find(main_module_id);
    if ((module == emuenv.kernel.loaded_modules.end()) || !module->second)
        return bases;

    for (size_t index = 0; index < bases.size(); index++) {
        const auto &segment = module->second->info.segments[index];
        if (segment.memsz != 0)
            bases[index] = segment.vaddr.address();
    }

    return bases;
}

static RuntimeCheats load_runtime_cheats(const EmuEnvState &emuenv, const int32_t main_module_id) {
    RuntimeCheats runtime;
    if (!emuenv.cfg.cheats_enabled)
        return runtime;

    const auto source = cheat_paths::find_vitacheat_file(emuenv.base_path, emuenv.shared_path, emuenv.pref_path, emuenv.io.title_id);
    if (!source.has_value())
        return runtime;

    runtime.source = *source;
    runtime.segment_bases = get_main_module_segment_bases(emuenv, main_module_id);

    fs::ifstream file(*source);
    if (!file) {
        LOG_WARN("Could not open VitaCheat file {}", *source);
        runtime.source.clear();
        return runtime;
    }

    RuntimeCheat *current = nullptr;
    std::optional<PendingPointerWrite> pending_pointer;
    int current_relative_segment = -1;
    uint32_t line_number = 0;
    uint32_t unsupported_lines = 0;

    const auto mark_unsupported = [&]() {
        if (current)
            current->unsupported_lines++;
        unsupported_lines++;
    };

    const auto clear_pending_pointer_as_unsupported = [&]() {
        if (!pending_pointer.has_value())
            return;

        mark_unsupported();
        pending_pointer.reset();
    };

    std::string line;
    while (std::getline(file, line)) {
        line_number++;
        line = trim_copy(line);
        if (line.empty() || line.starts_with("#") || line.starts_with("//"))
            continue;

        if (line.starts_with("_V0") || line.starts_with("_V1")) {
            clear_pending_pointer_as_unsupported();
            auto name = trim_copy(line.substr(3));
            if (name.empty())
                name = fmt::format("Cheat {}", runtime.cheats.size() + 1);

            runtime.cheats.push_back({ name, line.starts_with("_V1") });
            current = &runtime.cheats.back();
            current_relative_segment = -1;
            continue;
        }

        if (!line.starts_with("$"))
            continue;

        if (!current) {
            runtime.cheats.push_back({ "Loose VitaCheat codes", true });
            current = &runtime.cheats.back();
        }

        const auto fields = split_fields(line);
        if (fields.size() < 3) {
            clear_pending_pointer_as_unsupported();
            mark_unsupported();
            continue;
        }

        const auto code_type = parse_hex_word(fields[0]);
        const auto address = parse_hex_word(fields[1]);
        const auto value = parse_hex_word(fields[2]);
        if (!code_type.has_value() || !address.has_value() || !value.has_value()) {
            clear_pending_pointer_as_unsupported();
            mark_unsupported();
            continue;
        }

        if (pending_pointer.has_value()) {
            if (*code_type == 0x3300) {
                current->writes.push_back({ RuntimeCheatWriteKind::PointerLevel1, pending_pointer->base_address, *value, pending_pointer->width, pending_pointer->relative_segment, pending_pointer->first_offset, *address, pending_pointer->line });
                pending_pointer.reset();
                continue;
            }

            clear_pending_pointer_as_unsupported();
        }

        if (*code_type == 0xB200) {
            if ((*address < MODULE_INFO_NUM_SEGMENTS) && (*value == 0)) {
                current_relative_segment = static_cast<int>(*address);
            } else {
                mark_unsupported();
            }
            continue;
        }

        uint8_t width = 0;
        uint8_t pointer_level = 0;
        if (vita_cheat_static_write_width(*code_type, width)) {
            current->writes.push_back({ RuntimeCheatWriteKind::Direct, *address, *value, width, current_relative_segment, 0, 0, line_number, false });
            continue;
        }

        if (vita_cheat_arm_write_width(*code_type, width)) {
            current->writes.push_back({ RuntimeCheatWriteKind::Direct, *address, *value, width, current_relative_segment, 0, 0, line_number, true });
            continue;
        }

        if (vita_cheat_pointer_header(*code_type, width, pointer_level)) {
            if (pointer_level == 1) {
                pending_pointer = PendingPointerWrite{ *address, *value, width, current_relative_segment, line_number };
            } else {
                mark_unsupported();
            }
            continue;
        }

        mark_unsupported();
    }

    clear_pending_pointer_as_unsupported();

    for (const auto &cheat : runtime.cheats) {
        if (!cheat.enabled)
            continue;

        runtime.enabled_write_count += static_cast<uint32_t>(cheat.writes.size());
        for (const auto &write : cheat.writes) {
            if (write.kind == RuntimeCheatWriteKind::PointerLevel1)
                runtime.pointer_write_count++;
            if (write.code_patch)
                runtime.code_patch_write_count++;
        }
    }

    LOG_INFO("Loaded VitaCheat file {} for {} with {} enabled writes ({} ARM/code patches, {} level-1 pointer writes); {} unsupported lines skipped",
        *source, emuenv.io.title_id, runtime.enabled_write_count, runtime.code_patch_write_count, runtime.pointer_write_count, unsupported_lines);
    return runtime;
}

static bool valid_cheat_range(const MemState &mem, const Address address, const uint8_t width) {
    if ((width == 0) || (address > (std::numeric_limits<Address>::max() - width)))
        return false;

    return is_valid_addr_range(mem, address, address + width);
}

static std::optional<Address> add_address_offset(const Address address, const uint32_t offset) {
    if (address > (std::numeric_limits<Address>::max() - offset))
        return std::nullopt;

    return address + offset;
}

static std::optional<Address> resolve_direct_cheat_address(const MemState &mem, const RuntimeCheats &runtime, const Address address, const uint8_t width, const int relative_segment) {
    if (valid_cheat_range(mem, address, width))
        return address;

    if ((relative_segment < 0) || (static_cast<size_t>(relative_segment) >= runtime.segment_bases.size()))
        return std::nullopt;

    const Address base = runtime.segment_bases[relative_segment];
    if (base == 0)
        return std::nullopt;

    const auto relative_address = add_address_offset(base, address);
    if (!relative_address.has_value())
        return std::nullopt;

    if (!valid_cheat_range(mem, *relative_address, width))
        return std::nullopt;

    return *relative_address;
}

static std::optional<Address> resolve_cheat_address(const MemState &mem, const RuntimeCheats &runtime, const RuntimeCheatWrite &write) {
    if (write.kind == RuntimeCheatWriteKind::Direct)
        return resolve_direct_cheat_address(mem, runtime, write.address, write.width, write.relative_segment);

    const auto pointer_address = resolve_direct_cheat_address(mem, runtime, write.address, 4, write.relative_segment);
    if (!pointer_address.has_value())
        return std::nullopt;

    const auto base_pointer = *Ptr<uint32_t>(*pointer_address).get(mem);
    const auto with_first_offset = add_address_offset(base_pointer, write.pointer_offset);
    if (!with_first_offset.has_value())
        return std::nullopt;

    const auto final_address = add_address_offset(*with_first_offset, write.final_offset);
    if (!final_address.has_value() || !valid_cheat_range(mem, *final_address, write.width))
        return std::nullopt;

    return *final_address;
}

static uint32_t normalized_cheat_value(const RuntimeCheatWrite &write) {
    if (write.width == 1)
        return write.value & 0xFF;
    if (write.width == 2)
        return write.value & 0xFFFF;

    return write.value;
}

static void apply_runtime_cheats(EmuEnvState &emuenv, RuntimeCheats &runtime) {
    if (runtime.source.empty())
        return;

    for (auto &cheat : runtime.cheats) {
        if (!cheat.enabled)
            continue;

        for (auto &write : cheat.writes) {
            const auto address = resolve_cheat_address(emuenv.mem, runtime, write);
            if (!address.has_value()) {
                if (!write.warned_invalid) {
                    LOG_WARN("Skipping invalid VitaCheat write at {}:{} for {}", runtime.source, write.line, emuenv.io.title_id);
                    write.warned_invalid = true;
                }
                continue;
            }

            const uint32_t value = normalized_cheat_value(write);
            auto *target = Ptr<uint8_t>(*address).get(emuenv.mem);
            if (std::memcmp(target, &value, write.width) == 0)
                continue;

            std::memcpy(target, &value, write.width);
            if (write.code_patch)
                emuenv.kernel.invalidate_jit_cache(*address, write.width);
        }
    }
}

static void draw_runtime_status_overlay(const EmuEnvState &emuenv, const RuntimeCheats &runtime_cheats) {
    const uint32_t speed_percent = emuenv.display.speed_percent.load();
    if ((speed_percent <= 100) && (runtime_cheats.enabled_write_count == 0))
        return;

    std::string status;
    if (speed_percent > 100)
        status = fmt::format("FF {}%", speed_percent);
    if (runtime_cheats.enabled_write_count > 0) {
        if (!status.empty())
            status += "  |  ";
        status += fmt::format("Cheats {}", runtime_cheats.enabled_write_count);
    }

    const auto pos = ImVec2(emuenv.logical_viewport_pos.x + 16.f, emuenv.logical_viewport_pos.y + 16.f);
    ImGui::GetForegroundDrawList()->AddText(pos, IM_COL32(255, 230, 128, 235), status.c_str());
}

static void refresh_runtime_cheat_counts(RuntimeCheats &runtime_cheats) {
    runtime_cheats.enabled_write_count = 0;
    runtime_cheats.code_patch_write_count = 0;
    runtime_cheats.pointer_write_count = 0;

    for (const auto &cheat : runtime_cheats.cheats) {
        if (!cheat.enabled)
            continue;

        runtime_cheats.enabled_write_count += static_cast<uint32_t>(cheat.writes.size());
        for (const auto &write : cheat.writes) {
            if (write.kind == RuntimeCheatWriteKind::PointerLevel1)
                runtime_cheats.pointer_write_count++;
            if (write.code_patch)
                runtime_cheats.code_patch_write_count++;
        }
    }
}

static uint32_t runtime_cheat_unsupported_count(const RuntimeCheats &runtime_cheats) {
    uint32_t count = 0;
    for (const auto &cheat : runtime_cheats.cheats)
        count += cheat.unsupported_lines;
    return count;
}

static void draw_runtime_osd(GuiState &gui, EmuEnvState &emuenv, RuntimeCheats &runtime_cheats, const int32_t main_module_id) {
    if (!runtime_osd_is_open())
        return;

    const ImVec2 viewport_pos(emuenv.logical_viewport_pos.x, emuenv.logical_viewport_pos.y);
    const ImVec2 viewport_size(emuenv.logical_viewport_size.x, emuenv.logical_viewport_size.y);
    const float available_width = std::max(1.f, viewport_size.x - 16.f);
    const float available_height = std::max(1.f, viewport_size.y - 16.f);
    const float width = std::min(available_width, std::min(980.f, std::max(520.f, viewport_size.x * 0.88f)));
    const float height = std::min(available_height, std::min(760.f, std::max(440.f, viewport_size.y * 0.88f)));
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport_pos,
        ImVec2(viewport_pos.x + viewport_size.x, viewport_pos.y + viewport_size.y),
        IM_COL32(0, 0, 0, 168));
    ImGui::SetNextWindowPos(ImVec2(viewport_pos.x + (viewport_size.x - width) * 0.5f, viewport_pos.y + (viewport_size.y - height) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowBgAlpha(0.98f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.f, 20.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.f, 12.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.f, 12.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 24.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.055f, 0.065f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.86f, 0.90f, 0.98f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.97f, 0.99f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.68f, 0.71f, 0.78f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.24f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.40f, 0.52f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.50f, 0.62f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.17f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.26f, 0.32f, 0.42f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.25f, 0.47f, 0.58f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.23f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.39f, 0.50f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.24f, 0.50f, 0.62f, 1.0f));

    const auto end_osd_window = []() {
        ImGui::SetWindowFontScale(1.f);
        ImGui::End();
        ImGui::PopStyleColor(14);
        ImGui::PopStyleVar(6);
    };

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    bool open = true;
    if (!ImGui::Begin("Thor OSD", &open, flags)) {
        end_osd_window();
        if (!open)
            runtime_osd_set_open(emuenv, false);
        return;
    }
    ImGui::SetWindowFontScale(1.22f);
    const bool window_appearing = ImGui::IsWindowAppearing();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        runtime_osd_set_open(emuenv, false);
        end_osd_window();
        return;
    }

    const uint32_t current_speed = emuenv.display.speed_percent.load();
    const uint32_t configured_speed = static_cast<uint32_t>(std::clamp(emuenv.cfg.fast_forward_speed_percent, 101, 1000));
    const std::string quick_state_status = runtime_quick_state_slot_status(emuenv);

    ImGui::BeginChild("##runtime_status", ImVec2(0.f, 158.f), true);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.92f, 0.58f, 1.0f));
    ImGui::TextWrapped("%s", emuenv.current_app_title.empty() ? "Running game" : emuenv.current_app_title.c_str());
    ImGui::PopStyleColor();
    ImGui::Text("Title ID: %s", emuenv.io.title_id.c_str());
    ImGui::SameLine();
    ImGui::Text("Speed: %u%%", current_speed);
    ImGui::SameLine();
    ImGui::Text("Preset: %u%%", configured_speed);
#ifdef __ANDROID__
    ImGui::Text("Driver: %s", emuenv.cfg.current_config.custom_driver_name.empty() ? "system" : emuenv.cfg.current_config.custom_driver_name.c_str());
#endif
    ImGui::TextWrapped("Quickstate slot 0: %s", quick_state_status.c_str());
    if (emuenv.renderer) {
        bool renderer_trace = emuenv.renderer->renderer_trace_gxm_state;
        if (ImGui::Checkbox("Renderer Trace", &renderer_trace)) {
            emuenv.renderer->renderer_trace_gxm_state = renderer_trace;
            LOG_INFO("{} Thor renderer GXM trace", renderer_trace ? "Enabled" : "Disabled");
        }
    }
    ImGui::EndChild();

    const float content_width = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 action_button(std::max(128.f, (content_width - spacing * 2.f) / 3.f), 52.f);
    const ImVec2 speed_button(std::max(84.f, (content_width - spacing * 3.f) / 4.f), 52.f);

    ImGui::TextUnformatted("Fast Forward");
    const auto draw_speed_preset = [&](const char *label, const uint32_t speed_percent) {
        const bool active = speed_percent == 100 ? current_speed == 100 : current_speed == speed_percent;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.48f, 0.36f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.62f, 0.46f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.70f, 0.52f, 1.0f));
        }
        if (ImGui::Button(label, speed_button)) {
            if (speed_percent > 100)
                emuenv.cfg.fast_forward_speed_percent = static_cast<int>(speed_percent);
            runtime_set_speed_percent(emuenv, speed_percent);
        }
        if (active)
            ImGui::PopStyleColor(3);
    };

    draw_speed_preset("Off", 100);
    ImGui::SameLine();
    draw_speed_preset("2x", 200);
    ImGui::SameLine();
    draw_speed_preset("3x", 300);
    ImGui::SameLine();
    draw_speed_preset("4x", 400);

    const ImVec2 confirm_button(std::max(128.f, (content_width - spacing) / 2.f), 52.f);
    ImGui::TextUnformatted("Confirm Button");
    const auto draw_confirm_button = [&](const char *label, const int sys_button) {
        const bool active = emuenv.cfg.sys_button == sys_button;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.40f, 0.62f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.52f, 0.76f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.60f, 0.86f, 1.0f));
        }
        if (ImGui::Button(label, confirm_button)) {
            emuenv.cfg.sys_button = sys_button;
            if (config::serialize_config(emuenv.cfg, emuenv.cfg.config_path) != Success)
                LOG_WARN("Failed to save runtime confirm button setting");
            LOG_INFO("Runtime confirm button set to {}", sys_button == 0 ? "Circle/O" : "Cross/X");
        }
        if (active)
            ImGui::PopStyleColor(3);
    };

    draw_confirm_button("O / Japan", 0);
    ImGui::SameLine();
    draw_confirm_button("X / West", 1);

    ImGui::Separator();

    if (ImGui::Button("Resume", action_button))
        runtime_osd_set_open(emuenv, false);
    if (window_appearing)
        ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button(emuenv.kernel.is_threads_paused() ? "Resume Threads" : "Pause", action_button)) {
        if (emuenv.kernel.is_threads_paused())
            emuenv.kernel.resume_threads();
        else
            emuenv.kernel.pause_threads();
    }
    ImGui::SameLine();
    if (ImGui::Button("Settings", action_button)) {
        gui.configuration_menu.settings_dialog = true;
    }

    if (ImGui::Button("Save State 0", action_button))
        runtime_request_save_state(emuenv);
    ImGui::SameLine();
    if (ImGui::Button("Load State 0", action_button))
        runtime_request_load_state(emuenv);
    ImGui::SameLine();
    if (ImGui::Button("Screenshot", action_button))
        runtime_take_screenshot(emuenv);

    ImGui::BeginDisabled();
    ImGui::Button("Reset Game", action_button);
    ImGui::SameLine();
    ImGui::Button("Close Game", action_button);
    ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Cheats", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (runtime_cheats.source.empty()) {
            ImGui::TextWrapped("No matching VitaCheat file loaded for this title.");
            if (ImGui::TreeNode("Searched paths")) {
                const auto candidates = cheat_paths::get_vitacheat_candidate_files(emuenv.base_path, emuenv.shared_path, emuenv.pref_path, emuenv.io.title_id);
                for (const auto &candidate : candidates)
                    ImGui::TextDisabled("%s", fs_utils::path_to_utf8(candidate).c_str());
                ImGui::TreePop();
            }
        } else {
            ImGui::Text("File: %s", fs_utils::path_to_utf8(runtime_cheats.source).c_str());
            ImGui::Text("Enabled writes: %u  Code patches: %u  Pointer writes: %u  Unsupported lines: %u",
                runtime_cheats.enabled_write_count,
                runtime_cheats.code_patch_write_count,
                runtime_cheats.pointer_write_count,
                runtime_cheat_unsupported_count(runtime_cheats));

            if (ImGui::Button("Reload Cheat File"))
                runtime_cheats = load_runtime_cheats(emuenv, main_module_id);

            ImGui::Separator();
            for (size_t index = 0; index < runtime_cheats.cheats.size(); index++) {
                auto &cheat = runtime_cheats.cheats[index];
                const auto label = fmt::format("{}##cheat{}", cheat.name, index);
                if (ImGui::Checkbox(label.c_str(), &cheat.enabled)) {
                    refresh_runtime_cheat_counts(runtime_cheats);
                    LOG_INFO("{} cheat '{}' for {}", cheat.enabled ? "Enabled" : "Disabled", cheat.name, emuenv.io.title_id);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%zu writes, %u unsupported", cheat.writes.size(), cheat.unsupported_lines);
            }
        }
    }

    if (!open)
        runtime_osd_set_open(emuenv, false);
    end_osd_window();
}

} // namespace

#ifdef __ANDROID__
static void set_current_game_id(const std::string_view game_id) {
    // retrieve the JNI environment.
    JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());

    // retrieve the Java instance of the SDLActivity
    jobject activity = reinterpret_cast<jobject>(SDL_GetAndroidActivity());

    // find the Java class of the activity. It should be SDLActivity or a subclass of it.
    jclass clazz(env->GetObjectClass(activity));

    // find the identifier of the method to call
    jmethodID method_id = env->GetMethodID(clazz, "setCurrentGameId", "(Ljava/lang/String;)V");
    jstring j_game_id = env->NewStringUTF(game_id.data());
    env->CallVoidMethod(activity, method_id, j_game_id);

    // clean up the local references.
    env->DeleteLocalRef(j_game_id);
    env->DeleteLocalRef(activity);
    env->DeleteLocalRef(clazz);
}

static void run_execv(char *argv[], EmuEnvState &emuenv) {
    // retrieve the JNI environment.
    JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());

    // retrieve the Java instance of the SDLActivity
    jobject activity = reinterpret_cast<jobject>(SDL_GetAndroidActivity());

    // find the Java class of the activity. It should be SDLActivity or a subclass of it.
    jclass clazz(env->GetObjectClass(activity));

    // find the identifier of the method to call
    jmethodID method_id = env->GetMethodID(clazz, "restartApp", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");

    // create the java string for the different parameters
    jstring app_path = env->NewStringUTF(emuenv.load_app_path.c_str());
    jstring exec_path = env->NewStringUTF(emuenv.load_exec_path.c_str());
    jstring exec_args = env->NewStringUTF(emuenv.load_exec_argv.c_str());

    env->CallVoidMethod(activity, method_id, app_path, exec_path, exec_args);

    // The function call above will exit with some delay
    // Exit now to match the behavior on PC
    exit(0);
};
#else
static void run_execv(char *argv[], EmuEnvState &emuenv) {
    char const *args[10];
    args[0] = argv[0];
    args[1] = "-a";
    args[2] = "true";
    if (!emuenv.load_app_path.empty()) {
        args[3] = "-r";
        args[4] = emuenv.load_app_path.data();
        if (!emuenv.load_exec_path.empty()) {
            args[5] = "--self";
            args[6] = emuenv.load_exec_path.data();
            if (!emuenv.load_exec_argv.empty()) {
                args[7] = "--app-args";
                args[8] = emuenv.load_exec_argv.data();
                args[9] = nullptr;
            } else
                args[7] = nullptr;
        } else
            args[5] = nullptr;
    } else {
        args[3] = nullptr;
    }

    // Execute the emulator again with some arguments
#ifdef _WIN32
    FreeConsole();
    _execv(argv[0], args);
#elif defined(__unix__) || defined(__APPLE__) && defined(__MACH__)
    execv(argv[0], const_cast<char *const *>(args));
#endif
}
#endif

int main(int argc, char *argv[]) {
#ifdef TRACY_ENABLE
    ZoneScoped; // Tracy - Track main function scope
#endif
    Root root_paths;

    app::init_paths(root_paths);

    if (logging::init(root_paths, true) != Success)
        return InitConfigFailed;

#ifdef __ANDROID__
    setvbuf(stdout, 0, _IOLBF, 0);
    setvbuf(stderr, 0, _IONBF, 0);
    int pfd[2];
    pipe(pfd);
    dup2(pfd[1], 1);
    dup2(pfd[1], 2);
    std::thread cout_thread([&pfd]() {
        ssize_t rdsz;
        char buf[512];
        while ((rdsz = read(pfd[0], buf, sizeof buf - 1)) > 0) {
            if (buf[rdsz - 1] == '\n')
                --rdsz;
            buf[rdsz] = 0; /* add null-terminator */
            LOG_DEBUG("{}", buf);
        }
    });
    cout_thread.detach();
#endif

    // Check admin privs before init starts to avoid creating of file as other user by accident
    bool adminPriv = false;
#ifdef _WIN32
    // https://stackoverflow.com/questions/8046097/how-to-check-if-a-process-has-the-administrative-rights
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
            adminPriv = Elevation.TokenIsElevated;
        }
    }
    if (hToken) {
        CloseHandle(hToken);
    }
#else
    auto uid = getuid();
    auto euid = geteuid();

    // if either effective uid or uid is the one of the root user assume running as root.
    // else if euid and uid are different then permissions errors can happen if its running
    // as a completely different user than the uid/euid
    if (uid == 0 || euid == 0 || uid != euid)
        adminPriv = true;
#endif

    if (adminPriv) {
        LOG_CRITICAL("PLEASE. DO NOT RUN VITA3K AS ADMIN OR WITH ADMIN PRIVILEGES.");
    }

    Config cfg{};
    EmuEnvState emuenv;
    const auto config_err = config::init_config(cfg, argc, argv, root_paths);

    fs::create_directories(cfg.get_pref_path());

    if (config_err != Success) {
        if (config_err == QuitRequested) {
            if (cfg.recompile_shader_path.has_value()) {
                LOG_INFO("Recompiling {}", *cfg.recompile_shader_path);
                shader::convert_gxp_to_glsl_from_filepath(*cfg.recompile_shader_path);
            }
            if (cfg.delete_title_id.has_value()) {
                LOG_INFO("Deleting title id {}", *cfg.delete_title_id);
                fs::remove_all(cfg.get_pref_path() / "ux0/app" / *cfg.delete_title_id);
                fs::remove_all(cfg.get_pref_path() / "ux0/addcont" / *cfg.delete_title_id);
                fs::remove_all(cfg.get_pref_path() / "ux0/user/00/savedata" / *cfg.delete_title_id);
                fs::remove_all(root_paths.get_cache_path() / "shaders" / *cfg.delete_title_id);
            }
            if (cfg.pup_path.has_value()) {
                LOG_INFO("Installing firmware file {}", *cfg.pup_path);
                install_pup(cfg.get_pref_path(), *cfg.pup_path, [](uint32_t progress) {
                    LOG_INFO("Firmware installation progress: {}%", progress);
                });
            }
            if (cfg.pkg_path.has_value() && cfg.pkg_zrif.has_value()) {
                LOG_INFO("Installing pkg from {} ", *cfg.pkg_path);
                emuenv.cache_path = root_paths.get_cache_path().generic_path();
                emuenv.pref_path = cfg.get_pref_path();
                auto pkg_path = fs_utils::utf8_to_path(*cfg.pkg_path);
                install_pkg(pkg_path, emuenv, *cfg.pkg_zrif, [](float) {});
            }
            return Success;
        }
        LOG_ERROR("Failed to initialise config");
        return InitConfigFailed;
    }

#ifdef _WIN32
    {
        auto res = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        LOG_ERROR_IF(res == S_FALSE, "Failed to initialize COM Library");
    }
#endif

    if (cfg.console) {
        cfg.show_gui = false;
        if (logging::init(root_paths, false) != Success)
            return InitConfigFailed;
    } else {
        std::atexit(SDL_Quit);

        // Enable HIDAPI rumble for DS4/DS
        SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");

        // Enable Switch controller
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");

#ifdef __ANDROID__
        // The Audio driver (used by default) is really really bad
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "openslES");
#endif

        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_SENSOR | SDL_INIT_CAMERA)) {
            LOG_ERROR("SDL initialisation failed: {}", SDL_GetError());
            app::error_dialog("SDL initialisation failed.");
            return SDLInitFailed;
        }
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }

    LOG_INFO("{}", window_title);
    LOG_INFO("OS: {}", CppCommon::Environment::OSVersion());
    LOG_INFO("CPU: {} | {} Threads | {} GHz", CppCommon::CPU::Architecture(), CppCommon::CPU::LogicalCores(), static_cast<float>(CppCommon::CPU::ClockSpeed()) / 1000.f);
    LOG_INFO("Available ram memory: {} MiB", SDL_GetSystemRAM());

    app::AppRunType run_type = app::AppRunType::Unknown;
    if (cfg.run_app_path)
        run_type = app::AppRunType::Extracted;

    if (!app::init(emuenv, cfg, root_paths)) {
        app::error_dialog("Emulated environment initialization failed.", emuenv.window.get());
        return 1;
    }
    if (cfg.thor_renderer_trace && emuenv.renderer) {
        emuenv.renderer->renderer_trace_gxm_state = true;
        LOG_INFO("Thor renderer GXM trace enabled from command line");
    }

    if (emuenv.cfg.controller_binds.empty() || (emuenv.cfg.controller_binds.size() != 15))
        gui::reset_controller_binding(emuenv);

    init_libraries(emuenv);

    GuiState gui;

    std::chrono::system_clock::time_point present = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point later = std::chrono::system_clock::now();
    constexpr double frame_time = 1000.0 / 60.0;

    const auto wait_for_frame_done = [&]() {
        // get the current time & get the time we worked for
        present = std::chrono::system_clock::now();
        std::chrono::duration<double, std::milli> work_time = present - later;
        const double speed = std::max<uint32_t>(emuenv.display.speed_percent.load(), 1) / 100.0;
        const double target_frame_time = frame_time / speed;
        // check if we are running faster than ~60fps (16.67ms)
        if (work_time.count() < target_frame_time) {
            // sleep for delta time.
            std::chrono::duration<double, std::milli> delta_ms(target_frame_time - work_time.count());
            auto delta_ms_duration = std::chrono::duration_cast<std::chrono::milliseconds>(delta_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delta_ms_duration.count()));
        }
        // save the later time
        later = std::chrono::system_clock::now();
    };

    if (!cfg.console) {
        gui::pre_init(gui, emuenv);
        bgm_player::init_bgm_player(emuenv.cfg.bgm_volume);
        if (!emuenv.cfg.initial_setup) {
            emuenv.cfg.system_music.emplace(false);
            if (bgm_player::init_bgm(emuenv.pref_path, true))
                bgm_player::switch_bgm_state(true);
            while (!emuenv.cfg.initial_setup) {
                wait_for_frame_done();
                if (handle_events(emuenv, gui)) {
                    gui::draw_begin(gui, emuenv);
                    gui::draw_initial_setup(gui, emuenv);
                    gui::draw_end(gui);
                    emuenv.renderer->swap_window(emuenv.window.get());
                } else
                    return QuitRequested;
            }
            if (!gui.fw_font)
                gui::load_fonts(gui, emuenv, true);
        }
        gui::init(gui, emuenv);
        app::update_viewport(emuenv);
    }

    if (cfg.content_path.has_value()) {
        auto gui_ptr = cfg.console ? nullptr : &gui;
        const auto extention = string_utils::tolower(cfg.content_path->extension().string());
        const auto is_archive = (extention == ".vpk") || (extention == ".zip");
        const auto is_rif = (extention == ".rif") || (extention == "work.bin");
        const auto is_directory = fs::is_directory(*cfg.content_path);

        const auto content_is_app = [&]() {
            if (cfg.cartridge_mode) {
                const ContentInfo content = mount_archive_as_cartridge(emuenv, *cfg.content_path);
                if (content.state) {
                    emuenv.app_info.app_title_id = content.title_id;
                    return true;
                }

                return false;
            }

            std::vector<ContentInfo> contents_info = install_archive(emuenv, gui_ptr, *cfg.content_path);
            const auto content_index = std::find_if(contents_info.begin(), contents_info.end(), [&](const ContentInfo &c) {
                return c.category == "gd";
            });
            if ((content_index != contents_info.end()) && content_index->state) {
                emuenv.app_info.app_title_id = content_index->title_id;
                return true;
            }

            return false;
        };
        if ((is_archive && content_is_app()) || (is_directory && (install_contents(emuenv, gui_ptr, *cfg.content_path) == 1) && (emuenv.app_info.app_category == "gd")))
            run_type = app::AppRunType::Extracted;
        else {
            if (is_rif)
                copy_license(emuenv, *cfg.content_path);
            else if (!is_archive && !is_directory)
                LOG_ERROR("File dropped: [{}] is not supported.", *cfg.content_path);

            emuenv.cfg.content_path.reset();
            if (!cfg.console)
                gui::init_home(gui, emuenv);
        }
    }

    if (run_type == app::AppRunType::Extracted) {
        emuenv.io.app_path = cfg.run_app_path ? *cfg.run_app_path : emuenv.app_info.app_title_id;
        gui::init_user_app(gui, emuenv, emuenv.io.app_path);
        if (emuenv.cfg.run_app_path.has_value())
            emuenv.cfg.run_app_path.reset();
        else if (emuenv.cfg.content_path.has_value())
            emuenv.cfg.content_path.reset();
    }

    if (!cfg.console) {
#if USE_DISCORD
        auto discord_rich_presence_old = emuenv.cfg.discord_rich_presence;
#endif

        // Application not provided via argument, show app selector
        while (run_type == app::AppRunType::Unknown) {
            wait_for_frame_done();

            if (handle_events(emuenv, gui)) {
                gui::draw_begin(gui, emuenv);

#ifdef TRACY_ENABLE
                ZoneScopedN("UI rendering"); // Tracy - Track UI rendering loop scope
#endif

#if USE_DISCORD
                discordrpc::update_init_status(emuenv.cfg.discord_rich_presence, &discord_rich_presence_old);
#endif
                gui::draw_vita_area(gui, emuenv);
                gui::draw_ui(gui, emuenv);

                gui::draw_end(gui);
                emuenv.renderer->swap_window(emuenv.window.get());
#ifdef TRACY_ENABLE
                FrameMark; // Tracy - Frame end mark for UI rendering loop
#endif
            } else {
                return QuitRequested;
            }

            if (!emuenv.io.app_path.empty()) {
                run_type = app::AppRunType::Extracted;
                gui.vita_area.home_screen = false;
                gui.vita_area.live_area_screen = false;
            }
        }
    }

    gui::set_config(emuenv);

    // When backend render is changed before boot app, reboot emu in new backend render and run app
    if ((emuenv.renderer->current_backend != emuenv.backend_renderer)
#ifdef __ANDROID__
        || (emuenv.renderer->current_custom_driver != emuenv.cfg.current_config.custom_driver_name)
#endif
    ) {
        emuenv.load_app_path = emuenv.io.app_path;
        run_execv(argv, emuenv);
        return Success;
    }

    const auto selected_app_path = emuenv.io.app_path;
    emuenv.app_path = selected_app_path;
    const auto APP_INDEX = gui::get_app_index(gui, selected_app_path);
    if (!APP_INDEX) {
        LOG_ERROR("Selected app {} was not found in the application list.", selected_app_path);
        return InvalidApplicationPath;
    }

    if (APP_INDEX->virtual_cartridge) {
        if (APP_INDEX->encrypted_content) {
            LOG_WARN("Virtual cartridge {} [{}] appears encrypted and cannot run directly from ZIP.", APP_INDEX->title_id, APP_INDEX->source_path);
            app::error_dialog(fmt::format("{} appears to be an encrypted Vita dump.\n\nPure ZIP mode needs Vita3K-readable app files from your own legally dumped content.", APP_INDEX->title_id), emuenv.window.get());
            return InvalidApplicationPath;
        }

        ContentInfo mounted_content;
        const auto source_path = fs_utils::utf8_to_path(APP_INDEX->source_path);
        if (fs::is_directory(source_path))
            mounted_content = mount_directory_as_cartridge(emuenv, source_path);
        else
            mounted_content = mount_archive_as_cartridge(emuenv, source_path);

        if (!mounted_content.state) {
            app::error_dialog(fmt::format("Failed to mount virtual cartridge {}", APP_INDEX->source_path), emuenv.window.get());
            return InvalidApplicationPath;
        }

        emuenv.io.app_path = APP_INDEX->title_id;
    }

    emuenv.app_info.app_version = APP_INDEX->app_ver;
    emuenv.app_info.app_category = APP_INDEX->category;
    emuenv.io.addcont = APP_INDEX->addcont;
    emuenv.io.content_id = APP_INDEX->content_id;
    emuenv.io.savedata = APP_INDEX->savedata;
    emuenv.current_app_title = APP_INDEX->title;
    emuenv.app_info.app_short_title = APP_INDEX->stitle;
    emuenv.io.title_id = APP_INDEX->title_id;

#ifdef __ANDROID__
    set_current_game_id(emuenv.io.title_id);
#endif

    // Check license for PS App Only
    get_license(emuenv, emuenv.io.title_id, emuenv.io.content_id);

    if (cfg.console) {
        auto main_thread = emuenv.kernel.get_thread(emuenv.main_thread_id);
        auto lock = std::unique_lock<std::mutex>(main_thread->mutex);
        main_thread->status_cond.wait(lock, [&]() {
            return main_thread->status == ThreadStatus::dormant;
        });
        return Success;
    } else {
        gui.imgui_state->do_clear_screen = false;
    }

    bgm_player::switch_bgm_state(true);
    gui::init_app_background(gui, emuenv, emuenv.io.app_path);
    gui::update_last_time_app_used(gui, emuenv, emuenv.app_path);

    if (!app::late_init(emuenv)) {
        app::error_dialog("Failed to initialize Vita3K", emuenv.window.get());
        return 1;
    }

    const auto draw_app_background = [](GuiState &gui, EmuEnvState &emuenv) {
        const auto pos_min = ImVec2(emuenv.logical_viewport_pos.x, emuenv.logical_viewport_pos.y);
        const auto pos_max = ImVec2(pos_min.x + emuenv.logical_viewport_size.x, pos_min.y + emuenv.logical_viewport_size.y);

        if (gui.apps_background.contains(emuenv.io.app_path))
            // Display application background
            ImGui::GetBackgroundDrawList()->AddImage(gui.apps_background[emuenv.io.app_path], pos_min, pos_max);
        // Application background not found
        else
            gui::draw_background(gui, emuenv);
    };

    int32_t main_module_id;
    {
        const auto err = load_app(main_module_id, emuenv);
        if (err != Success)
            return err;
    }
    gui.vita_area.information_bar = false;

    // Pre-Compile Shaders
    emuenv.renderer->set_app(emuenv.io.title_id.c_str(), emuenv.self_name.c_str());
    if (renderer::get_shaders_cache_hashs(*emuenv.renderer) && cfg.shader_cache) {
        SDL_SetWindowTitle(emuenv.window.get(), fmt::format("{} | {} ({}) | Please wait, compiling shaders...", window_title, emuenv.current_app_title, emuenv.io.title_id).c_str());
        for (const auto &hash : emuenv.renderer->shaders_cache_hashs) {
            handle_events(emuenv, gui);
            gui::draw_begin(gui, emuenv);
            draw_app_background(gui, emuenv);

            emuenv.renderer->precompile_shader(hash);
            gui::draw_pre_compiling_shaders_progress(gui, emuenv, static_cast<uint32_t>(emuenv.renderer->shaders_cache_hashs.size()));

            gui::draw_end(gui);
            emuenv.renderer->swap_window(emuenv.window.get());
        }
    }
    {
        const auto err = run_app(emuenv, main_module_id);
        if (err != Success)
            return err;
    }
    auto runtime_cheats = load_runtime_cheats(emuenv, main_module_id);
    SDL_SetWindowTitle(emuenv.window.get(), fmt::format("{} | {} ({}) | Please wait, loading...", window_title, emuenv.current_app_title, emuenv.io.title_id).c_str());

    while (handle_events(emuenv, gui) && (emuenv.frame_count == 0) && !emuenv.load_exec) {
#ifdef TRACY_ENABLE
        ZoneScopedN("Game loading"); // Tracy - Track game loading loop scope
#endif
        wait_for_frame_done();
        apply_runtime_cheats(emuenv, runtime_cheats);

        // Driver acto!
        renderer::process_batches(*emuenv.renderer.get(), emuenv.renderer->features, emuenv.mem, emuenv.cfg);

        const SceFVector2 viewport_pos = { emuenv.drawable_viewport_pos.x, emuenv.drawable_viewport_pos.y };
        const SceFVector2 viewport_size = { emuenv.drawable_viewport_size.x, emuenv.drawable_viewport_size.y };
        emuenv.renderer->render_frame(viewport_pos, viewport_size, emuenv.display, emuenv.gxm, emuenv.mem);

        gui::draw_begin(gui, emuenv);
        gui::draw_common_dialog(gui, emuenv);
        draw_app_background(gui, emuenv);

        gui::draw_end(gui);
        emuenv.renderer->swap_window(emuenv.window.get());
#ifdef TRACY_ENABLE
        FrameMark; // Tracy - Frame end mark for game loading loop
#endif
    }

    while (handle_events(emuenv, gui) && !emuenv.load_exec) {
#ifdef TRACY_ENABLE
        ZoneScopedN("Game rendering"); // Tracy - Track game rendering loop scope
#endif
        if (emuenv.kernel.is_threads_paused())
            wait_for_frame_done();

        apply_runtime_cheats(emuenv, runtime_cheats);

        // Driver acto!
        renderer::process_batches(*emuenv.renderer.get(), emuenv.renderer->features, emuenv.mem, emuenv.cfg);

        const SceFVector2 viewport_pos = { emuenv.drawable_viewport_pos.x, emuenv.drawable_viewport_pos.y };
        const SceFVector2 viewport_size = { emuenv.drawable_viewport_size.x, emuenv.drawable_viewport_size.y };
        emuenv.renderer->render_frame(viewport_pos, viewport_size, emuenv.display, emuenv.gxm, emuenv.mem);
        // Calculate FPS
        app::calculate_fps(emuenv);

        // Set shaders compiled display
        gui::set_shaders_compiled_display(gui, emuenv);

        gui::draw_begin(gui, emuenv);
        if (!emuenv.kernel.is_threads_paused())
            gui::draw_common_dialog(gui, emuenv);
        gui::draw_vita_area(gui, emuenv);
        draw_runtime_status_overlay(emuenv, runtime_cheats);

        if (emuenv.cfg.performance_overlay && !emuenv.kernel.is_threads_paused() && (emuenv.common_dialog.status != SCE_COMMON_DIALOG_STATUS_RUNNING)) {
            ImGui::PushFont(gui.vita_font[emuenv.current_font_level]);
            gui::draw_perf_overlay(gui, emuenv);
            ImGui::PopFont();
        }

        if (emuenv.cfg.current_config.show_touchpad_cursor && !emuenv.kernel.is_threads_paused())
            gui::draw_touchpad_cursor(emuenv);

        if (emuenv.display.imgui_render) {
            gui::draw_ui(gui, emuenv);
        }
        draw_runtime_osd(gui, emuenv, runtime_cheats, main_module_id);

        gui::draw_end(gui);
        emuenv.renderer->swap_window(emuenv.window.get());
#ifdef TRACY_ENABLE
        FrameMark; // Tracy - Frame end mark for game rendering loop
#endif
    }

#ifdef _WIN32
    CoUninitialize();
#endif

    emuenv.renderer->preclose_action();
    app::destroy(emuenv, gui.imgui_state.get());

    if (emuenv.load_exec)
        run_execv(argv, emuenv);

    return Success;
}
