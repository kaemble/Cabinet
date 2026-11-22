#include "../src/runner.cpp"
#include <cstring>
#include <array>
#include "test_support.h"
#include <iostream>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static uint64_t value = 0;
static int reject_loads = 0;
static bool reject_save = false;
static size_t state_size() { return sizeof(value); }
static bool serialize(void* data, size_t size) {
    if (reject_save || size != sizeof(value)) return false;
    std::memcpy(data, &value, size); return true;
}
static bool unserialize(const void* data, size_t size) {
    if (size != sizeof(value)) return false;
    std::memcpy(&value, data, size);
    if (reject_loads > 0) { --reject_loads; value = 0xbad; return false; }
    return true;
}
template<class F> static void rejects(F operation, const std::string& message) {
    try { operation(); } catch (const std::exception& error) {
        require(std::string(error.what()).find(message) != std::string::npos, error.what()); return;
    }
    throw std::runtime_error("Invalid state operation succeeded: " + message);
}
int main() {
    try {
        TestDirectory directory("cabinet-state-failures");
        require(SDL_Init(0), SDL_GetError());
        {
            Session s;
            s.saves.directory = directory.path.string();
            s.core.api.serialize_size = state_size; s.core.api.serialize = serialize; s.core.api.unserialize = unserialize;
            s.saves.state_path = directory.path / "slot.state";
            s.saves.metadata = {{"core_hash", "build-a"}};
            value = 42; save_state(s);
            const auto original = read_content(s.saves.state_path);
            value = 99;
            s.core.api.serialize_size = []() -> size_t { return 0; };
            rejects([&] { save_state(s); }, "resume the game");
            s.core.api.serialize_size = []() -> size_t { return SaveManager::max_state_size + 1; };
            rejects([&] { save_state(s); }, "512 MiB limit");
            s.core.api.serialize_size = state_size;
            require(read_content(s.saves.state_path) == original, "Unavailable/oversized state damaged the saved slot");
            s.saves.metadata["core_hash"] = "build-b";
            rejects([&] { load_state(s); }, "different core binary");
            require(value == 99, "Wrong-core state mutated emulation");
            s.saves.metadata["core_hash"] = "build-a";

            // An interrupted writer's temporary file must never become a load candidate.
            std::ofstream(s.saves.state_path.string() + ".tmp") << "CABST001partial";
            load_state(s); require(value == 42, "Load trusted an unfinished temporary state");
            fs::remove(s.saves.state_path.string() + ".tmp");
            for (const std::string corrupt : {std::string("CABST001"), std::string("BADMAGICpayload"), std::string("CABST001abcd")}) {
                { std::ofstream out(s.saves.state_path, std::ios::binary | std::ios::trunc); out << corrupt; }
                value = 99;
                rejects([&] { load_state(s); }, corrupt.size() == 8 ? "Invalid save state" : corrupt.starts_with("BADMAGIC") ? "header" : "progress restored");
                require(value == 99, "Corrupt/truncated state damaged current progress");
            }
            { std::ofstream out(s.saves.state_path, std::ios::binary | std::ios::trunc); out.write(original.data(), original.size()); }
            value = 99; reject_loads = 1;
            rejects([&] { load_state(s); }, "progress restored");
            require(value == 99, "Partially mutating load did not roll back");
            reject_loads = 2;
            rejects([&] { execute_action(s, Action::Load); }, "reset the game");
            require(s.frontend.paused(), "Failed rollback left damaged emulation running");

            reject_save = true;
            rejects([&] { save_state(s); }, "could not save state");
            require(read_content(s.saves.state_path) == original, "Failed serialization replaced the previous state");
            reject_save = false;
            // A directory at the staging path deterministically simulates an
            // unwritable destination, even when the tests run as root.
            const auto staging = fs::path(s.saves.state_path.string() + ".tmp");
            fs::create_directory(staging); std::ofstream(staging / "keep") << "sentinel";
            rejects([&] { save_state(s); }, "write save state");
            require(read_content(s.saves.state_path) == original, "Failed staging write replaced the previous state");
            fs::remove_all(staging);
            const auto slot = s.saves.state_path;
            s.saves.state_path = slot / "blocked.state";
            rejects([&] { save_state(s); }, "");
            require(read_content(slot) == original, "Invalid parent damaged the existing state");
            s.saves.state_path = directory.path / "directory.state";
            fs::create_directory(s.saves.state_path); std::ofstream(s.saves.state_path / "keep") << "sentinel";
            rejects([&] { save_state(s); }, "");
            require(fs::exists(s.saves.state_path / "keep") && !fs::exists(s.saves.state_path.string() + ".tmp"),
                "Failed commit destroyed destination or leaked a staging file");
        }
        {
            SaveManager saves;
            saves.directory = (directory.path / "backups").string();
            saves.battery_path = fs::path(saves.directory) / "game";
            std::array<char, 4> ram{}, rtc{};
            // Memory accessors cannot capture; point them at local test regions.
            static char* regions[2]; regions[0] = ram.data(); regions[1] = rtc.data();
            SaveCore core{
                [](unsigned region) -> void* { return regions[region == RETRO_MEMORY_RTC]; },
                [](unsigned) -> size_t { return 4; }, state_size, serialize, unserialize};
            const auto ram_file = fs::path(saves.battery_path.string() + ".srm");
            const auto rtc_file = fs::path(saves.battery_path.string() + ".rtc");
            saves.battery(core, false);
            const auto initial_ram = read_content(ram_file);
            require(!fs::exists(ram_file.string() + ".bak"), "First save created a redundant backup");
            ram[0] = 1; saves.battery(core, false);
            require(read_content(ram_file.string() + ".bak") == initial_ram, "Battery backup did not retain previous complete RAM");
            saves.battery(core, false);
            require(read_content(ram_file.string() + ".bak") == initial_ram &&
                !fs::exists(rtc_file.string() + ".bak"), "Unchanged autosave rotated a backup");
            rtc[0] = 2; saves.battery(core, false);
            require(read_content(rtc_file.string() + ".bak") == initial_ram, "RTC backup missing");
            const auto blocker = fs::path(ram_file.string() + ".bak.tmp");
            fs::create_directory(blocker); std::ofstream(blocker / "keep") << "sentinel";
            ram[0] = 3;
            rejects([&] { saves.battery(core, false); }, "");
            require(read_content(ram_file)[0] == 1 && read_content(ram_file.string() + ".bak") == initial_ram,
                "Failed backup damaged the live save or previous backup");
            fs::remove_all(blocker); saves.battery(core, false);
            require(read_content(ram_file)[0] == 3 && read_content(ram_file.string() + ".bak")[0] == 1,
                "Failed battery write was incorrectly marked saved");

            saves.metadata = {{"core_hash", "build-a"}};
            saves.playtime.seconds = 1; saves.write_playtime();
            saves.playtime.seconds = 2; saves.write_playtime();
            saves.write_playtime();
            const auto manifest = fs::path(saves.directory) / "cabinet-game.txt";
            require(save_storage::read(manifest).at("playtime") == "2" &&
                save_storage::read(manifest.string() + ".bak").at("playtime") == "1",
                "Unchanged playtime rotated its backup");
            saves.state_path = fs::path(saves.directory) / "slot.state";
            value = 1; saves.save_state(core);
            value = 2; saves.save_state(core);
            value = 3; saves.save_state(core);
            saves.load_state(core, saves.state_path.string() + ".bak");
            require(value == 2, "Save-state backup did not contain the previous complete state");
            saves.metadata["core_hash"] = "build-b";
            rejects([&] { saves.load_state(core, saves.state_path.string() + ".bak"); }, "different core binary");
            require(!fs::exists(saves.state_path.string() + ".bak.tmp"), "Completed backup left temporary files");
        }
        SDL_Quit();
        std::cout << "Corruption, core mismatch, rollback, interrupted writes, and failed commits passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; SDL_Quit(); return 1; }
}
