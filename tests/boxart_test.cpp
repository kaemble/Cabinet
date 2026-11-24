#include "boxart.h"
#include "test_support.h"
#include <cstdio>
#include <jpeglib.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        TestDirectory directory("cabinet-boxart");
        auto* window = SDL_CreateWindow("Box-art test", 80, 80, SDL_WINDOW_HIDDEN);
        require(window, SDL_GetError());
        auto* renderer = SDL_CreateRenderer(window, nullptr);
        require(renderer, SDL_GetError());
        {
            BoxArt art(renderer, directory.path);
            const std::string id = "abc123";
            const auto png = directory.path / "Original.PNG";
            const auto bmp = directory.path / "Replacement.BMP";
            const auto jpeg = directory.path / "Photo.JPEG";
            auto* image = SDL_CreateSurface(32, 16, SDL_PIXELFORMAT_RGBA32);
            require(image, SDL_GetError());
            SDL_FillSurfaceRect(image, nullptr, SDL_MapSurfaceRGBA(image, 220, 0, 0, 255));
            require(SDL_SavePNG(image, png.c_str()), SDL_GetError());
            SDL_FillSurfaceRect(image, nullptr, SDL_MapSurfaceRGBA(image, 0, 0, 220, 255));
            require(SDL_SaveBMP(image, bmp.c_str()), SDL_GetError());
            SDL_DestroySurface(image);
            jpeg_compress_struct compressor{};
            jpeg_error_mgr errors;
            compressor.err = jpeg_std_error(&errors);
            jpeg_create_compress(&compressor);
            auto* output = std::fopen(jpeg.c_str(), "wb");
            require(output, "Cannot create JPEG fixture");
            jpeg_stdio_dest(&compressor, output);
            compressor.image_width = 8;
            compressor.image_height = 8;
            compressor.input_components = 3;
            compressor.in_color_space = JCS_RGB;
            jpeg_set_defaults(&compressor);
            jpeg_start_compress(&compressor, TRUE);
            JSAMPLE row[24]{};
            for (int x = 0; x < 8; ++x)
            {
                row[x * 3 + 1] = 220;
            }
            while (compressor.next_scanline < compressor.image_height)
            {
                JSAMPROW pixels = row;
                jpeg_write_scanlines(&compressor, &pixels, 1);
            }
            jpeg_finish_compress(&compressor);
            jpeg_destroy_compress(&compressor);
            std::fclose(output);
            const SDL_FRect area{0, 0, 80, 80};
            auto pixel = [&](BoxArt& images, bool preview, int x, int y)
            {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                require(images.draw(id, area, preview), "Box art did not render");
                auto* surface = SDL_RenderReadPixels(renderer, nullptr);
                require(surface, SDL_GetError());
                SDL_Color color{};
                require(SDL_ReadSurfacePixel(surface, x, y, &color.r, &color.g, &color.b, &color.a), SDL_GetError());
                SDL_DestroySurface(surface);
                return color;
            };
            require(!art.draw(id, area), "Missing art should use the placeholder");
            art.select(png);
            require(art.pending() && !fs::exists(art.file(id)), "Selection persisted before confirmation");
            require(pixel(art, true, 40, 40).r == 220, "Preview did not use the selected image");
            require(pixel(art, true, 40, 5).r == 0, "Box art was stretched instead of preserving its aspect ratio");
            art.save(id);
            require(fs::exists(png) && fs::is_regular_file(art.file(id)), "Owned copy or original image missing");
            require(!art.pending() && pixel(art, false, 40, 40).r == 220, "Saved image did not invalidate the placeholder cache");
            std::ifstream jpeg_input(jpeg, std::ios::binary);
            const std::string jpeg_bytes{std::istreambuf_iterator<char>(jpeg_input), std::istreambuf_iterator<char>()};
            art.select_data(jpeg_bytes);
            require(pixel(art, true, 40, 40).g > 210, "Downloaded JPEG data did not decode");
            art.select(jpeg);
            require(pixel(art, true, 40, 40).g > 210, "JPEG preview did not decode");
            art.reset();
            require(!art.pending() && pixel(art, false, 40, 40).r == 220, "Cancelled edit replaced existing box art");
            art.select(bmp);
            art.save(id);
            require(pixel(art, false, 40, 40).b == 220, "Replacement left a stale cached image");
            fs::remove(bmp);
            BoxArt reopened(renderer, directory.path);
            require(pixel(reopened, false, 40, 40).b == 220, "Box art depended on the original file or Home instance");
            for (const auto suffix : {".png", ".jpg", ".bmp", ".txt"})
            {
                const auto invalid = directory.path / (std::string("Invalid") + suffix);
                std::ofstream(invalid) << "This is not an image";
                bool rejected = false;
                try
                {
                    art.select(invalid);
                }
                catch (const std::exception&)
                {
                    rejected = true;
                }
                require(rejected && !art.pending(), "Invalid image was accepted or changed the draft");
            }
            require(pixel(art, false, 40, 40).b == 220, "Invalid selection damaged the stored image");
            const auto blocked = directory.path / "blocked";
            fs::create_directories(blocked);
            std::ofstream(blocked / "boxart") << "Not a directory";
            BoxArt unwritable(renderer, blocked);
            unwritable.select(png);
            bool failed = false;
            try
            {
                unwritable.save(id);
            }
            catch (const std::exception&)
            {
                failed = true;
            }
            require(failed && unwritable.pending() && fs::exists(png), "Failed copy lost the draft or original image");
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "Box-art formats, staging, copies, replacement and rendering passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
