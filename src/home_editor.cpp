#include "home_window.h"
#include "firmware.h"
#include "rom_archive.h"

void Home::start_browse(BrowsePurpose intent)
{
    if (browser.loading())
    {
        return;
    }
    if (intent == BrowsePurpose::Import && sidebar < 3)
    {
        notify("Select a system before adding a game.");
        return;
    }
    stop_text();
    modal = Modal::Browse;
    browser.start(intent, draft.core);
}
void Home::select_system(const std::string& id)
{
    const int index = system_index(id);
    if (index < 0)
    {
        throw std::runtime_error("Unknown system: " + id);
    }
    sidebar = index < system_count() ? index + 3 : 0;
    refilter();
}
void Home::import_file(const fs::path& path, bool custom)
{
    if (!custom && sidebar < 3)
    {
        throw std::runtime_error("Select a system before adding a game.");
    }
    if (!fs::is_regular_file(path))
    {
        throw std::runtime_error("Choose an existing game file.");
    }
    stop_text();
    ++artwork_generation;
    queued_artwork.clear();
    boxart.reset();
    draft = {};
    draft.path = fs::canonical(path).string();
    draft.title = path.stem().string();
    const auto& selected_system = systems()[custom ? system_index("generic") : sidebar - 3];
    draft.system = selected_system.id;
    draft.core = bundled_core(selected_system).string();
    browser.remember_folder(BrowsePurpose::Import, fs::path(draft.path).parent_path());
    if (draft.system == "generic")
    {
        editing = false;
        dirty = false;
        modal = Modal::Edit;
        choice = edit_index(EditAction::Core);
        browser.set_folder(fs::path(draft.path).parent_path());
        import_artwork();
        return;
    }
    if (rom_archive::is_zip(path))
    {
        browse_archive(path, BrowsePurpose::Import);
        return;
    }
    const auto candidates = identify_systems(path);
    if (std::find(candidates.begin(), candidates.end(), sidebar - 3) == candidates.end())
    {
        throw std::runtime_error("This file format does not match the selected system.");
    }
    editing = false;
    dirty = false;
    modal = Modal::Edit;
    choice = 0;
    browser.set_folder(fs::path(draft.path).parent_path());
    import_artwork();
}
void Home::browse_archive(const fs::path& path, BrowsePurpose intent)
{
    modal = Modal::Browse;
    browser.browse_archive(path, intent, draft.system);
}
void Home::finish_locate(const Game& changed)
{
    library.update(changed);
    refresh(changed.id);
    modal = Modal::Details;
    choice = 0;
    notify("Game file located.");
}
void Home::choose_file(const fs::path& path)
{
    if (browser.purpose() == BrowsePurpose::Archive)
    {
        draft.archive_member = path.generic_string();
        if (browser.intent() == BrowsePurpose::Locate)
        {
            finish_locate(draft);
            return;
        }
        draft.title = path.stem().string();
        editing = false;
        dirty = false;
        modal = Modal::Edit;
        choice = 0;
        browser.set_folder(fs::path(draft.path).parent_path());
        import_artwork();
    }
    else if (browser.purpose() == BrowsePurpose::Bios)
    {
        const auto result = install_bios(bios_system, path, fs::path(data_dir) / "system");
        bios_installed = true;
        modal = Modal::Bios;
        choice = 1;
        notify(result);
    }
    else if (browser.purpose() == BrowsePurpose::Import)
    {
        import_file(path);
    }
    else if (browser.purpose() == BrowsePurpose::Custom)
    {
        ++artwork_generation;
        queued_artwork.clear();
        boxart.reset();
        draft = {};
        draft.system = "generic";
        draft.core = fs::canonical(path).string();
        draft.title = path.stem().string();
        editing = false;
        dirty = false;
        modal = Modal::Edit;
        choice = edit_index(EditAction::Content);
        import_artwork();
    }
    else if (browser.purpose() == BrowsePurpose::Content)
    {
        draft.path = fs::canonical(path).string();
        draft.title = path.stem().string();
        dirty = true;
        modal = Modal::Edit;
        choice = edit_index(EditAction::Save);
        import_artwork();
    }
    else if (browser.purpose() == BrowsePurpose::BoxArt)
    {
        ++artwork_generation;
        queued_artwork.clear();
        boxart.select(path);
        dirty = true;
        modal = Modal::Edit;
        choice = edit_index(EditAction::BoxArt);
    }
    else if (browser.purpose() == BrowsePurpose::Core)
    {
        draft.core = fs::canonical(path).string();
        dirty = true;
        modal = Modal::Edit;
        choice = edit_index(EditAction::Core);
    }
    else
    {
        if (!game())
        {
            return;
        }
        Game changed = *game();
        changed.path = fs::canonical(path).string();
        if (changed.system != "generic" && rom_archive::is_zip(path))
        {
            draft = changed;
            browse_archive(path, BrowsePurpose::Locate);
            return;
        }
        changed.archive_member.clear();
        finish_locate(changed);
    }
}
void Home::finish_import()
{
    if (artwork_loading())
    {
        notify("Artwork is loading. Select the SteamGridDB row to cancel, or wait before saving.");
        return;
    }
    const bool duplicate = !editing && std::any_of(all.begin(), all.end(), [&](const Game& game)
    {
        return game.path == draft.path && game.archive_member == draft.archive_member &&
               (!draft.path.empty() || game.core == draft.core);
    });
    if (editing)
    {
        library.update(draft);
    }
    else
    {
        draft.id = library.add(draft);
    }
    // A duplicate import selects the existing entry without overwriting it.
    const auto saved = library.game(draft.id);
    if (!saved)
    {
        throw std::runtime_error("Saved game was not found in the library");
    }
    draft = *saved;
    if (!duplicate)
    {
        boxart.save(draft.save_id);
    }
    ++artwork_generation;
    queued_artwork.clear();
    boxart.reset();
    cache_game(draft);
    sidebar = system_index(draft.system) + 3;
    query.clear();
    refilter(draft.id);
    stop_text();
    modal = Modal::Details;
    choice = 0;
    focus = 1;
    notify(editing ? "Game details saved" : "Game added to your library");
}
void Home::set_system(int direction)
{
    if (draft.system == "generic")
    {
        return;
    }
    int index = system_index(draft.system);
    const int count = static_cast<int>(systems().size()) - 1;
    index = (index < 0) ? ((direction > 0) ? 0 : count - 1) : (index + direction + count) % count;
    draft.system = systems()[index].id;
    draft.core = bundled_core(systems()[index]).string();
    dirty = true;
}
void Home::import_artwork()
{
    artwork_title = draft.title;
    boxart.reset();
    ++artwork_generation;
    queued_artwork.clear();
    if (!api_key.empty())
    {
        queued_artwork = draft.title;
    }
}

void Home::fetch_artwork(std::int64_t game_id, bool automatic)
{
    if (artwork_task.valid())
    {
        notify("A SteamGridDB request is still finishing. Try again shortly.");
        return;
    }
    SteamGridDB client(api_key, artwork_request);
    const auto generation = ++artwork_generation;
    artwork_task_generation = generation;
    const auto title = artwork_query;
    artwork_task = std::async(std::launch::async, [client = std::move(client), generation, title, game_id, automatic]
    {
        ArtworkResult result;
        result.generation = generation;
        result.searching = !automatic && game_id == 0;
        try
        {
            if (result.searching)
            {
                result.games = client.search(title);
            }
            else
            {
                result.image = automatic ? client.import_image(title) : client.image(game_id);
            }
        }
        catch (const std::exception& error)
        {
            result.error = error.what();
        }
        return result;
    });
    needs_redraw = true;
}

void Home::poll_artwork()
{
    if (artwork_task.valid() && artwork_task.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        auto result = artwork_task.get();
        needs_redraw = true;
        if (result.generation == artwork_generation)
        {
            if (!result.error.empty())
            {
                notify(result.error);
            }
            else if (result.searching && modal == Modal::SteamGrid)
            {
                artwork_games = std::move(result.games);
                if (artwork_games.size() > 5)
                {
                    artwork_games.resize(5);
                }
                choice = artwork_games.empty() ? 1 : 2;
                if (artwork_games.empty())
                {
                    notify("No matching games found on SteamGridDB");
                }
            }
            else if (!result.image.empty())
            {
                try
                {
                    boxart.select_data(result.image);
                    dirty = true;
                    if (modal == Modal::SteamGrid)
                    {
                        modal = Modal::Edit;
                        choice = edit_index(EditAction::Artwork);
                    }
                    notify("SteamGridDB artwork selected. Save to keep it.");
                }
                catch (const std::exception& error)
                {
                    notify(error.what());
                }
            }
        }
    }
    if (!artwork_task.valid() && !queued_artwork.empty())
    {
        artwork_query = std::move(queued_artwork);
        queued_artwork.clear();
        fetch_artwork(0, true);
    }
}

void Home::edit_game()
{
    if (!game())
    {
        return;
    }
    draft = *game();
    ++artwork_generation;
    queued_artwork.clear();
    boxart.reset();
    editing = true;
    dirty = false;
    modal = Modal::Edit;
    choice = 0;
}
void Home::close_editor()
{
    stop_text();
    ++artwork_generation;
    queued_artwork.clear();
    boxart.reset();
    modal = editing ? Modal::Details : Modal::None;
    choice = 0;
}
