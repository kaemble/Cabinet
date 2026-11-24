#pragma once
#include "home_log.h"
#include "save_storage.h"
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

enum class BrowsePurpose
{
    Import,
    Custom,
    Content,
    Locate,
    Core,
    Bios,
    Archive,
    BoxArt
};
struct BrowserEntry
{
    std::filesystem::path path;
    bool directory;
};

class FileBrowser
{
public:
    explicit FileBrowser(const std::filesystem::path& data, HomeLogger logger = {});
    void start(BrowsePurpose intent, const std::filesystem::path& core = {});
    void read_folder(const std::filesystem::path& path);
    void browse_archive(const std::filesystem::path& path, BrowsePurpose intent, const std::string& system);
    void remember_folder(BrowsePurpose intent, const std::filesystem::path& path);
    // A completed listing returns its error, empty on success; otherwise nullopt.
    std::optional<std::string> poll();
    void navigate(int dx, int dy);
    void select(int index);
    void scroll_into_view(int rows);
    void set_folder(const std::filesystem::path& path)
    {
        folder = path;
    }
    const std::filesystem::path& current_folder() const
    {
        return folder;
    }
    const std::vector<BrowserEntry>& items() const
    {
        return entries;
    }
    BrowsePurpose purpose() const
    {
        return current_purpose;
    }
    BrowsePurpose intent() const
    {
        return current_purpose == BrowsePurpose::Archive ? archive_purpose : current_purpose;
    }
    bool loading() const
    {
        return browsing;
    }
    int selected() const
    {
        return browse_selected;
    }
    int top() const
    {
        return browse_top;
    }

private:
    struct Listing
    {
        std::vector<BrowserEntry> entries;
        std::string error;
    };
    std::filesystem::path settings_path;
    HomeLogger logger;
    save_storage::Metadata browser_folders;
    BrowsePurpose current_purpose = BrowsePurpose::Import, archive_purpose = BrowsePurpose::Import;
    std::filesystem::path folder = std::filesystem::current_path();
    std::vector<BrowserEntry> entries;
    std::future<Listing> listing;
    bool browsing = false;
    int browse_selected = 0, browse_top = 0;
};
