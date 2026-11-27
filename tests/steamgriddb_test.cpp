#include "home_window.h"
#include "save_storage.h"
#include <iostream>
#include "test_support.h"
#include <atomic>
#include <fstream>

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static void key(Home& home, SDL_Scancode code)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = code;
    home.event(event);
}

static void down(Home& home, int count)
{
    for (int i = 0; i < count; ++i)
    {
        key(home, SDL_SCANCODE_DOWN);
    }
}

static void settle(Home& home)
{
    for (int i = 0; i < 100; ++i)
    {
        home.tick();
        SDL_Delay(1);
    }
}

int main()
{
    try
    {
        int requests = 0;
        SteamGridDB client("testkey", [&](const std::string& url, const std::string& key)
        {
            ++requests;
            if (requests == 1)
            {
                require(url.ends_with("/search/autocomplete/Test%20%26%20Game"), "Search query was not encoded");
                require(key == "testkey", "Search did not use the configured key");
                return std::string(R"({"success":true,"data":[{"id":1,"name":"Wrong"},{"id":2,"name":"Test & Game"}]})");
            }
            if (requests == 2)
            {
                require(url.find("/grids/game/2?") != std::string::npos, "Exact title match was not preferred");
                require(url.ends_with("&dimensions=512x512,1024x1024&styles=alternate"), "Alternate square art was not preferred");
                return std::string(R"({"success":true,"data":[]})");
            }
            if (requests == 3)
            {
                require(url.ends_with("&dimensions=512x512,1024x1024"), "Square fallback was not used");
                return std::string(R"({"success":true,"data":[]})");
            }
            if (requests == 4)
            {
                require(url.ends_with("mimes=image/png,image/jpeg"), "Final fallback did not request supported formats");
                return std::string(R"({"success":true,"data":[{"url":"https://cdn.example/art.png"}]})");
            }
            require(key.empty() && url == "https://cdn.example/art.png", "CDN request leaked the API key");
            return std::string("image bytes");
        });
        require(client.import_image("Test & Game") == "image bytes" && requests == 5, "Fallback download failed");
        for (const auto* response : {"broken", R"({"success":false,"data":[]})", R"({"success":true,"data":{}})"})
        {
            bool rejected = false;
            try
            {
                SteamGridDB invalid("testkey", [response](const auto&, const auto&) { return std::string(response); });
                invalid.search("Game");
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            require(rejected, "Invalid API response was accepted");
        }
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        TestDirectory directory("cabinet-steamgriddb");
        const auto profile = directory.path / "profile";
        const auto rom = directory.path / "Imported Title.gbc";
        std::ofstream(rom) << "ROM fixture";
        auto* surface = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
        require(surface, SDL_GetError());
        const auto png = directory.path / "cover.png";
        require(SDL_SavePNG(surface, png.c_str()), SDL_GetError());
        SDL_DestroySurface(surface);
        std::ifstream input(png, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        auto* window = SDL_CreateWindow("SteamGridDB test", 1200, 800, SDL_WINDOW_HIDDEN);
        require(window, SDL_GetError());
        auto* renderer = SDL_CreateRenderer(window, nullptr);
        require(renderer, SDL_GetError());
        std::atomic<int> searches = 0;
        SteamGridDB::Request request = [&](const std::string& url, const std::string& key)
        {
            if (url.find("/search/autocomplete/") != std::string::npos)
            {
                require(url.ends_with("/Imported%20Title"), "Import did not use the game title as its query");
                require(key == "testkey", "Saved API key did not survive restart");
                ++searches;
                return std::string(R"({"success":true,"data":[{"id":123,"name":"Imported Title"}]})");
            }
            if (url.find("/grids/game/") != std::string::npos)
            {
                return std::string(R"({"success":true,"data":[{"url":"https://cdn.example/cover.png"}]})");
            }
            require(key.empty(), "Image request included credentials");
            return bytes;
        };
        {
            Home home(window, renderer, profile, "/unused", {}, request);
            key(home, SDL_SCANCODE_ESCAPE);
            down(home, 3);
            key(home, SDL_SCANCODE_RETURN);
            key(home, SDL_SCANCODE_RETURN);
            SDL_Event event{};
            event.type = SDL_EVENT_TEXT_INPUT;
            event.text.text = "testkey";
            home.event(event);
            home.draw();
            key(home, SDL_SCANCODE_RETURN);
            down(home, 1);
            key(home, SDL_SCANCODE_RETURN);
            require(save_storage::read(profile / "steamgriddb/key.conf").at("api_key") == "testkey", "Menu did not save the key");
            require((fs::status(profile / "steamgriddb").permissions() & fs::perms::others_all) == fs::perms::none,
                    "Credentials directory allows access to other users");
        }
        {
            Home home(window, renderer, profile, "/unused", {}, request);
            home.select_system("gb");
            home.import_file(rom);
            settle(home);
            home.draw();
            require(searches == 1, "Import did not automatically fetch artwork");
            Library library(profile / "library.sqlite3");
            require(library.games().empty(), "Artwork fetch saved the unconfirmed import");
            down(home, 5);
            key(home, SDL_SCANCODE_RETURN);
            require(library.games().size() == 1, "Import did not save after artwork arrived");
            const auto owned = profile / "boxart" / (library.games()[0].save_id + ".png");
            require(fs::is_regular_file(owned), "Imported artwork was not persisted");
            // Reopen the picker and choose a game through the real UI handlers.
            down(home, 1);
            key(home, SDL_SCANCODE_RETURN);
            down(home, 5);
            key(home, SDL_SCANCODE_RETURN);
            settle(home);
            home.draw();
            key(home, SDL_SCANCODE_RETURN);
            settle(home);
            down(home, 1);
            key(home, SDL_SCANCODE_RETURN);
            require(searches == 2 && fs::is_regular_file(owned), "Manual game picker did not save artwork");
            // A cancelled import must not apply its late download to another entry.
            home.import_file(rom);
            home.tick();
            key(home, SDL_SCANCODE_ESCAPE);
            settle(home);
            require(library.games().size() == 1 && searches == 3, "Cancelled lookup changed the library");
            const auto other_rom = directory.path / "other" / rom.filename();
            fs::create_directories(other_rom.parent_path());
            std::ofstream(other_rom) << "Other ROM fixture";
            home.import_file(other_rom);
            down(home, 4);
            key(home, SDL_SCANCODE_RETURN); // Cancel the queued lookup.
            settle(home);
            down(home, 1);
            key(home, SDL_SCANCODE_RETURN);
            require(library.games().size() == 2 && searches == 3, "Cancelling a queued lookup still sent a search");
            const auto games = library.games();
            const auto other = std::find_if(games.begin(), games.end(), [&](const Game& game) { return game.path == other_rom; });
            require(other != games.end() && !fs::exists(profile / "boxart" / (other->save_id + ".png")),
                    "Cancelled artwork leaked into another import");
            key(home, SDL_SCANCODE_ESCAPE);
            key(home, SDL_SCANCODE_ESCAPE);
            down(home, 3);
            key(home, SDL_SCANCODE_RETURN);
            down(home, 2);
            key(home, SDL_SCANCODE_RETURN);
            require(save_storage::read(profile / "steamgriddb/key.conf").at("api_key").empty(), "Menu did not remove the API key");
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "SteamGridDB preference, fallback, credentials and automatic import passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
