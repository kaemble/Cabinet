#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

inline std::optional<std::int64_t> parse_playtime(std::string_view text)
{
    std::int64_t seconds = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), seconds);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || seconds < 0)
    {
        return {};
    }
    return seconds;
}

struct Playtime
{
    std::int64_t seconds = 0;
    std::uint64_t last_tick = 0;
    bool active = false;

    void update(std::uint64_t now, bool playing)
    {
        if (!active && !playing)
        {
            return;
        }
        if (active && now >= last_tick)
        {
            const auto additional = (now - last_tick) / 1000;
            const auto room = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - seconds);
            seconds += static_cast<std::int64_t>((additional > room) ? room : additional);
            last_tick += additional * 1000;
        }
        if (!active || !playing)
        {
            last_tick = now;
        }
        active = playing;
    }
};
