#pragma once
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

enum class HomeLogLevel
{
    Off,
    Error,
    Warn,
    Info,
    Debug
};

inline HomeLogLevel parse_home_log_level(std::string_view value)
{
    if (value == "off")
    {
        return HomeLogLevel::Off;
    }
    if (value == "error")
    {
        return HomeLogLevel::Error;
    }
    if (value == "warn")
    {
        return HomeLogLevel::Warn;
    }
    if (value == "info")
    {
        return HomeLogLevel::Info;
    }
    if (value == "debug")
    {
        return HomeLogLevel::Debug;
    }
    throw std::runtime_error("--log-level must be off, error, warn, info, or debug");
}

struct HomeLogger
{
    HomeLogLevel level = HomeLogLevel::Warn;
    std::ostream* output = &std::cout;
    void write(HomeLogLevel severity, const std::string& message) const
    {
        if (severity == HomeLogLevel::Off || severity > level)
        {
            return;
        }
        const char* names[]{"off", "error", "warn", "info", "debug"};
        *output << "[cabinet " << names[static_cast<int>(severity)] << "] " << message << std::endl;
    }
};

// POSIX shell notation for display/copying only; SDL receives the original argv.
inline std::string shell_argument(const std::string& value)
{
    if (!value.empty() &&
        value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_@%+=:,./-") ==
            std::string::npos)
    {
        return value;
    }
    std::string result = "'";
    for (char c : value)
    {
        if (c == '\'')
        {
            result += "'\\''";
        }
        else
        {
            result += c;
        }
    }
    return result + "'";
}
inline std::string shell_command(const std::vector<std::string>& args)
{
    std::string result;
    for (const auto& arg : args)
    {
        if (!result.empty())
        {
            result += ' ';
        }
        result += shell_argument(arg);
    }
    return result;
}
