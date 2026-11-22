#include "libretro_core.h"
#include <stdexcept>
#include <string>

template <class T> static void symbol(SDL_SharedObject* library, T& target, const char* name)
{
    target = reinterpret_cast<T>(SDL_LoadFunction(library, name));
    if (!target)
    {
        throw std::runtime_error(std::string("Missing core entry point: ") + name);
    }
}

LibretroCore::~LibretroCore()
{
    close();
}

void LibretroCore::open(const char* path)
{
    if (library_)
    {
        throw std::runtime_error("Core library is already open");
    }
    auto* library = SDL_LoadObject(path);
    if (!library)
    {
        throw std::runtime_error(SDL_GetError());
    }
    LibretroApi entry_points;
    try
    {
#define LOAD(name) symbol(library, entry_points.name, "retro_" #name)
        LOAD(init);
        LOAD(deinit);
        LOAD(api_version);
        LOAD(set_environment);
        LOAD(set_video_refresh);
        LOAD(set_audio_sample);
        LOAD(set_audio_sample_batch);
        LOAD(set_input_poll);
        LOAD(set_input_state);
        LOAD(set_controller_port_device);
        LOAD(get_system_info);
        LOAD(get_system_av_info);
        LOAD(load_game);
        LOAD(unload_game);
        LOAD(run);
        LOAD(get_memory_data);
        LOAD(get_memory_size);
        LOAD(reset);
        LOAD(serialize_size);
        LOAD(serialize);
        LOAD(unserialize);
#undef LOAD
        if (entry_points.api_version() != RETRO_API_VERSION)
        {
            throw std::runtime_error("Unsupported libretro API version");
        }
    }
    catch (...)
    {
        SDL_UnloadObject(library);
        throw;
    }
    library_ = library;
    api = entry_points;
}

void LibretroCore::initialize()
{
    if (initialized_)
    {
        return;
    }
    api.init();
    initialized_ = true;
}

bool LibretroCore::load_game(const retro_game_info* game)
{
    loaded_ = api.load_game(game);
    return loaded_;
}

void LibretroCore::shutdown()
{
    if (loaded_)
    {
        api.unload_game();
        loaded_ = false;
    }
    if (initialized_)
    {
        api.deinit();
        initialized_ = false;
    }
}

void LibretroCore::close()
{
    shutdown();
    if (library_)
    {
        SDL_UnloadObject(library_);
    }
    library_ = nullptr;
    api = {};
}
