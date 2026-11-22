#pragma once
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <stdexcept>
#include <string>

namespace save_storage
{
namespace fs = std::filesystem;
using Metadata = std::map<std::string, std::string>;
inline std::string folder_name(std::string name, std::size_t limit = 160)
{
    for (char& c : name)
    {
        if (static_cast<unsigned char>(c) < 32 || std::string("/\\:*?\"<>|").find(c) != std::string::npos)
        {
            c = '_';
        }
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' '))
    {
        name.pop_back();
    }
    while (!name.empty() && name.front() == ' ')
    {
        name.erase(0, 1);
    }
    if (name.empty() || name == "." || name == "..")
    {
        name = "Game";
    }
    // Leave space for collision suffixes and native save-file extensions.
    if (name.size() > limit)
    {
        std::size_t end = limit;
        while (end && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
        {
            --end;
        }
        name.resize(end);
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' '))
    {
        name.pop_back();
    }
    if (name.empty())
    {
        name = "Game";
    }
    return name;
}
inline Metadata read(const fs::path& path)
{
    Metadata result;
    std::ifstream in(path);
    std::string key, value;
    while (in >> key >> std::quoted(value))
    {
        result[key] = value;
    }
    return result;
}
// Cabinet replaces its files instead of modifying them in place, so a hard
// link can retain the complete old file without copying large save states.
inline void backup(const fs::path& path)
{
    if (!fs::is_regular_file(path))
    {
        return;
    }
    const fs::path destination = path.string() + ".bak";
    const fs::path temporary = destination.string() + ".tmp";
    try
    {
        fs::remove(temporary);
        std::error_code error;
        fs::create_hard_link(path, temporary, error);
        if (error)
        {
            fs::copy_file(path, temporary);
        }
        fs::rename(temporary, destination);
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
inline void commit_with_backup(const fs::path& temporary, const fs::path& path)
{
    backup(path);
    fs::rename(temporary, path);
}
inline void write(const fs::path& path, const Metadata& values, bool retain_backup = false)
{
    fs::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary);
    for (const auto& [key, value] : values)
    {
        out << key << ' ' << std::quoted(value) << '\n';
    }
    out.close();
    if (!out)
    {
        throw std::runtime_error("Cannot write save metadata: " + path.string());
    }
    if (retain_backup)
    {
        commit_with_backup(temporary, path);
    }
    else
    {
        fs::rename(temporary, path);
    }
}
inline void copy_file_if_missing(const fs::path& from, const fs::path& to)
{
    if (from == to || !fs::is_regular_file(from) || fs::exists(to))
    {
        return;
    }
    fs::create_directories(to.parent_path());
    auto temporary = to;
    temporary += ".migrating";
    fs::copy_file(from, temporary, fs::copy_options::overwrite_existing);
    fs::rename(temporary, to);
}
inline void relocate_native_saves(const fs::path& directory, const std::string& old_stem, const std::string& new_stem,
                                  const std::string& core)
{
    if (old_stem.empty() || old_stem == new_stem || fs::path(old_stem).filename() != old_stem)
    {
        return;
    }
    // These cores can key native card files by content basename. Serial-based
    // files (PPSSPP, Dolphin, and Flycast where available) are left untouched.
    if (core == "flycast_libretro")
    {
        for (const auto* slot :
             {".A1.bin", ".A2.bin", ".B1.bin", ".B2.bin", ".C1.bin", ".C2.bin", ".D1.bin", ".D2.bin"})
        {
            copy_file_if_missing(directory / (old_stem + slot), directory / (new_stem + slot));
        }
    }
    else if (core == "swanstation_libretro")
    {
        for (const auto* slot : {"_1.mcd", "_2.mcd"})
        {
            copy_file_if_missing(directory / (old_stem + slot), directory / (new_stem + slot));
        }
    }
}
}
