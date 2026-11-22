#pragma once
#include <string>

namespace aspect_ratio
{
enum Mode
{
    Auto,
    Wide,
    Standard,
    SquareSNES,
    Stretch,
    Count
};
inline const char* name(int mode)
{
    static constexpr const char* names[]{"Auto", "16:9", "4:3", "8:7", "Stretch to fit"};
    return names[(mode >= 0 && mode < Count) ? mode : Auto];
}
inline float value(int mode, float automatic)
{
    switch (mode)
    {
    case Wide:
        return 16.0f / 9;
    case Standard:
        return 4.0f / 3;
    case SquareSNES:
        return 8.0f / 7;
    default:
        return automatic;
    }
}
inline int parse(const std::string& text)
{
    if (text == "auto")
    {
        return Auto;
    }
    if (text == "16:9")
    {
        return Wide;
    }
    if (text == "4:3")
    {
        return Standard;
    }
    if (text == "8:7")
    {
        return SquareSNES;
    }
    if (text == "stretch")
    {
        return Stretch;
    }
    return -1;
}
}
