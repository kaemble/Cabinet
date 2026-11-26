#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace firmware_fixture
{
constexpr std::size_t romdir = 0x100, extinfo = 0x150, romver = 0x170;

inline std::vector<unsigned char> ps2(std::string_view version = "0220AC20050620", std::size_t size = 4 * 1024 * 1024)
{
    // Synthetic ROM directory metadata, without console firmware code.
    std::vector<unsigned char> bytes(size);
    const auto entry = [&](unsigned index, std::string_view name, std::uint32_t length)
    {
        const auto offset = romdir + index * 16;
        std::copy(name.begin(), name.end(), bytes.begin() + offset);
        for (unsigned i = 0; i < 4; ++i)
        {
            bytes[offset + 12 + i] = static_cast<unsigned char>(length >> (i * 8));
        }
    };
    entry(0, "RESET", romdir);
    entry(1, "ROMDIR", 80);
    entry(2, "EXTINFO", 32);
    entry(3, "ROMVER", 14);
    std::copy(version.begin(), version.end(), bytes.begin() + romver);
    return bytes;
}

inline void write(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
}
