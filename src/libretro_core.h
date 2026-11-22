#pragma once
#include <SDL3/SDL.h>
#include <libretro.h>

// Entry points remain valid until LibretroCore::close().
struct LibretroApi
{
#define CORE_FUNCTION(name) decltype(&retro_##name) name = nullptr
    CORE_FUNCTION(init);
    CORE_FUNCTION(deinit);
    CORE_FUNCTION(api_version);
    CORE_FUNCTION(set_environment);
    CORE_FUNCTION(set_video_refresh);
    CORE_FUNCTION(set_audio_sample);
    CORE_FUNCTION(set_audio_sample_batch);
    CORE_FUNCTION(set_input_poll);
    CORE_FUNCTION(set_input_state);
    CORE_FUNCTION(set_controller_port_device);
    CORE_FUNCTION(get_system_info);
    CORE_FUNCTION(get_system_av_info);
    CORE_FUNCTION(load_game);
    CORE_FUNCTION(unload_game);
    CORE_FUNCTION(run);
    CORE_FUNCTION(get_memory_data);
    CORE_FUNCTION(get_memory_size);
    CORE_FUNCTION(reset);
    CORE_FUNCTION(serialize_size);
    CORE_FUNCTION(serialize);
    CORE_FUNCTION(unserialize);
#undef CORE_FUNCTION
};

// The session must call shutdown() with the core graphics context/device still
// alive, after its context_destroy callback. close() then releases the library.
class LibretroCore
{
public:
    LibretroCore() = default;
    ~LibretroCore();
    LibretroCore(const LibretroCore&) = delete;
    LibretroCore& operator=(const LibretroCore&) = delete;

    LibretroApi api;
    void open(const char* path);
    void initialize();
    bool load_game(const retro_game_info* game);
    bool loaded() const
    {
        return loaded_;
    }
    void shutdown();
    void close();

private:
    SDL_SharedObject* library_ = nullptr;
    bool initialized_ = false, loaded_ = false;
};
