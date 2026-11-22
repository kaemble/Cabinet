#include "armada_gbm.h"
#include <cstdint>
#include <SDL3/SDL_opengl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void require(bool ok, const char* message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}
bool extension(const char* list, const char* name)
{
    if (!list)
    {
        return false;
    }
    const std::string extensions = std::string(" ") + list + " ";
    return extensions.find(std::string(" ") + name + " ") != std::string::npos;
}
template <typename T> T gl_proc(const char* name)
{
    auto function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    require(function != nullptr, name);
    return function;
}
template <typename T> T egl_proc(const char* name)
{
    auto function = reinterpret_cast<T>(eglGetProcAddress(name));
    require(function != nullptr, name);
    return function;
}
struct Fd
{
    int value;
    ~Fd()
    {
        if (value >= 0)
        {
            close(value);
        }
    }
};
}

struct ArmadaGbm::Impl
{
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    gbm_device* gbm = nullptr;
    int fd = -1;
    bool modifiers = false, native_fences = false;
    struct Buffer
    {
        gbm_bo* bo = nullptr;
        std::uint32_t fb = 0;
        EGLImageKHR image = EGL_NO_IMAGE_KHR;
        SDL_Texture* texture = nullptr;
    };
    std::array<Buffer, 2> buffers{};
    EGLSyncKHR sync = EGL_NO_SYNC_KHR;
    PFNEGLCREATEIMAGEKHRPROC create_image = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = nullptr;
    PFNEGLCREATESYNCKHRPROC create_sync = nullptr;
    PFNEGLDESTROYSYNCKHRPROC destroy_sync = nullptr;
    PFNEGLCLIENTWAITSYNCKHRPROC client_wait = nullptr;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_fence = nullptr;
    using ImageTarget = void(APIENTRY*)(GLenum, void*);
    ImageTarget image_target = nullptr;
    decltype(&glBindTexture) bind_texture = nullptr;
    decltype(&glGetIntegerv) get_integer = nullptr;
    decltype(&glGetError) get_error = nullptr;
    decltype(&glFlush) flush = nullptr;
    decltype(&glFinish) finish = nullptr;

    ~Impl()
    {
        if (context)
        {
            SDL_GL_MakeCurrent(window, context);
        }
        // Teardown only: finish outstanding GPU work before freeing imported
        // images. Steady-state presentation uses native fences or zero-time polls.
        if (finish)
        {
            finish();
        }
        if (sync != EGL_NO_SYNC_KHR)
        {
            destroy_sync(display, sync);
        }
        clear_buffers();
        if (gbm)
        {
            gbm_device_destroy(gbm);
        }
    }
    void clear_buffers()
    {
        for (auto& b : buffers)
        {
            if (b.texture)
            {
                SDL_DestroyTexture(b.texture);
            }
            if (b.image != EGL_NO_IMAGE_KHR)
            {
                destroy_image(display, b.image);
            }
            if (b.fb)
            {
                drmModeRmFB(fd, b.fb);
            }
            if (b.bo)
            {
                gbm_bo_destroy(b.bo);
            }
            b = {};
        }
    }
    std::vector<std::uint64_t> matching_modifiers(std::uint32_t plane)
    {
        if (!modifiers)
        {
            return {};
        }
        auto query = egl_proc<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>("eglQueryDmaBufModifiersEXT");
        EGLint count = 0;
        require(query(display, DRM_FORMAT_XRGB8888, 0, nullptr, nullptr, &count), "cannot query EGL modifiers");
        std::vector<EGLuint64KHR> values(count);
        std::vector<EGLBoolean> external(count);
        require(query(display, DRM_FORMAT_XRGB8888, count, values.data(), external.data(), &count),
                "cannot read EGL modifiers");
        auto* properties = drmModeObjectGetProperties(fd, plane, DRM_MODE_OBJECT_PLANE);
        if (!properties)
        {
            return {};
        }
        std::uint32_t blob_id = 0;
        for (std::uint32_t i = 0; i < properties->count_props; ++i)
        {
            auto* prop = drmModeGetProperty(fd, properties->props[i]);
            if (prop && std::strcmp(prop->name, "IN_FORMATS") == 0)
            {
                blob_id = properties->prop_values[i];
            }
            if (prop)
            {
                drmModeFreeProperty(prop);
            }
        }
        drmModeFreeObjectProperties(properties);
        std::vector<std::uint64_t> result;
        auto* blob = blob_id ? drmModeGetPropertyBlob(fd, blob_id) : nullptr;
        if (!blob)
        {
            return result;
        }
        drmModeFormatModifierIterator iterator{};
        while (drmModeFormatModifierBlobIterNext(blob, &iterator))
        {
            if (iterator.fmt != DRM_FORMAT_XRGB8888)
            {
                continue;
            }
            for (int i = 0; i < count; ++i)
            {
                if (!external[i] && values[i] == iterator.mod)
                {
                    result.push_back(iterator.mod);
                    break;
                }
            }
        }
        drmModeFreePropertyBlob(blob);
        return result;
    }
    void create_buffer(Buffer& b, int width, int height, const std::vector<std::uint64_t>& candidates, bool linear)
    {
        if (!candidates.empty() && !linear)
        {
            b.bo = gbm_bo_create_with_modifiers2(gbm, width, height, GBM_FORMAT_XRGB8888, candidates.data(),
                                                 candidates.size(), GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
        }
        else
        {
            b.bo = gbm_bo_create(gbm, width, height, GBM_FORMAT_XRGB8888,
                                 GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | (linear ? GBM_BO_USE_LINEAR : 0));
        }
        require(b.bo != nullptr, "cannot allocate renderable GBM scanout buffer");
        const int planes = gbm_bo_get_plane_count(b.bo);
        require(planes > 0 && planes <= 4, "unsupported GBM plane count");
        const auto modifier = gbm_bo_get_modifier(b.bo);
        require(modifiers || modifier == DRM_FORMAT_MOD_INVALID || modifier == DRM_FORMAT_MOD_LINEAR,
                "EGL cannot import the allocated non-linear buffer modifier");
        std::uint32_t handles[4]{}, pitches[4]{}, offsets[4]{};
        std::uint64_t drm_modifiers[4]{};
        for (int i = 0; i < planes; ++i)
        {
            handles[i] = gbm_bo_get_handle_for_plane(b.bo, i).u32;
            pitches[i] = gbm_bo_get_stride_for_plane(b.bo, i);
            offsets[i] = gbm_bo_get_offset(b.bo, i);
            drm_modifiers[i] = modifier;
        }
        const int result =
            (modifier == DRM_FORMAT_MOD_INVALID)
                ? drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b.fb, 0)
                : drmModeAddFB2WithModifiers(fd, width, height, DRM_FORMAT_XRGB8888, handles, pitches, offsets,
                                             drm_modifiers, &b.fb, DRM_MODE_FB_MODIFIERS);
        require(result == 0, "display plane rejected the GBM buffer format/modifier");
        std::vector<EGLint> attributes{EGL_WIDTH,          width, EGL_HEIGHT, height, EGL_LINUX_DRM_FOURCC_EXT,
                                       DRM_FORMAT_XRGB8888};
        const EGLint fd_keys[]{EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT,
                               EGL_DMA_BUF_PLANE3_FD_EXT};
        const EGLint offset_keys[]{EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
                                   EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT};
        const EGLint pitch_keys[]{EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
                                  EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT};
        const EGLint lo_keys[]{EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
                               EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
        const EGLint hi_keys[]{EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
                               EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};
        std::array<Fd, 4> dma_fds{{{-1}, {-1}, {-1}, {-1}}};
        for (int i = 0; i < planes; ++i)
        {
            dma_fds[i].value = gbm_bo_get_fd_for_plane(b.bo, i);
            require(dma_fds[i].value >= 0, "cannot export GBM DMA-BUF");
            attributes.insert(attributes.end(),
                              {fd_keys[i], dma_fds[i].value, offset_keys[i], static_cast<EGLint>(offsets[i]),
                               pitch_keys[i], static_cast<EGLint>(pitches[i])});
            if (modifiers && modifier != DRM_FORMAT_MOD_INVALID)
            {
                attributes.insert(attributes.end(), {lo_keys[i], static_cast<EGLint>(modifier & 0xffffffffu),
                                                     hi_keys[i], static_cast<EGLint>(modifier >> 32)});
            }
        }
        attributes.push_back(EGL_NONE);
        b.image = create_image(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.data());
        require(b.image != EGL_NO_IMAGE_KHR, "cannot import GBM DMA-BUF as EGL image");
        // SDL calls glTexImage2D even for imported native textures. Create its
        // target first, then replace that storage with the GBM image. SDL owns
        // the GL texture/FBO; GBM retains ownership of the underlying buffer.
        b.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
        require(b.texture != nullptr, SDL_GetError());
        const bool es = std::string_view(SDL_GetRendererName(renderer)) == "opengles2";
        const auto native = SDL_GetNumberProperty(
            SDL_GetTextureProperties(b.texture),
            es ? SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER : SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0);
        require(native != 0, "SDL render target has no native GL texture");
        GLint previous = 0;
        get_integer(GL_TEXTURE_BINDING_2D, &previous);
        bind_texture(GL_TEXTURE_2D, static_cast<GLuint>(native));
        while (get_error() != GL_NO_ERROR)
        {
        }
        image_target(GL_TEXTURE_2D, b.image);
        const auto import_error = get_error();
        bind_texture(GL_TEXTURE_2D, previous);
        require(import_error == GL_NO_ERROR, "GL rejected the imported EGL image");
        auto* previous_target = SDL_GetRenderTarget(renderer);
        const bool bound = SDL_SetRenderTarget(renderer, b.texture);
        const bool restored = SDL_SetRenderTarget(renderer, previous_target);
        require(bound && restored, "GBM image is not a complete SDL render target");
    }
};

ArmadaGbm::ArmadaGbm(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
ArmadaGbm::~ArmadaGbm() = default;
std::unique_ptr<ArmadaGbm> ArmadaGbm::create(SDL_Renderer* renderer, int lease_fd, std::uint32_t plane, int width,
                                             int height)
{
    auto s = std::make_unique<Impl>();
    s->renderer = renderer;
    s->fd = lease_fd;
    s->window = SDL_GetRenderWindow(renderer);
    // A tiny SDL allocation activates its renderer context, even when a core's
    // shared context was left current by context_reset.
    auto* activation = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 1, 1);
    require(activation != nullptr, SDL_GetError());
    SDL_DestroyTexture(activation);
    s->context = SDL_GL_GetCurrentContext();
    s->display = eglGetCurrentDisplay();
    require(s->context && s->display != EGL_NO_DISPLAY, "renderer has no current EGL context");
    const char* extensions = eglQueryString(s->display, EGL_EXTENSIONS);
    require(extension(extensions, "EGL_EXT_image_dma_buf_import"), "EGL DMA-BUF import unavailable");
    require(extension(extensions, "EGL_KHR_fence_sync"), "EGL fence synchronization unavailable");
    s->modifiers = extension(extensions, "EGL_EXT_image_dma_buf_import_modifiers");
    s->native_fences = extension(extensions, "EGL_ANDROID_native_fence_sync");
    s->create_image = egl_proc<PFNEGLCREATEIMAGEKHRPROC>("eglCreateImageKHR");
    s->destroy_image = egl_proc<PFNEGLDESTROYIMAGEKHRPROC>("eglDestroyImageKHR");
    s->create_sync = egl_proc<PFNEGLCREATESYNCKHRPROC>("eglCreateSyncKHR");
    s->destroy_sync = egl_proc<PFNEGLDESTROYSYNCKHRPROC>("eglDestroySyncKHR");
    s->client_wait = egl_proc<PFNEGLCLIENTWAITSYNCKHRPROC>("eglClientWaitSyncKHR");
    if (s->native_fences)
    {
        s->dup_fence = egl_proc<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>("eglDupNativeFenceFDANDROID");
    }
    s->image_target = gl_proc<Impl::ImageTarget>("glEGLImageTargetTexture2DOES");
    s->bind_texture = gl_proc<decltype(s->bind_texture)>("glBindTexture");
    s->get_integer = gl_proc<decltype(s->get_integer)>("glGetIntegerv");
    s->get_error = gl_proc<decltype(s->get_error)>("glGetError");
    s->flush = gl_proc<decltype(s->flush)>("glFlush");
    s->finish = gl_proc<decltype(s->finish)>("glFinish");
    s->gbm = gbm_create_device(lease_fd);
    require(s->gbm != nullptr, "cannot create GBM device from lease");
    const auto candidates = s->matching_modifiers(plane);
    try
    {
        for (auto& buffer : s->buffers)
        {
            s->create_buffer(buffer, width, height, candidates, false);
        }
    }
    catch (const std::exception&)
    {
        s->clear_buffers();
        // A linear allocation is a safe second attempt when modifier import or
        // renderability fails. Both EGL import and KMS registration still validate it.
        for (auto& buffer : s->buffers)
        {
            s->create_buffer(buffer, width, height, {}, true);
        }
    }
    return std::unique_ptr<ArmadaGbm>(new ArmadaGbm(std::move(s)));
}
SDL_Texture* ArmadaGbm::target(unsigned buffer) const
{
    return impl_->buffers.at(buffer).texture;
}
std::uint32_t ArmadaGbm::framebuffer(unsigned buffer) const
{
    return impl_->buffers.at(buffer).fb;
}
int ArmadaGbm::finish(bool native_fence)
{
    auto& s = *impl_;
    require(s.sync == EGL_NO_SYNC_KHR, "previous GPU scanout fence still pending");
    require(SDL_FlushRenderer(s.renderer), SDL_GetError());
    const bool native = native_fence && s.native_fences;
    s.sync = s.create_sync(s.display, native ? EGL_SYNC_NATIVE_FENCE_ANDROID : EGL_SYNC_FENCE_KHR, nullptr);
    require(s.sync != EGL_NO_SYNC_KHR, "cannot create scanout render fence");
    s.flush();
    const int fd = native ? s.dup_fence(s.display, s.sync) : -1;
    if (fd >= 0)
    {
        s.destroy_sync(s.display, s.sync);
        s.sync = EGL_NO_SYNC_KHR;
    }
    return fd;
}
bool ArmadaGbm::ready()
{
    auto& s = *impl_;
    require(s.sync != EGL_NO_SYNC_KHR, "no pending GPU scanout fence");
    const auto result = s.client_wait(s.display, s.sync, 0, 0);
    if (result == EGL_TIMEOUT_EXPIRED_KHR)
    {
        return false;
    }
    require(result == EGL_CONDITION_SATISFIED_KHR, "scanout render fence failed");
    s.destroy_sync(s.display, s.sync);
    s.sync = EGL_NO_SYNC_KHR;
    return true;
}
