#include "vulkan_video.h"
#include "secondary_window.h"
#include <SDL3/SDL_vulkan.h>
#include <iostream>
#include <stdexcept>
#ifdef CABINET_TEST_QUEUE_WAIT_PROBE
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

static std::atomic<bool> ProbeArmed{false}, FenceWaitEntered{false}, QueueAcquired{false};
static bool QueueAvailableDuringWait = false;

extern "C" VKAPI_ATTR VkResult VKAPI_CALL __real_vkWaitForFences(
    VkDevice Device, uint32_t Count, const VkFence* Fences, VkBool32 WaitAll, uint64_t Timeout);

extern "C" VKAPI_ATTR VkResult VKAPI_CALL __wrap_vkWaitForFences(
    VkDevice Device, uint32_t Count, const VkFence* Fences, VkBool32 WaitAll, uint64_t Timeout)
{
    if (ProbeArmed.exchange(false))
    {
        // Pause at the wait boundary so a worker must acquire the queue while
        // the frontend is inside the wait, even when the GPU finishes quickly.
        FenceWaitEntered = true;
        const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!QueueAcquired && std::chrono::steady_clock::now() < Deadline)
            std::this_thread::yield();
        QueueAvailableDuringWait = QueueAcquired;
    }
    return __real_vkWaitForFences(Device, Count, Fences, WaitAll, Timeout);
}
#endif

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void vk_ok(VkResult r) { require(r == VK_SUCCESS, "Vulkan test operation failed"); }
#ifdef CABINET_TEST_QUEUE_WAIT_PROBE
static void check_queue_during_wait(const retro_hw_render_interface_vulkan& Api, const std::function<void()>& Wait)
{
    ProbeArmed = true;
    FenceWaitEntered = false;
    QueueAcquired = false;
    QueueAvailableDuringWait = false;
    std::atomic<bool> Finished{false};
    std::thread Worker([&]
    {
        while (!FenceWaitEntered && !Finished) std::this_thread::yield();
        if (Finished) return;
        Api.lock_queue(Api.handle);
        QueueAcquired = true;
        Api.unlock_queue(Api.handle);
    });
    try
    {
        Wait();
    }
    catch (...)
    {
        Finished = true;
        Worker.join();
        ProbeArmed = false;
        throw;
    }
    Finished = true;
    Worker.join();
    ProbeArmed = false;
    require(FenceWaitEntered && QueueAvailableDuringWait,
        "Frontend frame fence wait blocked a core worker's queue access");
}
#endif
int main(int argc, char**) {
    SDL_Window* window = nullptr;
    static VkSurfaceKHR native_surface = VK_NULL_HANDLE;
    try {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        window = SDL_CreateWindow("Vulkan bridge test", 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
            (argc > 1 ? SDL_WINDOW_FULLSCREEN : 0));
        require(window, SDL_GetError());
        {
            VulkanVideo video;
            retro_hw_render_context_negotiation_interface_vulkan negotiation{};
            negotiation.interface_type = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN;
            negotiation.interface_version = 1;
            negotiation.create_device = [](retro_vulkan_context*, VkInstance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
                PFN_vkGetInstanceProcAddr, const char**, unsigned, const char**, unsigned,
                const VkPhysicalDeviceFeatures* features) {
                require(features != nullptr, "Vulkan negotiation omitted required features");
                require(gpu != VK_NULL_HANDLE, "Vulkan negotiation omitted the selected GPU");
                native_surface = surface;
                return false; // Exercise the frontend's fallback device creation too.
            };
            video.negotiate(&negotiation);
            auto renderer = video.create_renderer(window, argc > 1);
            SecondaryWindow bottom;
            bottom.create(window, renderer, nullptr, &video, "Vulkan screens", 320, 240);
            require(video.frame(32, 32) == nullptr, "Startup without an exported image was not deferred");
            auto& api = *reinterpret_cast<const retro_hw_render_interface_vulkan*>(video.interface());
            if (argc > 1) {
                int width = 0, height = 0;
                require(SDL_GetWindowSizeInPixels(window, &width, &height), SDL_GetError());
                const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
                require(mode && width == mode->w && height == mode->h,
                    "Vulkan core would initialize before fullscreen pixel size was applied");
                VkSurfaceCapabilitiesKHR caps{};
                vk_ok(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(api.gpu, native_surface, &caps));
                require(caps.currentExtent.width == UINT32_MAX ||
                    (caps.currentExtent.width == static_cast<unsigned>(width) && caps.currentExtent.height == static_cast<unsigned>(height)),
                    "Vulkan core would inherit an undersized native surface");
            }
            require(api.interface_version == 5 && api.get_sync_index_mask(api.handle) == 7, "Invalid Vulkan interface");
            VkCommandPool pool{};
            VkCommandPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pi.queueFamilyIndex = api.queue_index;
            pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            vk_ok(vkCreateCommandPool(api.device, &pi, nullptr, &pool));
            VkCommandBuffer cmd{};
            VkCommandBufferAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
            vk_ok(vkAllocateCommandBuffers(api.device, &ai, &cmd));
            for (unsigned frame_index = 0; frame_index < 8; ++frame_index) {
                const unsigned size = frame_index < 4 ? 32 : 64;
                video.begin_frame();
                require(api.get_sync_index(api.handle) == frame_index % 3, "Vulkan synchronization slots did not rotate");
                VkImage image{}; VkDeviceMemory memory{};
                VkImageCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; ci.imageType = VK_IMAGE_TYPE_2D; ci.format = VK_FORMAT_R8G8B8A8_UNORM;
                ci.extent = {size, size, 1}; ci.mipLevels = ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
                // Exported images need sampling support, not transfer-source usage (Flycast).
                ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                vk_ok(vkCreateImage(api.device, &ci, nullptr, &image));
                VkMemoryRequirements req{}; vkGetImageMemoryRequirements(api.device, image, &req);
                VkPhysicalDeviceMemoryProperties props{}; vkGetPhysicalDeviceMemoryProperties(api.gpu, &props);
                uint32_t type = 0; while (!(req.memoryTypeBits & (1u << type))) ++type;
                VkMemoryAllocateInfo mi{}; mi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; mi.allocationSize = req.size; mi.memoryTypeIndex = type;
                vk_ok(vkAllocateMemory(api.device, &mi, nullptr, &memory)); vk_ok(vkBindImageMemory(api.device, image, memory, 0));
                vk_ok(vkResetCommandPool(api.device, pool, 0));
                VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                vk_ok(vkBeginCommandBuffer(cmd, &bi));
                VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; barrier.image = image;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                VkClearColorValue red{{1, 0, 0, 1}};
                vkCmdClearColorImage(cmd, image, barrier.newLayout, &red, 1, &barrier.subresourceRange);
                barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL; barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                vk_ok(vkEndCommandBuffer(cmd));
                retro_vulkan_image frame{}; frame.image_layout = VK_IMAGE_LAYOUT_GENERAL;
                frame.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; frame.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                frame.create_info.image = image; frame.create_info.format = ci.format; frame.create_info.subresourceRange = barrier.subresourceRange;
                VkSemaphore ready{}, released{};
                VkSemaphoreCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                vk_ok(vkCreateSemaphore(api.device, &si, nullptr, &released));
                if (size == 64) {
                    vk_ok(vkCreateSemaphore(api.device, &si, nullptr, &ready));
                    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
                    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &ready;
                    api.lock_queue(api.handle);
                    vk_ok(vkQueueSubmit(api.queue, 1, &submit, VK_NULL_HANDLE));
                    api.unlock_queue(api.handle);
                    api.set_image(api.handle, &frame, 1, &ready, api.queue_index);
                } else {
                    api.set_image(api.handle, &frame, 0, nullptr, api.queue_index);
                    api.set_command_buffers(api.handle, 1, &cmd);
                }
                api.set_signal_semaphore(api.handle, released);
                auto texture = video.frame(size, size);
                VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                VkSubmitInfo wait{}; wait.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                wait.waitSemaphoreCount = 1; wait.pWaitSemaphores = &released; wait.pWaitDstStageMask = &wait_stage;
                api.lock_queue(api.handle);
                vk_ok(vkQueueSubmit(api.queue, 1, &wait, VK_NULL_HANDLE));
                vk_ok(vkQueueWaitIdle(api.queue));
                api.unlock_queue(api.handle);
                vkDestroySemaphore(api.device, released, nullptr);
                if (ready) vkDestroySemaphore(api.device, ready, nullptr);
                if (frame_index == 7) {
                    // Keep several frames in flight without a queue/device
                    // idle between copies and SDL sampling the shared snapshot.
                    VkCommandBuffer producers[3]{};
                    auto producer_alloc = ai; producer_alloc.commandBufferCount = 3;
                    vk_ok(vkAllocateCommandBuffers(api.device, &producer_alloc, producers));
                    for (unsigned n = 0; n < 48; ++n) {
                        video.begin_frame();
                        api.wait_sync_index(api.handle);
                        auto producer = producers[api.get_sync_index(api.handle)];
                        vk_ok(vkResetCommandBuffer(producer, 0));
                        vk_ok(vkBeginCommandBuffer(producer, &bi));
                        auto before = barrier;
                        before.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                        before.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        before.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                        before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        vkCmdPipelineBarrier(producer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &before);
                        VkClearColorValue color{{n % 2 ? 0.f : 1.f, 0, n % 2 ? 1.f : 0.f, 1}};
                        vkCmdClearColorImage(producer, image, before.newLayout, &color, 1, &before.subresourceRange);
                        before.oldLayout = before.newLayout; before.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                        before.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                        vkCmdPipelineBarrier(producer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &before);
                        vk_ok(vkEndCommandBuffer(producer));
                        api.set_image(api.handle, &frame, 0, nullptr, api.queue_index);
                        api.set_command_buffers(api.handle, 1, &producer);
                        texture = video.frame(size, size);
                        require(SDL_RenderClear(renderer), SDL_GetError());
                        require(SDL_RenderTexture(renderer, texture, nullptr, nullptr), SDL_GetError());
                        if (n == 47) {
                            auto pixels = SDL_RenderReadPixels(renderer, nullptr); require(pixels, SDL_GetError());
                            Uint8 r, g, b, a;
                            require(SDL_ReadSurfacePixel(pixels, 100, 100, &r, &g, &b, &a), SDL_GetError());
                            require(r < 5 && g < 5 && b > 250, "Asynchronous frame reuse corrupted snapshot pixels");
                            SDL_DestroySurface(pixels);
                        }
                        require(SDL_RenderPresent(renderer), SDL_GetError());
                        api.lock_queue(api.handle);
                        require(SDL_RenderTexture(bottom.renderer, bottom.texture(texture), nullptr, nullptr), SDL_GetError());
                        if (n == 47) {
                            auto pixels = SDL_RenderReadPixels(bottom.renderer, nullptr); require(pixels, SDL_GetError());
                            Uint8 r, g, b, a;
                            require(SDL_ReadSurfacePixel(pixels, 100, 100, &r, &g, &b, &a), SDL_GetError());
                            require(r < 5 && g < 5 && b > 250, "Shared Vulkan second window corrupted an asynchronous frame");
                            SDL_DestroySurface(pixels);
                        }
                        require(SDL_RenderPresent(bottom.renderer), SDL_GetError());
                        api.unlock_queue(api.handle);
                    }
                    video.idle();
                    // Restore red for the original snapshot/lifetime checks.
                    video.begin_frame();
                    vk_ok(vkResetCommandBuffer(cmd, 0));
                    vk_ok(vkBeginCommandBuffer(cmd, &bi));
                    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                    vkCmdClearColorImage(cmd, image, barrier.newLayout, &red, 1, &barrier.subresourceRange);
                    barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                    vk_ok(vkEndCommandBuffer(cmd));
                    api.set_image(api.handle, &frame, 0, nullptr, api.queue_index);
                    api.set_command_buffers(api.handle, 1, &cmd);
                    texture = video.frame(size, size);
                }
                // The snapshot must remain valid after the core destroys its source image.
                api.wait_sync_index(api.handle);
                vkDestroyImage(api.device, image, nullptr); vkFreeMemory(api.device, memory, nullptr);
                // A duplicate must still submit deferred commands and signal
                // release, without touching the destroyed source image.
                vk_ok(vkResetCommandBuffer(cmd, 0));
                vk_ok(vkBeginCommandBuffer(cmd, &bi));
                vk_ok(vkEndCommandBuffer(cmd));
                vk_ok(vkCreateSemaphore(api.device, &si, nullptr, &released));
                api.set_command_buffers(api.handle, 1, &cmd);
                api.set_signal_semaphore(api.handle, released);
                require(texture == video.frame(0, 0), "Duplicate lost snapshot");
                api.lock_queue(api.handle);
                vk_ok(vkQueueSubmit(api.queue, 1, &wait, VK_NULL_HANDLE));
                api.unlock_queue(api.handle);
                video.idle();
                vkDestroySemaphore(api.device, released, nullptr);
                require(texture == video.frame(size, size), "Duplicate lost snapshot");
                require(SDL_RenderTexture(renderer, texture, nullptr, nullptr), SDL_GetError());
                auto pixels = SDL_RenderReadPixels(renderer, nullptr); require(pixels, SDL_GetError());
                Uint8 r, g, b, a;
                require(SDL_ReadSurfacePixel(pixels, 100, 100, &r, &g, &b, &a), SDL_GetError());
                require(r > 250 && g < 5 && b < 5, "Vulkan frame pixels corrupted");
                SDL_DestroySurface(pixels);
                api.lock_queue(api.handle);
                require(SDL_RenderTexture(bottom.renderer, bottom.texture(texture), nullptr, nullptr), SDL_GetError());
                pixels = SDL_RenderReadPixels(bottom.renderer, nullptr); require(pixels, SDL_GetError());
                require(SDL_ReadSurfacePixel(pixels, 100, 100, &r, &g, &b, &a), SDL_GetError());
                require(r > 250 && g < 5 && b < 5, "Shared Vulkan second window lost its image after resize");
                SDL_DestroySurface(pixels);
                require(SDL_RenderPresent(bottom.renderer), SDL_GetError());
                api.unlock_queue(api.handle);
                require(SDL_RenderPresent(renderer), SDL_GetError());
                video.idle();
                require(SDL_SetWindowSize(window, 720, 540), SDL_GetError());
                SDL_PumpEvents();
                SDL_FRect menu{0, 0, 100, 30};
                require(SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255), SDL_GetError());
                require(SDL_RenderFillRect(renderer, &menu), SDL_GetError());
                require(SDL_RenderPresent(renderer), SDL_GetError());
                video.idle();
            }
#ifdef CABINET_TEST_QUEUE_WAIT_PROBE
            // Exercise all frame-resource wait entry points. An empty core
            // command buffer gives the bridge a pending fence without needing
            // additional images or changing its snapshot lifetime guarantees.
            auto Submit = [&]
            {
                video.idle();
                vk_ok(vkResetCommandBuffer(cmd, 0));
                VkCommandBufferBeginInfo Begin{};
                Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                vk_ok(vkBeginCommandBuffer(cmd, &Begin));
                vk_ok(vkEndCommandBuffer(cmd));
                api.set_command_buffers(api.handle, 1, &cmd);
                video.frame(0, 0);
            };
            Submit();
            check_queue_during_wait(api, [&] { api.wait_sync_index(api.handle); });
            Submit();
            api.set_command_buffers(api.handle, 1, &cmd);
            check_queue_during_wait(api, [&] { video.frame(0, 0); });
            Submit();
            check_queue_during_wait(api, [&]
            {
                for (unsigned Index = 0; Index < 3; ++Index) video.begin_frame();
            });
            video.idle();
#endif
            vkDestroyCommandPool(api.device, pool, nullptr);
        }
        SDL_DestroyWindow(window); SDL_Quit();
        std::cout << "Vulkan frame copy, command submission, duplicate, resize and teardown passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
