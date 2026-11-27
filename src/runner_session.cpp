#include "runner_session.h"
#include <iostream>
#include <mutex>

    Session::~Session()
    {
        async_audio.clear();
        frontend.stop_rumble();
        if (!saves.metadata.empty())
        {
            saves.playtime.update(SDL_GetTicks(), false);
            try
            {
                saves.write_playtime();
            }
            catch (const std::exception& error)
            {
                std::cerr << "Cabinet: " << error.what() << '\n';
            }
        }
        SDL_ShowCursor();
        {
            std::unique_lock<VulkanVideo> lock;
            if (vulkan)
            {
                lock = std::unique_lock<VulkanVideo>(*vulkan);
            }
            bottom_presentation.reset();
        }
        secondary.reset();
#ifdef CABINET_ARMADA_DRM
        if (dual_screen && leased_bottom)
        {
            std::unique_lock<VulkanVideo> lock;
            if (vulkan)
            {
                lock = std::unique_lock<VulkanVideo>(*vulkan);
            }
            leased_bottom.reset();
        }
#endif
        {
            std::unique_lock<VulkanVideo> lock;
            if (vulkan)
            {
                lock = std::unique_lock<VulkanVideo>(*vulkan);
            }
            presentation.reset();
        }
        if (texture && !(vulkan && texture_is_hardware))
        {
            SDL_DestroyTexture(texture);
        }
        texture = nullptr;
        if (vulkan)
        {
            try
            {
                vulkan->idle();
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << '\n';
            }
        }
        if (hardware)
        {
            try
            {
                hardware->make_current();
            }
            catch (const std::exception& error)
            {
                std::cerr << error.what() << '\n';
            }
        }
        if (core.loaded() && saves.battery_ready)
        {
            try
            {
                saves.battery(save_core(), false);
            }
            catch (const std::exception& error)
            {
                std::cerr << "Cabinet: " << error.what() << '\n';
            }
        }
        // The core may delete its graphics callback owner in unload_game.
        if (hw_ready && hw.context_destroy)
        {
            hw.context_destroy();
        }
        core.shutdown();
        // Cores can release Vulkan resources during unload/deinit as well as
        // context_destroy. Keep their frontend-owned device alive until then.
        if (vulkan)
        {
            vulkan.reset();
            renderer = nullptr;
        }
        hardware.reset();
        audio.close();
        if (texture)
        {
            SDL_DestroyTexture(texture);
        }
        if (renderer)
        {
            SDL_DestroyRenderer(renderer);
        }
        if (window)
        {
            SDL_DestroyWindow(window);
        }
        core.close();
    }
