#pragma once
namespace wii_controller
{
inline constexpr int count = 4;
inline constexpr const char* labels[]{"Wii Remote + Nunchuck", "Sideways Wii Remote", "Classic Controller",
                                      "GameCube Controller"};
// Match the descriptors advertised by Dolphin rather than assuming device IDs.
inline constexpr const char* descriptors[]{"WiiMote + Nunchuk", "WiiMote (sideways)", "WiiMote + Classic Controller",
                                           "GameCube Controller"};
}
