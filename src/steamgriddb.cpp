#include "steamgriddb.h"
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
const std::string api_url = "https://www.steamgriddb.com/api/v2";
constexpr std::size_t maximum_response = 32 * 1024 * 1024;

std::string get(const std::string& url, const std::string& key)
{
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK)
    {
        throw std::runtime_error("Cannot initialize SteamGridDB networking");
    }
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
    {
        throw std::runtime_error("Cannot create SteamGridDB request");
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        key.empty() ? nullptr : curl_slist_append(nullptr, ("Authorization: Bearer " + key).c_str()), curl_slist_free_all);
    if (!key.empty() && !headers)
    {
        throw std::runtime_error("Cannot authorize SteamGridDB request");
    }
    std::string response;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "Cabinet/1.0");
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, +[](char* data, std::size_t size, std::size_t count, void* target) -> std::size_t
    {
        auto& result = *static_cast<std::string*>(target);
        const auto bytes = size * count;
        if (bytes > maximum_response - result.size())
        {
            return 0;
        }
        try
        {
            result.append(data, bytes);
            return bytes;
        }
        catch (...)
        {
            return 0;
        }
    });
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    const auto status = curl_easy_perform(curl.get());
    long code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
    if (code == 401 || code == 403)
    {
        throw std::runtime_error("SteamGridDB rejected the API key. Update it in the Cabinet menu.");
    }
    if (status != CURLE_OK || code != 200)
    {
        throw std::runtime_error("SteamGridDB request failed. Check your connection and try again.");
    }
    return response;
}

nlohmann::json data(const std::string& response)
{
    const auto result = nlohmann::json::parse(response, nullptr, false);
    if (!result.is_object() || !result.contains("success") || result["success"] != true ||
        !result.contains("data") || !result["data"].is_array())
    {
        throw std::runtime_error("SteamGridDB returned an invalid response");
    }
    return result["data"];
}

std::string encode(const std::string& title)
{
    const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : title)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            result += static_cast<char>(c);
        }
        else
        {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
}

SteamGridDB::SteamGridDB(std::string api_key, Request request)
    : api_key(std::move(api_key)), request(request ? std::move(request) : get)
{
    if (this->api_key.empty() || !std::all_of(this->api_key.begin(), this->api_key.end(), [](unsigned char c)
                                          {
                                              return std::isalnum(c);
                                          }))
    {
        throw std::runtime_error("Add a SteamGridDB API key in the Cabinet menu first.");
    }
}

std::vector<SteamGridGame> SteamGridDB::search(const std::string& title) const
{
    if (title.empty())
    {
        throw std::runtime_error("Enter a game title to search SteamGridDB");
    }
    std::vector<SteamGridGame> games;
    for (const auto& game : data(request(api_url + "/search/autocomplete/" + encode(title), api_key)))
    {
        if (game.is_object() && game.contains("id") && game["id"].is_number_integer() &&
            game["id"].get<std::int64_t>() > 0 && game.contains("name") && game["name"].is_string())
        {
            games.push_back({game["id"].get<std::int64_t>(), game["name"].get<std::string>()});
        }
    }
    return games;
}

std::string SteamGridDB::image(std::int64_t game_id) const
{
    const auto endpoint = api_url + "/grids/game/" + std::to_string(game_id) + "?types=static&mimes=image/png,image/jpeg";
    for (const auto& filter : {"&dimensions=512x512,1024x1024&styles=alternate",
                               "&dimensions=512x512,1024x1024", ""})
    {
        for (const auto& grid : data(request(endpoint + filter, api_key)))
        {
            if (!grid.is_object() || !grid.contains("url") || !grid["url"].is_string())
            {
                continue;
            }
            const auto url = grid["url"].get<std::string>();
            if (!url.starts_with("https://"))
            {
                continue;
            }
            // Artwork lives on a CDN and must never receive the API credential.
            return request(url, {});
        }
    }
    throw std::runtime_error("No supported box art found on SteamGridDB");
}

std::string SteamGridDB::import_image(const std::string& title) const
{
    const auto games = search(title);
    if (games.empty())
    {
        throw std::runtime_error("No games found on SteamGridDB for " + title);
    }
    const auto exact = std::find_if(games.begin(), games.end(), [&](const SteamGridGame& game)
    {
        return SDL_strcasecmp(game.name.c_str(), title.c_str()) == 0;
    });
    return image((exact == games.end() ? games.front() : *exact).id);
}
