#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <stdexcept>
#include <string>

inline std::filesystem::path application_data(const std::string& override_path = {})
{
    if (!override_path.empty())
    {
        return std::filesystem::absolute(override_path);
    }
    const char* home = SDL_getenv("HOME");
    if (!home || !*home)
    {
        throw std::runtime_error("HOME is not set; supply --data-dir");
    }
    return std::filesystem::path(home) / ".local/share/cabinet";
}

inline std::filesystem::path sibling_runner()
{
    const char* base = SDL_GetBasePath();
    if (!base)
    {
        throw std::runtime_error(SDL_GetError());
    }
#ifdef _WIN32
    return std::filesystem::path(base) / "cabinet-runner.exe";
#else
    return std::filesystem::path(base) / "cabinet-runner";
#endif
}

inline std::filesystem::path application_config(const std::string& data_override = {})
{
    if (!data_override.empty() &&
        std::filesystem::absolute(data_override).lexically_normal() != application_data().lexically_normal())
    {
        return std::filesystem::absolute(data_override) / "config";
    }
    const char* home = SDL_getenv("HOME");
    if (!home || !*home)
    {
        throw std::runtime_error("HOME is not set; supply --data-dir");
    }
    return std::filesystem::path(home) / ".config/cabinet";
}
