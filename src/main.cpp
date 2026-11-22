#include <SDL3/SDL.h>
#include "home.h"
#include "rom_archive.h"
#include <string_view>
#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--internal-core-info")
    {
        return rom_archive::probe_core(argv[2]);
    }
    int result = 0;
    try
    {
        result = run_home(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Cabinet: " << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
