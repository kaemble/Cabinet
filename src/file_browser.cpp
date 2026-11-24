#include "file_browser.h"
#include "boxart.h"
#include "library.h"
#include "rom_archive.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstddef>

namespace fs = std::filesystem;
namespace
{
const char* browser_folder_key(BrowsePurpose purpose)
{
    if (purpose == BrowsePurpose::BoxArt)
    {
        return "boxart";
    }
    if (purpose == BrowsePurpose::Core || purpose == BrowsePurpose::Custom)
    {
        return "core";
    }
    if (purpose == BrowsePurpose::Import || purpose == BrowsePurpose::Content || purpose == BrowsePurpose::Locate)
    {
        return "rom";
    }
    return nullptr;
}

std::string browser_initial(const BrowserEntry& entry, BrowsePurpose purpose)
{
    const auto name =
        (purpose == BrowsePurpose::Archive) ? entry.path.generic_string() : entry.path.filename().string();
    const char* end = name.c_str();
    SDL_StepUTF8(&end, nullptr);
    return std::string(name.c_str(), end);
}

} // namespace

FileBrowser::FileBrowser(const fs::path& data, HomeLogger logger)
    : settings_path(data / "browser.conf"), logger(logger), browser_folders(save_storage::read(settings_path))
{
}

void FileBrowser::remember_folder(BrowsePurpose intent, const fs::path& path)
{
    if (const auto* key = browser_folder_key(intent); key && browser_folders[key] != path.string())
    {
        std::error_code ec;
        if (fs::is_directory(path, ec))
        {
            browser_folders[key] = path.string();
            try
            {
                save_storage::write(settings_path, browser_folders);
            }
            catch (const std::exception& error)
            {
                logger.write(HomeLogLevel::Warn, error.what());
            }
        }
    }
}
void FileBrowser::read_folder(const fs::path& path)
{
    if (browsing)
    {
        return;
    }
    folder = path;
    browsing = true;
    browse_selected = browse_top = 0;
    entries.clear();
    remember_folder(current_purpose, path);
    listing = std::async(
        std::launch::async,
        [path, images_only = current_purpose == BrowsePurpose::BoxArt]
        {
            Listing result;
            try
            {
                if (path.has_parent_path() && path.parent_path() != path)
                {
                    result.entries.push_back({path.parent_path(), true});
                }
                auto begin = result.entries.size();
                for (const auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied))
                {
                    std::error_code ec;
                    if (entry.is_directory(ec) || (entry.is_regular_file(ec) && (!images_only || boxart_image(entry.path()))))
                    {
                        result.entries.push_back({entry.path(), entry.is_directory(ec)});
                    }
                    if (result.entries.size() >= 5000)
                    {
                        result.error = "Showing the first 5,000 entries. Choose a smaller folder.";
                        break;
                    }
                }
                std::sort(result.entries.begin() + static_cast<std::ptrdiff_t>(begin), result.entries.end(),
                          [](const BrowserEntry& a, const BrowserEntry& b)
                          {
                              if (a.directory != b.directory)
                              {
                                  return a.directory;
                              }
                              return SDL_strcasecmp(a.path.filename().string().c_str(),
                                                    b.path.filename().string().c_str()) < 0;
                          });
            }
            catch (const std::exception& error)
            {
                result.error = error.what();
            }
            return result;
        });
}
void FileBrowser::start(BrowsePurpose intent, const fs::path& core)
{
    if (browsing)
    {
        return;
    }
    current_purpose = intent;
    auto start = folder;
    if (intent == BrowsePurpose::Import)
    {
        start = fs::current_path();
    }
    if (intent == BrowsePurpose::Core && !core.empty())
    {
        start = core.parent_path();
    }
    if (const auto* key = browser_folder_key(intent); key && browser_folders.contains(key))
    {
        std::error_code ec;
        if (fs::is_directory(browser_folders.at(key), ec))
        {
            start = browser_folders.at(key);
        }
    }
    std::error_code ec;
    if (!fs::is_directory(start, ec))
    {
        start = fs::current_path();
    }
    read_folder(start);
}
void FileBrowser::browse_archive(const fs::path& path, BrowsePurpose intent, const std::string& system)
{
    archive_purpose = intent;
    current_purpose = BrowsePurpose::Archive;
    folder = path;
    browse_selected = browse_top = 0;
    entries.clear();
    browsing = true;
    listing =
        std::async(std::launch::async,
                   [path, selected = system_index(system)]
                   {
                       Listing result;
                       try
                       {
                           for (const auto& member : rom_archive::members(path))
                           {
                               const auto candidates = identify_systems(member);
                               if (std::find(candidates.begin(), candidates.end(), selected) != candidates.end())
                               {
                                   result.entries.push_back({member, false});
                               }
                           }
                           std::stable_sort(result.entries.begin(), result.entries.end(),
                                            [](const BrowserEntry& a, const BrowserEntry& b)
                                            {
                                                return SDL_strcasecmp(a.path.generic_string().c_str(),
                                                                      b.path.generic_string().c_str()) < 0;
                                            });
                           if (result.entries.empty())
                           {
                               result.error = "ZIP contains no ROM files for the selected system.";
                           }
                       }
                       catch (const std::exception& error)
                       {
                           result.error = error.what();
                       }
                       return result;
                   });
}
std::optional<std::string> FileBrowser::poll()
{
    if (!browsing || listing.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        return std::nullopt;
    }
    auto result = listing.get();
    entries = std::move(result.entries);
    browsing = false;
    return result.error;
}

void FileBrowser::navigate(int dx, int dy)
{
    if (dy && !entries.empty())
    {
        browse_selected = std::clamp(browse_selected + dy, 0, static_cast<int>(entries.size()) - 1);
    }
    if (dx && !entries.empty())
    {
        auto same_group = [&](int a, int b)
        {
            return entries[a].directory == entries[b].directory &&
                   SDL_strcasecmp(browser_initial(entries[a], current_purpose).c_str(),
                                  browser_initial(entries[b], current_purpose).c_str()) == 0;
        };
        int next = browse_selected;
        if (dx > 0)
        {
            while (next < static_cast<int>(entries.size()) && same_group(browse_selected, next))
            {
                ++next;
            }
            if (next < static_cast<int>(entries.size()))
            {
                browse_selected = next;
            }
        }
        else
        {
            while (next > 0 && same_group(browse_selected, next - 1))
            {
                --next;
            }
            if (next > 0)
            {
                --next;
            }
            while (next > 0 && same_group(next, next - 1))
            {
                --next;
            }
            browse_selected = next;
        }
    }
}

void FileBrowser::select(int index)
{
    if (!entries.empty())
    {
        browse_selected = std::clamp(index, 0, static_cast<int>(entries.size()) - 1);
    }
}

void FileBrowser::scroll_into_view(int rows)
{
    if (browse_selected < browse_top)
    {
        browse_top = browse_selected;
    }
    if (browse_selected >= browse_top + rows)
    {
        browse_top = browse_selected - rows + 1;
    }
}
