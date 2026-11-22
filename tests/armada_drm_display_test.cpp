// Manual hardware smoke test. No core or ROM needed. Exits after three seconds
// and releases the panel back to Armada's compositor.
#include "armada_drm.h"
#include "vulkan_video.h"
#include "armada_drm_geometry.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
// Wrap only the native backend's Vulkan calls (SDL is a shared library). This
// adds a single test-only readback to the bottom swapchain before its first
// presentation; production code needs neither a capture option nor a copy pass.
struct SwapchainCapture {
    VkPhysicalDevice gpu{};
    VkDevice device{};
    uint32_t family = 0;
    VkSwapchainKHR swapchain{};
    VkExtent2D extent{};
    VkFormat format{};
    int turns = 0;
    int sharp_factor = 1;
    bool flipped = false;
    float dim = 1;
    VkCommandPool pool{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkFence fence{};
    VkSemaphore ready{};
    bool captured = false, correct = false;
    void cleanup() {
        if (!device) return;
        vkDeviceWaitIdle(device);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (ready) vkDestroySemaphore(device, ready, nullptr);
        device = VK_NULL_HANDLE;
    }
    static void check(VkResult result) {
        if (result != VK_SUCCESS) throw std::runtime_error("Swapchain pixel capture failed: " + std::to_string(result));
    }
    void record(VkQueue queue, const VkPresentInfoKHR& present) {
        uint32_t count = 0; check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr));
        std::vector<VkImage> images(count); check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()));
        const auto image = images.at(present.pImageIndices[0]);
        VkBufferCreateInfo buffer_ci{}; buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_ci.size = uint64_t(extent.width)*extent.height*4; buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vkCreateBuffer(device, &buffer_ci, nullptr, &buffer));
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device, buffer, &requirements);
        VkPhysicalDeviceMemoryProperties properties{}; vkGetPhysicalDeviceMemoryProperties(gpu, &properties);
        uint32_t type = 0;
        for (; type < properties.memoryTypeCount; ++type)
            if ((requirements.memoryTypeBits & (1u << type)) &&
                (properties.memoryTypes[type].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) break;
        if (type == properties.memoryTypeCount) throw std::runtime_error("No coherent capture memory");
        VkMemoryAllocateInfo alloc{}; alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size; alloc.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &alloc, nullptr, &memory)); check(vkBindBufferMemory(device, buffer, memory, 0));
        VkCommandPoolCreateInfo pool_ci{}; pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pool_ci.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &pool_ci, nullptr, &pool));
        VkCommandBufferAllocateInfo command_alloc{}; command_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_alloc.commandPool = pool; command_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_alloc.commandBufferCount = 1;
        VkCommandBuffer command{}; check(vkAllocateCommandBuffers(device, &command_alloc, &command));
        VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        check(vkBeginCommandBuffer(command, &begin));
        VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent = {extent.width,extent.height,1};
        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
        check(vkEndCommandBuffer(command));
        VkSemaphoreCreateInfo semaphore{}; semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(device, &semaphore, nullptr, &ready));
        VkFenceCreateInfo fence_ci{}; fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(device, &fence_ci, nullptr, &fence));
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = present.waitSemaphoreCount; submit.pWaitSemaphores = present.pWaitSemaphores; submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &ready;
        check(vkQueueSubmit(queue, 1, &submit, fence)); check(vkWaitForFences(device, 1, &fence, VK_TRUE, 2000000000));
        void* mapped = nullptr; check(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        const Uint8 colors[]{255,0,0, 0,0,255, 0,255,0, 255,255,255};
        correct = true;
        const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM;
        auto points = [](int size) { return std::array<int,9>{0,size/4,size/2-size/16,size/2-1,size/2,size/2+1,size/2+size/16,3*size/4,size-1}; };
        for (int y : points(extent.height)) for (int x : points(extent.width)) {
            float nx = (x+.5f)/extent.width, ny = (y+.5f)/extent.height;
            armada_drm::unrotate(nx, ny, turns); if (flipped) ny = 1-ny;
            auto expanded = [&](int ix, int iy, int channel) {
                return float(colors[(std::clamp(iy,0,2*sharp_factor-1)/sharp_factor*2+
                    std::clamp(ix,0,2*sharp_factor-1)/sharp_factor)*3+channel]);
            };
            const auto* pixel = static_cast<const Uint8*>(mapped) + (y*extent.width+x)*4;
            bool matched = true;
            for (int channel = 0; channel < 3; ++channel) {
                float expected;
                if (sharp_factor == 1) expected = expanded(int(nx*2),int(ny*2),channel);
                else {
                    // Independent reference: an integer nearest enlargement,
                    // then bilinear interpolation of its four neighboring pixels.
                    const float px = nx*2*sharp_factor-.5f, py = ny*2*sharp_factor-.5f;
                    const int ix = int(std::floor(px)), iy = int(std::floor(py));
                    const float fx = px-ix, fy = py-iy;
                    expected = std::lerp(std::lerp(expanded(ix,iy,channel),expanded(ix+1,iy,channel),fx),
                        std::lerp(expanded(ix,iy+1,channel),expanded(ix+1,iy+1,channel),fx),fy);
                }
                const int actual = pixel[bgra ? 2-channel : channel];
                matched = matched && std::abs(actual-int(std::lround(expected*dim))) <= 1;
            }
            if (!matched) std::cerr << "Vulkan pixel " << x << ',' << y << " is " << int(pixel[bgra?2:0]) << ',' << int(pixel[1]) << ',' << int(pixel[bgra?0:2]) << '\n';
            correct = correct && matched;
        }
        vkUnmapMemory(device, memory); captured = true;
    }
};
static SwapchainCapture capture;
extern "C" VkResult __real_vkCreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);
extern "C" VkResult __wrap_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain) {
    auto ci = *info; ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const auto result = __real_vkCreateSwapchainKHR(device, &ci, allocator, swapchain);
    if (result == VK_SUCCESS) { capture.swapchain = *swapchain; capture.extent = ci.imageExtent; capture.format = ci.imageFormat; }
    return result;
}
extern "C" VkResult __real_vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR*);
extern "C" VkResult __wrap_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    if (!capture.captured && capture.device && info->swapchainCount == 1 && info->pSwapchains[0] == capture.swapchain) {
        capture.record(queue, *info);
        auto present = *info; present.waitSemaphoreCount = 1; present.pWaitSemaphores = &capture.ready;
        return __real_vkQueuePresentKHR(queue, &present);
    }
    return __real_vkQueuePresentKHR(queue, info);
}
static unsigned readbacks = 0;
extern "C" SDL_Surface* __real_SDL_RenderReadPixels(SDL_Renderer*, const SDL_Rect*);
extern "C" SDL_Surface* __wrap_SDL_RenderReadPixels(SDL_Renderer* renderer, const SDL_Rect* rect) {
    ++readbacks;
    return __real_SDL_RenderReadPixels(renderer, rect);
}
int main(int argc, char** argv) {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* pattern = nullptr;
    std::unique_ptr<VulkanVideo> vulkan;
    int result = 0;
    const bool sharp_test = argc > 2 && std::string_view(argv[2]) == "sharp";
    const bool bridge_test = argc > 2 && std::string_view(argv[2]) == "bridge";
    try {
        const bool armada_video = ArmadaDrm::configure_video();
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("Cabinet DRM lease test", 640, 480,
            argc > 1 && std::string_view(argv[1]) == "vulkan" ? SDL_WINDOW_VULKAN : 0);
        if (!window) throw std::runtime_error(SDL_GetError());
        if (argc > 1 && std::string_view(argv[1]) == "vulkan") {
            vulkan = std::make_unique<VulkanVideo>();
            renderer = vulkan->create_renderer(window, false, armada_video);
        } else renderer = SDL_CreateRenderer(window, argc > 1 ? argv[1] : nullptr);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        if (vulkan) {
            const auto properties = SDL_GetRendererProperties(renderer);
            capture.device = static_cast<VkDevice>(SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_VULKAN_DEVICE_POINTER, nullptr));
            capture.gpu = static_cast<VkPhysicalDevice>(SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_VULKAN_PHYSICAL_DEVICE_POINTER, nullptr));
            capture.family = SDL_GetNumberProperty(properties, SDL_PROP_RENDERER_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER, 0);
        }
        auto panel = ArmadaDrm::try_create(renderer);
        if (!panel) throw std::runtime_error("No Armada lease available");
        capture.turns = panel->rotation();
        if (sharp_test) {
            if (!panel->native_vulkan()) throw std::runtime_error("Sharp native shader test requires Vulkan");
            capture.sharp_factor = 4; capture.flipped = true; capture.dim = 90.f/255;
        }
        const std::string_view renderer_name = SDL_GetRendererName(renderer);
        if ((renderer_name == "opengl" || renderer_name == "opengles2") && !panel->gpu_scanout())
            throw std::runtime_error("GL smoke test fell back to CPU readback");
        if (renderer_name == "vulkan" && !panel->native_vulkan())
            throw std::runtime_error("Vulkan smoke test fell back to CPU readback");
        pattern = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 2, 2);
        if (!pattern) throw std::runtime_error(SDL_GetError());
        const Uint8 pixels[]{255,0,0,255, 0,0,255,255, 0,255,0,255, 255,255,255,255};
        if (!SDL_UpdateTexture(pattern, nullptr, pixels, 8) || !SDL_SetTextureBlendMode(pattern, SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureScaleMode(pattern, sharp_test ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST))
            throw std::runtime_error(SDL_GetError());
        if (bridge_test) {
            if (!vulkan) throw std::runtime_error("Bridge test requires Vulkan");
            // Finish the pattern upload, then present it through the real
            // libretro Vulkan snapshot interface instead of sampling it directly.
            SDL_SetRenderDrawColor(renderer, 0,0,0,255); SDL_RenderClear(renderer); SDL_RenderPresent(renderer);
        }
        unsigned frames = 0;
        const auto start = SDL_GetTicks();
        while (SDL_GetTicks() - start < 3000) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_QUIT) throw std::runtime_error("Display test cancelled");
            panel->pump();
            if (panel->begin_draw()) {
                panel->clear();
                const SDL_FRect source{0, 0, 2, 2}, dest{0, 0, float(panel->width()), float(panel->height())};
                auto* image = pattern;
                if (bridge_test) {
                    vulkan->begin_frame();
                    const auto& api = *reinterpret_cast<const retro_hw_render_interface_vulkan*>(vulkan->interface());
                    retro_vulkan_image core_image{};
                    core_image.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    core_image.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    core_image.create_info.image = reinterpret_cast<VkImage>(SDL_GetNumberProperty(SDL_GetTextureProperties(pattern), SDL_PROP_TEXTURE_VULKAN_TEXTURE_NUMBER, 0));
                    core_image.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    core_image.create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
                    core_image.create_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                    api.set_image(api.handle, &core_image, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
                    image = vulkan->frame(2,2);
                    if (!image) throw std::runtime_error("Vulkan bridge did not export the frame");
                    SDL_SetTextureScaleMode(image, SDL_SCALEMODE_NEAREST);
                }
                panel->render_texture(image, source, dest, sharp_test ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE, sharp_test ? 4 : 1);
                if (sharp_test) panel->dim(165);
                if (!frames && !panel->native_vulkan()) {
                    // Test-only readback checks actual imported-FBO rendering,
                    // channel order and rotation before handing it to KMS.
                    auto* image = SDL_RenderReadPixels(renderer, nullptr);
                    if (!image) throw std::runtime_error(SDL_GetError());
                    const std::array<std::array<int, 4>, 4> order{{{0,1,2,3}, {2,0,3,1}, {3,2,1,0}, {1,3,0,2}}};
                    bool correct = true;
                    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
                        Uint8 r = 0, g = 0, b = 0, a = 0;
                        if (!SDL_ReadSurfacePixel(image, image->w * (2*x+1) / 4, image->h * (2*y+1) / 4, &r, &g, &b, &a))
                            correct = false;
                        const auto color = order[panel->gpu_scanout() ? panel->rotation() : 0][y*2+x] * 4;
                        if (r != pixels[color] || g != pixels[color+1] || b != pixels[color+2])
                            std::cerr << "Corner " << x << ',' << y << " RGB " << int(r) << ',' << int(g) << ',' << int(b)
                                << ", expected " << int(pixels[color]) << ',' << int(pixels[color+1]) << ',' << int(pixels[color+2])
                                << " in " << image->w << 'x' << image->h << " target\n";
                        correct = correct && r == pixels[color] && g == pixels[color+1] && b == pixels[color+2];
                    }
                    SDL_DestroySurface(image);
                    if (!correct) throw std::runtime_error("Scanout target pixels have incorrect color or rotation");
                }
                const auto before = readbacks;
                panel->present();
                if (panel->gpu_scanout() && readbacks != before)
                    throw std::runtime_error("GPU scanout performed a CPU readback");
                ++frames;
            }
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer); SDL_RenderPresent(renderer);
            panel->after_main_present();
            SDL_Delay(16);
        }
        if (frames < 10) throw std::runtime_error("Scanout stalled before ten frames");
        if (panel->native_vulkan() && (!capture.captured || !capture.correct))
            throw std::runtime_error("Native Vulkan swapchain pixels have incorrect colors or rotation");
        std::cout << "Direct lease, " << (panel->native_vulkan() ? "Vulkan direct" : panel->gpu_scanout() ? "GBM/EGL GPU" : "CPU fallback")
                  << " scanout and flip lifecycle passed (" << frames << " frames)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (pattern) SDL_DestroyTexture(pattern);
    capture.cleanup();
    if (renderer && !vulkan) SDL_DestroyRenderer(renderer);
    vulkan.reset();
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
