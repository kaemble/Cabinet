#include "home_window.h"
#include "firmware.h"
#include "save_storage.h"

std::vector<Home::EditRow> Home::edit_rows() const
{
    const int sys = system_index(draft.system);
    const std::string system = draft.system == "generic" ? "Generic" : sys < 0 ? "Choose a system" : systems()[sys].name;
    const std::string core = draft.core.empty() ? "Choose a core file" : fs::path(draft.core).filename().string();
    std::vector<EditRow> rows{
        {EditAction::Title, "Title: " + draft.title},
        {EditAction::System, "System: " + system},
        {EditAction::Core, "Core: " + core},
        {EditAction::Favorite, draft.favorite ? "Favorite: yes" : "Favorite: no"}
    };
    if (editing)
    {
        rows.push_back({EditAction::BoxArt, boxart.pending() ? "Box art: new image selected" : "Box art: choose image"});
    }
    else if (draft.system == "generic")
    {
        const std::string rom = draft.path.empty() ? "Skip (run without content)" : fs::path(draft.path).filename().string();
        rows.push_back({EditAction::Content, "ROM: " + rom});
    }
    if (!api_key.empty())
    {
        rows.push_back({EditAction::Artwork, artwork_loading() ? "SteamGridDB: loading... (select to cancel)"
                   : boxart.pending() ? "SteamGridDB: image selected (search again)" : "SteamGridDB: find box art"});
    }
    rows.push_back({EditAction::Save, editing ? "Save changes" : "Add to library"});
    rows.push_back({EditAction::Cancel, "Cancel"});
    return rows;
}

int Home::edit_index(EditAction action) const
{
    const auto rows = edit_rows();
    const auto row = std::ranges::find(rows, action, &EditRow::action);
    return static_cast<int>(row - rows.begin());
}

std::vector<std::string> Home::modal_rows()
{
    if (modal == Modal::Menu)
    {
        return {"Add custom entry", "Settings", "Close Cabinet", "SteamGridDB API key", "Back"};
    }
    if (modal == Modal::ApiKey)
    {
        return {"API key: " + std::string(key_draft.size(), '*'), "Save key", "Remove key", "Cancel"};
    }
    if (modal == Modal::SteamGrid)
    {
        std::vector<std::string> rows{"Search: " + artwork_query, "Search SteamGridDB"};
        for (const auto& game : artwork_games)
        {
            rows.push_back(game.name);
        }
        rows.push_back("Cancel search");
        return rows;
    }
    if (modal == Modal::Settings)
    {
        return {std::string("Swap confirm/cancel: ") + (swap_confirm_cancel ? "on" : "off"),
                std::string("Dual-screen runner: ") + (dual_screen ? "on" : "off"), "Back"};
    }
    if (modal == Modal::Bios)
    {
        return {"Choose BIOS file", bios_for_import ? (bios_installed ? "Continue import" : "Skip for now") : "Done",
                "Back"};
    }
    if (modal == Modal::Details)
    {
        std::vector<std::string> result{process.active() ? "Game running in another window" : "Play", "Edit details",
                                        (game() && game()->favorite) ? "Remove from favorites" : "Add to favorites",
                                        "Locate file", "Remove from library"};
        if (game() && requires_bios(game()->system))
        {
            result.push_back("BIOS files");
        }
        result.push_back("Back");
        return result;
    }
    if (modal == Modal::Edit)
    {
        std::vector<std::string> rows;
        for (auto& row : edit_rows())
        {
            rows.push_back(std::move(row.label));
        }
        return rows;
    }
    if (modal == Modal::Remove)
    {
        return {"Cancel", "Remove from library"};
    }
    if (modal == Modal::Discard)
    {
        return {"Keep editing", "Discard changes"};
    }
    return {};
}

void Home::navigate(int dx, int dy)
{
    update_layout();
    if (typing_title || search)
    {
        return;
    }
    if (modal == Modal::Browse)
    {
        browser.navigate(dx, dy);
        return;
    }
    if (modal != Modal::None)
    {
        auto rows = modal_rows();
        if (dy && !rows.empty())
        {
            choice = (choice + dy + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
        }
        if (dx && modal == Modal::Edit && choice == 1)
        {
            set_system(dx);
        }
        return;
    }
    if (focus == 0)
    {
        if (dy)
        {
            sidebar = std::clamp(sidebar + dy, 0, system_count() + 2);
            refilter();
        }
        if (dx > 0)
        {
            focus = visible.empty() ? 2 : 1;
        }
    }
    else if (focus == 2)
    {
        if (dx < 0)
        {
            toolbar = std::max(1, toolbar - 1);
        }
        if (dx > 0 && toolbar >= 1)
        {
            toolbar = 2;
        }
        if (dy > 0)
        {
            if (toolbar == 0)
            {
                toolbar = 2;
            }
            else
            {
                focus = visible.empty() ? 0 : 1;
            }
        }
        if (dy < 0)
        {
            if (toolbar >= 2 && sidebar >= 3)
            {
                toolbar = 0;
            }
            else
            {
                focus = 0;
            }
        }
    }
    else
    {
        const int cols = list_view ? 1 : layout.columns;
        if (dx < 0 && (list_view || selected % cols == 0))
        {
            focus = 0;
            return;
        }
        if (dy < 0 && selected < cols)
        {
            focus = 2;
            return;
        }
        if (!visible.empty())
        {
            selected = std::clamp(selected + dx + dy * cols, 0, static_cast<int>(visible.size()) - 1);
        }
    }
}
void Home::back()
{
    if (typing_title)
    {
        stop_text();
        return;
    }
    if (search)
    {
        stop_text();
        focus = 2;
        return;
    }
    if (modal == Modal::Browse)
    {
        const auto intent = browser.intent();
        modal = (intent == BrowsePurpose::Bios) ? Modal::Bios
                : (intent == BrowsePurpose::Core || intent == BrowsePurpose::Content || intent == BrowsePurpose::BoxArt) ? Modal::Edit
                : (intent == BrowsePurpose::Locate) ? Modal::Details
                : (intent == BrowsePurpose::Custom) ? Modal::Menu
                                                     : Modal::None;
        choice = intent == BrowsePurpose::BoxArt ? edit_index(EditAction::BoxArt)
                 : intent == BrowsePurpose::Content ? edit_index(EditAction::Content) : 0;
        return;
    }
    if (modal == Modal::ApiKey)
    {
        key_draft.clear();
        modal = Modal::Menu;
        choice = 3;
    }
    else if (modal == Modal::SteamGrid)
    {
        ++artwork_generation;
        queued_artwork.clear();
        modal = Modal::Edit;
        choice = edit_index(EditAction::Artwork);
    }
    else if (modal == Modal::Settings)
    {
        modal = Modal::Menu;
        choice = 1;
    }
    else if (modal == Modal::Menu)
    {
        modal = Modal::None;
    }
    else if (modal == Modal::Edit)
    {
        if (dirty)
        {
            modal = Modal::Discard;
            choice = 0;
        }
        else
        {
            close_editor();
        }
    }
    else if (modal == Modal::Discard)
    {
        modal = Modal::Edit;
        choice = 0;
        exit_after_discard = false;
    }
    else if (modal == Modal::Remove)
    {
        modal = Modal::Details;
        choice = 0;
    }
    else if (modal == Modal::Bios)
    {
        modal = bios_for_import ? Modal::Edit : Modal::Details;
        choice = bios_for_import ? edit_index(api_key.empty() ? EditAction::Save : EditAction::Artwork) : 0;
    }
    else if (modal == Modal::Details)
    {
        modal = Modal::None;
    }
    else
    {
        focus = 0;
    }
}
void Home::open_menu()
{
    if (modal == Modal::Menu)
    {
        back();
    }
    else if (modal == Modal::None)
    {
        stop_text();
        modal = Modal::Menu;
        choice = 0;
    }
    needs_redraw = true;
}
void Home::request_close()
{
    if (process.active())
    {
        notify("Quit the game from its menu before closing the library.");
    }
    else if (dirty && (modal == Modal::Edit || modal == Modal::Discard || modal == Modal::SteamGrid))
    {
        stop_text();
        modal = Modal::Discard;
        choice = 0;
        exit_after_discard = true;
    }
    else
    {
        running = false;
    }
}
void Home::activate(bool with_controller)
{
    if (modal == Modal::Menu)
    {
        if (choice == 0)
        {
            start_browse(BrowsePurpose::Custom);
        }
        else if (choice == 1)
        {
            modal = Modal::Settings;
            choice = 0;
        }
        else if (choice == 2)
        {
            request_close();
        }
        else if (choice == 3)
        {
            key_draft.clear();
            modal = Modal::ApiKey;
            choice = 0;
        }
        else
        {
            back();
        }
        return;
    }
    if ((modal == Modal::ApiKey || modal == Modal::SteamGrid) && typing_title)
    {
        stop_text();
        return;
    }
    if (modal == Modal::ApiKey)
    {
        if (choice == 0)
        {
            start_text(true);
        }
        else if (choice == 1 || choice == 2)
        {
            if (choice == 1)
            {
                SteamGridDB validate(key_draft);
            }
            const auto directory = fs::path(data_dir) / "steamgriddb";
            fs::create_directories(directory);
            fs::permissions(directory, fs::perms::owner_all);
            const auto next_key = choice == 2 ? std::string{} : key_draft;
            save_storage::write(directory / "key.conf", {{"api_key", next_key}});
            fs::permissions(directory / "key.conf", fs::perms::owner_read | fs::perms::owner_write);
            api_key = next_key;
            back();
            notify(api_key.empty() ? "SteamGridDB key removed" : "SteamGridDB key saved");
        }
        else
        {
            back();
        }
        return;
    }
    if (modal == Modal::SteamGrid)
    {
        if (choice == 0)
        {
            start_text(true);
        }
        else if (choice == 1)
        {
            fetch_artwork();
        }
        else if (choice - 2 < static_cast<int>(artwork_games.size()))
        {
            fetch_artwork(artwork_games[choice - 2].id);
        }
        else
        {
            back();
        }
        return;
    }
    if (modal == Modal::Settings)
    {
        if (choice == 2)
        {
            back();
            return;
        }
        const bool next_swap = choice == 0 ? !swap_confirm_cancel : swap_confirm_cancel;
        const bool next_dual = choice == 1 ? !dual_screen : dual_screen;
        save_storage::write(fs::path(data_dir) / "home.conf",
                            {{"swap_confirm_cancel", next_swap ? "1" : "0"}, {"dual_screen", next_dual ? "1" : "0"}});
        swap_confirm_cancel = next_swap;
        dual_screen = next_dual;
        block_controller();
        return;
    }
    if (modal == Modal::Bios)
    {
        if (choice == 0)
        {
            start_browse(BrowsePurpose::Bios);
        }
        else if (choice == 1)
        {
            if (bios_for_import)
            {
                finish_import();
            }
            else
            {
                modal = Modal::Details;
                choice = 0;
            }
        }
        else
        {
            back();
        }
        return;
    }
    if (typing_title || search)
    {
        stop_text();
        return;
    }
    if (modal == Modal::Browse)
    {
        if (browser.loading() || browser.items().empty())
        {
            return;
        }
        const auto entry = browser.items()[browser.selected()];
        if (entry.directory)
        {
            browser.read_folder(entry.path);
        }
        else
        {
            choose_file(entry.path);
        }
        return;
    }
    if (modal == Modal::Remove)
    {
        if (choice == 1 && game())
        {
            const auto id = game()->id;
            library.remove(id);
            std::erase_if(all,
                          [&](const Game& entry)
                          {
                              return entry.id == id;
                          });
            recount_systems();
            refilter();
            modal = Modal::None;
            notify("Removed from library. The game file remains untouched.");
        }
        else
        {
            modal = Modal::Details;
            choice = 0;
        }
        return;
    }
    if (modal == Modal::Discard)
    {
        if (choice == 1)
        {
            close_editor();
            if (exit_after_discard)
            {
                running = false;
            }
        }
        else
        {
            modal = Modal::Edit;
            choice = 0;
        }
        exit_after_discard = false;
        return;
    }
    if (modal == Modal::Edit)
    {
        switch (edit_rows().at(choice).action)
        {
        case EditAction::Title:
            start_text(true);
            break;
        case EditAction::System:
            set_system(1);
            break;
        case EditAction::Core:
            start_browse(BrowsePurpose::Core);
            break;
        case EditAction::Favorite:
            draft.favorite = !draft.favorite;
            dirty = true;
            break;
        case EditAction::BoxArt:
            start_browse(BrowsePurpose::BoxArt);
            break;
        case EditAction::Content:
            start_browse(BrowsePurpose::Content);
            break;
        case EditAction::Artwork:
        {
            if (artwork_loading())
            {
                ++artwork_generation;
                queued_artwork.clear();
                notify("Artwork search cancelled");
                return;
            }
            SteamGridDB validate(api_key);
            stop_text();
            modal = Modal::SteamGrid;
            choice = 1;
            artwork_query = draft.title;
            artwork_games.clear();
            fetch_artwork();
            break;
        }
        case EditAction::Save:
            if (!editing && requires_bios(draft.system) &&
                !has_bios(draft.system, fs::path(data_dir) / "system"))
            {
                bios_system = draft.system;
                bios_for_import = true;
                bios_installed = false;
                stop_text();
                modal = Modal::Bios;
                choice = 0;
            }
            else
            {
                finish_import();
            }
            break;
        case EditAction::Cancel:
            back();
            break;
        }
        return;
    }
    if (modal == Modal::Details && game())
    {
        if (choice == 0)
        {
            std::vector<std::string> arguments{"--data-dir", data_dir, "--title", game()->title};
            if (swap_confirm_cancel)
            {
                arguments.push_back("--swap-confirm-cancel");
            }
            if (dual_screen)
            {
                arguments.push_back("--dual-screen");
            }
            if (with_controller && library.session_count() < 3)
            {
                arguments.push_back("--show-menu-hint");
            }
            process.start(executable, *game(), arguments);
            launched_id = game()->id;
            launched_with_controller = with_controller;
            played_recorded = false;
            modal = Modal::None;
            notify(game()->archive_member.empty() ? "Opening " + game()->title
                                                  : "Preparing game...");
        }
        else if (choice == 1)
        {
            edit_game();
        }
        else if (choice == 2)
        {
            auto changed = *game();
            changed.favorite = !changed.favorite;
            library.set_favorite(changed.id, changed.favorite);
            refresh(changed.id);
            if (!game())
            {
                modal = Modal::None;
            }
        }
        else if (choice == 3)
        {
            browser.set_folder(fs::path(game()->path).parent_path());
            start_browse(BrowsePurpose::Locate);
        }
        else if (choice == 4)
        {
            modal = Modal::Remove;
            choice = 0;
        }
        else if (choice == 5 && requires_bios(game()->system))
        {
            bios_system = game()->system;
            bios_for_import = false;
            bios_installed = has_bios(bios_system, fs::path(data_dir) / "system");
            modal = Modal::Bios;
            choice = 0;
        }
        else
        {
            modal = Modal::None;
        }
        return;
    }
    if (focus == 2)
    {
        if (toolbar == 0)
        {
            start_browse(BrowsePurpose::Import);
        }
        else if (toolbar == 1)
        {
            start_text(false);
        }
        else
        {
            list_view = !list_view;
            first_row = 0;
        }
    }
    else if (focus == 0)
    {
        focus = visible.empty() ? 2 : 1;
    }
    else if (game())
    {
        modal = Modal::Details;
        choice = 0;
    }
}
