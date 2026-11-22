#include "boxart.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <csetjmp>
#include <jpeglib.h>
#include <stdexcept>

namespace
{
using Image = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

Image fit_image(Image image, int maximum)
{
    const float scale = std::min(1.0f, static_cast<float>(maximum) / std::max(image->w, image->h));
    if (scale >= 1)
    {
        return image;
    }
    Image resized(SDL_CreateSurface(std::max(1, static_cast<int>(image->w * scale)),
                                    std::max(1, static_cast<int>(image->h * scale)), SDL_PIXELFORMAT_RGBA32),
                  SDL_DestroySurface);
    SDL_SetSurfaceBlendMode(image.get(), SDL_BLENDMODE_NONE);
    if (!resized || !SDL_BlitSurfaceScaled(image.get(), nullptr, resized.get(), nullptr, SDL_SCALEMODE_LINEAR))
    {
        throw std::runtime_error(SDL_GetError());
    }
    return resized;
}

struct JpegFailure
{
    jpeg_error_mgr manager{};
    std::jmp_buf failure;
    char message[JMSG_LENGTH_MAX]{};
};

struct JpegImage
{
    jpeg_decompress_struct decoder{};
    JpegFailure errors;
    FILE* input = nullptr;
    SDL_Surface* surface = nullptr;
    ~JpegImage()
    {
        jpeg_destroy_decompress(&decoder);
        if (input)
        {
            std::fclose(input);
        }
        SDL_DestroySurface(surface);
    }
};

SDL_Surface* load_jpeg(const std::filesystem::path& path)
{
    // Decoder state lives on the heap so libjpeg's longjmp cannot invalidate it.
    auto image = std::make_unique<JpegImage>();
    image->decoder.err = jpeg_std_error(&image->errors.manager);
    image->errors.manager.error_exit = [](j_common_ptr decoder)
    {
        auto* error = reinterpret_cast<JpegFailure*>(decoder->err);
        decoder->err->format_message(decoder, error->message);
        std::longjmp(error->failure, 1);
    };
    if (setjmp(image->errors.failure))
    {
        throw std::runtime_error(std::string("Could not read JPEG: ") + image->errors.message);
    }
    jpeg_create_decompress(&image->decoder);
    image->input = std::fopen(path.c_str(), "rb");
    if (!image->input)
    {
        throw std::runtime_error("Could not open box-art image");
    }
    jpeg_stdio_src(&image->decoder, image->input);
    jpeg_read_header(&image->decoder, TRUE);
    if (image->decoder.image_width > 8192 || image->decoder.image_height > 8192)
    {
        throw std::runtime_error("Box-art images must be at most 8192 pixels on each side");
    }
    image->decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&image->decoder);
    image->surface = SDL_CreateSurface(image->decoder.output_width, image->decoder.output_height, SDL_PIXELFORMAT_RGB24);
    if (!image->surface)
    {
        throw std::runtime_error(SDL_GetError());
    }
    while (image->decoder.output_scanline < image->decoder.output_height)
    {
        auto* row = static_cast<JSAMPLE*>(image->surface->pixels) +
                    image->decoder.output_scanline * image->surface->pitch;
        jpeg_read_scanlines(&image->decoder, &row, 1);
    }
    jpeg_finish_decompress(&image->decoder);
    auto* surface = image->surface;
    image->surface = nullptr;
    return surface;
}

std::string extension(const std::filesystem::path& path)
{
    auto result = path.extension().string();
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}
}

bool boxart_image(const std::filesystem::path& path)
{
    const auto suffix = extension(path);
    return suffix == ".png" || suffix == ".jpg" || suffix == ".jpeg" || suffix == ".bmp";
}

BoxArt::BoxArt(SDL_Renderer* renderer, const std::filesystem::path& data)
    : renderer(renderer), directory(data / "boxart")
{
}

std::filesystem::path BoxArt::file(const std::string& game_id) const
{
    if (game_id.empty() || !std::all_of(game_id.begin(), game_id.end(), [](unsigned char c)
                                    {
                                        return std::isalnum(c) || c == '-' || c == '_';
                                    }))
    {
        throw std::runtime_error("Invalid game identity for box art");
    }
    return directory / (game_id + ".png");
}

void BoxArt::select(const std::filesystem::path& source)
{
    if (!boxart_image(source))
    {
        throw std::runtime_error("Choose a PNG, JPEG, or BMP image");
    }
    const auto suffix = extension(source);
    Surface image(suffix == ".png" ? SDL_LoadPNG(source.c_str())
                  : suffix == ".bmp" ? SDL_LoadBMP(source.c_str()) : load_jpeg(source), SDL_DestroySurface);
    if (!image)
    {
        throw std::runtime_error(std::string("Could not read box-art image: ") + SDL_GetError());
    }
    if (image->w <= 0 || image->h <= 0 || image->w > 8192 || image->h > 8192)
    {
        throw std::runtime_error("Box-art images must be at most 8192 pixels on each side");
    }
    image = fit_image(std::move(image), 1024);
    Texture texture(SDL_CreateTextureFromSurface(renderer, image.get()), SDL_DestroyTexture);
    if (!texture)
    {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
    selected = std::move(image);
    preview_texture = std::move(texture);
}

void BoxArt::save(const std::string& game_id)
{
    if (!selected)
    {
        return;
    }
    const auto destination = file(game_id);
    const auto temporary = std::filesystem::path(destination.string() + ".tmp");
    std::filesystem::create_directories(directory);
    try
    {
        if (!SDL_SavePNG(selected.get(), temporary.c_str()))
        {
            throw std::runtime_error(std::string("Could not save box art: ") + SDL_GetError());
        }
        std::filesystem::rename(temporary, destination);
    }
    catch (...)
    {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
    textures.erase(game_id);
    reset();
}

void BoxArt::reset()
{
    selected.reset();
    preview_texture.reset();
}

bool BoxArt::pending() const
{
    return selected != nullptr;
}

bool BoxArt::draw(const std::string& game_id, const SDL_FRect& area, bool preview)
{
    SDL_Texture* texture = preview ? preview_texture.get() : nullptr;
    if (!texture && !game_id.empty())
    {
        auto found = textures.find(game_id);
        if (found == textures.end())
        {
            if (textures.size() >= 64)
            {
                textures.clear();
            }
            Surface image(SDL_LoadPNG(file(game_id).c_str()), SDL_DestroySurface);
            if (image)
            {
                image = fit_image(std::move(image), 512);
            }
            Texture loaded(image ? SDL_CreateTextureFromSurface(renderer, image.get()) : nullptr, SDL_DestroyTexture);
            if (loaded)
            {
                SDL_SetTextureScaleMode(loaded.get(), SDL_SCALEMODE_LINEAR);
            }
            found = textures.emplace(game_id, std::move(loaded)).first;
        }
        texture = found->second.get();
    }
    if (!texture)
    {
        return false;
    }
    const float scale = std::min(area.w / texture->w, area.h / texture->h);
    const SDL_FRect destination{area.x + (area.w - texture->w * scale) / 2,
                               area.y + (area.h - texture->h * scale) / 2, texture->w * scale, texture->h * scale};
    SDL_RenderTexture(renderer, texture, nullptr, &destination);
    return true;
}
