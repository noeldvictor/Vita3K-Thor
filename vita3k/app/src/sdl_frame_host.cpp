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

#include <app/sdl_frame_host.h>

#include <util/log.h>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

namespace app {

SdlFrameHost::SdlFrameHost(SDL_Window *window)
    : m_window(window) {
}

SdlFrameHost::~SdlFrameHost() {
    destroy_render_context();
}

renderer::DisplayHandle SdlFrameHost::handle() const {
    if (!m_window)
        return std::monostate{};

    const SDL_PropertiesID props = SDL_GetWindowProperties(m_window);

#if defined(_WIN32)
    renderer::Win32DisplayHandle win32;
    win32.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    return win32;
#elif defined(__APPLE__)
    renderer::MacOSDisplayHandle macos;
    macos.view = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    return macos;
#elif defined(__ANDROID__)
    renderer::AndroidDisplayHandle android;
    android.window = m_window;
    return android;
#else
    // Wayland first: on a Wayland session SDL still reports an X11 handle under
    // XWayland, and the native surface is the one the swapchain wants.
    if (void *wl_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr)) {
        renderer::WaylandDisplayHandle wayland;
        wayland.display = wl_display;
        wayland.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (wayland.surface)
            return wayland;
    }

    if (void *x_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr)) {
        renderer::X11DisplayHandle x11;
        x11.display = x_display;
        x11.window = static_cast<std::uintptr_t>(
            SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        return x11;
    }

    LOG_ERROR("SDL window exposes no usable native display handle");
    return std::monostate{};
#endif
}

int SdlFrameHost::drawable_width() const {
    int w = 0;
    int h = 0;
    if (m_window)
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
    return w;
}

int SdlFrameHost::drawable_height() const {
    int w = 0;
    int h = 0;
    if (m_window)
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
    return h;
}

std::vector<std::string> SdlFrameHost::font_dirs() const {
    // Qt enumerates system font directories for the overlay; SDL has no
    // equivalent, so the overlay falls back to the firmware fonts.
    return {};
}

void *SdlFrameHost::get_proc_address(const char *name) const {
    return reinterpret_cast<void *>(SDL_GL_GetProcAddress(name));
}

unsigned int SdlFrameHost::default_fbo() const {
    return 0;
}

bool SdlFrameHost::create_gl_context() {
    if (m_gl_context)
        return true;

    m_gl_context = SDL_GL_CreateContext(m_window);
    if (!m_gl_context) {
        LOG_ERROR("Failed to create GL context: {}", SDL_GetError());
        return false;
    }

    return true;
}

bool SdlFrameHost::make_current() {
    if (!m_gl_context && !create_gl_context())
        return false;

    return SDL_GL_MakeCurrent(m_window, m_gl_context);
}

void SdlFrameHost::done_current() {
    SDL_GL_MakeCurrent(m_window, nullptr);
}

void SdlFrameHost::swap_buffers() {
    SDL_GL_SwapWindow(m_window);
}

bool SdlFrameHost::set_vsync(bool enabled) {
    return SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

void SdlFrameHost::prepare_for_render_thread() {
    // The GL context is created on whichever thread first calls make_current(),
    // so release it here and let the render thread take it.
    done_current();
}

void SdlFrameHost::finalize_render_thread_start() {
}

void SdlFrameHost::destroy_render_context() {
    if (!m_gl_context)
        return;

    SDL_GL_DestroyContext(m_gl_context);
    m_gl_context = nullptr;
}

} // namespace app
