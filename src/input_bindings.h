#pragma once
#include <cstdint>
#include <cstddef>
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

struct InputBinding
{
    int target; // RetroPad button 0..15, then negative/positive halves of LX, LY, RX, RY.
    std::string name;
    int pad = 0; // 0 = none, 1 + SDL button, 100 + 2 * SDL axis + positive.
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
    std::vector<int> extra_pads{}, extra_keys{};
};
struct InputSnapshot
{
    std::array<std::int16_t, 16> buttons{};
    std::array<std::int16_t, 16> pressure{};
    std::array<std::int16_t, 4> axes{};
    std::int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;
};
class InputBindings
{
public:
    InputBindings();
    void configure(const std::string& system, const std::filesystem::path& directory, int wii_type = 0);
    void configure_hotkeys(const std::string& system, const std::filesystem::path& directory);
    void select_wii_controller(int type);
    void assign(std::size_t row, bool keyboard, int source);
    void add(std::size_t row, bool keyboard, int source);
    void reset();
    void copy_to(const std::filesystem::path& directory);
    // mouse_buttons is an SDL button bitmask from SDL_GetMouseState.
    InputSnapshot sample(SDL_Gamepad* pad, const bool* keys, std::uint32_t mouse_buttons = 0, int stick_deadzone = 5,
                         int trigger_deadzone = 5) const;
    const std::vector<InputBinding>& rows() const
    {
        return bindings;
    }
    const std::string& system() const
    {
        return system_id;
    }
    static std::string identify(const std::string& rom, const std::string& core);
    static bool valid_system(const std::string& id);
    static int axis_source(SDL_GamepadAxis axis, bool positive)
    {
        return 100 + 2 * axis + positive;
    }
    static std::string source_name(int source);
    // Keyboard binding sources include normal scancodes and mouse buttons
    // encoded as mouse_source(SDL_BUTTON_*).
    static constexpr int mouse_source_base = 1000;
    static int mouse_source(int button)
    {
        return mouse_source_base + button;
    }
    static bool is_mouse_source(int source)
    {
        return source >= mouse_source_base && source < mouse_source_base + 16;
    }
    static std::string key_name(int source);
    static int pad_value(SDL_Gamepad* pad, int source, int stick_deadzone = 5, int trigger_deadzone = 5);
    static int capture(SDL_Gamepad* pad);

private:
    std::string system_id = "generic";
    std::filesystem::path path;
    std::vector<InputBinding> bindings, defaults;
    bool digital_stick = true;
    int controller_type = 0;
    std::string profile_name() const;
    void save(const std::vector<InputBinding>& values) const;
    void load();
};
