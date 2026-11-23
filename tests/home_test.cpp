// Exercise the actual UI event handlers and drawing against a temporary library.
#include "../src/home.cpp"
#include <fstream>
#include <sqlite3.h>
#include "archive_fixture.h"
#include "virtual_gamepad.h"
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void key(Home& home, SDL_Scancode code, SDL_Keymod mod = SDL_KMOD_NONE) {
    SDL_Event e{}; e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = code; e.key.mod = mod; home.event(e);
}
static void type(Home& home, const char* value) {
    SDL_Event e{}; e.type = SDL_EVENT_TEXT_INPUT; e.text.text = value; home.event(e);
}
static int sql_statements = 0;
static int full_library_reads = 0;
static int trace_sql(unsigned, void*, void* statement, void*) {
    ++sql_statements;
    if (std::string_view(sqlite3_sql(static_cast<sqlite3_stmt*>(statement))).find(" FROM games ORDER BY") != std::string_view::npos)
        ++full_library_reads;
    return 0;
}
static int trace_connection(sqlite3* db, char**, const sqlite3_api_routines*) {
    return sqlite3_trace_v2(db, SQLITE_TRACE_STMT, trace_sql, nullptr);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scratch directory");
        fs::path dir = fs::path(argv[1]) / ("home-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        auto rom = dir / "A test game.gbc";
        std::ofstream(rom) << "metadata test only";
        isolate_virtual_gamepads();
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), SDL_GetError());
        auto* window = SDL_CreateWindow("Home test", 1200, 800, 0);
        require(window != nullptr, SDL_GetError());
        auto* renderer = SDL_CreateRenderer(window, nullptr);
        require(renderer != nullptr, SDL_GetError());
        {
            Home home(window, renderer, dir, "/unused");
            home.draw();
            bool rejected = false;
            try { home.import_file(rom); } catch (const std::exception&) { rejected = true; }
            require(rejected, "Import allowed without selecting a system");
            home.select_system("gb");
            home.import_file(rom);
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            Library inspect(dir / "library.sqlite3");
            require(inspect.games().size() == 1, "Confirm import must persist a game");
            require(inspect.games()[0].system == "gb" && !inspect.games()[0].core.empty(), "Import must record reviewed system/core");
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(inspect.games()[0].favorite, "Favorite action must persist");
            key(home, SDL_SCANCODE_UP); key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_RETURN); key(home, SDL_SCANCODE_A, SDL_KMOD_CTRL);
            type(home, "Renamed game"); key(home, SDL_SCANCODE_RETURN);
            for (int i = 0; i < 5; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            require(inspect.games()[0].title == "Renamed game", "Edit details must save new title");
            home.draw();
            auto* screen = SDL_RenderReadPixels(renderer, nullptr);
            require(screen != nullptr, SDL_GetError());
            require(SDL_SaveBMP(screen, (dir / "details.bmp").string().c_str()), SDL_GetError());
            SDL_DestroySurface(screen);
            key(home, SDL_SCANCODE_ESCAPE); home.draw();
            screen = SDL_RenderReadPixels(renderer, nullptr);
            require(screen != nullptr, SDL_GetError());
            require(SDL_SaveBMP(screen, (dir / "library.bmp").string().c_str()), SDL_GetError());
            SDL_DestroySurface(screen);
            SDL_SetWindowSize(window, 900, 600);
            home.draw();
            screen = SDL_RenderReadPixels(renderer, nullptr);
            require(screen != nullptr, SDL_GetError());
            require(SDL_SaveBMP(screen, (dir / "library-small.bmp").string().c_str()), SDL_GetError());
            SDL_DestroySurface(screen);
            home.select_system("gb");
            home.import_file(rom);
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            require(inspect.games().size() == 1 && inspect.games()[0].title == "Renamed game", "Duplicate import must preserve existing metadata");
            // Controller-only navigation from the library to a persisted action.
            key(home, SDL_SCANCODE_ESCAPE);
            SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
            identify_virtual_gamepad(desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT; desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
            desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
            auto id = SDL_AttachVirtualJoystick(&desc);
            require(id != 0, SDL_GetError());
            auto* joystick = SDL_OpenJoystick(id);
            require(joystick != nullptr, SDL_GetError());
            SDL_Event added{}; added.type = SDL_EVENT_GAMEPAD_ADDED; added.gdevice.which = id; home.event(added);
            home.controller_input(true);
            auto press = [&](SDL_GamepadButton b) {
                require(SDL_SetJoystickVirtualButton(joystick, b, true), SDL_GetError());
                SDL_UpdateJoysticks(); SDL_UpdateGamepads(); home.controller_input(true);
                require(SDL_SetJoystickVirtualButton(joystick, b, false), SDL_GetError());
                SDL_UpdateJoysticks(); SDL_UpdateGamepads(); home.controller_input(true);
            };
            auto tilt = [&](int value) {
                require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, value), SDL_GetError());
                SDL_UpdateJoysticks(); SDL_UpdateGamepads();
            };
            for (int direction : {-24000, 24000}) {
                tilt(direction); home.controller_input(true);
                SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST; home.event(focus);
                home.controller_input(false);
                // A release received while unfocused must not leave repeat armed.
                tilt(0);
                focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED; home.event(focus);
                home.needs_redraw = false;
                home.controller_input(true); home.controller_input(true);
                require(!home.needs_redraw, "Stick released in background restarted menu navigation on focus return");
                // Model a backend that still reports the pre-focus-loss tilt.
                tilt(direction);
                focus.type = SDL_EVENT_WINDOW_FOCUS_LOST; home.event(focus);
                focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED; home.event(focus);
                home.needs_redraw = false;
                for (int i = 0; i < 3; ++i) home.controller_input(true);
                require(!home.needs_redraw, "Stale stick state rearmed repeat without returning to neutral");
                tilt(0); home.controller_input(true);
                tilt(direction); home.controller_input(true);
                require(home.needs_redraw, "Fresh stick movement was ignored after returning to neutral");
                tilt(0); home.controller_input(true);
            }
            // The navigation checks may have changed sidebar/focus. Return to
            // the game card before continuing the existing details flow.
            home.select_system("gb"); home.draw();
            SDL_Event card{}; card.type = SDL_EVENT_MOUSE_BUTTON_DOWN; card.button.button = SDL_BUTTON_LEFT;
            card.button.x = home.layout.left + home.layout.gap;
            card.button.y = home.layout.grid_top + home.layout.gap;
            home.event(card); key(home, SDL_SCANCODE_ESCAPE);
            press(SDL_GAMEPAD_BUTTON_SOUTH); // Open selected game's details.
            press(SDL_GAMEPAD_BUTTON_DPAD_DOWN); press(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            press(SDL_GAMEPAD_BUTTON_SOUTH);
            require(!inspect.games()[0].favorite, "Controller must navigate details and toggle favorite");
            // Returning focus with confirm held must not activate a second action.
            home.controller_input(false);
            SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true);
            SDL_UpdateJoysticks(); SDL_UpdateGamepads(); home.controller_input(true);
            require(!inspect.games()[0].favorite, "Held controller button must not activate on focus return");
            SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, false);
            SDL_UpdateJoysticks(); SDL_UpdateGamepads(); home.controller_input(true);
            press(SDL_GAMEPAD_BUTTON_EAST); // Return to library.
            require(SDL_DetachVirtualJoystick(id), SDL_GetError());
            SDL_Event removed{}; removed.type = SDL_EVENT_GAMEPAD_REMOVED; removed.gdevice.which = id; home.event(removed);
            SDL_CloseJoystick(joystick);
            key(home, SDL_SCANCODE_RETURN); // Reopen details, initial choice is Play.
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN); key(home, SDL_SCANCODE_RETURN);
            require(inspect.games().size() == 1, "Cancel must keep library entry");
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(inspect.games().empty() && fs::exists(rom), "Confirmed remove must keep original file");
            home.draw();
            // Jump past both A titles to the first B title.
            std::ofstream(dir / "another.gbc") << "metadata test only";
            std::ofstream(dir / "Beta.gbc") << "metadata test only";
            key(home, SDL_SCANCODE_I);
            for (int i = 0; i < 100; ++i) { home.tick(); SDL_Delay(1); }
            key(home, SDL_SCANCODE_DOWN); // Parent folder is first; fixture ROM sorts next.
            key(home, SDL_SCANCODE_RIGHT);
            key(home, SDL_SCANCODE_RETURN);
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            require(inspect.games().size() == 1 && inspect.games()[0].title == "Beta",
                    "File browser must jump past all titles with the same letter");
        }
        {
            auto responsive_dir = dir / "responsive";
            fs::create_directories(responsive_dir);
            Library seed(responsive_dir / "library.sqlite3");
            for (int i = 0; i < 18; ++i) {
                std::string title = "Game " + std::string(i < 10 ? "0" : "") + std::to_string(i);
                auto path = responsive_dir / (title + ".gbc");
                std::ofstream(path) << "layout test fixture";
                seed.add({0, path.string(), title, "gb", "test-core"});
            }
            Home home(window, renderer, responsive_dir, "/unused");
            for (auto size : {std::pair{1920, 1080}, std::pair{1440, 900}, std::pair{600, 1000}, std::pair{560, 600}, std::pair{1000, 700}}) {
                require(SDL_SetWindowSize(window, size.first, size.second), SDL_GetError());
                home.draw();
                int w, h; SDL_RendererLogicalPresentation mode;
                SDL_GetRenderLogicalPresentation(renderer, &w, &h, &mode);
                require(w == size.first && h == size.second && mode == SDL_LOGICAL_PRESENTATION_STRETCH,
                    "Home layout must use the actual window aspect ratio");
                require(home.layout.side == size.first * .20f, "Sidebar must occupy 20 percent of the viewport");
                require(home.layout.grid_top + home.layout.grid_rows * (home.layout.card_height + home.layout.gap)
                            <= size.second - home.layout.bar_height + 1,
                        "Game grid must stay above the bottom bar");
                require(home.layout.columns == (size.first == 1920 ? 5 : size.first == 1440 ? 4 : size.first <= 600 ? 1 : 3),
                    "Grid must change column count with available width");
                if (size.first == 600) {
                    require(home.layout.card_height > home.layout.card_width,
                            "Grid cards must reserve space below the square artwork area");
                    auto* image = SDL_RenderReadPixels(renderer, nullptr);
                    require(image != nullptr, SDL_GetError());
                    SDL_SaveBMP(image, (dir / "portrait.bmp").string().c_str()); SDL_DestroySurface(image);
                }
            }
            SDL_SetWindowSize(window, 1440, 900); home.draw();
            key(home, SDL_SCANCODE_RIGHT); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(seed.games()[4].favorite, "Grid navigation must advance by the current column count");
            key(home, SDL_SCANCODE_ESCAPE);
            SDL_SetWindowSize(window, 600, 1000); home.draw();
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(seed.games()[5].favorite, "Portrait navigation must advance one game per row after resizing");
            home.draw();
            auto* image = SDL_RenderReadPixels(renderer, nullptr);
            require(image != nullptr, SDL_GetError());
            SDL_SaveBMP(image, (dir / "portrait-details.bmp").string().c_str()); SDL_DestroySurface(image);
            key(home, SDL_SCANCODE_ESCAPE); home.draw();
            SDL_Event click{}; click.type = SDL_EVENT_MOUSE_BUTTON_DOWN; click.button.button = SDL_BUTTON_LEFT;
            click.button.x = home.layout.left + home.layout.content - 30; click.button.y = home.layout.gap * 2 + home.layout.header_height + home.layout.toolbar_height / 2;
            home.event(click); home.draw(); // Switch the portrait view to list via its resized hit target.
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(seed.games()[6].favorite, "Resized list-view button and single-row navigation must work");
        }
        {
            SDL_SetWindowSize(window, 1200, 800);
            const auto profile = dir / "cached-search";
            fs::create_directories(profile);
            Library seed(profile / "library.sqlite3");
            for (const auto* title : {"Alpha", "Beta"}) {
                const auto path = profile / (std::string(title) + ".gbc");
                std::ofstream(path) << "search fixture";
                seed.add({0, path.string(), title, "gb", "test-core"});
            }
            const auto extension = reinterpret_cast<void(*)()>(trace_connection);
            require(sqlite3_auto_extension(extension) == SQLITE_OK, "Cannot trace Home's database connection");
            Home home(window, renderer, profile, "/unused");
            sqlite3_cancel_auto_extension(extension);
            const int initial_statements = sql_statements;
            const int initial_library_reads = full_library_reads;
            require(initial_statements > 0, "SQL tracing must observe the initial library load");
            key(home, SDL_SCANCODE_F);
            type(home, "b"); type(home, "e");
            key(home, SDL_SCANCODE_BACKSPACE);
            key(home, SDL_SCANCODE_A, SDL_KMOD_CTRL);
            type(home, "no matches");
            key(home, SDL_SCANCODE_A, SDL_KMOD_CTRL);
            type(home, "AlPhA");
            home.draw();
            key(home, SDL_SCANCODE_RETURN); // Finish text entry.
            home.select_system("gb");
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_UP);
            require(sql_statements == initial_statements, "Search and sidebar filtering must not query SQLite");
            key(home, SDL_SCANCODE_RIGHT); key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN); // Favorite the filtered result.
            const auto games = seed.games();
            require(games[0].title == "Alpha" && games[0].favorite && !games[1].favorite,
                    "Cached case-insensitive search must select the matching game");
            key(home, SDL_SCANCODE_ESCAPE);
            const int statements_after_edit = sql_statements;
            key(home, SDL_SCANCODE_F); key(home, SDL_SCANCODE_A, SDL_KMOD_CTRL);
            key(home, SDL_SCANCODE_RETURN);
            home.draw();
            require(sql_statements == statements_after_edit, "Clearing search after a library edit must use the refreshed cache");
            // Rename Alpha past Beta, then select the first card. Cached ordering
            // must match SQLite, while selection follows the renamed game's ID.
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_RETURN); key(home, SDL_SCANCODE_A, SDL_KMOD_CTRL);
            type(home, "Zulu"); key(home, SDL_SCANCODE_RETURN);
            for (int i = 0; i < 5; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            require(seed.game(games[0].id)->title == "Zulu", "Editing must update the selected cached entry");
            key(home, SDL_SCANCODE_ESCAPE); home.draw();
            SDL_Event click{}; click.type = SDL_EVENT_MOUSE_BUTTON_DOWN; click.button.button = SDL_BUTTON_LEFT;
            click.button.x = home.layout.left + home.layout.gap;
            click.button.y = home.layout.grid_top + home.layout.gap;
            home.event(click);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(seed.game(games[1].id)->favorite, "Renaming must reorder cached entries by title");
            key(home, SDL_SCANCODE_ESCAPE);
            home.import_file(profile / "Alpha.gbc");
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(!seed.game(games[0].id)->favorite, "Duplicate import must retain metadata and select the existing cached entry");
            key(home, SDL_SCANCODE_ESCAPE);
            const auto added_path = profile / "Added.gbc";
            std::ofstream(added_path) << "new cached entry";
            home.import_file(added_path);
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(seed.games().size() == 2 && fs::exists(added_path), "Removing a newly cached game must preserve its file");
            require(full_library_reads == initial_library_reads,
                    "Favorite, edit, import, duplicate import and removal must not reload the full library");
        }
        SDL_SetWindowSize(window, 1200, 800);
        for (const std::string system : {"psx", "saturn"}) {
            const auto profile = dir / ("bios-" + system); fs::create_directories(profile);
            const auto image = profile / "game.cue"; std::ofstream(image) << "fixture";
            Home home(window, renderer, profile, "/unused");
            home.select_system(system);
            home.import_file(image);
            key(home, SDL_SCANCODE_DOWN);
            for (int i = 0; i < 3; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN); home.draw();
            Library inspect(profile / "library.sqlite3");
            require(inspect.games().empty(), "Missing BIOS did not prompt before import");
            key(home, SDL_SCANCODE_ESCAPE); home.draw();
            require(inspect.games().empty(), "Cancelling BIOS setup imported the game");
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(inspect.games().size() == 1 && inspect.games()[0].system == system, "Skipping BIOS setup did not finish import");
            home.draw();
            for (int i = 0; i < 5; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN); home.draw();
            key(home, SDL_SCANCODE_ESCAPE);
            const auto invalid = profile / "fake-bios.bin";
            { std::ofstream file(invalid, std::ios::binary); std::string zeros(512 * 1024, 0); file.write(zeros.data(), zeros.size()); }
            bool rejected = false;
            try { install_bios(system, invalid, profile / "system"); } catch (const std::exception&) { rejected = true; }
            require(rejected && !has_bios(system, profile / "system"), "Random 512 KiB file accepted as a BIOS");
        }
        {
            const auto profile = dir / "zip-ui";
            fs::create_directories(profile);
            const auto zip = profile / "single.zip";
            write_zip(zip, {{"folder/Inside.gbc", "ROM fixture"}, {"Other.gba", "different system"}, {"readme.txt", "ignored"}});
            Home home(window, renderer, profile, "/unused");
            home.select_system("gb");
            home.import_file(zip);
            for (int i=0;i<100;++i) { home.tick(); SDL_Delay(1); }
            home.draw();
            for (int i=0;i<4;++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            Library inspect(profile / "library.sqlite3");
            require(inspect.games().size() == 1 && inspect.games()[0].title == "Inside" && inspect.games()[0].archive_member == "folder/Inside.gbc", "Single ROM ZIP did not auto-select and import");
            write_zip(profile / "multiple.zip", {{"First.gbc", "one"}, {"Second.gbc", "two"}});
            home.import_file(profile / "multiple.zip");
            for (int i=0;i<100;++i) { home.tick(); SDL_Delay(1); }
            home.draw(); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            for (int i=0;i<4;++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            require(inspect.games().size() == 2 && inspect.games()[1].archive_member == "Second.gbc", "ZIP member chooser did not retain selection");
        }
        // Exercise the same edit/confirm flow used after file chooser selection.
        for (const auto* filename : {"PSP test.cso", "PSP test.CSO", "PSP test.chd", "PSP God of War - Chains of Olympus (USA).cso"}) {
            const auto profile = dir / filename;
            fs::create_directories(profile);
            const auto disc = profile / filename;
            std::ofstream(disc) << "disc fixture";
            // Sparse fixture: large path-based discs must not inherit the ZIP
            // extraction limit or require reading the image during import.
            if (std::string_view(filename).starts_with("PSP God")) fs::resize_file(disc, 1314994625);
            Home home(window, renderer, profile, "/bin/false");
            Library inspect(profile / "library.sqlite3");
            home.select_system("psp"); home.import_file(disc);
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            const auto games = inspect.games();
            require(games.size() == 1 && games[0].system == "psp" && games[0].path == fs::canonical(disc).string() &&
                games[0].core == bundled_core(systems()[system_index("psp")]).string(), "PSP compressed disc import failed");
            const auto id = games[0].id;
            home.import_file(disc);
            for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            require(inspect.games().size() == 1 && inspect.games()[0].id == id, "PSP reimport did not select the existing entry");
            fs::remove(disc);
        }
        {
            const auto profile = dir / "boxart-profile";
            const auto images = dir / "boxart-images";
            fs::create_directories(images);
            const auto source = images / "Cover.PNG";
            auto* image = SDL_CreateSurface(24, 40, SDL_PIXELFORMAT_RGBA32);
            require(image, SDL_GetError());
            require(SDL_FillSurfaceRect(image, nullptr, SDL_MapSurfaceRGBA(image, 190, 30, 20, 255)), SDL_GetError());
            require(SDL_SavePNG(image, source.c_str()), SDL_GetError());
            SDL_DestroySurface(image);
            std::ofstream(images / "not-an-image.txt") << "Ignore this file";
            Library seed(profile / "library.sqlite3");
            const auto id = seed.add({0, rom.string(), "Box-art fixture", "gb", bundled_core(systems()[system_index("gb")]).string()});
            const auto game = *seed.game(id);
            const auto owned = profile / "boxart" / (game.save_id + ".png");
            save_storage::write(profile / "browser.conf", {{"boxart", images.string()}});
            Home home(window, renderer, profile, "/unused");
            home.select_system("gb");
            key(home, SDL_SCANCODE_RIGHT); key(home, SDL_SCANCODE_RETURN);
            auto select_image = [&]
            {
                key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
                for (int i = 0; i < 4; ++i) key(home, SDL_SCANCODE_DOWN);
                key(home, SDL_SCANCODE_RETURN);
                for (int i = 0; i < 100; ++i) { home.tick(); SDL_Delay(1); }
                key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
                home.draw();
            };
            select_image();
            auto* preview = SDL_RenderReadPixels(renderer, nullptr);
            require(preview && SDL_SaveBMP(preview, (dir / "boxart-edit.bmp").c_str()), SDL_GetError());
            SDL_DestroySurface(preview);
            require(!fs::exists(owned), "Choosing box art saved an unconfirmed edit");
            key(home, SDL_SCANCODE_ESCAPE); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(!fs::exists(owned) && fs::exists(source), "Discarding box art changed stored or original files");
            select_image();
            fs::remove(source);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            require(fs::is_regular_file(owned), "Saving Edit Details did not copy box art into Cabinet data");
            require(seed.game(id)->save_id == game.save_id && seed.game(id)->title == game.title,
                    "Choosing box art changed the game's identity or metadata");
            key(home, SDL_SCANCODE_ESCAPE); home.draw();
            SDL_SetWindowSize(window, 640, 480);
            key(home, SDL_SCANCODE_RETURN); key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            home.draw();
            preview = SDL_RenderReadPixels(renderer, nullptr);
            require(preview && SDL_SaveBMP(preview, (dir / "boxart-edit-small.bmp").c_str()), SDL_GetError());
            SDL_DestroySurface(preview);
            SDL_SetWindowSize(window, 1200, 800);
        }
        {
            SDL_SetWindowSize(window, 1200, 800);
            const auto profile = dir / "text-edit-profile";
            Library seed(profile / "library.sqlite3");
            const auto id = seed.add({0, rom.string(), "Alpha βeta", "gb", bundled_core(systems()[system_index("gb")]).string()});
            Home home(window, renderer, profile, "/unused");
            home.select_system("gb");
            key(home, SDL_SCANCODE_RIGHT); key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_DOWN); key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_HOME); type(home, "New ");
            key(home, SDL_SCANCODE_END); key(home, SDL_SCANCODE_BACKSPACE, SDL_KMOD_CTRL);
            type(home, "世界");
            key(home, SDL_SCANCODE_HOME); key(home, SDL_SCANCODE_RIGHT, SDL_KMOD_CTRL);
            key(home, SDL_SCANCODE_DELETE, SDL_KMOD_CTRL);
            home.draw();
            auto* screen = SDL_RenderReadPixels(renderer, nullptr);
            require(screen && SDL_SaveBMP(screen, (dir / "text-cursor-title.bmp").c_str()), SDL_GetError());
            SDL_DestroySurface(screen);
            key(home, SDL_SCANCODE_RETURN);
            for (int i = 0; i < 5; ++i) key(home, SDL_SCANCODE_DOWN);
            key(home, SDL_SCANCODE_RETURN);
            require(seed.game(id)->title == "New 世界", "Title cursor editing did not save the expected Unicode text");
            key(home, SDL_SCANCODE_ESCAPE);
            key(home, SDL_SCANCODE_F); type(home, "abcdef");
            for (int i = 0; i < 3; ++i) key(home, SDL_SCANCODE_LEFT);
            type(home, "X");
            auto copied = [&]
            {
                key(home, SDL_SCANCODE_A, SDL_KMOD_CTRL);
                key(home, SDL_SCANCODE_C, SDL_KMOD_CTRL);
                auto* value = SDL_GetClipboardText();
                require(value, SDL_GetError());
                const std::string result(value);
                SDL_free(value);
                return result;
            };
            require(copied() == "abcXdef", "Search did not insert at its cursor");
            key(home, SDL_SCANCODE_HOME);
            home.draw();
            TextRenderer measure(renderer);
            const float left = home.layout.left + home.layout.gap;
            const float y = home.layout.gap * 2 + home.layout.header_height + home.layout.toolbar_height / 2;
            auto click = [&](float x)
            {
                SDL_Event event{};
                event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.x = x; event.button.y = y;
                home.event(event);
            };
            click(left + measure.width("abcX", home.layout.body));
            type(home, "!");
            require(copied() == "abcX!def", "Mouse click did not position the search cursor");
            key(home, SDL_SCANCODE_HOME);
            home.draw();
            click(left);
            SDL_Event motion{}; motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.x = left + measure.width("abc", home.layout.body); motion.motion.y = y;
            home.event(motion);
            SDL_Event release{}; release.type = SDL_EVENT_MOUSE_BUTTON_UP; release.button.button = SDL_BUTTON_LEFT;
            home.event(release);
            key(home, SDL_SCANCODE_X, SDL_KMOD_CTRL);
            require(copied() == "X!def", "Dragging did not select text for cutting");
            const std::string long_query(200, 'x');
            type(home, long_query.c_str());
            key(home, SDL_SCANCODE_END);
            home.draw();
            key(home, SDL_SCANCODE_HOME);
            home.draw();
            click(left); home.event(release);
            type(home, "start");
            require(copied() == "start" + long_query, "Long-field scrolling did not track the cursor");
            key(home, SDL_SCANCODE_END);
            for (int i = 0; i < 5; ++i) key(home, SDL_SCANCODE_LEFT, SDL_KMOD_SHIFT);
            home.draw();
            screen = SDL_RenderReadPixels(renderer, nullptr);
            require(screen, SDL_GetError());
            bool highlighted = false;
            const int right = static_cast<int>(home.layout.width - home.layout.gap * 3 - home.layout.toolbar_height);
            for (int x = right - 80; x < right - 5; ++x)
            {
                SDL_Color color{};
                require(SDL_ReadSurfacePixel(screen, x, static_cast<int>(y),
                                             &color.r, &color.g, &color.b, &color.a), SDL_GetError());
                highlighted = highlighted || (color.r == accent.r && color.g == accent.g && color.b == accent.b);
            }
            require(highlighted, "A short selection at the end of a long field must remain visible");
            require(screen && SDL_SaveBMP(screen, (dir / "text-cursor-search.bmp").c_str()), SDL_GetError());
            SDL_DestroySurface(screen);
        }
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        std::cout << "Home import, edit, favorite, duplicate, and removal UI checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; SDL_Quit(); return 1; }
}
