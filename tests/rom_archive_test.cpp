#include "rom_archive.h"
#include "library.h"
#include "archive_fixture.h"
#include "save_storage.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
#include <chrono>
namespace fs = std::filesystem;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> static void rejects(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } require(caught, "Invalid archive operation succeeded"); }
int main(int argc, char** argv) {
 try {
    require(argc == 4, "Expected scratch directory, memory core and path core"); require(SDL_Init(0), SDL_GetError());
    const auto dir = fs::path(argv[1]) / ("archive-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir); const auto zip = dir / "collection.ZIP";
    write_zip(zip, {{"folder/First.gbc", "ROM fixture"}, {"Second.gba", "Other ROM"}, {"readme.txt", "ignored"}});
    require(rom_archive::members(zip).size() == 2, "ZIP candidate filtering failed");
    auto extracted = rom_archive::extract(zip, "folder/First.gbc");
    require(fs::is_regular_file(extracted / "First.gbc") && fs::file_size(extracted / "First.gbc") == 11, "ZIP extraction changed name or bytes");
    require(!fs::exists(extracted / "readme.txt"), "Unselected file extracted"); fs::remove_all(extracted);
    rejects([&]{rom_archive::extract(zip,"missing.gbc");});
    write_zip(dir / "unsafe.zip", {{"../escape.gbc", "bad"}});
    rejects([&]{rom_archive::members(dir / "unsafe.zip");});
    write_zip(dir / "duplicate.zip", {{"same.gbc", "one"}, {"same.gbc", "two"}});
    rejects([&]{rom_archive::members(dir / "duplicate.zip");});
    std::ofstream(dir / "corrupt.zip") << "not a ZIP";
    rejects([&]{rom_archive::members(dir / "corrupt.zip");});
    write_zip(dir / "checksum.zip", {{"Bad.gbc", std::string(65536, 'x')}});
    {
        std::fstream file(dir / "checksum.zip", std::ios::binary | std::ios::in | std::ios::out);
        unsigned char header[30]{}; file.read(reinterpret_cast<char*>(header), 30);
        const int offset = 30 + header[26] + 256 * header[27] + header[28] + 256 * header[29];
        file.seekg(offset + 4); char byte = 0; file.read(&byte, 1); byte ^= 0x55;
        file.seekp(offset + 4); file.write(&byte, 1);
    }
    rejects([&]{auto temporary = rom_archive::extract(dir / "checksum.zip", "Bad.gbc"); fs::remove_all(temporary);});
    Library library(dir / "library.sqlite3");
    Game game{0, zip.string(), "First", "gb", argv[2]};
    game.archive_member = "folder/First.gbc"; const auto first = library.add(game);
    require(library.add(game) == first, "Repeated member import duplicated game");
    game.archive_member = "Second.gba"; game.title = "Second"; game.system = "gba";
    require(library.add(game) != first, "Two members share library identity");
    auto saved = library.games().front(); require(saved.save_name == "First", "Save folder used archive name");
    const auto identity = saved.save_id;
    fs::rename(zip, dir / "moved.zip"); saved.path = (dir / "moved.zip").string(); library.update(saved);
    require(library.games().front().save_id == identity && library.games().front().archive_member == "folder/First.gbc", "Relocation lost member or save identity");
    fs::rename(saved.path, dir / "offline.zip");
    saved.title = "First renamed";
    library.update(saved);
    library.set_favorite(saved.id, true);
    require(library.games().front().title == saved.title && library.games().front().favorite,
            "Metadata edits and favorite toggles must work without the archive");
    auto changed = saved; changed.archive_member = "Missing.gbc";
    rejects([&]{library.update(changed);});
    changed = saved; changed.core = "/missing/core.so";
    rejects([&]{library.update(changed);});
    changed = saved; changed.path = (dir / "missing.zip").string();
    rejects([&]{library.update(changed);});
    fs::rename(dir / "offline.zip", saved.path);
    rejects([&]{rom_archive::validate_core(argv[3], dir / "ineligible-core-info", "psp");});
    const auto sega_zip = dir / "sega.zip";
    write_zip(sega_zip, {{"Genesis.md", "cartridge"}, {"Master.sms", "cartridge"}, {"Gear.gg", "cartridge"}});
    for (const auto& [system, member] : {std::pair{"md", "Genesis.md"}, {"sms", "Master.sms"}, {"gg", "Gear.gg"}}) {
        Game sega{0, sega_zip.string(), member, system, argv[2]};
        sega.archive_member = member;
        sega.id = library.add(sega);
        require(sega.id > 0, "Sega ZIP cartridge was not imported");
        library.update(sega);
    }
    rejects([&]{rom_archive::validate_core(argv[3], dir / "ineligible-core-info", "psp");});
    const auto custom = dir / "custom.so";
    fs::copy_file(argv[2], custom);
    rom_archive::validate_core(custom, dir / "core-info", "gb");
    rom_archive::validate_core(custom, dir / "core-info", "gb");
    require(std::distance(fs::directory_iterator(dir / "core-info"), fs::directory_iterator{}) == 1, "Custom core result not cached");
    std::ofstream(custom, std::ios::app) << "different binary hash";
    rom_archive::validate_core(custom, dir / "core-info", "gb");
    require(std::distance(fs::directory_iterator(dir / "core-info"), fs::directory_iterator{}) == 2, "Changed binary did not invalidate cached eligibility");
    fs::remove_all(dir); SDL_Quit(); std::cout << "ZIP import, extraction, identity, and eligibility passed\n"; return 0;
 } catch (const std::exception& error) { std::cerr << error.what() << '\n'; SDL_Quit(); return 1; }
}
