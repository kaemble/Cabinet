#include "hardware_video.h"
#include <cstdint>
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace
{
void require(bool ok, const char* operation)
{
    if (!ok)
    {
        throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }
}
template <typename T> void load(T& function, const char* name)
{
    function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (!function)
    {
        throw std::runtime_error(std::string("Missing OpenGL function: ") + name);
    }
}
}
struct HardwareVideo::Impl
{
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr, ui_context = nullptr;
    SDL_Window* ui_window = nullptr;
    SDL_Renderer* renderer = nullptr;
    GLuint fbo = 0, color = 0, depth = 0;
    unsigned width = 0, height = 0;
    bool flip = false, modern = false;
    bool sync_capable = false;
    bool es = false, needs_depth = false, needs_stencil = false;

#define FUNCTION(api_name, name) decltype(&gl##api_name) name = nullptr
    FUNCTION(GenFramebuffers, gen_framebuffers);
    FUNCTION(BindFramebuffer, bind_framebuffer);
    FUNCTION(DeleteFramebuffers, delete_framebuffers);
    FUNCTION(CheckFramebufferStatus, check_framebuffer_status);
    FUNCTION(FramebufferTexture2D, framebuffer_texture_2d);
    FUNCTION(GenTextures, gen_textures);
    FUNCTION(BindTexture, bind_texture);
    FUNCTION(TexImage2D, tex_image_2d);
    FUNCTION(TexParameteri, tex_parameteri);
    FUNCTION(DeleteTextures, delete_textures);
    FUNCTION(GenRenderbuffers, gen_renderbuffers);
    FUNCTION(BindRenderbuffer, bind_renderbuffer);
    FUNCTION(RenderbufferStorage, renderbuffer_storage);
    FUNCTION(FramebufferRenderbuffer, framebuffer_renderbuffer);
    FUNCTION(DeleteRenderbuffers, delete_renderbuffers);
    FUNCTION(Flush, flush);
    FUNCTION(FenceSync, fence_sync);
    FUNCTION(WaitSync, wait_sync);
    FUNCTION(DeleteSync, delete_sync);
    FUNCTION(Finish, finish);
    FUNCTION(GetIntegerv, get_integerv);
    FUNCTION(GetString, get_string);
#undef FUNCTION
    ~Impl()
    {
        if (context)
        {
            SDL_GL_MakeCurrent(window, context);
            if (fbo)
            {
                delete_framebuffers(1, &fbo);
            }
            if (depth)
            {
                delete_renderbuffers(1, &depth);
            }
            if (color)
            {
                delete_textures(1, &color);
            }
            SDL_GL_DestroyContext(context);
        }
        if (window)
        {
            SDL_DestroyWindow(window);
        }
    }
};
HardwareVideo::HardwareVideo() : impl(std::make_unique<Impl>())
{
}
HardwareVideo::~HardwareVideo() = default;
bool HardwareVideo::supports(retro_hw_context_type type)
{
    return type == RETRO_HW_CONTEXT_OPENGL || type == RETRO_HW_CONTEXT_OPENGL_CORE ||
           type == RETRO_HW_CONTEXT_OPENGLES2 || type == RETRO_HW_CONTEXT_OPENGLES3 ||
           type == RETRO_HW_CONTEXT_OPENGLES_VERSION;
}
void HardwareVideo::create_context(const retro_hw_render_callback& cb, SDL_Window* share_window,
                                   SDL_GLContext share_context)
{
    auto& g = *impl;
    if (g.context)
    {
        throw std::runtime_error("Hardware context already created");
    }
    const bool es = cb.context_type == RETRO_HW_CONTEXT_OPENGLES2 || cb.context_type == RETRO_HW_CONTEXT_OPENGLES3 ||
                    cb.context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION;
    int major = 2, minor = es ? 0 : 1;
    if (cb.context_type == RETRO_HW_CONTEXT_OPENGLES3)
    {
        major = 3;
        minor = 0;
    }
    if (cb.context_type == RETRO_HW_CONTEXT_OPENGL_CORE || cb.context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION)
    {
        major = cb.version_major;
        minor = cb.version_minor;
    }
    SDL_GL_ResetAttributes();
    require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major), "GL version");
    require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor), "GL version");
    require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, es ? SDL_GL_CONTEXT_PROFILE_ES
                                                             : (cb.context_type == RETRO_HW_CONTEXT_OPENGL_CORE)
                                                                 ? SDL_GL_CONTEXT_PROFILE_CORE
                                                                 : 0),
            "GL profile");
    require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, cb.debug_context ? SDL_GL_CONTEXT_DEBUG_FLAG : 0), "GL flags");
    if (share_context)
    {
        require(SDL_GL_MakeCurrent(share_window, share_context), "Activate shared graphics context");
        require(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1), "Share graphics objects");
    }
    // A hidden drawable keeps the core context intact across visible-window resizes/fullscreen changes.
    g.window = SDL_CreateWindow("Cabinet core context", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    require(g.window != nullptr, "Create hardware drawable");
    g.context = SDL_GL_CreateContext(g.window);
    require(g.context != nullptr, "Create requested hardware context");
    SDL_GL_ResetAttributes();
    make_current();

#define LOAD(api_name, name) load(g.name, "gl" #api_name)
    LOAD(Flush, flush);
    LOAD(Finish, finish);
    LOAD(GenFramebuffers, gen_framebuffers);
    LOAD(BindFramebuffer, bind_framebuffer);
    LOAD(DeleteFramebuffers, delete_framebuffers);
    LOAD(CheckFramebufferStatus, check_framebuffer_status);
    LOAD(FramebufferTexture2D, framebuffer_texture_2d);
    LOAD(GenTextures, gen_textures);
    LOAD(BindTexture, bind_texture);
    LOAD(TexImage2D, tex_image_2d);
    LOAD(TexParameteri, tex_parameteri);
    LOAD(DeleteTextures, delete_textures);
    LOAD(GenRenderbuffers, gen_renderbuffers);
    LOAD(BindRenderbuffer, bind_renderbuffer);
    LOAD(RenderbufferStorage, renderbuffer_storage);
    LOAD(FramebufferRenderbuffer, framebuffer_renderbuffer);
    LOAD(DeleteRenderbuffers, delete_renderbuffers);
    LOAD(GetIntegerv, get_integerv);
    LOAD(GetString, get_string);
#undef LOAD
    // Compatibility/GLES2 requests may be satisfied by newer contexts.
    const char* version = reinterpret_cast<const char*>(g.get_string(GL_VERSION));
    int actual_major = major, actual_minor = minor;
    if (version)
    {
        std::sscanf(version, es ? "OpenGL ES %d.%d" : "%d.%d", &actual_major, &actual_minor);
    }
    g.sync_capable = es ? actual_major >= 3 : actual_major > 3 || (actual_major == 3 && actual_minor >= 2);
    if (g.sync_capable)
    {
        load(g.fence_sync, "glFenceSync");
        load(g.wait_sync, "glWaitSync");
        load(g.delete_sync, "glDeleteSync");
    }
    g.modern = actual_major >= 3;
    g.flip = cb.bottom_left_origin;
    g.es = es;
    g.needs_depth = cb.depth;
    g.needs_stencil = cb.stencil;
}
void HardwareVideo::create_framebuffer(unsigned width, unsigned height)
{
    auto& g = *impl;
    if (!g.context)
    {
        throw std::runtime_error("Framebuffer requires a context");
    }
    if (g.fbo && width == g.width && height == g.height)
    {
        return;
    }
    make_current();
    GLint limit = 0, rb_limit = 0;
    g.get_integerv(GL_MAX_TEXTURE_SIZE, &limit);
    g.get_integerv(GL_MAX_RENDERBUFFER_SIZE, &rb_limit);
    if (!width || !height || width > static_cast<unsigned>(std::min(limit, rb_limit)) ||
        height > static_cast<unsigned>(std::min(limit, rb_limit)))
    {
        throw std::runtime_error("Core requested an unsupported hardware framebuffer size");
    }
    const bool resizing = g.fbo != 0;
    GLint texture = 0, renderbuffer = 0, draw_framebuffer = 0, read_framebuffer = 0;
    if (resizing)
    {
        g.get_integerv(GL_TEXTURE_BINDING_2D, &texture);
        g.get_integerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        g.get_integerv(g.modern ? GL_DRAW_FRAMEBUFFER_BINDING : GL_FRAMEBUFFER_BINDING, &draw_framebuffer);
        if (g.modern)
        {
            g.get_integerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
        }
    }
    g.width = width;
    g.height = height;
    if (!g.color)
    {
        g.gen_textures(1, &g.color);
    }
    g.bind_texture(GL_TEXTURE_2D, g.color);
    g.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g.tex_image_2d(GL_TEXTURE_2D, 0, (g.es && !g.modern) ? GL_RGBA : GL_RGBA8, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
    if (!g.fbo)
    {
        g.gen_framebuffers(1, &g.fbo);
    }
    g.bind_framebuffer(GL_FRAMEBUFFER, g.fbo);
    g.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.color, 0);
    if (g.needs_depth)
    {
        if (!g.depth)
        {
            g.gen_renderbuffers(1, &g.depth);
        }
        g.bind_renderbuffer(GL_RENDERBUFFER, g.depth);
        g.renderbuffer_storage(GL_RENDERBUFFER, g.needs_stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT16, width,
                               height);
        g.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g.depth);
        if (g.needs_stencil)
        {
            g.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g.depth);
        }
    }
    if (g.check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        throw std::runtime_error(
            "Cannot create complete hardware framebuffer with requested depth/stencil attachments");
    }
    g.bind_texture(GL_TEXTURE_2D, texture);
    g.bind_renderbuffer(GL_RENDERBUFFER, renderbuffer);
    if (resizing)
    {
        g.bind_framebuffer(g.modern ? GL_DRAW_FRAMEBUFFER : GL_FRAMEBUFFER, draw_framebuffer);
        if (g.modern)
        {
            g.bind_framebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);
        }
    }
}

void HardwareVideo::attach_renderer(SDL_Window* window, SDL_Renderer* renderer, SDL_GLContext context)
{
    impl->ui_window = window;
    impl->renderer = renderer;
    impl->ui_context = context;
    require(SDL_GL_MakeCurrent(window, context), "Activate shared renderer");
    int major = 0, minor = 0;
    const char* version = reinterpret_cast<const char*>(impl->get_string(GL_VERSION));
    if (version)
    {
        std::sscanf(version, impl->es ? "OpenGL ES %d.%d" : "%d.%d", &major, &minor);
    }
    impl->sync_capable = impl->sync_capable && (impl->es ? major >= 3 : major > 3 || (major == 3 && minor >= 2));
}
SDL_GLContext HardwareVideo::renderer_context() const
{
    return impl->ui_context;
}
SDL_Renderer* HardwareVideo::create_renderer(SDL_Window* window)
{
    make_current();
    require(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1), "Share renderer context");
    auto* renderer = SDL_CreateRenderer(window, impl->es ? "opengles2" : "opengl");
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    require(renderer != nullptr, "Create shared OpenGL renderer");
    attach_renderer(window, renderer, SDL_GL_GetCurrentContext());
    return renderer;
}
void HardwareVideo::make_current()
{
    auto& g = *impl;
    const auto current = SDL_GL_GetCurrentContext();
    if (!g.context || (current == g.context && SDL_GL_GetCurrentWindow() == g.window))
    {
        return;
    }
    GLsync fence = nullptr;
    if (g.ui_context && current == g.ui_context)
    {
        require(SDL_FlushRenderer(g.renderer), "Flush presentation commands");
        if (g.sync_capable)
        {
            fence = g.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            g.flush();
        }
        if (!fence)
        {
            g.finish();
        }
    }
    require(SDL_GL_MakeCurrent(g.window, g.context), "Activate core graphics context");
    if (fence)
    {
        g.wait_sync(fence, 0, GL_TIMEOUT_IGNORED);
        g.delete_sync(fence);
    }
}
void HardwareVideo::finish_frame()
{
    make_current();
    auto& g = *impl;
    GLsync fence = g.sync_capable ? g.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0) : nullptr;
    if (fence)
    {
        g.flush();
    }
    else
    {
        g.finish();
    }
    require(SDL_GL_MakeCurrent(g.ui_window, g.ui_context), "Activate presentation context");
    if (fence)
    {
        g.wait_sync(fence, 0, GL_TIMEOUT_IGNORED);
        g.delete_sync(fence);
    }
}
SDL_Texture* HardwareVideo::wrap_texture()
{
    auto& g = *impl;
    if (SDL_GL_GetCurrentContext() != g.ui_context || SDL_GL_GetCurrentWindow() != g.ui_window)
    {
        require(SDL_GL_MakeCurrent(g.ui_window, g.ui_context), "Activate presentation context");
    }
    auto properties = SDL_CreateProperties();
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, g.width);
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, g.height);
    SDL_SetNumberProperty(properties,
                          g.es ? SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER
                               : SDL_PROP_TEXTURE_CREATE_OPENGL_TEXTURE_NUMBER,
                          g.color);
    auto* texture = SDL_CreateTextureWithProperties(g.renderer, properties);
    SDL_DestroyProperties(properties);
    require(texture != nullptr, "Wrap core framebuffer texture");
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    return texture;
}
std::uintptr_t HardwareVideo::framebuffer() const
{
    return impl->fbo;
}
bool HardwareVideo::flipped() const
{
    return impl->flip;
}
void HardwareVideo::validate_frame(unsigned width, unsigned height) const
{
    if (!impl->fbo || !width || !height || width > impl->width || height > impl->height)
    {
        throw std::runtime_error("Hardware video frame exceeds the core's declared maximum dimensions");
    }
}
