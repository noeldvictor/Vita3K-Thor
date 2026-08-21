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

#pragma once

#include <renderer/frame_host.h>

#include <string>
#include <vector>

typedef struct SDL_Window SDL_Window;
typedef struct SDL_GLContextState *SDL_GLContext;

namespace app {

/**
 * @brief renderer::FrameHost backed by an SDL_Window.
 *
 * Upstream only ships two FrameHost implementations - Qt's game_window on
 * desktop and main_android's on Android - so adopting upstream's renderer would
 * otherwise drag in a hard Qt 6.11 dependency. Thor keeps its SDL/ImGui
 * frontend, so it needs this one.
 *
 * The renderer only ever asks for a native display handle, the drawable size,
 * and (for the GL backend) context management, all of which SDL3 exposes
 * directly.
 */
class SdlFrameHost final : public renderer::FrameHost {
public:
    explicit SdlFrameHost(SDL_Window *window);
    ~SdlFrameHost() override;

    renderer::DisplayHandle handle() const override;
    int drawable_width() const override;
    int drawable_height() const override;
    std::vector<std::string> font_dirs() const override;

    void *get_proc_address(const char *name) const override;
    unsigned int default_fbo() const override;

    bool make_current() override;
    void done_current() override;
    void swap_buffers() override;
    bool set_vsync(bool enabled) override;

    void prepare_for_render_thread() override;
    void finalize_render_thread_start() override;
    void destroy_render_context() override;

    SDL_Window *window() const { return m_window; }

    /// Creates the GL context. No-op for the Vulkan backend.
    bool create_gl_context();

private:
    SDL_Window *m_window = nullptr;
    SDL_GLContext m_gl_context = nullptr;
};

} // namespace app
