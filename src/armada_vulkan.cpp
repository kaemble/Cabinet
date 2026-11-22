#include "armada_vulkan.h"
#include <cstdint>
#include "armada_drm_geometry.h"
#include "armada_display_shaders.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
    }
}
void require(bool ok, const char* message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}
template <typename T> T instance_proc(VkInstance instance, const char* name)
{
    auto proc = reinterpret_cast<T>(vkGetInstanceProcAddr(instance, name));
    require(proc != nullptr, name);
    return proc;
}
VkFormat texture_format(SDL_Texture* texture)
{
    const auto format = static_cast<SDL_PixelFormat>(
        SDL_GetNumberProperty(SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_FORMAT_NUMBER, 0));
    if (format == SDL_PIXELFORMAT_RGBA32 || format == SDL_PIXELFORMAT_RGBX32)
    {
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    if (format == SDL_PIXELFORMAT_BGRA32 || format == SDL_PIXELFORMAT_BGRX32)
    {
        return VK_FORMAT_B8G8R8A8_UNORM;
    }
    // Callers use native SDL formats rather than opaque conversion wrappers,
    // since those wrappers do not expose their underlying VkImage.
    throw std::runtime_error("Unsupported Vulkan source texture format: " +
                             std::string(SDL_GetPixelFormatName(format)));
}
}

struct ArmadaVulkan::Impl
{
    SDL_Renderer* renderer = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkDisplayKHR display = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    PFN_vkReleaseDisplayEXT release_display = nullptr;
    bool acquired_display = false, drawing = false;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE, linear = VK_NULL_HANDLE;
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        // Indexed by swapchain image, so reacquisition proves presentation
        // consumed this binary semaphore before it can be signaled again.
        VkSemaphore ready = VK_NULL_HANDLE;
    };
    std::vector<Image> images;
    struct Frame
    {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore available = VK_NULL_HANDLE;
        VkDescriptorSet descriptors = VK_NULL_HANDLE;
        VkImageView source_view = VK_NULL_HANDLE;
        bool pending = false;
        std::uint64_t submitted = 0;
    };
    std::array<Frame, 2> frames{};
    unsigned slot = 0;
    std::uint32_t image_index = 0;
    SDL_Texture* source_texture = nullptr;
    struct Parameters
    {
        std::array<float, 4> destination, source, options, panel;
    } parameters{};

    ~Impl()
    {
        if (!device)
        {
            return;
        }
        // Teardown only. All steady-state acquire/fence checks use timeout zero.
        vkDeviceWaitIdle(device);
        for (auto& frame : frames)
        {
            if (frame.source_view)
            {
                vkDestroyImageView(device, frame.source_view, nullptr);
            }
            if (frame.fence)
            {
                vkDestroyFence(device, frame.fence, nullptr);
            }
            if (frame.available)
            {
                vkDestroySemaphore(device, frame.available, nullptr);
            }
        }
        if (pool)
        {
            vkDestroyCommandPool(device, pool, nullptr);
        }
        if (pipeline)
        {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        if (layout)
        {
            vkDestroyPipelineLayout(device, layout, nullptr);
        }
        if (descriptor_pool)
        {
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        }
        if (descriptors)
        {
            vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
        }
        if (nearest)
        {
            vkDestroySampler(device, nearest, nullptr);
        }
        if (linear)
        {
            vkDestroySampler(device, linear, nullptr);
        }
        for (auto& image : images)
        {
            if (image.framebuffer)
            {
                vkDestroyFramebuffer(device, image.framebuffer, nullptr);
            }
            if (image.view)
            {
                vkDestroyImageView(device, image.view, nullptr);
            }
            if (image.ready)
            {
                vkDestroySemaphore(device, image.ready, nullptr);
            }
        }
        if (pass)
        {
            vkDestroyRenderPass(device, pass, nullptr);
        }
        if (swapchain)
        {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        }
        if (surface)
        {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
        if (acquired_display)
        {
            release_display(gpu, display);
        }
    }

    void create_surface(int fd, std::uint32_t connector, unsigned width, unsigned height)
    {
        auto get_display = instance_proc<PFN_vkGetDrmDisplayEXT>(instance, "vkGetDrmDisplayEXT");
        auto acquire_display = instance_proc<PFN_vkAcquireDrmDisplayEXT>(instance, "vkAcquireDrmDisplayEXT");
        release_display = instance_proc<PFN_vkReleaseDisplayEXT>(instance, "vkReleaseDisplayEXT");
        check(get_display(gpu, fd, connector, &display), "vkGetDrmDisplayEXT");
        check(acquire_display(gpu, fd, display), "vkAcquireDrmDisplayEXT");
        acquired_display = true;
        std::uint32_t count = 0;
        check(vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &count, nullptr), "display property count");
        std::vector<VkDisplayPropertiesKHR> displays(count);
        check(vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &count, displays.data()), "display properties");
        const auto properties = std::find_if(displays.begin(), displays.end(),
                                             [&](const auto& d)
                                             {
                                                 return d.display == display;
                                             });
        require(properties != displays.end() &&
                    (properties->supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR),
                "leased display does not support an identity surface transform");
        check(vkGetDisplayModePropertiesKHR(gpu, display, &count, nullptr), "display mode count");
        std::vector<VkDisplayModePropertiesKHR> modes(count);
        check(vkGetDisplayModePropertiesKHR(gpu, display, &count, modes.data()), "display modes");
        auto mode = std::find_if(modes.begin(), modes.end(),
                                 [&](const auto& m)
                                 {
                                     return m.parameters.visibleRegion.width == width &&
                                            m.parameters.visibleRegion.height == height;
                                 });
        require(mode != modes.end(), "leased Vulkan display has no matching native mode");
        check(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &count, nullptr), "display plane count");
        std::vector<VkDisplayPlanePropertiesKHR> planes(count);
        check(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &count, planes.data()), "display planes");
        std::uint32_t chosen = UINT32_MAX;
        for (std::uint32_t i = 0; i < planes.size(); ++i)
        {
            if (planes[i].currentDisplay && planes[i].currentDisplay != display)
            {
                continue;
            }
            std::uint32_t n = 0;
            check(vkGetDisplayPlaneSupportedDisplaysKHR(gpu, i, &n, nullptr), "supported display count");
            std::vector<VkDisplayKHR> supported(n);
            check(vkGetDisplayPlaneSupportedDisplaysKHR(gpu, i, &n, supported.data()), "supported displays");
            if (std::find(supported.begin(), supported.end(), display) != supported.end())
            {
                chosen = i;
                break;
            }
        }
        require(chosen != UINT32_MAX, "no Vulkan plane supports the leased display");
        VkDisplayPlaneCapabilitiesKHR caps{};
        check(vkGetDisplayPlaneCapabilitiesKHR(gpu, mode->displayMode, chosen, &caps), "display plane capabilities");
        VkDisplayPlaneAlphaFlagBitsKHR alpha = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedAlpha & alpha))
        {
            alpha = VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
            require(caps.supportedAlpha & alpha, "leased display has no opaque/global alpha mode");
        }
        VkDisplaySurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
        ci.displayMode = mode->displayMode;
        ci.planeIndex = chosen;
        ci.planeStackIndex = planes[chosen].currentStackIndex;
        ci.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        ci.globalAlpha = 1;
        ci.alphaMode = alpha;
        ci.imageExtent = {width, height};
        check(vkCreateDisplayPlaneSurfaceKHR(instance, &ci, nullptr, &surface), "display surface creation");
        VkBool32 supported = VK_FALSE;
        check(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, family, surface, &supported), "display queue support");
        require(supported, "SDL graphics queue cannot present to the leased display");
    }
    void create_swapchain()
    {
        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps), "display surface capabilities");
        std::uint32_t count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr), "display format count");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data()), "display formats");
        auto selected =
            std::find_if(formats.begin(), formats.end(),
                         [](const auto& f)
                         {
                             return (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) &&
                                    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                         });
        require(selected != formats.end(), "leased display has no supported SDR format");
        format = selected->format;
        require(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                "display images cannot be render targets");
        require(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                "display requires an unsupported transform");
        if (caps.currentExtent.width != UINT32_MAX)
        {
            require(caps.currentExtent.width == extent.width && caps.currentExtent.height == extent.height,
                    "leased Vulkan surface extent differs from the DRM panel mode");
        }
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & alpha))
        {
            for (auto candidate : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                   VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            {
                if (caps.supportedCompositeAlpha & candidate)
                {
                    alpha = candidate;
                    break;
                }
            }
        }
        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface;
        ci.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount)
        {
            ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
        }
        ci.imageFormat = format;
        ci.imageColorSpace = selected->colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        ci.compositeAlpha = alpha;
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        ci.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain), "leased swapchain creation");
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "swapchain image count");
        std::vector<VkImage> handles(count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, handles.data()), "swapchain images");
        images.resize(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            images[i].image = handles[i];
        }
    }
    void create_pipeline()
    {
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass_ci{};
        pass_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass_ci.attachmentCount = 1;
        pass_ci.pAttachments = &attachment;
        pass_ci.subpassCount = 1;
        pass_ci.pSubpasses = &subpass;
        pass_ci.dependencyCount = 1;
        pass_ci.pDependencies = &dependency;
        check(vkCreateRenderPass(device, &pass_ci, nullptr, &pass), "display render pass");
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo descriptor_ci{};
        descriptor_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor_ci.bindingCount = 1;
        descriptor_ci.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device, &descriptor_ci, nullptr, &descriptors), "display descriptor layout");
        VkPushConstantRange constants{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Parameters)};
        VkPipelineLayoutCreateInfo layout_ci{};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = &descriptors;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &constants;
        check(vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout), "display pipeline layout");
        VkShaderModule modules[2]{};
        VkShaderModuleCreateInfo module_ci{};
        module_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_ci.codeSize = sizeof(armada_display_vertex);
        module_ci.pCode = armada_display_vertex;
        check(vkCreateShaderModule(device, &module_ci, nullptr, &modules[0]), "display vertex shader");
        module_ci.codeSize = sizeof(armada_display_fragment);
        module_ci.pCode = armada_display_fragment;
        const auto fragment_result = vkCreateShaderModule(device, &module_ci, nullptr, &modules[1]);
        if (fragment_result != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, modules[0], nullptr);
            check(fragment_result, "display fragment shader");
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto& stage : stages)
        {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = modules[0];
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = modules[1];
        VkPipelineVertexInputStateCreateInfo vertex{};
        vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        VkPipelineViewportStateCreateInfo viewport_ci{};
        viewport_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_ci.viewportCount = 1;
        viewport_ci.pViewports = &viewport;
        viewport_ci.scissorCount = 1;
        viewport_ci.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1;
        raster.cullMode = VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend_ci{};
        blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend_ci.attachmentCount = 1;
        blend_ci.pAttachments = &blend;
        VkGraphicsPipelineCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vertex;
        ci.pInputAssemblyState = &assembly;
        ci.pViewportState = &viewport_ci;
        ci.pRasterizationState = &raster;
        ci.pMultisampleState = &samples;
        ci.pColorBlendState = &blend_ci;
        ci.layout = layout;
        ci.renderPass = pass;
        const auto result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
        for (auto module : modules)
        {
            vkDestroyShaderModule(device, module, nullptr);
        }
        check(result, "display graphics pipeline");
    }
    void create_resources()
    {
        VkSamplerCreateInfo sampler{};
        sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        check(vkCreateSampler(device, &sampler, nullptr, &nearest), "display nearest sampler");
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        check(vkCreateSampler(device, &sampler, nullptr, &linear), "display linear sampler");
        VkCommandPoolCreateInfo pool_ci{};
        pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_ci.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &pool_ci, nullptr, &pool), "display command pool");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, std::uint32_t(frames.size())};
        VkDescriptorPoolCreateInfo descriptor_ci{};
        descriptor_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_ci.maxSets = frames.size();
        descriptor_ci.poolSizeCount = 1;
        descriptor_ci.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &descriptor_ci, nullptr, &descriptor_pool), "display descriptor pool");
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (auto& frame : frames)
        {
            VkCommandBufferAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc.commandPool = pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &alloc, &frame.command), "display command buffer");
            VkFenceCreateInfo fence{};
            fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            check(vkCreateFence(device, &fence, nullptr, &frame.fence), "display frame fence");
            check(vkCreateSemaphore(device, &semaphore, nullptr, &frame.available), "display acquire semaphore");
            VkDescriptorSetAllocateInfo descriptor_alloc{};
            descriptor_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_alloc.descriptorPool = descriptor_pool;
            descriptor_alloc.descriptorSetCount = 1;
            descriptor_alloc.pSetLayouts = &descriptors;
            check(vkAllocateDescriptorSets(device, &descriptor_alloc, &frame.descriptors), "display descriptor set");
        }
        for (auto& image : images)
        {
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = image.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device, &view, nullptr, &image.view), "display image view");
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = pass;
            fb.attachmentCount = 1;
            fb.pAttachments = &image.view;
            fb.width = extent.width;
            fb.height = extent.height;
            fb.layers = 1;
            check(vkCreateFramebuffer(device, &fb, nullptr, &image.framebuffer), "display framebuffer");
            check(vkCreateSemaphore(device, &semaphore, nullptr, &image.ready), "display present semaphore");
        }
    }
};

ArmadaVulkan::ArmadaVulkan(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
ArmadaVulkan::~ArmadaVulkan() = default;
std::unique_ptr<ArmadaVulkan> ArmadaVulkan::create(SDL_Renderer* renderer, int lease_fd, std::uint32_t connector,
                                                   unsigned width, unsigned height)
{
    auto s = std::make_unique<Impl>();
    s->renderer = renderer;
    s->extent = {width, height};
    const auto props = SDL_GetRendererProperties(renderer);
    s->instance =
        static_cast<VkInstance>(SDL_GetPointerProperty(props, SDL_PROP_RENDERER_VULKAN_INSTANCE_POINTER, nullptr));
    s->gpu = static_cast<VkPhysicalDevice>(
        SDL_GetPointerProperty(props, SDL_PROP_RENDERER_VULKAN_PHYSICAL_DEVICE_POINTER, nullptr));
    s->device = static_cast<VkDevice>(SDL_GetPointerProperty(props, SDL_PROP_RENDERER_VULKAN_DEVICE_POINTER, nullptr));
    s->family = SDL_GetNumberProperty(props, SDL_PROP_RENDERER_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER, 0);
    require(s->instance && s->gpu && s->device, "SDL Vulkan device is unavailable");
    vkGetDeviceQueue(s->device, s->family, 0, &s->queue);
    s->create_surface(lease_fd, connector, width, height);
    s->create_swapchain();
    s->create_pipeline();
    s->create_resources();
    return std::unique_ptr<ArmadaVulkan>(new ArmadaVulkan(std::move(s)));
}
bool ArmadaVulkan::begin()
{
    auto& s = *impl_;
    auto& frame = s.frames[s.slot];
    require(!s.drawing, "previous Vulkan display frame was not presented");
    if (frame.pending)
    {
        const auto status = vkGetFenceStatus(s.device, frame.fence);
        if (status == VK_NOT_READY)
        {
            require(SDL_GetTicks() - frame.submitted < 2000, "Vulkan display frame timed out");
            return false;
        }
        check(status, "display frame fence");
        frame.pending = false;
    }
    const auto result =
        vkAcquireNextImageKHR(s.device, s.swapchain, 0, frame.available, VK_NULL_HANDLE, &s.image_index);
    if (result == VK_NOT_READY || result == VK_TIMEOUT)
    {
        return false;
    }
    if (result != VK_SUBOPTIMAL_KHR)
    {
        check(result, "leased display image acquire");
    }
    if (frame.source_view)
    {
        vkDestroyImageView(s.device, frame.source_view, nullptr);
        frame.source_view = VK_NULL_HANDLE;
    }
    s.source_texture = nullptr;
    s.parameters.options = {0, 1, 0, 0};
    s.drawing = true;
    return true;
}
void ArmadaVulkan::draw(SDL_Texture* texture, SDL_FRect source, SDL_FRect destination, SDL_FlipMode flip,
                        int logical_width, int logical_height, int turns, int sharp_factor)
{
    auto& s = *impl_;
    require(s.drawing, "Vulkan draw without an acquired image");
    const auto props = SDL_GetTextureProperties(texture);
    const auto width = SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_WIDTH_NUMBER, 0);
    const auto height = SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_HEIGHT_NUMBER, 0);
    require(width > 0 && height > 0, "Vulkan display source has invalid dimensions");
    const auto dest = armada_drm::rotated_destination(destination, logical_width, logical_height, turns);
    s.parameters.destination = {dest.x + dest.w / 2, dest.y + dest.h / 2, dest.w / 2, dest.h / 2};
    s.parameters.source = {source.x / width, source.y / height, source.w / width, source.h / height};
    s.parameters.options[0] = float(turns);
    s.parameters.options[2] = (flip == SDL_FLIP_VERTICAL) ? 1.f : 0.f;
    s.parameters.options[3] = float(sharp_factor);
    s.parameters.panel = {2.f / s.extent.width, 2.f / s.extent.height, float(width), float(height)};
    s.source_texture = texture;
}
void ArmadaVulkan::dim(std::uint8_t alpha)
{
    impl_->parameters.options[1] = (255 - alpha) / 255.f;
}
void ArmadaVulkan::present()
{
    auto& s = *impl_;
    if (!s.drawing)
    {
        return;
    }
    auto& frame = s.frames[s.slot];
    auto& image = s.images[s.image_index];
    check(vkResetCommandBuffer(frame.command, 0), "display command reset");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.command, &begin), "display command begin");
    if (s.source_texture)
    {
        const auto handle = SDL_GetNumberProperty(SDL_GetTextureProperties(s.source_texture),
                                                  SDL_PROP_TEXTURE_VULKAN_TEXTURE_NUMBER, 0);
        require(handle != 0, "SDL texture has no native Vulkan image");
        const auto source = reinterpret_cast<VkImage>(handle);
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = source;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = texture_format(s.source_texture);
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(s.device, &view, nullptr, &frame.source_view), "display source view");
        // SDL_RenderPresent has submitted uploads and top-screen reads on this
        // same queue. Make those writes visible to the native fragment shader.
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = source;
        barrier.subresourceRange = view.subresourceRange;
        vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        SDL_ScaleMode scaling = SDL_SCALEMODE_NEAREST;
        SDL_GetTextureScaleMode(s.source_texture, &scaling);
        VkDescriptorImageInfo input{(scaling == SDL_SCALEMODE_NEAREST) ? s.nearest : s.linear, frame.source_view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.descriptors;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &input;
        vkUpdateDescriptorSets(s.device, 1, &write, 0, nullptr);
    }
    VkClearValue clear{};
    clear.color.float32[3] = 1;
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = s.pass;
    pass.framebuffer = image.framebuffer;
    pass.renderArea.extent = s.extent;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(frame.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    if (s.source_texture)
    {
        vkCmdBindPipeline(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipeline);
        vkCmdBindDescriptorSets(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS, s.layout, 0, 1, &frame.descriptors, 0,
                                nullptr);
        vkCmdPushConstants(frame.command, s.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(s.parameters), &s.parameters);
        vkCmdDraw(frame.command, 6, 1, 0, 0);
    }
    vkCmdEndRenderPass(frame.command);
    check(vkEndCommandBuffer(frame.command), "display command end");
    check(vkResetFences(s.device, 1, &frame.fence), "display fence reset");
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &image.ready;
    check(vkQueueSubmit(s.queue, 1, &submit, frame.fence), "display render submission");
    frame.pending = true;
    frame.submitted = SDL_GetTicks();
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &image.ready;
    present.swapchainCount = 1;
    present.pSwapchains = &s.swapchain;
    present.pImageIndices = &s.image_index;
    const auto result = vkQueuePresentKHR(s.queue, &present);
    if (result != VK_SUBOPTIMAL_KHR)
    {
        check(result, "leased display presentation");
    }
    s.drawing = false;
    s.source_texture = nullptr;
    s.slot = (s.slot + 1) % s.frames.size();
}
