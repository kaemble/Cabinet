#include "vulkan_video.h"
#include <cstdint>
#include "vulkan_snapshot_shader.h"
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void vk_check(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan operation failed: " + std::to_string(result));
    }
}
void sdl_check(bool result)
{
    if (!result)
    {
        throw std::runtime_error(SDL_GetError());
    }
}
void append(std::vector<const char*>& names, const char* name)
{
    if (std::none_of(names.begin(), names.end(),
                     [&](const char* n)
                     {
                         return std::strcmp(n, name) == 0;
                     }))
    {
        names.push_back(name);
    }
}
}
struct VulkanVideo::Impl
{
    retro_hw_render_context_negotiation_interface_vulkan negotiation{};
    retro_hw_render_interface_vulkan api{};
    VkInstance instance = VK_NULL_HANDLE;
    retro_vulkan_context context{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    static constexpr std::uint32_t frame_count = 3;
    struct Frame
    {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkDescriptorSet descriptors = VK_NULL_HANDLE;
        VkImageView source_view = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool pending = false;
    };
    std::array<Frame, frame_count> frames;
    std::atomic<std::uint32_t> sync_index{0};
    bool frame_started = false, shared_queue = false, output_initialized = false;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView output_view = VK_NULL_HANDLE;
    VkImage output = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    SDL_Texture* texture = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Renderer* secondary_renderer = nullptr;
    SDL_Texture* secondary_texture = nullptr;
    VkSurfaceKHR secondary_surface = VK_NULL_HANDLE;
    unsigned width = 0, height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    const retro_vulkan_image* image = nullptr;
    std::vector<VkSemaphore> waits;
    std::vector<VkCommandBuffer> commands;
    VkSemaphore signal = VK_NULL_HANDLE;
    std::uint32_t source_family = VK_QUEUE_FAMILY_IGNORED;
    // Frame resources may be waited/recycled without excluding core queue
    // submissions. If both locks are needed, take frame_mutex before queue_mutex.
    std::mutex frame_mutex;
    std::mutex queue_mutex;
    bool negotiated = false, loader_loaded = false;
    bool direct_display = false;
    std::string error;

    void wait_frame(Frame& frame)
    {
        if (frame.pending)
        {
            vk_check(vkWaitForFences(context.device, 1, &frame.fence, VK_TRUE, UINT64_MAX));
            frame.pending = false;
        }
        if (frame.source_view)
        {
            vkDestroyImageView(context.device, frame.source_view, nullptr);
            frame.source_view = VK_NULL_HANDLE;
        }
    }
    Frame& current_frame()
    {
        return frames[sync_index.load(std::memory_order_relaxed)];
    }
    void submit_frame(Frame& frame, const VkSubmitInfo& submit)
    {
        vk_check(vkResetFences(context.device, 1, &frame.fence));
        {
            // Like RetroArch, exclude other submitters only for the queue call.
            // frame_mutex is already held by frame(), protecting fence reuse.
            std::lock_guard queue_lock(queue_mutex);
            vk_check(vkQueueSubmit(context.queue, 1, &submit, frame.fence));
        }
        frame.pending = true;
    }
    void begin_frame()
    {
        std::lock_guard frame_lock(frame_mutex);
        const auto index = frame_started ? (sync_index.load(std::memory_order_relaxed) + 1) % frame_count : 0;
        // Recycle only work from this slot, instead of draining the device.
        wait_frame(frames[index]);
        sync_index.store(index, std::memory_order_relaxed);
        frame_started = true;
    }
    // Caller holds queue_mutex. SDL flushes and device-wide drains still require
    // queue exclusion; unlike frame fences, they access the queues themselves.
    void idle_locked()
    {
        if (secondary_renderer)
        {
            sdl_check(SDL_FlushRenderer(secondary_renderer));
        }
        if (renderer)
        {
            sdl_check(SDL_FlushRenderer(renderer));
        }
        if (context.device)
        {
            vk_check(vkDeviceWaitIdle(context.device));
        }
    }
    void idle()
    {
        std::lock_guard queue_lock(queue_mutex);
        idle_locked();
    }
    void destroy_output()
    {
        if (secondary_texture)
        {
            SDL_DestroyTexture(secondary_texture);
        }
        secondary_texture = nullptr;
        if (texture)
        {
            SDL_DestroyTexture(texture);
        }
        texture = nullptr;
        if (output_view)
        {
            vkDestroyImageView(context.device, output_view, nullptr);
        }
        output_view = VK_NULL_HANDLE;
        if (output)
        {
            vkDestroyImage(context.device, output, nullptr);
        }
        if (memory)
        {
            vkFreeMemory(context.device, memory, nullptr);
        }
        output = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        output_initialized = false;
    }
    ~Impl()
    {
        if (secondary_renderer)
        {
            SDL_FlushRenderer(secondary_renderer);
        }
        if (renderer)
        {
            SDL_FlushRenderer(renderer);
        }
        if (context.device)
        {
            vkDeviceWaitIdle(context.device);
        }
        destroy_output();
        if (secondary_renderer)
        {
            SDL_DestroyRenderer(secondary_renderer);
        }
        if (secondary_surface)
        {
            vkDestroySurfaceKHR(instance, secondary_surface, nullptr);
        }
        if (renderer)
        {
            SDL_DestroyRenderer(renderer);
        }
        for (auto& frame : frames)
        {
            if (frame.source_view)
            {
                vkDestroyImageView(context.device, frame.source_view, nullptr);
            }
            if (frame.fence)
            {
                vkDestroyFence(context.device, frame.fence, nullptr);
            }
        }
        if (pipeline)
        {
            vkDestroyPipeline(context.device, pipeline, nullptr);
        }
        if (pipeline_layout)
        {
            vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
        }
        if (sampler)
        {
            vkDestroySampler(context.device, sampler, nullptr);
        }
        if (descriptor_pool)
        {
            vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
        }
        if (descriptor_layout)
        {
            vkDestroyDescriptorSetLayout(context.device, descriptor_layout, nullptr);
        }
        if (pool)
        {
            vkDestroyCommandPool(context.device, pool, nullptr);
        }
        if (negotiated && negotiation.destroy_device)
        {
            negotiation.destroy_device();
        }
        if (context.device)
        {
            vkDestroyDevice(context.device, nullptr);
        }
        if (surface)
        {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
        if (instance)
        {
            vkDestroyInstance(instance, nullptr);
        }
        if (loader_loaded)
        {
            SDL_Vulkan_UnloadLibrary();
        }
    }
    static VkInstance create_instance(void* opaque, const VkInstanceCreateInfo* info)
    {
        auto& self = *static_cast<Impl*>(opaque);
        auto ci = *info;
        std::uint32_t count = 0;
        auto extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        if (!extensions)
        {
            return VK_NULL_HANDLE;
        }
        std::vector<const char*> names;
        for (std::uint32_t i = 0; i < ci.enabledExtensionCount; ++i)
        {
            append(names, ci.ppEnabledExtensionNames[i]);
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            append(names, extensions[i]);
        }
        append(names, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        if (self.direct_display)
        {
            std::uint32_t count = 0;
            if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
            {
                return VK_NULL_HANDLE;
            }
            std::vector<VkExtensionProperties> available(count);
            if (vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()) != VK_SUCCESS)
            {
                return VK_NULL_HANDLE;
            }
            const char* display_extensions[]{VK_KHR_DISPLAY_EXTENSION_NAME, VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
                                             VK_EXT_ACQUIRE_DRM_DISPLAY_EXTENSION_NAME};
            const bool supported =
                std::all_of(std::begin(display_extensions), std::end(display_extensions),
                            [&](const char* name)
                            {
                                return std::any_of(available.begin(), available.end(),
                                                   [&](const auto& e)
                                                   {
                                                       return std::strcmp(e.extensionName, name) == 0;
                                                   });
                            });
            if (supported)
            {
                for (const auto* extension : display_extensions)
                {
                    append(names, extension);
                }
            }
        }
        ci.enabledExtensionCount = static_cast<std::uint32_t>(names.size());
        ci.ppEnabledExtensionNames = names.data();
        if (vkCreateInstance(&ci, nullptr, &self.instance) != VK_SUCCESS)
        {
            return VK_NULL_HANDLE;
        }
        return self.instance;
    }
    static VkDevice create_device(VkPhysicalDevice gpu, void*, const VkDeviceCreateInfo* info)
    {
        auto ci = *info;
        std::vector<const char*> names;
        for (std::uint32_t i = 0; i < ci.enabledExtensionCount; ++i)
        {
            append(names, ci.ppEnabledExtensionNames[i]);
        }
        append(names, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        ci.enabledExtensionCount = static_cast<std::uint32_t>(names.size());
        ci.ppEnabledExtensionNames = names.data();
        VkDevice device = VK_NULL_HANDLE;
        if (vkCreateDevice(gpu, &ci, nullptr, &device) != VK_SUCCESS)
        {
            return VK_NULL_HANDLE;
        }
        return device;
    }
    void allocate(unsigned w, unsigned h, VkFormat fmt)
    {
        if (w == width && h == height && fmt == format && output)
        {
            return;
        }
        {
            // SDL texture destruction may submit work too. Keep the transition
            // guarded without holding the queue lock during image allocation.
            std::lock_guard queue_lock(queue_mutex);
            idle_locked();
            destroy_output();
        }
        VkPhysicalDeviceProperties limits{};
        vkGetPhysicalDeviceProperties(context.gpu, &limits);
        if (!w || !h || w > limits.limits.maxImageDimension2D || h > limits.limits.maxImageDimension2D)
        {
            throw std::runtime_error("Unsupported Vulkan frame dimensions");
        }
        width = w;
        height = h;
        format = fmt;
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = fmt;
        ci.extent = {w, h, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        // SDL's external texture view includes transfer-source usage too.
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_STORAGE_BIT;
        vk_check(vkCreateImage(context.device, &ci, nullptr, &output));
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(context.device, output, &req);
        VkPhysicalDeviceMemoryProperties props{};
        vkGetPhysicalDeviceMemoryProperties(context.gpu, &props);
        std::uint32_t type = 0;
        for (; type < props.memoryTypeCount; ++type)
        {
            if ((req.memoryTypeBits & (1u << type)) &&
                (props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                break;
            }
        }
        if (type == props.memoryTypeCount)
        {
            throw std::runtime_error("No Vulkan image memory available");
        }
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        vk_check(vkAllocateMemory(context.device, &alloc, nullptr, &memory));
        vk_check(vkBindImageMemory(context.device, output, memory, 0));
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = output;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = fmt;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk_check(vkCreateImageView(context.device, &view, nullptr, &output_view));
    }
};
VulkanVideo::VulkanVideo() : impl(std::make_unique<Impl>())
{
}
VulkanVideo::~VulkanVideo() = default;
void VulkanVideo::negotiate(const retro_hw_render_context_negotiation_interface_vulkan* n)
{
    if (!n || n->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN || !n->interface_version)
    {
        throw std::runtime_error("Unsupported Vulkan negotiation interface");
    }
    // Only read fields present in the advertised version.
    std::memcpy(&impl->negotiation, n,
                (n->interface_version >= 2)
                    ? sizeof(*n)
                    : offsetof(retro_hw_render_context_negotiation_interface_vulkan, create_instance));
}
SDL_Renderer* VulkanVideo::create_renderer(SDL_Window* window, bool fullscreen, bool direct_display)
{
    auto& s = *impl;
    s.direct_display = direct_display;
    // Apply pending window state before negotiating the Vulkan device.
    sdl_check(SDL_SyncWindow(window));
    sdl_check(SDL_Vulkan_LoadLibrary(nullptr));
    s.loader_loaded = true;
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Cabinet";
    app.apiVersion = VK_API_VERSION_1_1;
    const auto* requested = s.negotiation.get_application_info ? s.negotiation.get_application_info() : nullptr;
    if (requested)
    {
        app = *requested;
    }
    app.apiVersion = std::max(app.apiVersion, VK_API_VERSION_1_1);
    if (s.negotiation.interface_version >= 2 && s.negotiation.create_instance)
    {
        s.negotiated = true;
        s.instance = s.negotiation.create_instance(vkGetInstanceProcAddr, &app, Impl::create_instance, &s);
    }
    else
    {
        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        s.instance = Impl::create_instance(&s, &ci);
    }
    if (!s.instance)
    {
        throw std::runtime_error("Cannot create Vulkan instance");
    }
    sdl_check(SDL_Vulkan_CreateSurface(window, s.instance, nullptr, &s.surface));
    // Like RetroArch, select a present-capable GPU before core negotiation.
    // Some v1 cores (Azahar) use this handle directly rather than selecting one.
    std::uint32_t gpu_count = 0;
    vk_check(vkEnumeratePhysicalDevices(s.instance, &gpu_count, nullptr));
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vk_check(vkEnumeratePhysicalDevices(s.instance, &gpu_count, gpus.data()));
    VkPhysicalDevice preferred_gpu = VK_NULL_HANDLE;
    for (auto gpu : gpus)
    {
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, props.data());
        bool graphics = false, present = false;
        for (std::uint32_t i = 0; i < families; ++i)
        {
            graphics |= (props[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                        (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
            VkBool32 supported = false;
            vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, s.surface, &supported));
            present |= supported;
        }
        if (graphics && present)
        {
            preferred_gpu = gpu;
            break;
        }
    }
    if (!preferred_gpu)
    {
        throw std::runtime_error("No compatible Vulkan graphics/presentation GPU");
    }
    bool created = false;
    if (s.negotiation.interface_version >= 2 && s.negotiation.create_device2)
    {
        s.negotiated = true;
        created = s.negotiation.create_device2(&s.context, s.instance, preferred_gpu, s.surface, vkGetInstanceProcAddr,
                                               Impl::create_device, &s);
    }
    else if (s.negotiation.create_device)
    {
        const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        const VkPhysicalDeviceFeatures features{};
        s.negotiated = true;
        created = s.negotiation.create_device(&s.context, s.instance, preferred_gpu, s.surface, vkGetInstanceProcAddr,
                                              &extension, 1, nullptr, 0, &features);
    }
    if (!created)
    {
        for (auto gpu : gpus)
        {
            std::uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
            std::vector<VkQueueFamilyProperties> props(families);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, props.data());
            for (std::uint32_t i = 0; i < families; ++i)
            {
                VkBool32 present = false;
                vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, s.surface, &present));
                if (!present || (props[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) !=
                                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
                {
                    continue;
                }
                float priority = 1;
                VkDeviceQueueCreateInfo queue{};
                queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queue.queueFamilyIndex = i;
                queue.queueCount = 1;
                queue.pQueuePriorities = &priority;
                VkDeviceCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                ci.queueCreateInfoCount = 1;
                ci.pQueueCreateInfos = &queue;
                s.context.device = Impl::create_device(gpu, &s, &ci);
                if (!s.context.device)
                {
                    continue;
                }
                s.context.gpu = gpu;
                s.context.queue_family_index = s.context.presentation_queue_family_index = i;
                vkGetDeviceQueue(s.context.device, i, 0, &s.context.queue);
                s.context.presentation_queue = s.context.queue;
                created = true;
                break;
            }
            if (created)
            {
                break;
            }
        }
    }
    if (!created)
    {
        throw std::runtime_error("No compatible Vulkan graphics/presentation device");
    }
    auto p = SDL_CreateProperties();
    SDL_SetStringProperty(p, SDL_PROP_RENDERER_CREATE_NAME_STRING, "vulkan");
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_INSTANCE_POINTER, s.instance);
    SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_SURFACE_NUMBER, reinterpret_cast<std::int64_t>(s.surface));
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_PHYSICAL_DEVICE_POINTER, s.context.gpu);
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_DEVICE_POINTER, s.context.device);
    SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER,
                          s.context.queue_family_index);
    SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_PRESENT_QUEUE_FAMILY_INDEX_NUMBER,
                          s.context.presentation_queue_family_index);
    s.renderer = SDL_CreateRendererWithProperties(p);
    SDL_DestroyProperties(p);
    sdl_check(s.renderer != nullptr);
    // Map the native surface before context_reset lets a core size its output.
    // Xwayland can retain the initial window buffer size until the first present,
    // even though the fullscreen state has already been acknowledged.
    sdl_check(SDL_SetRenderDrawColor(s.renderer, 0, 0, 0, 255));
    sdl_check(SDL_RenderClear(s.renderer));
    sdl_check(SDL_RenderPresent(s.renderer));
    sdl_check(SDL_SyncWindow(window));
    SDL_PumpEvents();
    if (fullscreen)
    {
        // Wayland can defer its initial fullscreen configure until the surface
        // is mapped. Preserve the request separately from the current flags.
        const auto deadline = SDL_GetTicks() + 1000;
        do
        {
            SDL_PumpEvents();
            int width = 0, height = 0;
            sdl_check(SDL_GetWindowSizeInPixels(window, &width, &height));
            const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
            if (!mode ||
                ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) && width == mode->w && height == mode->h))
            {
                break;
            }
            sdl_check(SDL_SyncWindow(window));
            SDL_Delay(1);
        } while (SDL_GetTicks() < deadline);
    }
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = s.context.queue_family_index;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vk_check(vkCreateCommandPool(s.context.device, &pool, nullptr, &s.pool));
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = s.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = Impl::frame_count;
    std::array<VkCommandBuffer, Impl::frame_count> buffers{};
    vk_check(vkAllocateCommandBuffers(s.context.device, &alloc, buffers.data()));
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for (std::uint32_t i = 0; i < Impl::frame_count; ++i)
    {
        s.frames[i].command = buffers[i];
        vk_check(vkCreateFence(s.context.device, &fence, nullptr, &s.frames[i].fence));
    }
    // SDL's renderer obtains queue zero of the requested graphics family.
    // Shared-queue barriers order snapshot copies against its texture reads.
    VkQueue renderer_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(s.context.device, s.context.queue_family_index, 0, &renderer_queue);
    s.shared_queue = renderer_queue == s.context.queue;
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 2;
    layout.pBindings = bindings;
    vk_check(vkCreateDescriptorSetLayout(s.context.device, &layout, nullptr, &s.descriptor_layout));
    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Impl::frame_count},
                                 {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, Impl::frame_count}};
    VkDescriptorPoolCreateInfo descriptors{};
    descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptors.maxSets = Impl::frame_count;
    descriptors.poolSizeCount = 2;
    descriptors.pPoolSizes = sizes;
    vk_check(vkCreateDescriptorPool(s.context.device, &descriptors, nullptr, &s.descriptor_pool));
    for (auto& frame : s.frames)
    {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = s.descriptor_pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &s.descriptor_layout;
        vk_check(vkAllocateDescriptorSets(s.context.device, &info, &frame.descriptors));
    }
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vk_check(vkCreateSampler(s.context.device, &sampler, nullptr, &s.sampler));
    VkPipelineLayoutCreateInfo pipeline_layout{};
    pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &s.descriptor_layout;
    vk_check(vkCreatePipelineLayout(s.context.device, &pipeline_layout, nullptr, &s.pipeline_layout));
    VkShaderModuleCreateInfo shader{};
    shader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader.codeSize = sizeof(vulkan_snapshot_shader);
    shader.pCode = vulkan_snapshot_shader;
    VkShaderModule module = VK_NULL_HANDLE;
    vk_check(vkCreateShaderModule(s.context.device, &shader, nullptr, &module));
    VkComputePipelineCreateInfo compute{};
    compute.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute.layout = s.pipeline_layout;
    compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute.stage.module = module;
    compute.stage.pName = "main";
    const auto result = vkCreateComputePipelines(s.context.device, VK_NULL_HANDLE, 1, &compute, nullptr, &s.pipeline);
    vkDestroyShaderModule(s.context.device, module, nullptr);
    vk_check(result);
    auto& a = s.api;
    a.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    a.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    a.handle = &s;
    a.instance = s.instance;
    a.gpu = s.context.gpu;
    a.device = s.context.device;
    a.queue = s.context.queue;
    a.queue_index = s.context.queue_family_index;
    a.get_device_proc_addr = vkGetDeviceProcAddr;
    a.get_instance_proc_addr = vkGetInstanceProcAddr;
    a.set_image = [](void* h, const retro_vulkan_image* image, std::uint32_t count, const VkSemaphore* semaphores,
                     std::uint32_t family)
    {
        auto& v = *static_cast<Impl*>(h);
        v.image = image;
        v.source_family = family;
        v.waits.clear();
        if (count)
        {
            v.waits.assign(semaphores, semaphores + count);
        }
    };
    a.get_sync_index = [](void* h) -> std::uint32_t
    {
        return static_cast<Impl*>(h)->sync_index.load(std::memory_order_relaxed);
    };
    a.get_sync_index_mask = [](void*) -> std::uint32_t
    {
        return (1u << Impl::frame_count) - 1;
    };
    a.wait_sync_index = [](void* handle)
    {
        auto& video = *static_cast<Impl*>(handle);
        std::lock_guard frame_lock(video.frame_mutex);
        try
        {
            // Completion may depend on work a core worker has yet to submit.
            // Never hold queue_mutex while waiting for a frame fence.
            video.wait_frame(video.current_frame());
        }
        catch (const std::exception& error)
        {
            video.error = error.what();
        }
    };
    a.lock_queue = [](void* handle)
    {
        static_cast<Impl*>(handle)->queue_mutex.lock();
    };
    a.unlock_queue = [](void* handle)
    {
        static_cast<Impl*>(handle)->queue_mutex.unlock();
    };
    a.set_command_buffers = [](void* h, std::uint32_t count, const VkCommandBuffer* cmds)
    {
        auto& v = *static_cast<Impl*>(h);
        if (count)
        {
            v.commands.insert(v.commands.end(), cmds, cmds + count);
        }
    };
    a.set_signal_semaphore = [](void* h, VkSemaphore sem)
    {
        static_cast<Impl*>(h)->signal = sem;
    };
    return s.renderer;
}
SDL_Renderer* VulkanVideo::create_secondary_renderer(SDL_Window* window)
{
    auto& s = *impl;
    std::lock_guard queue_lock(s.queue_mutex);
    sdl_check(SDL_SyncWindow(window));
    sdl_check(SDL_Vulkan_CreateSurface(window, s.instance, nullptr, &s.secondary_surface));
    VkBool32 supported = VK_FALSE;
    vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(s.context.gpu, s.context.presentation_queue_family_index,
                                                  s.secondary_surface, &supported));
    if (!supported)
    {
        throw std::runtime_error("Vulkan presentation queue cannot present the bottom-screen window");
    }
    auto p = SDL_CreateProperties();
    SDL_SetStringProperty(p, SDL_PROP_RENDERER_CREATE_NAME_STRING, "vulkan");
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_INSTANCE_POINTER, s.instance);
    SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_SURFACE_NUMBER,
                          reinterpret_cast<std::int64_t>(s.secondary_surface));
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_PHYSICAL_DEVICE_POINTER, s.context.gpu);
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_DEVICE_POINTER, s.context.device);
    SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER,
                          s.context.queue_family_index);
    SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_VULKAN_PRESENT_QUEUE_FAMILY_INDEX_NUMBER,
                          s.context.presentation_queue_family_index);
    s.secondary_renderer = SDL_CreateRendererWithProperties(p);
    SDL_DestroyProperties(p);
    sdl_check(s.secondary_renderer != nullptr);
    return s.secondary_renderer;
}
SDL_Texture* VulkanVideo::secondary_texture()
{
    auto& s = *impl;
    if (!s.secondary_renderer || !s.output)
    {
        return nullptr;
    }
    if (!s.secondary_texture)
    {
        auto p = SDL_CreateProperties();
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, s.width);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, s.height);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_VULKAN_TEXTURE_NUMBER,
                              reinterpret_cast<std::int64_t>(s.output));
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_VULKAN_LAYOUT_NUMBER,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        s.secondary_texture = SDL_CreateTextureWithProperties(s.secondary_renderer, p);
        SDL_DestroyProperties(p);
        sdl_check(s.secondary_texture != nullptr);
        sdl_check(SDL_SetTextureBlendMode(s.secondary_texture, SDL_BLENDMODE_NONE));
    }
    return s.secondary_texture;
}
void VulkanVideo::destroy_secondary_renderer()
{
    auto& s = *impl;
    std::lock_guard queue_lock(s.queue_mutex);
    // Teardown must also finish when a device has been lost.
    if (s.secondary_renderer)
    {
        SDL_FlushRenderer(s.secondary_renderer);
    }
    if (s.renderer)
    {
        SDL_FlushRenderer(s.renderer);
    }
    if (s.context.device)
    {
        vkDeviceWaitIdle(s.context.device);
    }
    if (s.secondary_texture)
    {
        SDL_DestroyTexture(s.secondary_texture);
    }
    s.secondary_texture = nullptr;
    if (s.secondary_renderer)
    {
        SDL_DestroyRenderer(s.secondary_renderer);
    }
    s.secondary_renderer = nullptr;
    if (s.secondary_surface)
    {
        vkDestroySurfaceKHR(s.instance, s.secondary_surface, nullptr);
    }
    s.secondary_surface = VK_NULL_HANDLE;
}
const retro_hw_render_interface* VulkanVideo::interface() const
{
    return impl->context.device ? reinterpret_cast<const retro_hw_render_interface*>(&impl->api) : nullptr;
}
void VulkanVideo::begin_frame()
{
    impl->begin_frame();
}
void VulkanVideo::idle()
{
    impl->idle();
}
void VulkanVideo::lock()
{
    impl->queue_mutex.lock();
}
void VulkanVideo::unlock()
{
    impl->queue_mutex.unlock();
}
SDL_Texture* VulkanVideo::frame(unsigned width, unsigned height)
{
    auto& s = *impl;
    std::lock_guard frame_lock(s.frame_mutex);
    if (!s.error.empty())
    {
        throw std::runtime_error(s.error);
    }
    if ((!width && !height) || !s.image)
    {
        if (s.signal || !s.commands.empty())
        {
            auto& frame = s.current_frame();
            s.wait_frame(frame);
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = static_cast<std::uint32_t>(s.commands.size());
            submit.pCommandBuffers = s.commands.data();
            if (s.signal)
            {
                submit.signalSemaphoreCount = 1;
                submit.pSignalSemaphores = &s.signal;
            }
            s.submit_frame(frame, submit);
        }
        s.signal = VK_NULL_HANDLE;
        s.image = nullptr;
        s.waits.clear();
        s.commands.clear();
        return s.texture;
    }
    if (!s.image || !width || !height)
    {
        throw std::runtime_error("Vulkan frame has no image or dimensions");
    }
    auto image = *s.image;
    if (!image.create_info.image || (image.image_layout != VK_IMAGE_LAYOUT_GENERAL &&
                                     image.image_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    {
        throw std::runtime_error("Invalid Vulkan image or layout from core");
    }
    const SDL_PixelFormat pixel = SDL_PIXELFORMAT_RGBA32;
    // The unusual case where a core negotiates a different queue within the
    // same family retains the conservative handoff. Normal shared-queue frames
    // rely on GPU barriers and fences for command-buffer reuse only.
    if (!s.shared_queue)
    {
        s.idle();
    }
    s.allocate(width, height, VK_FORMAT_R8G8B8A8_UNORM);
    auto& frame = s.current_frame();
    s.wait_frame(frame);
    const auto command = frame.command;
    auto source_view = image.image_view;
    if (!source_view)
    {
        auto view = image.create_info;
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vk_check(vkCreateImageView(s.context.device, &view, nullptr, &frame.source_view));
        source_view = frame.source_view;
    }
    VkDescriptorImageInfo inputs[]{{s.sampler, source_view, image.image_layout},
                                   {VK_NULL_HANDLE, s.output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[2]{};
    for (std::uint32_t i = 0; i < 2; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = frame.descriptors;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &inputs[i];
    }
    vkUpdateDescriptorSets(s.context.device, 2, writes, 0, nullptr);
    vk_check(vkResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(command, &begin));
    VkImageMemoryBarrier barriers[2]{};
    for (auto& b : barriers)
    {
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    const bool transfer = s.source_family != VK_QUEUE_FAMILY_IGNORED &&
                          s.source_family != s.context.queue_family_index && !s.waits.empty() && s.commands.empty();
    auto& src = barriers[0];
    src.image = image.create_info.image;
    src.oldLayout = src.newLayout = image.image_layout;
    src.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    src.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src.subresourceRange = image.create_info.subresourceRange;
    if (transfer)
    {
        src.srcQueueFamilyIndex = s.source_family;
        src.dstQueueFamilyIndex = s.context.queue_family_index;
        src.srcAccessMask = 0;
    }
    auto& dst = barriers[1];
    dst.image = s.output;
    dst.oldLayout = s.output_initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    dst.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    dst.srcAccessMask = s.output_initialized ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0;
    dst.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 2, barriers);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipeline_layout, 0, 1, &frame.descriptors, 0,
                            nullptr);
    vkCmdDispatch(command, (width + 15) / 16, (height + 15) / 16, 1);
    src.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    if (transfer)
    {
        src.srcQueueFamilyIndex = s.context.queue_family_index;
        src.dstQueueFamilyIndex = s.source_family;
        src.dstAccessMask = 0;
    }
    dst.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    dst.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dst.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dst.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    // Some cores reuse private depth/working images between submissions.
    // Include those writes in the GPU handoff before the next core frame.
    VkMemoryBarrier handoff{};
    handoff.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    handoff.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    handoff.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                         &handoff, 0, nullptr, 2, barriers);
    vk_check(vkEndCommandBuffer(command));
    const auto core_commands = static_cast<std::uint32_t>(s.commands.size());
    s.commands.push_back(command);
    std::vector<VkPipelineStageFlags> stages(s.waits.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (!core_commands)
    {
        submit.waitSemaphoreCount = static_cast<std::uint32_t>(s.waits.size());
        submit.pWaitSemaphores = s.waits.data();
        submit.pWaitDstStageMask = stages.data();
    }
    submit.commandBufferCount = static_cast<std::uint32_t>(s.commands.size());
    submit.pCommandBuffers = s.commands.data();
    if (s.signal)
    {
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &s.signal;
    }
    s.submit_frame(frame, submit);
    s.output_initialized = true;
    if (!s.shared_queue)
    {
        s.idle();
    }
    s.commands.clear();
    s.waits.clear();
    s.signal = VK_NULL_HANDLE;
    s.image = nullptr;
    if (!s.texture)
    {
        // External texture creation stays guarded as an SDL renderer operation;
        // command preparation above does not access the shared Vulkan queue.
        std::lock_guard queue_lock(s.queue_mutex);
        auto p = SDL_CreateProperties();
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, pixel);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_VULKAN_TEXTURE_NUMBER,
                              reinterpret_cast<std::int64_t>(s.output));
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_VULKAN_LAYOUT_NUMBER,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        s.texture = SDL_CreateTextureWithProperties(s.renderer, p);
        SDL_DestroyProperties(p);
        sdl_check(s.texture != nullptr);
        SDL_SetTextureBlendMode(s.texture, SDL_BLENDMODE_NONE);
    }
    return s.texture;
}
