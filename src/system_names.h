#pragma once
#include <string>
#include <string_view>

inline std::string system_display_name(std::string_view id)
{
    if (id == "gb")
    {
        return "Game Boy/Color";
    }
    if (id == "dc")
    {
        return "Sega Dreamcast";
    }
    if (id == "gba")
    {
        return "Game Boy Advance";
    }
    if (id == "wii")
    {
        return "Nintendo Wii";
    }
    if (id == "gc")
    {
        return "GameCube";
    }
    if (id == "gg")
    {
        return "Game Gear";
    }
    if (id == "nes")
    {
        return "Nintendo Entertainment System";
    }
    if (id == "n64")
    {
        return "Nintendo 64";
    }
    if (id == "3ds")
    {
        return "Nintendo 3DS";
    }
    if (id == "nds")
    {
        return "Nintendo DS";
    }
    if (id == "md")
    {
        return "Sega Genesis/Mega Drive";
    }
    if (id == "sms")
    {
        return "Sega Master System";
    }
    if (id == "saturn")
    {
        return "Sega Saturn";
    }
    if (id == "psp")
    {
        return "Sony PSP";
    }
    if (id == "psx")
    {
        return "Sony PlayStation";
    }
    if (id == "ps2")
    {
        return "Sony PlayStation 2";
    }
    if (id == "snes")
    {
        return "Super Nintendo";
    }
    if (id == "pce")
    {
        return "TurboGrafx-16/PC Engine";
    }
    return "Generic";
}
