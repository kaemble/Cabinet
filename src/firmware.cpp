#include "firmware.h"
#include <openssl/evp.h>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <string_view>
namespace fs = std::filesystem;

namespace
{
std::string ps2_region(const std::vector<unsigned char>& bytes)
{
    const auto name = [&](std::size_t entry)
    {
        const std::string_view value(reinterpret_cast<const char*>(bytes.data() + entry), 10);
        return value.substr(0, value.find('\0'));
    };
    const auto size = [&](std::size_t entry)
    {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            value |= std::uint32_t(bytes[entry + 12 + i]) << (i * 8);
        }
        return value;
    };
    // Both cores identify PS2 dumps through the aligned ROMDIR and its ROMVER module.
    for (std::size_t table = 16; table + 32 <= bytes.size(); table += 16)
    {
        if (name(table) != "RESET" || ((std::uint64_t(size(table)) + 15) & ~std::uint64_t(15)) != table)
        {
            continue;
        }
        std::size_t offset = 0, end = bytes.size();
        bool directory = false;
        std::string region;
        for (std::size_t entry = table; entry + 16 <= end; entry += 16)
        {
            const auto module = name(entry);
            if (module.empty())
            {
                return directory ? region : std::string{};
            }
            const auto length = size(entry);
            const auto aligned = (std::uint64_t(length) + 15) & ~std::uint64_t(15);
            if (module.size() == 10 || aligned > bytes.size() - offset)
            {
                return {};
            }
            if (module == "ROMDIR")
            {
                if (offset != table || length < 32 || length % 16)
                {
                    return {};
                }
                end = table + length;
                directory = true;
            }
            // PS1 compatibility ROMs can carry this PS2-style directory without being a PS2 BIOS.
            if (module == "EXTINFO" && length >= 31 &&
                std::string_view(reinterpret_cast<const char*>(bytes.data() + offset + 16), 15) == "20111012-094243")
            {
                return {};
            }
            if (module == "ROMVER")
            {
                if (length < 14)
                {
                    return {};
                }
                const auto* version = bytes.data() + offset;
                const auto decimal = [&](unsigned start, unsigned count)
                {
                    int value = 0;
                    for (unsigned i = start; i < start + count; ++i)
                    {
                        if (version[i] < '0' || version[i] > '9')
                        {
                            return -1;
                        }
                        value = value * 10 + version[i] - '0';
                    }
                    return value;
                };
                const auto year = decimal(6, 4), month = decimal(10, 2), day = decimal(12, 2);
                if (decimal(0, 4) <= 0 || year < 1990 || month < 1 || day < 1 ||
                    !std::chrono::year_month_day(std::chrono::year(year), std::chrono::month(month),
                                                 std::chrono::day(day)).ok() ||
                    (version[5] != 'C' && version[5] != 'D'))
                {
                    return {};
                }
                switch (version[4])
                {
                case 'J': region = "Japan"; break;
                case 'A': region = "North America"; break;
                case 'E': region = "Europe"; break;
                case 'H': region = "Asia"; break;
                case 'C': region = "China"; break;
                case 'T': region = "Development"; break;
                case 'X': region = "Test"; break;
                case 'P': region = "Region-free"; break;
                default: return {};
                }
            }
            offset += static_cast<std::size_t>(aligned);
        }
        return {};
    }
    return {};
}
}

bool requires_bios(const std::string& system)
{
    return system == "psx" || system == "ps2" || system == "saturn";
}
const std::vector<BiosInfo>& supported_bios()
{
    // PS1 hashes: SwanStation src/core/bios.cpp. Saturn: Libretro core documentation.
    static const std::vector<BiosInfo> bios{
        {"psx", "8dd7d5296a650fac7319bce665a6a53c", "scph5500.bin", "Japan"},
        {"psx", "490f666e1afb15b7362b406ed1cea246", "scph5501.bin", "North America"},
        {"psx", "32736f17079d0b2b7024407c39bd3050", "scph5502.bin", "Europe"},
        {"psx", "924e392ed05558ffdb115408c263dccf", "scph5501.bin", "North America"},
        {"psx", "239665b1a3dade1b5a52c06338011044", "scph5500.bin", "Japan"},
        {"psx", "54847e693405ffeb0359c6287434cbef", "scph5502.bin", "Europe"},
        {"psx", "417b34706319da7cf001e76e40136c23", "scph5502.bin", "Europe"},
        {"psx", "e2110b8a2b97a8e0b857a45d32f7e187", "scph5502.bin", "Europe"},
        {"psx", "ca5cfc321f916756e3f0effbfaeba13b", "scph5500.bin", "Japan"},
        {"psx", "849515939161e62f6b866f6853006780", "scph5500.bin", "Japan"},
        {"psx", "dc2b9bf8da62ec93e868cfd29f0d067d", "scph5501.bin", "North America"},
        {"psx", "cba733ceeff5aef5c32254f1d617fa62", "scph5500.bin", "Japan"},
        {"psx", "da27e8b6dab242d8f91a9b25d80c63b8", "scph5501.bin", "North America"},
        {"psx", "57a06303dfa9cf9351222dfcbb4a29d9", "scph5500.bin", "Japan"},
        {"psx", "8e4c14f567745eff2f0408c8129f72a6", "scph5500.bin", "Japan"},
        {"psx", "b84be139db3ee6cbd075630aa20a6553", "scph5500.bin", "Japan"},
        {"psx", "1e68c231d0896b7eadcad1d7d8e76129", "scph5501.bin", "North America"},
        {"psx", "b9d9a0286c33dc6b7237bb13cd46fdee", "scph5502.bin", "Europe"},
        {"psx", "8abc1b549a4a80954addc48ef02c4521", "scph5500.bin", "Japan"},
        {"psx", "9a09ab7e49b422c007e6d54d7c49b965", "scph5501.bin", "North America"},
        {"psx", "6e3735ff4c7dc899ee98981385f6f3d0", "scph5501.bin", "North America"},
        {"psx", "b10f5e0e3d9eb60e5159690680b1e774", "scph5502.bin", "Europe"},
        {"psx", "de93caec13d1a141a40a79f5c86168d6", "scph5502.bin", "Europe"},
        {"psx", "476d68a94ccec3b9c8303bbd1daf2810", "scph5500.bin", "Japan"},
        {"psx", "c53ca5908936d412331790f4426c6c33", "scph5501.bin", "Region-free"},
        {"saturn", "85ec9ca47d8f6807718151cbcca8b964", "sega_101.bin", "Japan"},
        {"saturn", "3240872c70984b6cbfda1586cab68dbe", "mpr-17933.bin", "North America / Europe"}};
    return bios;
}
std::optional<BiosInfo> identify_bios(const std::string& system, const fs::path& file)
{
    if (!fs::is_regular_file(file))
    {
        return std::nullopt;
    }
    const auto size = fs::file_size(file);
    if ((system == "ps2") ? (size < 4 * 1024 * 1024 || size > 8 * 1024 * 1024) : size != 512 * 1024)
    {
        return std::nullopt;
    }
    std::vector<unsigned char> bytes(size);
    std::ifstream input(file, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
    {
        return std::nullopt;
    }
    const auto region = system == "ps2" ? ps2_region(bytes) : std::string{};
    if (system == "ps2" && region.empty())
    {
        return std::nullopt;
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (!EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_md5(), nullptr))
    {
        throw std::runtime_error("Cannot verify BIOS checksum");
    }
    std::ostringstream hash;
    for (unsigned i = 0; i < length; ++i)
    {
        hash << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(digest[i]);
    }
    if (system == "ps2")
    {
        return BiosInfo{system, hash.str(), "pcsx2/bios/" + hash.str() + ".bin", region};
    }
    for (const auto& bios : supported_bios())
    {
        if (bios.system == system && bios.digest == hash.str())
        {
            return bios;
        }
    }
    return std::nullopt;
}
bool has_bios(const std::string& system, const fs::path& directory)
{
    if (system == "ps2")
    {
        const auto folder = directory / "pcsx2/bios";
        if (fs::is_directory(folder))
        {
            for (const auto& entry : fs::directory_iterator(folder))
            {
                if (identify_bios(system, entry.path()))
                {
                    return true;
                }
            }
        }
        return false;
    }
    for (const auto& bios : supported_bios())
    {
        if (bios.system == system)
        {
            const auto installed = identify_bios(system, directory / bios.filename);
            if (installed && (system == "psx" || installed->filename == bios.filename))
            {
                return true;
            }
        }
    }
    return false;
}
std::string install_bios(const std::string& system, const fs::path& file, const fs::path& directory)
{
    const auto bios = identify_bios(system, file);
    if (!bios)
    {
        throw std::runtime_error(system == "ps2" ? "Unrecognized BIOS. Select a complete PS2 BIOS dump (4–8 MiB)."
                                               : "Unrecognized BIOS. Select an unmodified 512 KiB BIOS for this system.");
    }
    const auto destination = directory / bios->filename;
    fs::create_directories(destination.parent_path());
    const auto installed = identify_bios(system, destination);
    if (installed && (system != "ps2" || installed->digest == bios->digest))
    {
        return bios->region + " BIOS already installed";
    }
    const auto temporary = destination.string() + ".tmp";
    // Write the verified bytes again only after confirming the source is unchanged.
    fs::copy_file(file, temporary, fs::copy_options::overwrite_existing);
    const auto copied = identify_bios(system, temporary);
    if (!copied || copied->digest != bios->digest)
    {
        fs::remove(temporary);
        throw std::runtime_error("BIOS file changed while copying");
    }
    fs::rename(temporary, destination);
    if (system == "psx" && bios->region == "Region-free")
    {
        for (const char* name : {"scph5500.bin", "scph5502.bin"})
        {
            if (!identify_bios(system, directory / name))
            {
                fs::copy_file(destination, directory / name, fs::copy_options::overwrite_existing);
            }
        }
    }
    return bios->region + " BIOS installed";
}
