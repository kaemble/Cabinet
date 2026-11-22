#include "secondary_window.h"
#include "hardware_video.h"
#include "vulkan_video.h"
#include <SDL3/SDL_opengl.h>
#include <stdexcept>

namespace
{
void check(bool ok)
{
    if (!ok)
    {
        throw std::runtime_error(SDL_GetError());
    }
}
}
SecondaryWindow::~SecondaryWindow()
{
    if (vulkan_)
    {
        vulkan_->destroy_secondary_renderer();
    }
    else
    {
        if (texture_)
        {
            SDL_DestroyTexture(texture_);
        }
        if (renderer)
        {
            SDL_DestroyRenderer(renderer);
        }
    }
    if (main_context_)
    {
        SDL_GL_MakeCurrent(main_window_, main_context_);
    }
    if (window)
    {
        SDL_DestroyWindow(window);
    }
}
void SecondaryWindow::create(SDL_Window* main_window, SDL_Renderer* main_renderer, HardwareVideo* hardware,
                             VulkanVideo* vulkan, const std::string& title, int width, int height)
{
    main_window_ = main_window;
    main_renderer_ = main_renderer;
    window = SDL_CreateWindow((title + " — Bottom screen").c_str(), width, height,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (vulkan ? SDL_WINDOW_VULKAN : 0));
    check(window != nullptr);
    SDL_SetWindowMinimumSize(window, 256, 192);
    int count = 0;
    if (auto* displays = SDL_GetDisplays(&count))
    {
        const auto main_display = SDL_GetDisplayForWindow(main_window);
        for (int i = 0; i < count; ++i)
        {
            if (displays[i] != main_display)
            {
                SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(displays[i]),
                                      SDL_WINDOWPOS_CENTERED_DISPLAY(displays[i]));
                break;
            }
        }
        SDL_free(displays);
    }
    if (vulkan)
    {
        vulkan_ = vulkan;
        renderer = vulkan_->create_secondary_renderer(window);
    }
    else if (hardware)
    {
        main_context_ = hardware->renderer_context();
        check(SDL_GL_MakeCurrent(main_window_, main_context_));
        check(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1));
        renderer = SDL_CreateRenderer(window, SDL_GetRendererName(main_renderer));
        context_ = SDL_GL_GetCurrentContext();
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
        check(renderer != nullptr);
        check(SDL_GL_MakeCurrent(main_window_, main_context_));
        fence_sync_ = SDL_GL_GetProcAddress("glFenceSync");
        wait_sync_ = SDL_GL_GetProcAddress("glWaitSync");
        delete_sync_ = SDL_GL_GetProcAddress("glDeleteSync");
        flush_ = SDL_GL_GetProcAddress("glFlush");
        finish_ = SDL_GL_GetProcAddress("glFinish");
        get_integer_ = SDL_GL_GetProcAddress("glGetIntegerv");
        bind_texture_ = SDL_GL_GetProcAddress("glBindTexture");
        texture_parameter_ = SDL_GL_GetProcAddress("glTexParameteri");
    }
    else
    {
        renderer = SDL_CreateRenderer(window, SDL_GetRendererName(main_renderer));
    }
    check(renderer != nullptr);
    SDL_SetRenderVSync(renderer, 0); // The primary surface is the only pacing clock.
    check(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
}
void SecondaryWindow::upload(const void* pixels, unsigned width, unsigned height, int pitch, SDL_PixelFormat format)
{
    if (main_context_ || vulkan_)
    {
        return;
    }
    if (!texture_ || width_ != width || height_ != height || format_ != format)
    {
        if (texture_)
        {
            SDL_DestroyTexture(texture_);
        }
        texture_ = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING, width, height);
        check(texture_ != nullptr);
        check(SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE));
        width_ = width;
        height_ = height;
        format_ = format;
    }
    check(SDL_UpdateTexture(texture_, nullptr, pixels, pitch));
}
SDL_Texture* SecondaryWindow::texture(SDL_Texture* primary)
{
    if (vulkan_)
    {
        return vulkan_->secondary_texture();
    }
    if (!main_context_)
    {
        return texture_;
    }
    primary_ = primary;
    if (!primary)
    {
        return nullptr;
    }
    const auto properties = SDL_GetTextureProperties(primary);
    const auto native =
        SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER,
                              SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0));
    const auto width = SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_WIDTH_NUMBER, 0);
    const auto height = SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_HEIGHT_NUMBER, 0);
    if (!texture_ || native_ != native || width_ != width || height_ != height)
    {
        if (texture_)
        {
            SDL_DestroyTexture(texture_);
        }
        auto p = SDL_CreateProperties();
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(p,
                              std::string(SDL_GetRendererName(renderer)) == "opengles2"
                                  ? SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER
                                  : SDL_PROP_TEXTURE_CREATE_OPENGL_TEXTURE_NUMBER,
                              native);
        texture_ = SDL_CreateTextureWithProperties(renderer, p);
        SDL_DestroyProperties(p);
        check(texture_ != nullptr);
        check(SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE));
        native_ = native;
        width_ = width;
        height_ = height;
    }
    restore_filter(texture_);
    return texture_;
}
void SecondaryWindow::restore_filter(SDL_Texture* state)
{
    // SDL caches filtering per wrapper, but GL stores it on the shared native
    // texture. Each renderer must see the filtering its own cache expects.
    if (!state || !native_)
    {
        return;
    }
    auto get = reinterpret_cast<void(APIENTRY*)(GLenum, GLint*)>(get_integer_);
    auto bind = reinterpret_cast<void(APIENTRY*)(GLenum, GLuint)>(bind_texture_);
    auto parameter = reinterpret_cast<void(APIENTRY*)(GLenum, GLenum, GLint)>(texture_parameter_);
    SDL_ScaleMode mode, other;
    check(SDL_GetTextureScaleMode(state, &mode));
    check(SDL_GetTextureScaleMode((state == primary_) ? texture_ : primary_, &other));
    if (mode == other)
    {
        return;
    }
    GLint previous = 0;
    get(GL_TEXTURE_BINDING_2D, &previous);
    bind(GL_TEXTURE_2D, static_cast<GLuint>(native_));
    parameter(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (mode == SDL_SCALEMODE_LINEAR) ? GL_LINEAR : GL_NEAREST);
    parameter(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (mode == SDL_SCALEMODE_LINEAR) ? GL_LINEAR : GL_NEAREST);
    bind(GL_TEXTURE_2D, previous);
}
void SecondaryWindow::handoff(SDL_Window* target, SDL_GLContext context)
{
    auto fence_sync = reinterpret_cast<PFNGLFENCESYNCPROC>(fence_sync_);
    auto wait_sync = reinterpret_cast<PFNGLWAITSYNCPROC>(wait_sync_);
    auto delete_sync = reinterpret_cast<PFNGLDELETESYNCPROC>(delete_sync_);
    auto flush = reinterpret_cast<void(APIENTRY*)()>(flush_);
    GLsync fence = (fence_sync && wait_sync && delete_sync) ? fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0) : nullptr;
    if (fence)
    {
        flush();
    }
    else
    {
        reinterpret_cast<void(APIENTRY*)()>(finish_)();
    }
    check(SDL_GL_MakeCurrent(target, context));
    if (fence)
    {
        wait_sync(fence, 0, GL_TIMEOUT_IGNORED);
        delete_sync(fence);
    }
}
void SecondaryWindow::begin_draw()
{
    if (!main_context_)
    {
        return;
    }
    check(SDL_FlushRenderer(main_renderer_));
    handoff(window, context_);
}
void SecondaryWindow::end_draw()
{
    if (!main_context_)
    {
        return;
    }
    check(SDL_FlushRenderer(renderer));
    handoff(main_window_, main_context_);
    restore_filter(primary_);
}
