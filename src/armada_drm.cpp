#include "armada_drm.h"
#include <cstdint>
#include <cstddef>
#include "armada_drm_geometry.h"
#include "armada_drm_input.h"
#include "armada_gbm.h"
#include "armada_vulkan.h"
#include "drm-lease-client.h"
#include <wayland-client.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void sdl_check(bool ok)
{
    if (!ok)
    {
        throw std::runtime_error(SDL_GetError());
    }
}
[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error("Armada DRM lease: " + message);
}
std::string os_id()
{
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line))
    {
        if (line.starts_with("ID="))
        {
            auto value = line.substr(3);
            if (value.size() >= 2 && (value.front() == '\"' || value.front() == '\''))
            {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
    }
    return {};
}
}

struct ArmadaDrm::Impl
{
    struct Offer
    {
        wp_drm_lease_connector_v1* handle = nullptr;
        std::string name;
        std::uint32_t id = 0;
        bool done = false, withdrawn = false;
    };
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wp_drm_lease_device_v1* device = nullptr;
    wp_drm_lease_v1* lease = nullptr;
    std::uint32_t global = 0;
    bool enumerated = false, finished = false;
    std::vector<std::unique_ptr<Offer>> offers;
    int fd = -1;
    std::uint32_t connector = 0, crtc = 0, plane = 0, blob = 0;
    drmModeModeInfo mode{};
    struct Property
    {
        std::uint32_t object, id;
    };
    enum Prop
    {
        ConnectorCrtc,
        CrtcMode,
        CrtcActive,
        PlaneCrtc,
        Fb,
        SrcX,
        SrcY,
        SrcW,
        SrcH,
        DstX,
        DstY,
        DstW,
        DstH,
        Count
    };
    std::array<Property, Count> props{};
    struct Buffer
    {
        std::uint32_t handle = 0, fb = 0, pitch = 0;
        std::uint64_t size = 0;
        void* map = MAP_FAILED;
    };
    std::array<Buffer, 2> buffers{};
    std::unique_ptr<ArmadaGbm> gpu;
    std::unique_ptr<ArmadaVulkan> vulkan;
    std::uint32_t in_fence_property = 0;
    int next = 0, turns = 0;
    bool modeset = false, pending = false, rendering = false;
    std::uint64_t render_started = 0;
    std::uint64_t flip_started = 0;
    std::uintptr_t cookie = 0;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* target = nullptr;
    SDL_Texture* previous_target = nullptr;
    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    std::uint8_t previous_r = 0, previous_g = 0, previous_b = 0, previous_a = 0;
    int w = 0, h = 0;
    int touch_fd = -1;
    armada_drm::TouchState touch_state;
    std::uint64_t touch_retry = 0;
    Pointer touch{};

    ~Impl()
    {
        if (touch_fd >= 0)
        {
            ioctl(touch_fd, EVIOCGRAB, 0);
            close(touch_fd);
        }
        if (target)
        {
            SDL_DestroyTexture(target);
        }
        // Disable scanout before destroying its buffers, then close the lease fd
        // before releasing the protocol object so gamescope can safely resume.
        if (modeset && !finished)
        {
            try
            {
                commit(0, true);
            }
            catch (...)
            {
            }
        }
        gpu.reset();
        vulkan.reset();
        for (auto& b : buffers)
        {
            if (b.map != MAP_FAILED)
            {
                munmap(b.map, b.size);
            }
            if (b.fb)
            {
                drmModeRmFB(fd, b.fb);
            }
            if (b.handle)
            {
                drm_mode_destroy_dumb destroy{};
                destroy.handle = b.handle;
                drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
            }
        }
        if (blob)
        {
            drmModeDestroyPropertyBlob(fd, blob);
        }
        if (fd >= 0)
        {
            close(fd);
        }
        if (lease)
        {
            wp_drm_lease_v1_destroy(lease);
        }
        for (auto& offer : offers)
        {
            wp_drm_lease_connector_v1_destroy(offer->handle);
        }
        if (device)
        {
            wp_drm_lease_device_v1_destroy(device);
        }
        if (registry)
        {
            wl_registry_destroy(registry);
        }
        if (display)
        {
            wl_display_flush(display);
            wl_display_disconnect(display);
        }
    }

    static void connector_name(void* data, wp_drm_lease_connector_v1*, const char* name)
    {
        static_cast<Offer*>(data)->name = name;
    }
    static void connector_description(void*, wp_drm_lease_connector_v1*, const char*)
    {
    }
    static void connector_id(void* data, wp_drm_lease_connector_v1*, std::uint32_t id)
    {
        static_cast<Offer*>(data)->id = id;
    }
    static void connector_done(void* data, wp_drm_lease_connector_v1*)
    {
        static_cast<Offer*>(data)->done = true;
    }
    static void connector_withdrawn(void* data, wp_drm_lease_connector_v1*)
    {
        static_cast<Offer*>(data)->withdrawn = true;
    }
    static void device_fd(void*, wp_drm_lease_device_v1*, int fd)
    {
        close(fd);
    }
    static void device_connector(void* data, wp_drm_lease_device_v1*, wp_drm_lease_connector_v1* handle)
    {
        auto& self = *static_cast<Impl*>(data);
        auto offer = std::make_unique<Offer>();
        offer->handle = handle;
        static const wp_drm_lease_connector_v1_listener listener{connector_name, connector_description, connector_id,
                                                                 connector_done, connector_withdrawn};
        wp_drm_lease_connector_v1_add_listener(handle, &listener, offer.get());
        self.offers.push_back(std::move(offer));
    }
    static void device_done(void* data, wp_drm_lease_device_v1*)
    {
        static_cast<Impl*>(data)->enumerated = true;
    }
    static void device_released(void*, wp_drm_lease_device_v1*)
    {
    }
    static void registry_global(void* data, wl_registry* registry, std::uint32_t name, const char* interface,
                                std::uint32_t)
    {
        auto& self = *static_cast<Impl*>(data);
        if (!self.device && std::string_view(interface) == wp_drm_lease_device_v1_interface.name)
        {
            self.global = name;
            self.device = static_cast<wp_drm_lease_device_v1*>(
                wl_registry_bind(registry, name, &wp_drm_lease_device_v1_interface, 1));
            static const wp_drm_lease_device_v1_listener listener{device_fd, device_connector, device_done,
                                                                  device_released};
            wp_drm_lease_device_v1_add_listener(self.device, &listener, &self);
        }
    }
    static void registry_removed(void* data, wl_registry*, std::uint32_t name)
    {
        auto& self = *static_cast<Impl*>(data);
        if (name == self.global)
        {
            self.finished = true;
        }
    }
    static void lease_fd(void* data, wp_drm_lease_v1*, int fd)
    {
        auto& self = *static_cast<Impl*>(data);
        if (self.fd >= 0)
        {
            close(fd);
        }
        else
        {
            self.fd = fd;
        }
    }
    static void lease_finished(void* data, wp_drm_lease_v1*)
    {
        static_cast<Impl*>(data)->finished = true;
    }

    // A separate connection avoids consuming SDL's events. All waits have a
    // deadline; the render loop only ever pumps with a zero timeout.
    void dispatch(int timeout, const std::function<bool()>& ready = {})
    {
        while (wl_display_prepare_read(display) != 0)
        {
            if (wl_display_dispatch_pending(display) < 0)
            {
                fail("Wayland connection lost");
            }
        }
        // Pending callbacks above may already have supplied the reply. Do not
        // wait for another socket event after the condition has been satisfied.
        if (ready && ready())
        {
            wl_display_cancel_read(display);
            return;
        }
        short events = POLLIN;
        if (wl_display_flush(display) < 0)
        {
            if (errno == EAGAIN)
            {
                events |= POLLOUT;
            }
            else
            {
                wl_display_cancel_read(display);
                fail("Wayland flush failed");
            }
        }
        pollfd pfd{wl_display_get_fd(display), events, 0};
        const int result = poll(&pfd, 1, timeout);
        if (result > 0 && (pfd.revents & POLLIN))
        {
            if (wl_display_read_events(display) < 0)
            {
                fail("Wayland read failed");
            }
        }
        else
        {
            wl_display_cancel_read(display);
        }
        if ((result < 0 && errno != EINTR) || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            fail("Wayland connection lost");
        }
        if (wl_display_dispatch_pending(display) < 0)
        {
            fail("Wayland dispatch failed");
        }
    }
    void wait(const std::function<bool()>& ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!ready())
        {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                    .count();
            if (left <= 0)
            {
                fail("compositor response timed out");
            }
            dispatch(static_cast<int>(left), ready);
        }
    }
    bool acquire(const char* socket)
    {
        display = wl_display_connect(socket);
        if (!display)
        {
            return false;
        }
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener{registry_global, registry_removed};
        wl_registry_add_listener(registry, &listener, this);
        bool synced = false;
        auto* sync = wl_display_sync(display);
        static const wl_callback_listener sync_listener{[](void* data, wl_callback*, std::uint32_t)
                                                        {
                                                            *static_cast<bool*>(data) = true;
                                                        }};
        wl_callback_add_listener(sync, &sync_listener, &synced);
        try
        {
            wait(
                [&]
                {
                    return synced;
                });
        }
        catch (...)
        {
            wl_callback_destroy(sync);
            throw;
        }
        wl_callback_destroy(sync);
        if (!device)
        {
            return false; // E.g. a desktop/nested gamescope without a reserved panel.
        }
        wait(
            [&]
            {
                return enumerated || finished;
            });
        Offer* chosen = nullptr;
        for (auto& offer : offers)
        {
            if (offer->done && !offer->withdrawn && armada_drm::internal(offer->name))
            {
                if (chosen)
                {
                    fail("more than one internal connector offered; refusing to guess");
                }
                chosen = offer.get();
            }
        }
        if (!chosen)
        {
            return false;
        }
        connector = chosen->id;
        auto* request = wp_drm_lease_device_v1_create_lease_request(device);
        wp_drm_lease_request_v1_request_connector(request, chosen->handle);
        lease = wp_drm_lease_request_v1_submit(request);
        static const wp_drm_lease_v1_listener lease_listener{lease_fd, lease_finished};
        wp_drm_lease_v1_add_listener(lease, &lease_listener, this);
        wait(
            [&]
            {
                return fd >= 0 || finished;
            });
        if (finished || fd < 0)
        {
            fail("bottom-panel lease rejected");
        }
        return true;
    }

    void property(Prop index, std::uint32_t object, std::uint32_t type, const char* name)
    {
        auto* properties = drmModeObjectGetProperties(fd, object, type);
        if (!properties)
        {
            fail("cannot enumerate KMS properties");
        }
        for (std::uint32_t i = 0; i < properties->count_props; ++i)
        {
            auto* prop = drmModeGetProperty(fd, properties->props[i]);
            if (prop && std::string_view(prop->name) == name)
            {
                props[index] = {object, prop->prop_id};
            }
            if (prop)
            {
                drmModeFreeProperty(prop);
            }
        }
        drmModeFreeObjectProperties(properties);
        if (!props[index].id)
        {
            fail(std::string("missing KMS property ") + name);
        }
    }
    void setup()
    {
        drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
        if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1))
        {
            fail("atomic modesetting unavailable");
        }
        auto* conn = drmModeGetConnector(fd, connector);
        if (!conn)
        {
            fail("cannot read leased connector");
        }
        if (!conn->count_modes)
        {
            drmModeFreeConnector(conn);
            fail("leased connector has no modes");
        }
        mode = conn->modes[0];
        for (int i = 0; i < conn->count_modes; ++i)
        {
            if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
            {
                mode = conn->modes[i];
                break;
            }
        }
        drmModeFreeConnector(conn);
        // Read rotation from KMS rather than hardcoding Thor's portrait panel.
        auto* properties = drmModeObjectGetProperties(fd, connector, DRM_MODE_OBJECT_CONNECTOR);
        bool orientation_found = false;
        if (properties)
        {
            for (std::uint32_t i = 0; i < properties->count_props; ++i)
            {
                auto* prop = drmModeGetProperty(fd, properties->props[i]);
                if (prop && std::string_view(prop->name) == "panel orientation")
                {
                    for (int j = 0; j < prop->count_enums; ++j)
                    {
                        if (prop->enums[j].value == properties->prop_values[i])
                        {
                            const int value = armada_drm::rotation(prop->enums[j].name);
                            if (value >= 0)
                            {
                                turns = value;
                                orientation_found = true;
                            }
                        }
                    }
                }
                if (prop)
                {
                    drmModeFreeProperty(prop);
                }
            }
            drmModeFreeObjectProperties(properties);
        }
        if (!orientation_found)
        {
            fail("leased panel has no usable orientation property");
        }
        // The kernel remaps possible_crtcs to the lease's resource list. Armada
        // reserves a single CRTC, and may supply a non-primary plane for it.
        auto* resources = drmModeGetResources(fd);
        if (!resources)
        {
            fail("cannot enumerate leased CRTC");
        }
        if (resources->count_crtcs == 1)
        {
            crtc = resources->crtcs[0];
        }
        drmModeFreeResources(resources);
        if (!crtc)
        {
            fail("expected one leased CRTC");
        }
        auto* planes = drmModeGetPlaneResources(fd);
        if (!planes)
        {
            fail("cannot enumerate leased planes");
        }
        for (std::uint32_t i = 0; i < planes->count_planes && !plane; ++i)
        {
            auto* p = drmModeGetPlane(fd, planes->planes[i]);
            if (!p)
            {
                continue;
            }
            if (p->possible_crtcs & 1u)
            {
                for (std::uint32_t f = 0; f < p->count_formats; ++f)
                {
                    if (p->formats[f] == DRM_FORMAT_XRGB8888)
                    {
                        plane = p->plane_id;
                        break;
                    }
                }
            }
            drmModeFreePlane(p);
        }
        drmModeFreePlaneResources(planes);
        if (!plane)
        {
            fail("no leased XRGB8888 scanout plane");
        }
        property(ConnectorCrtc, connector, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
        property(CrtcMode, crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID");
        property(CrtcActive, crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE");
        const char* names[]{"CRTC_ID", "FB_ID",  "SRC_X",  "SRC_Y",  "SRC_W",
                            "SRC_H",   "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"};
        for (int i = PlaneCrtc; i < Count; ++i)
        {
            property(static_cast<Prop>(i), plane, DRM_MODE_OBJECT_PLANE, names[i - PlaneCrtc]);
        }
        if (drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &blob))
        {
            fail("cannot create mode blob");
        }
        w = (turns % 2) ? mode.vdisplay : mode.hdisplay;
        h = (turns % 2) ? mode.hdisplay : mode.vdisplay;
        // GL renderers can import the scanout storage directly. Vulkan and the
        // software renderer retain the existing readback path for now.
        const std::string_view renderer_name = SDL_GetRendererName(renderer);
        if (renderer_name == "vulkan")
        {
            try
            {
                vulkan = ArmadaVulkan::create(renderer, fd, connector, mode.hdisplay, mode.vdisplay);
            }
            catch (const std::exception& error)
            {
                std::cerr << "Armada: Vulkan direct display unavailable (" << error.what() << "); using CPU readback\n";
            }
        }
        if (renderer_name == "opengl" || renderer_name == "opengles2")
        {
            try
            {
                gpu = ArmadaGbm::create(renderer, fd, plane, mode.hdisplay, mode.vdisplay);
            }
            catch (const std::exception& error)
            {
                std::cerr << "Armada: GBM/EGL unavailable (" << error.what() << "); using CPU readback\n";
            }
        }
        if (gpu)
        {
            auto* plane_properties = drmModeObjectGetProperties(fd, plane, DRM_MODE_OBJECT_PLANE);
            if (plane_properties)
            {
                for (std::uint32_t i = 0; i < plane_properties->count_props; ++i)
                {
                    auto* prop = drmModeGetProperty(fd, plane_properties->props[i]);
                    if (prop && std::string_view(prop->name) == "IN_FENCE_FD")
                    {
                        in_fence_property = prop->prop_id;
                    }
                    if (prop)
                    {
                        drmModeFreeProperty(prop);
                    }
                }
                drmModeFreeObjectProperties(plane_properties);
            }
        }
        else if (!vulkan)
        {
            for (auto& b : buffers)
            {
                drm_mode_create_dumb create{};
                create.width = mode.hdisplay;
                create.height = mode.vdisplay;
                create.bpp = 32;
                if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create))
                {
                    fail("cannot allocate scanout buffer");
                }
                b.handle = create.handle;
                b.pitch = create.pitch;
                b.size = create.size;
                std::uint32_t handles[4]{b.handle}, pitches[4]{b.pitch}, offsets[4]{};
                if (drmModeAddFB2(fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888, handles, pitches, offsets,
                                  &b.fb, 0))
                {
                    fail("cannot register scanout buffer");
                }
                drm_mode_map_dumb map{};
                map.handle = b.handle;
                if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map))
                {
                    fail("cannot map scanout buffer");
                }
                b.map = mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
                if (b.map == MAP_FAILED)
                {
                    fail("scanout mmap failed");
                }
            }
            target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
            sdl_check(target != nullptr);
        }
        cookie = static_cast<std::uintptr_t>(SDL_GetTicksNS()) ^ (static_cast<std::uintptr_t>(getpid()) << 32);
        open_touch();
        if (touch_fd < 0)
        {
            std::cerr << "Armada: bottom_touchscreen is unavailable or cannot be grabbed; retrying every two seconds\n";
        }
        std::cout << "Armada: leased bottom panel " << connector << " (" << w << 'x' << h << ", rotation " << turns * 90
                  << ", "
                  << (vulkan ? "Vulkan direct display"
                      : gpu  ? "GBM/EGL GPU scanout"
                             : "CPU readback")
                  << ")\n";
    }
    void commit(std::uint32_t fb, bool configure, int fence = -1)
    {
        auto* request = drmModeAtomicAlloc();
        if (!request)
        {
            fail("cannot allocate atomic request");
        }
        bool ok = true;
        auto add = [&](Prop p, std::uint64_t value)
        {
            ok = (drmModeAtomicAddProperty(request, props[p].object, props[p].id, value) >= 0) && ok;
        };
        if (configure)
        {
            add(ConnectorCrtc, fb ? crtc : 0);
            add(CrtcMode, fb ? blob : 0);
            add(CrtcActive, fb ? 1 : 0);
            add(PlaneCrtc, fb ? crtc : 0);
            add(SrcX, 0);
            add(SrcY, 0);
            add(SrcW, fb ? std::uint64_t(mode.hdisplay) << 16 : 0);
            add(SrcH, fb ? std::uint64_t(mode.vdisplay) << 16 : 0);
            add(DstX, 0);
            add(DstY, 0);
            add(DstW, fb ? mode.hdisplay : 0);
            add(DstH, fb ? mode.vdisplay : 0);
        }
        add(Fb, fb);
        if (fence >= 0)
        {
            ok = (drmModeAtomicAddProperty(request, plane, in_fence_property, fence) >= 0) && ok;
        }
        ++cookie;
        const std::uint32_t flags =
            configure ? DRM_MODE_ATOMIC_ALLOW_MODESET : DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
        const int result = ok ? drmModeAtomicCommit(fd, request, flags, reinterpret_cast<void*>(cookie)) : -1;
        drmModeAtomicFree(request);
        if (result)
        {
            fail(std::string("atomic commit failed: ") + std::strerror(errno));
        }
    }
    void submit_gpu(int fence = -1)
    {
        try
        {
            commit(gpu->framebuffer(next), !modeset, fence);
        }
        catch (...)
        {
            if (fence >= 0)
            {
                close(fence);
            }
            throw;
        }
        if (fence >= 0)
        {
            close(fence); // KMS retained its own reference.
        }
        rendering = false;
        pending = modeset;
        modeset = true;
        flip_started = SDL_GetTicks();
        next ^= 1;
    }
    void drain_flips()
    {
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 0) <= 0)
        {
            return;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            fail("DRM connection lost");
        }
        if (!(pfd.revents & POLLIN))
        {
            return;
        }
        // The broker shares the lease's DRM event queue with its companion.
        // Compare opaque cookies before touching data from a foreign flip.
        static thread_local Impl* owner = nullptr;
        owner = this;
        drmEventContext context{};
        context.version = 3;
        context.page_flip_handler2 = [](int, unsigned, unsigned, unsigned, unsigned, void* data)
        {
            if (owner && reinterpret_cast<std::uintptr_t>(data) == owner->cookie)
            {
                owner->pending = false;
            }
        };
        const int result = drmHandleEvent(fd, &context);
        owner = nullptr;
        if (result < 0 && errno != EINTR && errno != EAGAIN)
        {
            fail("DRM event dispatch failed");
        }
    }
    void close_touch()
    {
        if (touch_fd >= 0)
        {
            ioctl(touch_fd, EVIOCGRAB, 0);
            close(touch_fd);
        }
        touch_fd = -1;
        touch.down = false;
        touch_state = {};
    }
    void open_touch()
    {
        touch_retry = SDL_GetTicks() + 2000;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator("/dev/input", error))
        {
            if (!entry.path().filename().string().starts_with("event"))
            {
                continue;
            }
            // Match the actual bottom digitizer, and use openat as the reference
            // emulators do to avoid Steam overlay's interposition of open().
            const int candidate = openat(AT_FDCWD, entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (candidate < 0)
            {
                continue;
            }
            char name[128]{};
            ioctl(candidate, EVIOCGNAME(sizeof(name)), name);
            if (std::string_view(name) != "bottom_touchscreen" ||
                ioctl(candidate, EVIOCGABS(ABS_MT_POSITION_X), &touch_state.axis_x) < 0 ||
                ioctl(candidate, EVIOCGABS(ABS_MT_POSITION_Y), &touch_state.axis_y) < 0 ||
                touch_state.axis_x.maximum <= touch_state.axis_x.minimum ||
                touch_state.axis_y.maximum <= touch_state.axis_y.minimum || ioctl(candidate, EVIOCGRAB, 1) < 0)
            {
                close(candidate);
                continue;
            }
            touch_fd = candidate;
            touch_state.raw_x = touch_state.axis_x.value;
            touch_state.raw_y = touch_state.axis_y.value;
            return;
        }
    }
    void pump_touch()
    {
        if (touch_fd < 0)
        {
            if (SDL_GetTicks() >= touch_retry)
            {
                open_touch();
            }
            return;
        }
        input_event events[64];
        for (int batch = 0; batch < 4; ++batch)
        {
            const auto bytes = read(touch_fd, events, sizeof(events));
            if (bytes < 0 && (errno == EAGAIN || errno == EINTR))
            {
                return;
            }
            if (bytes <= 0)
            {
                close_touch();
                return;
            }
            for (std::size_t i = 0; i < std::size_t(bytes) / sizeof(input_event); ++i)
            {
                const auto result = touch_state.event(events[i]);
                if (touch_state.dropped)
                {
                    touch.down = false;
                }
                // Reopen on overflow rather than retaining stale slot state.
                if (result == armada_drm::TouchState::Reset)
                {
                    close_touch();
                    return;
                }
                if (result == armada_drm::TouchState::Report)
                {
                    float x = touch_state.x(), y = touch_state.y();
                    armada_drm::unrotate(x, y, turns);
                    touch = {x * w, y * h, touch_state.down()};
                }
            }
        }
    }
};

ArmadaDrm::ArmadaDrm(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
ArmadaDrm::~ArmadaDrm() = default;
bool ArmadaDrm::configure_video()
{
    const char* socket = SDL_getenv("GAMESCOPE_WAYLAND_DISPLAY");
    const bool eligible = socket && armada_drm::eligible(os_id(), socket);
    if (eligible)
    {
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_FORCE_EGL, "1", SDL_HINT_DEFAULT);
    }
    return eligible;
}
std::unique_ptr<ArmadaDrm> ArmadaDrm::try_create(SDL_Renderer* renderer)
{
    const char* socket = SDL_getenv("GAMESCOPE_WAYLAND_DISPLAY");
    if (!socket || !armada_drm::eligible(os_id(), socket))
    {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->renderer = renderer;
    if (!impl->acquire(socket))
    {
        return nullptr;
    }
    impl->setup();
    return std::unique_ptr<ArmadaDrm>(new ArmadaDrm(std::move(impl)));
}
void ArmadaDrm::pump()
{
    auto& s = *impl_;
    s.dispatch(0);
    if (s.finished)
    {
        s.close_touch();
        fail("lease revoked; ending the session");
    }
    // Vulkan WSI owns the lease's DRM event queue. Reading it here could steal
    // page-flip events from the driver's presentation thread.
    if (!s.vulkan)
    {
        s.drain_flips();
    }
    s.pump_touch();
    if (s.rendering)
    {
        if (s.gpu->ready())
        {
            s.submit_gpu();
        }
        else if (SDL_GetTicks() - s.render_started > 2000)
        {
            fail("GPU render fence timed out");
        }
    }
    if (s.pending && SDL_GetTicks() - s.flip_started > 2000)
    {
        fail("page flip timed out");
    }
}
bool ArmadaDrm::begin_draw()
{
    auto& s = *impl_;
    if (s.vulkan)
    {
        return s.vulkan->begin();
    }
    if (s.pending || s.rendering)
    {
        return false;
    }
    s.previous_target = SDL_GetRenderTarget(s.renderer);
    sdl_check(SDL_GetRenderDrawColor(s.renderer, &s.previous_r, &s.previous_g, &s.previous_b, &s.previous_a));
    sdl_check(SDL_GetRenderDrawBlendMode(s.renderer, &s.previous_blend));
    sdl_check(SDL_SetRenderTarget(s.renderer, s.gpu ? s.gpu->target(s.next) : s.target));
    return true;
}
void ArmadaDrm::present()
{
    auto& s = *impl_;
    if (s.vulkan)
    {
        return; // Native submission follows the top SDL presentation.
    }
    if (s.gpu)
    {
        // Restoring the SDL target flushes its queued drawing into the imported
        // GBM image. No readback, format conversion, CPU rotation or glFinish.
        sdl_check(SDL_SetRenderTarget(s.renderer, s.previous_target));
        SDL_SetRenderDrawColor(s.renderer, s.previous_r, s.previous_g, s.previous_b, s.previous_a);
        SDL_SetRenderDrawBlendMode(s.renderer, s.previous_blend);
        const int fence = s.gpu->finish(s.in_fence_property != 0);
        if (fence >= 0)
        {
            s.submit_gpu(fence);
        }
        else
        {
            s.rendering = true;
            s.render_started = SDL_GetTicks();
        }
        return;
    }
    auto* surface = SDL_RenderReadPixels(s.renderer, nullptr);
    // Always restore the target, including readback failure.
    const bool restored = SDL_SetRenderTarget(s.renderer, s.previous_target);
    SDL_SetRenderDrawColor(s.renderer, s.previous_r, s.previous_g, s.previous_b, s.previous_a);
    SDL_SetRenderDrawBlendMode(s.renderer, s.previous_blend);
    if (!surface)
    {
        sdl_check(false);
        return;
    }
    if (!restored)
    {
        SDL_DestroySurface(surface);
        sdl_check(false);
    }
    auto* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(surface);
    sdl_check(converted != nullptr);
    auto& b = s.buffers[s.next];
    armada_drm::rotate_pixels(converted->pixels, converted->pitch, b.map, b.pitch, s.w, s.h, s.turns);
    SDL_DestroySurface(converted);
    s.commit(b.fb, !s.modeset);
    s.pending = s.modeset;
    s.modeset = true;
    s.flip_started = SDL_GetTicks();
    s.next ^= 1;
}
void ArmadaDrm::render_texture(SDL_Texture* texture, const SDL_FRect& source, const SDL_FRect& destination,
                               SDL_FlipMode flip, int sharp_factor)
{
    const auto& s = *impl_;
    if (s.vulkan)
    {
        s.vulkan->draw(texture, source, destination, flip, s.w, s.h, s.turns, sharp_factor);
        return;
    }
    const auto dest = s.gpu ? armada_drm::rotated_destination(destination, s.w, s.h, s.turns) : destination;
    sdl_check(SDL_RenderTextureRotated(s.renderer, texture, &source, &dest, s.gpu ? s.turns * 90 : 0, nullptr, flip));
}
bool ArmadaDrm::gpu_scanout() const
{
    return impl_->gpu != nullptr || impl_->vulkan != nullptr;
}
bool ArmadaDrm::native_vulkan() const
{
    return impl_->vulkan != nullptr;
}
void ArmadaDrm::after_main_present()
{
    if (impl_->vulkan)
    {
        impl_->vulkan->present();
    }
}
void ArmadaDrm::clear()
{
    if (impl_->vulkan)
    {
        return; // The native render pass clears the whole swapchain image.
    }
    sdl_check(SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255));
    sdl_check(SDL_RenderClear(impl_->renderer));
}
void ArmadaDrm::dim(std::uint8_t alpha)
{
    if (impl_->vulkan)
    {
        impl_->vulkan->dim(alpha);
        return;
    }
    sdl_check(SDL_SetRenderDrawBlendMode(impl_->renderer, SDL_BLENDMODE_BLEND));
    sdl_check(SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, alpha));
    sdl_check(SDL_RenderFillRect(impl_->renderer, nullptr));
}
bool ArmadaDrm::presentation_pending() const
{
    return impl_->rendering || impl_->pending;
}
int ArmadaDrm::width() const
{
    return impl_->w;
}
int ArmadaDrm::height() const
{
    return impl_->h;
}
int ArmadaDrm::rotation() const
{
    return impl_->turns;
}
ArmadaDrm::Pointer ArmadaDrm::pointer() const
{
    return impl_->touch;
}
