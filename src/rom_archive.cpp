#include "rom_archive.h"
#include <cstdint>
#include "library.h"
#include "save_storage.h"
#include <SDL3/SDL.h>
#include <libretro.h>
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cctype>
namespace fs = std::filesystem;
namespace rom_archive
{
namespace
{
constexpr std::int64_t limit = 512ll * 1024 * 1024;
std::string lower(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}
bool safe(const std::string& name)
{
    if (name.empty() || name.find('\\') != std::string::npos || name.find(':') != std::string::npos ||
        fs::path(name).is_absolute())
    {
        return false;
    }
    for (const auto& part : fs::path(name))
    {
        if (part == ".." || part == ".")
        {
            return false;
        }
    }
    return true;
}
struct Reader
{
    archive* handle = archive_read_new();
    explicit Reader(const fs::path& file)
    {
        archive_read_support_format_zip(handle);
        if (archive_read_open_filename(handle, file.c_str(), 65536) != ARCHIVE_OK)
        {
            std::string error = archive_error_string(handle) ? archive_error_string(handle) : "Cannot open ZIP";
            archive_read_free(handle);
            handle = nullptr;
            throw std::runtime_error(error);
        }
    }
    ~Reader()
    {
        if (handle)
        {
            archive_read_free(handle);
        }
    }
};
std::string sha256(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("Cannot read core file");
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || !EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr))
    {
        throw std::runtime_error("Cannot hash core");
    }
    std::array<char, 65536> bytes;
    while (in)
    {
        in.read(bytes.data(), bytes.size());
        if (!EVP_DigestUpdate(ctx.get(), bytes.data(), in.gcount()))
        {
            throw std::runtime_error("Cannot hash core");
        }
    }
    if (!in.eof())
    {
        throw std::runtime_error("Cannot read core file");
    }
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (!EVP_DigestFinal_ex(ctx.get(), hash, &length))
    {
        throw std::runtime_error("Cannot hash core");
    }
    std::ostringstream result;
    for (unsigned i = 0; i < length; ++i)
    {
        result << std::hex << std::setw(2) << std::setfill('0') << unsigned(hash[i]);
    }
    return result.str();
}
}
bool is_zip(const fs::path& path)
{
    return lower(path.extension().string()) == ".zip";
}
std::vector<std::string> members(const fs::path& path)
{
    Reader reader(path);
    archive_entry* entry = nullptr;
    std::vector<std::string> result;
    std::set<std::string> names;
    int status, count = 0;
    while ((status = archive_read_next_header(reader.handle, &entry)) == ARCHIVE_OK)
    {
        if (++count > 10000)
        {
            throw std::runtime_error("ZIP has too many entries");
        }
        const char* raw = archive_entry_pathname(entry);
        std::string name = raw ? raw : "";
        if (archive_entry_filetype(entry) != AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry))
        {
            continue;
        }
        if (!safe(name))
        {
            throw std::runtime_error("ZIP contains an unsafe filename");
        }
        if (!names.insert(name).second)
        {
            throw std::runtime_error("ZIP contains duplicate filenames");
        }
        if (identify_systems(name).empty() || lower(fs::path(name).filename().string()).starts_with("._") ||
            name.starts_with("__MACOSX/"))
        {
            continue;
        }
        if (archive_entry_is_encrypted(entry) || archive_entry_size(entry) <= 0 || archive_entry_size(entry) > limit)
        {
            throw std::runtime_error("ZIP ROM is encrypted, empty, or larger than 512 MiB");
        }
        result.push_back(name);
    }
    if (status != ARCHIVE_EOF)
    {
        throw std::runtime_error("Cannot read ZIP directory");
    }
    std::sort(result.begin(), result.end());
    return result;
}
fs::path extract(const fs::path& path, const std::string& member)
{
    if (!safe(member))
    {
        throw std::runtime_error("Unsafe ZIP member");
    }
    const auto entries = members(path);
    if (std::find(entries.begin(), entries.end(), member) == entries.end())
    {
        throw std::runtime_error("Selected ROM is missing from ZIP. Reimport the archive to select another member.");
    }
    Reader reader(path);
    archive_entry* entry = nullptr;
    while (archive_read_next_header(reader.handle, &entry) == ARCHIVE_OK)
    {
        if (member != archive_entry_pathname(entry))
        {
            continue;
        }
        std::string pattern = (fs::temp_directory_path() / "cabinet-rom-XXXXXX").string();
        char* directory = mkdtemp(pattern.data());
        if (!directory)
        {
            throw std::runtime_error("Cannot create temporary ROM directory");
        }
        const fs::path dir(directory);
        try
        {
            std::ofstream out(dir / fs::path(member).filename(), std::ios::binary);
            std::array<char, 65536> buffer;
            std::int64_t total = 0;
            la_ssize_t n;
            while ((n = archive_read_data(reader.handle, buffer.data(), buffer.size())) > 0)
            {
                total += n;
                if (total > limit)
                {
                    throw std::runtime_error("ZIP ROM exceeds 512 MiB");
                }
                out.write(buffer.data(), n);
                if (!out)
                {
                    throw std::runtime_error("Cannot write extracted ROM");
                }
            }
            if (n < 0 || total != archive_entry_size(entry))
            {
                throw std::runtime_error("ZIP ROM is corrupt or incomplete");
            }
            out.close();
            if (!out)
            {
                throw std::runtime_error("Cannot finish extracting ROM");
            }
            return dir;
        }
        catch (...)
        {
            fs::remove_all(dir);
            throw;
        }
    }
    throw std::runtime_error("Selected ROM is missing from ZIP");
}
int probe_core(const char* path)
{
    auto* library = SDL_LoadObject(path);
    if (!library)
    {
        return 2;
    }
    const auto get_info =
        reinterpret_cast<void (*)(retro_system_info*)>(SDL_LoadFunction(library, "retro_get_system_info"));
    if (!get_info)
    {
        SDL_UnloadObject(library);
        return 2;
    }
    retro_system_info info{};
    get_info(&info);
    const int result = info.need_fullpath ? 11 : 10;
    SDL_UnloadObject(library);
    return result;
}
void validate_core(const fs::path& core, const fs::path& cache, std::string_view system_id)
{
    bool bundled = false;
    for (const auto& system : systems())
    {
        if (!fs::is_regular_file(core) || !fs::is_regular_file(bundled_core(system)) ||
            !fs::equivalent(core, bundled_core(system)))
        {
            continue;
        }
        bundled = true;
        // The catalog lists only single-file cartridges for PicoDrive's systems.
        if (system.memory_content || (system.core == "picodrive" && system.id == system_id))
        {
            return;
        }
    }
    if (bundled)
    {
        throw std::runtime_error(
            "This core and system do not support ZIP imports. Extract the ROM before importing it.");
    }
    const auto record = cache / (sha256(core) + ".txt");
    const auto stored = save_storage::read(record);
    int result = 0;
    if (stored.contains("memory"))
    {
        result = stored.at("memory") == "1" ? 10 : 11;
    }
    else
    {
        const fs::path home = fs::path(SDL_GetBasePath()) / "cabinet";
        const auto program = home.string(), file = fs::absolute(core).string();
        const char* args[]{program.c_str(), "--internal-core-info", file.c_str(), nullptr};
        auto* process = SDL_CreateProcess(args, false);
        if (!process)
        {
            throw std::runtime_error("Cannot inspect custom core");
        }
        const auto deadline = SDL_GetTicks() + 5000;
        while (!SDL_WaitProcess(process, false, &result) && SDL_GetTicks() < deadline)
        {
            SDL_Delay(5);
        }
        if (SDL_GetTicks() >= deadline)
        {
            SDL_KillProcess(process, true);
            SDL_WaitProcess(process, true, &result);
            result = 0;
        }
        SDL_DestroyProcess(process);
        if (result != 10 && result != 11)
        {
            throw std::runtime_error("Custom core inspection failed");
        }
        save_storage::write(record, {{"memory", (result == 10) ? "1" : "0"}});
    }
    if (result != 10)
    {
        // need_fullpath describes how content is supplied, not whether a ZIP
        // can be used. Home already extracts the selected ROM to a real file.
        // Keep this limited to single-file cartridges: disc descriptors can
        // reference tracks that are not part of the selected ZIP member.
        for (const auto& system : systems())
        {
            if (system.id == system_id && (system.memory_content || system.core == "picodrive"))
            {
                return;
            }
        }
        throw std::runtime_error(
            "This core requires unpacked content for this system. Extract the ZIP and locate the ROM file in game details.");
    }
}
}
