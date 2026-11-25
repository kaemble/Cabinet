#pragma once

#include "color_correction_definition.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace shaders
{
enum class Effect
{
    ColorCorrection
};

struct Shader
{
    Effect effect = Effect::ColorCorrection;
    bool enabled = false;
    std::array<int, color_parameters.size()> parameters = []
    {
        std::array<int, color_parameters.size()> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            result[i] = color_parameters[i].initial;
        }
        return result;
    }();
    bool operator==(const Shader&) const = default;
};

inline std::array<float, parameter_vector_count * 4> uniform_values(const Shader& shader)
{
    std::array<float, parameter_vector_count * 4> values{};
    for (std::size_t i = 0; i < shader.parameters.size(); ++i)
    {
        values[i] = shader.parameters[i] * color_parameters[i].scale;
    }
    return values;
}

inline const char* name(Effect)
{
    return color_name;
}

struct Stack
{
    std::vector<Shader> effects{Shader{}};
    bool operator==(const Stack&) const = default;
    bool active() const
    {
        return std::any_of(effects.begin(), effects.end(), [](const Shader& shader) { return shader.enabled; });
    }
    int toggle(int index)
    {
        auto shader = effects.at(index);
        shader.enabled = !shader.enabled;
        effects.erase(effects.begin() + index);
        const auto inactive =
            std::find_if(effects.begin(), effects.end(), [](const Shader& entry) { return !entry.enabled; });
        const int position = static_cast<int>(inactive - effects.begin());
        effects.insert(inactive, shader);
        return position;
    }
    int move(int index, int direction)
    {
        const int next = index + direction;
        if (next >= 0 && next < static_cast<int>(effects.size()) && effects[index].enabled == effects[next].enabled)
        {
            std::swap(effects[index], effects[next]);
            return next;
        }
        return index;
    }
    void write(std::ostream& out) const
    {
        for (const auto& shader : effects)
        {
            out << "shader " << std::quoted(color_id) << ' ' << shader.enabled;
            for (int value : shader.parameters)
            {
                out << ' ' << value;
            }
            out << '\n';
        }
    }
    void read(std::istream& in)
    {
        std::string id;
        int enabled;
        Shader shader;
        if (!(in >> std::quoted(id) >> enabled) || id != color_id || (enabled != 0 && enabled != 1))
        {
            return;
        }
        for (std::size_t i = 0; i < shader.parameters.size(); ++i)
        {
            int value;
            const auto& parameter = color_parameters[i];
            if (!(in >> value) || value < parameter.minimum || value > parameter.maximum)
            {
                return;
            }
            shader.parameters[i] = value;
        }
        shader.enabled = enabled != 0;
        auto found = std::find_if(effects.begin(), effects.end(),
                                  [&](const Shader& entry) { return entry.effect == shader.effect; });
        if (found != effects.end())
        {
            effects.erase(found);
        }
        effects.push_back(shader);
        std::stable_partition(effects.begin(), effects.end(), [](const Shader& entry) { return entry.enabled; });
    }
};
} // namespace shaders
