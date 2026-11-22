#include "runner_options.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <exception>

int main(int argc, char** argv)
{
    int result = 0;
    try
    {
        auto options = parse_runner_options(argc, argv);
        if (options.help)
        {
            print_runner_help(std::cout);
            return 0;
        }
        result = run_game(options);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Cabinet: " << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
