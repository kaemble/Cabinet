#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SteamGridGame
{
    std::int64_t id;
    std::string name;
};

class SteamGridDB
{
public:
    using Request = std::function<std::string(const std::string&, const std::string&)>;
    explicit SteamGridDB(std::string api_key, Request request = {});
    std::vector<SteamGridGame> search(const std::string& title) const;
    std::string image(std::int64_t game_id) const;
    std::string import_image(const std::string& title) const;

private:
    std::string api_key;
    Request request;
};
