#include "shader_pass.h"
#include "shader_programs.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace
{
void check(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan shader operation failed: " + std::to_string(result));
    }
}

struct VulkanPass : ShaderPass
{
    std::size_t stage;
    SDL_Renderer* renderer;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkCommandPool commands = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkDescriptorSetLayout bindings = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE, linear_sampler = VK_NULL_HANDLE;
    VkImage output = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView output_view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    SDL_Texture* texture = nullptr;
    unsigned width = 0, height = 0;
    struct Slot
    {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::vector<VkImageView> source_views;
    };
    std::array<Slot, 3> slots{};
    std::size_t slot_index = 0;

    VulkanPass(SDL_Renderer* renderer, shaders::Effect effect, std::size_t stage)
        : ShaderPass(effect), stage(stage), renderer(renderer)
    {
        const auto props = SDL_GetRendererProperties(renderer);
        device = static_cast<VkDevice>(SDL_GetPointerProperty(props, SDL_PROP_RENDERER_VULKAN_DEVICE_POINTER, nullptr));
        gpu = static_cast<VkPhysicalDevice>(
            SDL_GetPointerProperty(props, SDL_PROP_RENDERER_VULKAN_PHYSICAL_DEVICE_POINTER, nullptr));
        family = static_cast<std::uint32_t>(
            SDL_GetNumberProperty(props, SDL_PROP_RENDERER_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER, 0));
        if (!device || !gpu)
        {
            throw std::runtime_error("Missing Vulkan shader device");
        }
        vkGetDeviceQueue(device, family, 0, &queue);
    }
    void destroy_target()
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
        if (framebuffer)
        {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        if (output_view)
        {
            vkDestroyImageView(device, output_view, nullptr);
        }
        if (output)
        {
            vkDestroyImage(device, output, nullptr);
        }
        if (memory)
        {
            vkFreeMemory(device, memory, nullptr);
        }
        framebuffer = VK_NULL_HANDLE;
        output_view = VK_NULL_HANDLE;
        output = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }
    ~VulkanPass() override
    {
        // Resource changes and teardown are outside steady-state processing.
        SDL_FlushRenderer(renderer);
        vkQueueWaitIdle(queue);
        destroy_target();
        for (auto& slot : slots)
        {
            for (auto view : slot.source_views)
            {
                vkDestroyImageView(device, view, nullptr);
            }
            if (slot.fence)
            {
                vkDestroyFence(device, slot.fence, nullptr);
            }
        }
        if (pipeline)
        {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        if (layout)
        {
            vkDestroyPipelineLayout(device, layout, nullptr);
        }
        if (render_pass)
        {
            vkDestroyRenderPass(device, render_pass, nullptr);
        }
        if (sampler)
        {
            vkDestroySampler(device, sampler, nullptr);
        }
        if (linear_sampler)
        {
            vkDestroySampler(device, linear_sampler, nullptr);
        }
        if (descriptors)
        {
            vkDestroyDescriptorPool(device, descriptors, nullptr);
        }
        if (bindings)
        {
            vkDestroyDescriptorSetLayout(device, bindings, nullptr);
        }
        if (commands)
        {
            vkDestroyCommandPool(device, commands, nullptr);
        }
    }
    void initialize()
    {
        if (pipeline)
        {
            return;
        }
        const auto input_count = static_cast<std::uint32_t>(shaders::program(effect).stages[stage].inputs.size() + 1);
        std::vector<VkDescriptorSetLayoutBinding> binding(input_count);
        for (std::uint32_t i = 0; i < input_count; ++i)
        {
            binding[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        }
        VkDescriptorSetLayoutCreateInfo set_info{};
        set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set_info.bindingCount = input_count;
        set_info.pBindings = binding.data();
        check(vkCreateDescriptorSetLayout(device, &set_info, nullptr, &bindings));
        const auto constant_size =
            static_cast<std::uint32_t>((shaders::parameter_vector_count(effect) + 3) * 4 * sizeof(float));
        VkPushConstantRange constants{VK_SHADER_STAGE_FRAGMENT_BIT, 0, constant_size};
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &bindings;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &constants;
        check(vkCreatePipelineLayout(device, &layout_info, nullptr, &layout));
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        const std::array<VkSubpassDependency, 2> dependencies{
            {{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
             {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, 0}}};
        VkRenderPassCreateInfo pass_info{};
        pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass_info.attachmentCount = 1;
        pass_info.pAttachments = &attachment;
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = dependencies.size();
        pass_info.pDependencies = dependencies.data();
        check(vkCreateRenderPass(device, &pass_info, nullptr, &render_pass));
        auto module = [&](const auto& code)
        {
            VkShaderModuleCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = code.size_bytes();
            info.pCode = code.data();
            VkShaderModule result;
            check(vkCreateShaderModule(device, &info, nullptr, &result));
            return result;
        };
        const auto& code = shaders::program(effect).stages[stage];
        const auto vertex = module(code.vertex);
        VkShaderModule fragment = VK_NULL_HANDLE;
        try
        {
            fragment = module(code.fragment);
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            for (auto& stage : stages)
            {
                stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            }
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertex;
            stages[0].pName = "main";
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragment;
            stages[1].pName = "main";
            VkPipelineVertexInputStateCreateInfo input{};
            input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blend{};
            blend.colorWriteMask = 15;
            VkPipelineColorBlendStateCreateInfo blending{};
            blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blending.attachmentCount = 1;
            blending.pAttachments = &blend;
            const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = dynamic_states.size();
            dynamic.pDynamicStates = dynamic_states.data();
            VkGraphicsPipelineCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.stageCount = stages.size();
            info.pStages = stages.data();
            info.pVertexInputState = &input;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pColorBlendState = &blending;
            info.pDynamicState = &dynamic;
            info.layout = layout;
            info.renderPass = render_pass;
            check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
        }
        catch (...)
        {
            vkDestroyShaderModule(device, vertex, nullptr);
            if (fragment)
            {
                vkDestroyShaderModule(device, fragment, nullptr);
            }
            throw;
        }
        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyShaderModule(device, fragment, nullptr);
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device, &sampler_info, nullptr, &sampler));
        sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_LINEAR;
        check(vkCreateSampler(device, &sampler_info, nullptr, &linear_sampler));
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &pool, nullptr, &commands));
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  static_cast<std::uint32_t>(slots.size()) * input_count};
        VkDescriptorPoolCreateInfo descriptor_info{};
        descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_info.maxSets = slots.size();
        descriptor_info.poolSizeCount = 1;
        descriptor_info.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &descriptor_info, nullptr, &descriptors));
        for (auto& slot : slots)
        {
            VkCommandBufferAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate.commandPool = commands;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &allocate, &slot.command));
            VkDescriptorSetAllocateInfo set{};
            set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            set.descriptorPool = descriptors;
            set.descriptorSetCount = 1;
            set.pSetLayouts = &bindings;
            check(vkAllocateDescriptorSets(device, &set, &slot.descriptor));
            VkFenceCreateInfo fence{};
            fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(device, &fence, nullptr, &slot.fence));
        }
    }
    void create_target(unsigned frame_width, unsigned frame_height)
    {
        if (texture && width == frame_width && height == frame_height)
        {
            return;
        }
        check(vkQueueWaitIdle(queue));
        destroy_target();
        width = frame_width;
        height = frame_height;
        VkImageCreateInfo image{};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {width, height, 1};
        image.mipLevels = image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        check(vkCreateImage(device, &image, nullptr, &output));
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, output, &requirements);
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(gpu, &properties);
        std::uint32_t type = 0;
        while (type < properties.memoryTypeCount &&
               (!(requirements.memoryTypeBits & (1u << type)) ||
                !(properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
        {
            ++type;
        }
        if (type == properties.memoryTypeCount)
        {
            throw std::runtime_error("No shader image memory type");
        }
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &allocate, nullptr, &memory));
        check(vkBindImageMemory(device, output, memory, 0));
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = output;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = image.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device, &view, nullptr, &output_view));
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = render_pass;
        fb.attachmentCount = 1;
        fb.pAttachments = &output_view;
        fb.width = width;
        fb.height = height;
        fb.layers = 1;
        check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffer));
        const auto props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_VULKAN_TEXTURE_NUMBER,
                              reinterpret_cast<std::int64_t>(output));
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_VULKAN_LAYOUT_NUMBER,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        texture = SDL_CreateTextureWithProperties(renderer, props);
        SDL_DestroyProperties(props);
        if (!texture)
        {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    }
    SDL_Texture* draw(SDL_Texture* input, unsigned frame_width, unsigned frame_height,
                      const shaders::Shader& settings, unsigned frame_count, const SDL_FRect& crop, bool flipped,
                      std::span<const ShaderInput> auxiliary) override
    {
        if (!SDL_FlushRenderer(renderer))
        {
            throw std::runtime_error(SDL_GetError());
        }
        initialize();
        create_target(frame_width, frame_height);
        auto& slot = slots[slot_index++ % slots.size()];
        check(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX));
        for (auto view : slot.source_views)
        {
            vkDestroyImageView(device, view, nullptr);
        }
        slot.source_views.clear();
        const auto input_count = static_cast<std::uint32_t>(auxiliary.size() + 1);
        std::array<VkDescriptorImageInfo, shaders::maximum_sampler_count> sources{};
        std::array<VkWriteDescriptorSet, shaders::maximum_sampler_count> writes{};
        std::array<VkImageMemoryBarrier, shaders::maximum_sampler_count> barriers{};
        for (std::size_t i = 0; i < input_count; ++i)
        {
            auto* sampled = i == 0 ? input : auxiliary[i - 1].texture;
            const auto props = SDL_GetTextureProperties(sampled);
            const auto format = SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_FORMAT_NUMBER, 0);
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = reinterpret_cast<VkImage>(SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_VULKAN_TEXTURE_NUMBER, 0));
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = (format == SDL_PIXELFORMAT_BGRA32 || format == SDL_PIXELFORMAT_BGRX32)
                              ? VK_FORMAT_B8G8R8A8_UNORM
                              : VK_FORMAT_R8G8B8A8_UNORM;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView source_view = VK_NULL_HANDLE;
            check(vkCreateImageView(device, &view, nullptr, &source_view));
            slot.source_views.push_back(source_view);
            sources[i] = {(i > 0 && auxiliary[i - 1].linear) ? linear_sampler : sampler,
                          source_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            auto& write = writes[i];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = slot.descriptor;
            write.dstBinding = static_cast<std::uint32_t>(i);
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &sources[i];
            auto& barrier = barriers[i];
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = view.image;
            barrier.subresourceRange = view.subresourceRange;
        }
        vkUpdateDescriptorSets(device, input_count, writes.data(), 0, nullptr);
        check(vkResetCommandBuffer(slot.command, 0));
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(slot.command, &begin));
        vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, input_count, barriers.data());
        VkRenderPassBeginInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = render_pass;
        pass.framebuffer = framebuffer;
        pass.renderArea.extent = {width, height};
        vkCmdBeginRenderPass(slot.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(slot.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(slot.command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &slot.descriptor, 0,
                                nullptr);
        VkViewport viewport{0, 0, float(width), float(height), 0, 1};
        VkRect2D scissor{{0, 0}, {width, height}};
        vkCmdSetViewport(slot.command, 0, 1, &viewport);
        vkCmdSetScissor(slot.command, 0, 1, &scissor);
        const auto values = shaders::uniform_values(settings);
        const auto size = shaders::parameter_vector_count(effect) * 4;
        std::array<float, (shaders::maximum_parameter_vectors + 3) * 4> parameters{};
        std::copy_n(values.begin(), size, parameters.begin());
        const auto source_values = shader_source_values(input, crop, flipped, frame_count, width, height);
        std::copy(source_values.begin(), source_values.end(), parameters.begin() + size);
        vkCmdPushConstants(slot.command, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           static_cast<std::uint32_t>((size + 12) * sizeof(float)), parameters.data());
        vkCmdDraw(slot.command, 3, 1, 0, 0);
        vkCmdEndRenderPass(slot.command);
        check(vkEndCommandBuffer(slot.command));
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &slot.command;
        check(vkResetFences(device, 1, &slot.fence));
        check(vkQueueSubmit(queue, 1, &submit, slot.fence));
        return texture;
    }
};
} // namespace

std::unique_ptr<ShaderPass> create_vulkan_shader_pass(SDL_Renderer* renderer, shaders::Effect effect, std::size_t stage)
{
    return std::make_unique<VulkanPass>(renderer, effect, stage);
}
