#include "firmware.h"
#include "firmware_fixture.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        const auto directory = fs::temp_directory_path() /
            ("cabinet-firmware-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        const auto file = directory / "dump.rom", system = directory / "system";
        require(requires_bios("ps2") && requires_bios("psx") && requires_bios("saturn") && !requires_bios("gba"),
                "BIOS setup system list is incorrect");
        require(!has_bios("ps2", system), "Missing PS2 BIOS directory was accepted");
        for (const auto region : {'J', 'A', 'E', 'H', 'C', 'P'})
        {
            auto bytes = firmware_fixture::ps2();
            bytes[firmware_fixture::romver + 4] = region;
            firmware_fixture::write(file, bytes);
            const auto bios = identify_bios("ps2", file);
            require(bios && !bios->region.empty(), "Console PS2 BIOS metadata was rejected");
            require(fs::path(bios->filename).parent_path() == "pcsx2/bios" &&
                        fs::path(bios->filename).filename() == bios->digest + ".bin",
                    "PS2 BIOS destination is not stable and confined to the core's BIOS folder");
            install_bios("ps2", file, system);
            require(has_bios("ps2", system) && fs::is_regular_file(system / bios->filename),
                    "PS2 BIOS installation or installed detection failed");
            const auto installed = identify_bios("ps2", system / bios->filename);
            require(installed && installed->digest == bios->digest,
                    "Installed PS2 BIOS bytes changed");
            require(install_bios("ps2", file, system).find("already installed") != std::string::npos,
                    "Repeated BIOS installation did not reuse the identical dump");
            require(!identify_bios("psx", file) && !identify_bios("saturn", file),
                    "PS2 dump was accepted for another system");
        }
        require(std::distance(fs::directory_iterator(system / "pcsx2/bios"), fs::directory_iterator{}) == 6,
                "Installing another PS2 region replaced an earlier BIOS");
        require(!fs::exists(system / "scph5500.bin") && !fs::exists(system / "scph5502.bin"),
                "A region-free PS2 dump created PS1 BIOS aliases");
        const auto bios = identify_bios("ps2", file);
        firmware_fixture::write(system / bios->filename, firmware_fixture::ps2());
        install_bios("ps2", file, system);
        const auto repaired = identify_bios("ps2", system / bios->filename);
        require(repaired && repaired->digest == bios->digest, "An incorrectly named BIOS prevented the selected dump from installing");
        const auto manual = directory / "manual";
        fs::create_directories(manual / "pcsx2/bios");
        fs::copy_file(file, manual / "pcsx2/bios/custom-name.rom");
        require(has_bios("ps2", manual), "Valid manually installed BIOS with an arbitrary filename was ignored");
        firmware_fixture::write(file, firmware_fixture::ps2("0220AC20050620", 8 * 1024 * 1024));
        require(identify_bios("ps2", file).has_value(), "An 8 MiB PS2 dump was rejected");
        for (const auto version : {"0220AC20050230", "0220AC2005xx20", "0000AC20050620", "0220QC20050620",
                                   "0100TZ20000306"})
        {
            firmware_fixture::write(file, firmware_fixture::ps2(version));
            require(!identify_bios("ps2", file), "Invalid ROMVER or arcade BIOS metadata was accepted");
        }
        for (unsigned mutation = 0; mutation < 7; ++mutation)
        {
            auto bytes = firmware_fixture::ps2();
            if (mutation == 0)
            {
                bytes.resize(4 * 1024 * 1024 - 1);
            }
            if (mutation == 1)
            {
                bytes.resize(8 * 1024 * 1024 + 1);
            }
            if (mutation == 2)
            {
                std::fill(bytes.begin(), bytes.end(), 0);
            }
            if (mutation == 3)
            {
                std::fill_n(bytes.begin() + firmware_fixture::romdir + 3 * 16 + 12, 4, 255);
            }
            if (mutation == 4)
            {
                bytes[firmware_fixture::romdir + 3 * 16] = 'X';
            }
            if (mutation == 5)
            {
                bytes[firmware_fixture::romdir + 4 * 16] = 'X';
            }
            if (mutation == 6)
            {
                constexpr std::string_view stamp = "20111012-094243";
                std::copy(stamp.begin(), stamp.end(), bytes.begin() + firmware_fixture::extinfo + 16);
            }
            firmware_fixture::write(file, bytes);
            require(!identify_bios("ps2", file), "Malformed, truncated, or non-PS2 dump was accepted");
            bool rejected = false;
            try
            {
                install_bios("ps2", file, directory / "rejected");
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            require(rejected && !fs::exists(directory / "rejected"), "Rejected BIOS created an installation");
        }
        fs::remove_all(directory);
        std::cout << "BIOS validation, installation, and detection passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
