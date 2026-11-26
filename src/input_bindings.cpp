#include "input_bindings.h"
#include <cstdint>
#include <cstddef>
#include <libretro.h>
#include <algorithm>
#include <map>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace
{
const std::vector<std::string> ids{"generic", "3ds", "wii", "dc",  "gb",     "gba", "gc",  "gg",   "nes",
                                   "n64",     "nds", "md",  "sms", "saturn", "psp", "psx", "ps2", "snes", "pce"};
std::string lower(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}
bool valid_pad(int source)
{
    return source == 0 ||
           (source > 0 && source <= SDL_GAMEPAD_BUTTON_COUNT && source != 1 + SDL_GAMEPAD_BUTTON_GUIDE) ||
           (source >= 100 && source < 100 + SDL_GAMEPAD_AXIS_COUNT * 2 &&
            ((source - 100) / 2 < SDL_GAMEPAD_AXIS_LEFT_TRIGGER || (source & 1)));
}
bool valid_key(int key)
{
    if (InputBindings::is_mouse_source(key))
    {
        return true;
    }
    return key >= 0 && key < SDL_SCANCODE_COUNT && key != SDL_SCANCODE_F11 && key != SDL_SCANCODE_ESCAPE &&
           key != SDL_SCANCODE_DELETE;
}
}
std::int16_t InputSnapshot::state(unsigned port, unsigned device, unsigned index, unsigned id) const
{
    if (port)
    {
        return 0;
    }
    device &= RETRO_DEVICE_MASK;
    if (device == RETRO_DEVICE_JOYPAD && index == 0)
    {
        if (id < 16)
        {
            return buttons[id];
        }
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        {
            unsigned mask = 0;
            for (unsigned i = 0; i < 16; ++i)
            {
                if (buttons[i])
                {
                    mask |= 1u << i;
                }
            }
            return static_cast<std::int16_t>(mask);
        }
    }
    if (device == RETRO_DEVICE_ANALOG)
    {
        if (index < 2 && id < 2)
        {
            return axes[index * 2 + id];
        }
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON && id < 16)
        {
            return pressure[id];
        }
    }
    return 0;
}
bool InputBindings::valid_system(const std::string& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
std::string InputBindings::identify(const std::string& rom, const std::string& core)
{
    const auto core_name = lower(std::filesystem::path(core).stem().string());
    if (core_name == "pcee2_libretro" || core_name == "armsx2_libretro")
    {
        return "ps2";
    }
    auto ext = lower(std::filesystem::path(rom).extension().string());
    const std::vector<std::pair<std::string, std::string>> extensions{
        {".wbfs", "wii"},  {".wad", "wii"},  {".gdi", "dc"},   {".cdi", "dc"},   {".gb", "gb"},    {".gbc", "gb"},
        {".gba", "gba"},   {".nes", "nes"},  {".fds", "nes"},  {".z64", "n64"},  {".n64", "n64"},  {".v64", "n64"},
        {".3ds", "3ds"},   {".3dsx", "3ds"}, {".app", "3ds"},  {".axf", "3ds"},  {".cci", "3ds"},  {".cxi", "3ds"},
        {".z3dsx", "3ds"}, {".zcci", "3ds"}, {".zcxi", "3ds"}, {".nds", "nds"},  {".gg", "gg"},    {".sms", "sms"},
        {".md", "md"},     {".gen", "md"},   {".smd", "md"},   {".smc", "snes"}, {".sfc", "snes"}, {".pce", "pce"},
        {".gcm", "gc"},    {".rvz", "gc"},   {".cso", "psp"}};
    for (const auto& [suffix, id] : extensions)
    {
        if (suffix == ext)
        {
            return id;
        }
    }
    auto name = lower(std::filesystem::path(core).stem().string());
    const std::vector<std::pair<std::string, std::string>> cores{
        {"flycast", "dc"},    {"gambatte", "gb"},            {"mgba", "gba"},   {"dolphin", "gc"},
        {"blastem", "md"},    {"fceumm", "nes"},             {"azahar", "3ds"}, {"mupen64plus_next", "n64"},
        {"melondsds", "nds"}, {"mednafen_saturn", "saturn"}, {"ppsspp", "psp"}, {"swanstation", "psx"},
        {"bsnes", "snes"},    {"mednafen_pce", "pce"}};
    for (const auto& [prefix, id] : cores)
    {
        if (name == prefix + "_libretro")
        {
            return id;
        }
    }
    return "generic";
}
InputBindings::InputBindings()
{
    configure("generic", {});
}
std::string InputBindings::profile_name() const
{
    return system_id == "wii" ? "wii-" + std::to_string(controller_type) + ".conf" : system_id + ".conf";
}
void InputBindings::select_wii_controller(int type)
{
    if (system_id == "wii" && controller_type != type)
    {
        configure("wii", path.empty() ? std::filesystem::path{} : path.parent_path(), type);
    }
}
void InputBindings::configure(const std::string& system, const std::filesystem::path& directory, int wii_type)
{
    if (wii_type < 0 || wii_type > 3)
    {
        throw std::runtime_error("Invalid Wii controller");
    }
    const std::string id = (system == "wii" && wii_type == 3) ? "gc" : system;
    controller_type = wii_type;
    if (!valid_system(id))
    {
        throw std::runtime_error("Unknown input system: " + id);
    }
    system_id = system;
    path = directory.empty() ? std::filesystem::path{} : directory / profile_name();
    bindings.clear();
    digital_stick = id != "generic" && id != "3ds" && id != "wii" && id != "dc" && id != "n64" && id != "gc" &&
                    id != "psp" && id != "psx" && id != "ps2";
    auto add = [&](int target, const std::string& name, SDL_GamepadButton pad, SDL_Scancode key)
    {
        bindings.push_back({target, name, (pad == SDL_GAMEPAD_BUTTON_INVALID) ? 0 : 1 + pad, key});
    };
    auto axis_button = [&](int target, const std::string& name, SDL_GamepadAxis axis, SDL_Scancode key)
    {
        bindings.push_back({target, name, axis_source(axis, true), key});
    };
    add(RETRO_DEVICE_ID_JOYPAD_UP, "D-pad Up", SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_SCANCODE_UP);
    add(RETRO_DEVICE_ID_JOYPAD_DOWN, "D-pad Down", SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_SCANCODE_DOWN);
    add(RETRO_DEVICE_ID_JOYPAD_LEFT, "D-pad Left", SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_SCANCODE_LEFT);
    add(RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-pad Right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_SCANCODE_RIGHT);
    add(RETRO_DEVICE_ID_JOYPAD_START, "Start", SDL_GAMEPAD_BUTTON_START, SDL_SCANCODE_RETURN);
    if (id != "dc" && id != "n64" && id != "gc" && id != "gg" && id != "sms")
    {
        add(RETRO_DEVICE_ID_JOYPAD_SELECT, (id == "md" || id == "saturn") ? "Mode" : "Select", SDL_GAMEPAD_BUTTON_BACK,
            SDL_SCANCODE_BACKSPACE);
    }
    if (id == "dc")
    {
        add(RETRO_DEVICE_ID_JOYPAD_L, "C", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_SCANCODE_F);
        add(RETRO_DEVICE_ID_JOYPAD_R, "Z", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_SCANCODE_H);
        add(RETRO_DEVICE_ID_JOYPAD_SELECT, "D", SDL_GAMEPAD_BUTTON_BACK, SDL_SCANCODE_BACKSPACE);
        add(RETRO_DEVICE_ID_JOYPAD_B, "A", SDL_GAMEPAD_BUTTON_SOUTH, SDL_SCANCODE_X);
        add(RETRO_DEVICE_ID_JOYPAD_A, "B", SDL_GAMEPAD_BUTTON_EAST, SDL_SCANCODE_Z);
        add(RETRO_DEVICE_ID_JOYPAD_Y, "X", SDL_GAMEPAD_BUTTON_WEST, SDL_SCANCODE_C);
        add(RETRO_DEVICE_ID_JOYPAD_X, "Y", SDL_GAMEPAD_BUTTON_NORTH, SDL_SCANCODE_V);
        axis_button(RETRO_DEVICE_ID_JOYPAD_L2, "L trigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_SCANCODE_Q);
        axis_button(RETRO_DEVICE_ID_JOYPAD_R2, "R trigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_SCANCODE_E);
    }
    else if (id == "n64")
    {
        add(RETRO_DEVICE_ID_JOYPAD_B, "A", SDL_GAMEPAD_BUTTON_SOUTH, SDL_SCANCODE_X);
        add(RETRO_DEVICE_ID_JOYPAD_Y, "B", SDL_GAMEPAD_BUTTON_WEST, SDL_SCANCODE_Z);
        add(RETRO_DEVICE_ID_JOYPAD_L, "L", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_SCANCODE_Q);
        add(RETRO_DEVICE_ID_JOYPAD_R, "R", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_SCANCODE_E);
        axis_button(RETRO_DEVICE_ID_JOYPAD_L2, "Z", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_SCANCODE_SPACE);
    }
    else if (id == "md" || id == "saturn")
    {
        const bool md = id == "md";
        add(RETRO_DEVICE_ID_JOYPAD_B, "A", SDL_GAMEPAD_BUTTON_WEST, SDL_SCANCODE_Z);
        add(RETRO_DEVICE_ID_JOYPAD_A, "B", SDL_GAMEPAD_BUTTON_SOUTH, SDL_SCANCODE_X);
        add(RETRO_DEVICE_ID_JOYPAD_R, "C", SDL_GAMEPAD_BUTTON_EAST, SDL_SCANCODE_C);
        add(RETRO_DEVICE_ID_JOYPAD_Y, "X", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_SCANCODE_A);
        add(RETRO_DEVICE_ID_JOYPAD_X, "Y", SDL_GAMEPAD_BUTTON_NORTH, SDL_SCANCODE_S);
        add(RETRO_DEVICE_ID_JOYPAD_L, "Z", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_SCANCODE_D);
        if (!md)
        {
            axis_button(RETRO_DEVICE_ID_JOYPAD_L2, "L", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_SCANCODE_Q);
            axis_button(RETRO_DEVICE_ID_JOYPAD_R2, "R", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_SCANCODE_E);
        }
    }
    else if (id == "gg" || id == "sms")
    {
        add(RETRO_DEVICE_ID_JOYPAD_A, "1", SDL_GAMEPAD_BUTTON_SOUTH, SDL_SCANCODE_Z);
        add(RETRO_DEVICE_ID_JOYPAD_R, "2", SDL_GAMEPAD_BUTTON_EAST, SDL_SCANCODE_X);
    }
    else
    {
        const bool sony = id == "psx" || id == "ps2" || id == "psp";
        add(RETRO_DEVICE_ID_JOYPAD_A,
            sony          ? "Circle"
            : id == "pce" ? "I"
                          : "A",
            id == "gc" ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST, SDL_SCANCODE_X);
        add(RETRO_DEVICE_ID_JOYPAD_B,
            sony          ? "Cross"
            : id == "pce" ? "II"
                          : "B",
            id == "gc" ? SDL_GAMEPAD_BUTTON_WEST : SDL_GAMEPAD_BUTTON_SOUTH, SDL_SCANCODE_Z);
        if (id == "snes" || id == "nds" || id == "3ds" || id == "gc" || sony || id == "wii" || id == "generic" ||
            id == "pce")
        {
            add(RETRO_DEVICE_ID_JOYPAD_X,
                sony          ? "Triangle"
                : id == "pce" ? "IV"
                              : "X",
                id == "gc" ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_NORTH, SDL_SCANCODE_S);
            add(RETRO_DEVICE_ID_JOYPAD_Y,
                sony          ? "Square"
                : id == "pce" ? "III"
                              : "Y",
                id == "gc" ? SDL_GAMEPAD_BUTTON_NORTH : SDL_GAMEPAD_BUTTON_WEST, SDL_SCANCODE_A);
        }
        if (id == "gba" || id == "snes" || id == "nds" || id == "3ds" || sony || id == "wii" || id == "generic" ||
            id == "pce")
        {
            add(RETRO_DEVICE_ID_JOYPAD_L, id == "pce" ? "V" : "L", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_SCANCODE_Q);
            add(RETRO_DEVICE_ID_JOYPAD_R, id == "pce" ? "VI" : "R", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_SCANCODE_E);
        }
        if (id == "3ds" || id == "wii" || id == "gc" || id == "psx" || id == "ps2" || id == "generic")
        {
            axis_button(RETRO_DEVICE_ID_JOYPAD_L2,
                        id == "gc"    ? "L trigger"
                        : id == "3ds" ? "ZL"
                                      : "L2",
                        SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_SCANCODE_1);
            axis_button(RETRO_DEVICE_ID_JOYPAD_R2,
                        id == "gc"    ? "R trigger"
                        : id == "3ds" ? "ZR"
                                      : "R2",
                        SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_SCANCODE_3);
            if (id == "gc")
            {
                add(RETRO_DEVICE_ID_JOYPAD_R, "Z", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_SCANCODE_E);
            }
            else
            {
                add(RETRO_DEVICE_ID_JOYPAD_L3, id == "3ds" ? "Home / Swap screens" : "L3",
                    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_SCANCODE_F);
                add(RETRO_DEVICE_ID_JOYPAD_R3, id == "3ds" ? "Touchscreen press" : "R3", SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                    SDL_SCANCODE_H);
            }
        }
    }
    if (!digital_stick)
    {
        const SDL_Scancode keys[]{SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S,
                                  SDL_SCANCODE_J, SDL_SCANCODE_L, SDL_SCANCODE_I, SDL_SCANCODE_K};
        const char* directions[]{"Left", "Right", "Up", "Down"};
        for (int i = 0; i < (id == "psp" ? 4 : 8); ++i)
        {
            std::string name = (i < 4)       ? (id == "3ds" ? "Circle Pad " : "Stick ")
                               : id == "3ds" ? "C-stick / Pointer "
                               : id == "n64" ? "C "
                               : id == "gc"  ? "C-stick "
                                             : "Right stick ";
            bindings.push_back(
                {16 + i, name + directions[i % 4], axis_source(static_cast<SDL_GamepadAxis>(i / 2), i % 2), keys[i]});
        }
        // Keep the analog movement keys independent from face-button keys.
        for (auto& b : bindings)
        {
            if (b.target == 9)
            {
                b.key = SDL_SCANCODE_V;
            }
            if (b.target == 1 && id != "n64")
            {
                b.key = SDL_SCANCODE_C;
            }
        }
    }
    if (digital_stick)
    {
        const SDL_GamepadAxis axes[]{SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_LEFTX,
                                     SDL_GAMEPAD_AXIS_LEFTX};
        for (int i = 0; i < 4; ++i)
        {
            bindings[i].extra_pads.push_back(axis_source(axes[i], i % 2));
        }
    }
    if (id == "wii")
    {
        // DolphinLibretro/Input.cpp: attachment-specific RetroPad mappings.
        const std::map<int, std::string> nunchuk{{0, "B"},       {1, "Z"},     {2, "2"},
                                                 {3, "1"},       {8, "A"},     {9, "C"},
                                                 {10, "Minus"},  {11, "Plus"}, {12, "Nunchuck waggle"},
                                                 {13, "Waggle"}, {15, "Home"}};
        const std::map<int, std::string> sideways{{0, "1"}, {1, "B"}, {2, "Minus"},   {3, "Plus"},
                                                  {8, "2"}, {9, "A"}, {13, "Waggle"}, {15, "Home"}};
        const std::map<int, std::string> classic{{0, "B"},  {1, "Y"},  {2, "Minus"}, {3, "Plus"},
                                                 {8, "A"},  {9, "X"},  {10, "ZL"},   {11, "ZR"},
                                                 {12, "L"}, {13, "R"}, {15, "Home"}};
        const auto& names = (wii_type == 0) ? nunchuk : (wii_type == 1) ? sideways : classic;
        std::erase_if(bindings,
                      [&](const InputBinding& b)
                      {
                          if (b.target >= 16)
                          {
                              return wii_type == 1;
                          }
                          return !(b.target >= 4 && b.target <= 7) && !names.contains(b.target);
                      });
        for (auto& b : bindings)
        {
            if (names.contains(b.target))
            {
                b.name = names.at(b.target);
            }
            if (b.target >= 16 && wii_type == 0)
            {
                b.name = std::string((b.target < 20) ? "Nunchuck stick " : "Tilt ") +
                         std::array<const char*, 4>{"Left", "Right", "Up", "Down"}[(b.target - 16) % 4];
            }
        }
    }
    // Wii pointer play defaults to left → A and right → B. Other systems
    // keep mouse clicks available for touchscreen input and explicit rebinding.
    if (system_id == "wii")
    {
        for (auto& b : bindings)
        {
            if (b.name == "A")
            {
                b.extra_keys.push_back(mouse_source(SDL_BUTTON_LEFT));
            }
            else if (b.name == "B")
            {
                b.extra_keys.push_back(mouse_source(SDL_BUTTON_RIGHT));
            }
        }
    }
    defaults = bindings;
    load();
}
bool InputBindings::has_analog_input() const
{
    return std::any_of(defaults.begin(), defaults.end(),
                       [](const InputBinding& binding)
                       {
                           return binding.target >= 16 && binding.target < 24;
                       });
}
void InputBindings::configure_hotkeys(const std::string& system, const std::filesystem::path& directory)
{
    system_id = system + "-hotkeys";
    path = directory.empty() ? std::filesystem::path{} : directory / profile_name();
    bindings = {{0, "Save state", 0, SDL_SCANCODE_F5},
                {1, "Load state", 0, SDL_SCANCODE_F7},
                {2, "Fast forward", 0, SDL_SCANCODE_TAB}};
    defaults = bindings;
    load();
}
void InputBindings::load()
{
    if (path.empty() || !std::filesystem::exists(path))
    {
        return;
    }
    std::ifstream file(path);
    std::string magic;
    int version;
    if (!(file >> magic >> version) || magic != "cabinet-input" || version != 2)
    {
        throw std::runtime_error("Invalid input profile: " + path.string());
    }
    auto loaded = bindings;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }
        std::istringstream row(line);
        int target, pad, key;
        std::string extra;
        if (!(row >> target >> pad >> key) || !valid_pad(pad) || !valid_key(key))
        {
            throw std::runtime_error("Invalid binding in " + path.string());
        }
        std::vector<int> pads, keys;
        auto read_sources = [&](std::vector<int>& sources, bool keyboard)
        {
            int count;
            if (!(row >> count) || count < 0 || count > 128)
            {
                throw std::runtime_error("Invalid input binding count");
            }
            for (int i = 0, source; i < count; ++i)
            {
                if (!(row >> source) || !source || !(keyboard ? valid_key(source) : valid_pad(source)))
                {
                    throw std::runtime_error("Invalid additional input binding");
                }
                if (std::find(sources.begin(), sources.end(), source) == sources.end())
                {
                    sources.push_back(source);
                }
            }
        };
        read_sources(pads, false);
        read_sources(keys, true);
        if (row >> extra)
        {
            throw std::runtime_error("Invalid binding in " + path.string());
        }
        auto found = std::find_if(loaded.begin(), loaded.end(),
                                  [&](auto& b)
                                  {
                                      return b.target == target;
                                  });
        if (found != loaded.end())
        {
            found->pad = pad;
            found->key = static_cast<SDL_Scancode>(key);
            found->extra_pads = std::move(pads);
            found->extra_keys = std::move(keys);
        }
    }
    bindings = std::move(loaded);
}
void InputBindings::save(const std::vector<InputBinding>& values) const
{
    if (path.empty())
    {
        return;
    }
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += ".tmp";
    std::ofstream out(temp);
    out << "cabinet-input 2\n";
    for (const auto& b : values)
    {
        out << b.target << ' ' << b.pad << ' ' << b.key << ' ' << b.extra_pads.size();
        for (int source : b.extra_pads)
        {
            out << ' ' << source;
        }
        out << ' ' << b.extra_keys.size();
        for (int source : b.extra_keys)
        {
            out << ' ' << source;
        }
        out << '\n';
    }
    out.close();
    if (!out)
    {
        throw std::runtime_error("Cannot save input profile: " + path.string());
    }
    std::filesystem::rename(temp, path);
}
void InputBindings::assign(std::size_t row, bool keyboard, int source)
{
    if (row >= bindings.size() || !(keyboard ? valid_key(source) : valid_pad(source)))
    {
        throw std::runtime_error("Invalid input binding");
    }
    auto changed = bindings;
    if (keyboard)
    {
        changed[row].extra_keys.clear();
    }
    else
    {
        changed[row].extra_pads.clear();
    }
    const int old = keyboard ? changed[row].key : changed[row].pad;
    for (std::size_t i = 0; source && i < changed.size(); ++i)
    {
        if (i != row)
        {
            if (keyboard && changed[i].key == source)
            {
                changed[i].key = static_cast<SDL_Scancode>(old);
            }
            if (!keyboard && changed[i].pad == source)
            {
                changed[i].pad = old;
            }
        }
    }
    if (keyboard)
    {
        changed[row].key = static_cast<SDL_Scancode>(source);
    }
    else
    {
        changed[row].pad = source;
    }
    save(changed);
    bindings = std::move(changed);
}
void InputBindings::reset()
{
    save(defaults);
    bindings = defaults;
}
int InputBindings::pad_value(SDL_Gamepad* pad, int source, int stick_deadzone, int trigger_deadzone)
{
    if (!pad || !source)
    {
        return 0;
    }
    if (source < 100)
    {
        return SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(source - 1)) ? 32767 : 0;
    }
    int axis = (source - 100) / 2;
    int value = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(axis));
    value = std::clamp((source & 1) ? value : -value, 0, 32767);
    const int percent = (axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER) ? trigger_deadzone : stick_deadzone;
    const int deadzone = std::clamp(percent, 0, 30) * 32767 / 100;
    return (value <= deadzone) ? 0 : (value - deadzone) * 32767 / (32767 - deadzone);
}
static bool key_or_mouse_down(int source, const bool* keys, std::uint32_t mouse_buttons)
{
    if (!source)
    {
        return false;
    }
    if (InputBindings::is_mouse_source(source))
    {
        return (mouse_buttons & SDL_BUTTON_MASK(source - InputBindings::mouse_source_base)) != 0;
    }
    return keys && source > SDL_SCANCODE_UNKNOWN && source < SDL_SCANCODE_COUNT && keys[source];
}
InputSnapshot InputBindings::sample(SDL_Gamepad* pad, const bool* keys, std::uint32_t mouse_buttons, int stick_deadzone,
                                    int trigger_deadzone) const
{
    InputSnapshot out;
    for (const auto& b : bindings)
    {
        int value = key_or_mouse_down(b.key, keys, mouse_buttons)
                        ? 32767
                        : pad_value(pad, b.pad, stick_deadzone, trigger_deadzone);
        for (int source : b.extra_keys)
        {
            if (key_or_mouse_down(source, keys, mouse_buttons))
            {
                value = 32767;
            }
        }
        for (int source : b.extra_pads)
        {
            value = std::max(value, pad_value(pad, source, stick_deadzone, trigger_deadzone));
        }
        if (b.target < 16)
        {
            out.buttons[b.target] |= value > 16000;
            out.pressure[b.target] = std::max(out.pressure[b.target], static_cast<std::int16_t>(value));
        }
        else
        {
            const int axis = (b.target - 16) / 2;
            out.axes[axis] = static_cast<std::int16_t>(out.axes[axis] + ((b.target & 1) ? value : -value));
        }
    }
    return out;
}
std::string InputBindings::key_name(int source)
{
    if (!source)
    {
        return "Unbound";
    }
    if (is_mouse_source(source))
    {
        switch (source - mouse_source_base)
        {
        case SDL_BUTTON_LEFT:
            return "Left click";
        case SDL_BUTTON_MIDDLE:
            return "Middle click";
        case SDL_BUTTON_RIGHT:
            return "Right click";
        case SDL_BUTTON_X1:
            return "Mouse 4";
        case SDL_BUTTON_X2:
            return "Mouse 5";
        default:
            return "Mouse button";
        }
    }
    if (source > SDL_SCANCODE_UNKNOWN && source < SDL_SCANCODE_COUNT)
    {
        const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(source));
        if (name && name[0])
        {
            return name;
        }
    }
    return "Unknown";
}
int InputBindings::capture(SDL_Gamepad* pad)
{
    if (!pad)
    {
        return 0;
    }
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
    {
        if (b != SDL_GAMEPAD_BUTTON_GUIDE && SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b)))
        {
            return b + 1;
        }
    }
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
    {
        int value = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(a));
        if (value > 20000)
        {
            return axis_source(static_cast<SDL_GamepadAxis>(a), true);
        }
        if (value < -20000 && a < SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
        {
            return axis_source(static_cast<SDL_GamepadAxis>(a), false);
        }
    }
    return 0;
}
std::string InputBindings::source_name(int source)
{
    if (!source)
    {
        return "Unbound";
    }
    if (source < 100)
    {
        switch (source - 1)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return "South";
        case SDL_GAMEPAD_BUTTON_EAST:
            return "East";
        case SDL_GAMEPAD_BUTTON_WEST:
            return "West";
        case SDL_GAMEPAD_BUTTON_NORTH:
            return "North";
        case SDL_GAMEPAD_BUTTON_BACK:
            return "Select / View";
        case SDL_GAMEPAD_BUTTON_START:
            return "Start";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return "L3";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return "R3";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return "Left shoulder";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return "Right shoulder";
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return "D-pad Up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return "D-pad Down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return "D-pad Left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return "D-pad Right";
        default:
        {
            const char* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(source - 1));
            return name ? name : "Unknown";
        }
        }
    }
    const char* names[]{"Left stick X",  "Left stick Y", "Right stick X",
                        "Right stick Y", "Left trigger", "Right trigger"};
    const int axis = (source - 100) / 2;
    return std::string(names[axis]) + ((axis >= 4) ? "" : (source & 1) ? " +" : " -");
}

void InputBindings::copy_to(const std::filesystem::path& directory)
{
    const auto old_path = path;
    path = directory / profile_name();
    try
    {
        save(bindings);
        if (system_id == "wii" && !old_path.empty())
        {
            for (int type = 0; type < 4; ++type)
            {
                const auto source = old_path.parent_path() / ("wii-" + std::to_string(type) + ".conf");
                const auto destination = directory / source.filename();
                if (source != old_path && source != destination && std::filesystem::is_regular_file(source))
                {
                    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
    }
    catch (...)
    {
        path = old_path;
        throw;
    }
}

void InputBindings::add(std::size_t row, bool keyboard, int source)
{
    if (!source)
    {
        assign(row, keyboard, 0);
        return;
    }
    if (row >= bindings.size() || !(keyboard ? valid_key(source) : valid_pad(source)))
    {
        throw std::runtime_error("Invalid input binding");
    }
    auto changed = bindings;
    auto& binding = changed[row];
    auto& extras = keyboard ? binding.extra_keys : binding.extra_pads;
    const int previous = keyboard ? binding.key : binding.pad;
    if (source == previous || std::find(extras.begin(), extras.end(), source) != extras.end())
    {
        return;
    }
    if (extras.size() >= 128)
    {
        throw std::runtime_error("Too many bindings for this action");
    }
    if (previous)
    {
        extras.push_back(previous);
    }
    if (keyboard)
    {
        binding.key = static_cast<SDL_Scancode>(source);
    }
    else
    {
        binding.pad = source;
    }
    save(changed);
    bindings = std::move(changed);
}
