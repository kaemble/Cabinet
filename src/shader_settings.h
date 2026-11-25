#pragma once

#include "shader_definitions.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace shaders
{
inline std::string parameter_value(const Parameter& parameter, int value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(parameter.decimals) << value * parameter.display_scale << parameter.suffix;
    return out.str();
}

struct Shader
{
    Effect effect;
    bool enabled = false;
    std::vector<int> parameters;
    explicit Shader(Effect effect = definitions.front().effect) : effect(effect)
    {
        for (const auto& parameter : definition(effect).parameters)
        {
            parameters.push_back(parameter.initial);
        }
    }
    bool operator==(const Shader&) const = default;
};

inline std::size_t parameter_vector_count(Effect effect)
{
    return (definition(effect).parameters.size() + 3) / 4;
}

inline std::array<float, maximum_parameter_vectors * 4> uniform_values(const Shader& shader)
{
    std::array<float, maximum_parameter_vectors * 4> values{};
    const auto& parameters = definition(shader.effect).parameters;
    for (std::size_t i = 0; i < parameters.size(); ++i)
    {
        values[i] = shader.parameters.at(i) * parameters[i].scale;
    }
    return values;
}

inline const char* name(Effect effect)
{
    return definition(effect).name;
}

struct Stack
{
    std::vector<Shader> effects = []
    {
        std::vector<Shader> result;
        for (const auto& entry : definitions)
        {
            result.emplace_back(entry.effect);
        }
        return result;
    }();
    bool operator==(const Stack&) const = default;
    bool active() const
    {
        return std::any_of(effects.begin(), effects.end(), [](const Shader& shader) { return shader.enabled; });
    }
    int toggle(int index)
    {
        auto& shader = effects.at(index);
        shader.enabled = !shader.enabled;
        return index;
    }
    int sort(int index)
    {
        int position = index;
        if (index >= 0 && index < static_cast<int>(effects.size()))
        {
            const bool enabled = effects[index].enabled;
            position = static_cast<int>(std::count_if(effects.begin(), effects.begin() + index,
                [&](const Shader& shader) { return shader.enabled == enabled; }));
            if (!enabled)
            {
                position += static_cast<int>(std::count_if(effects.begin(), effects.end(),
                    [](const Shader& shader) { return shader.enabled; }));
            }
        }
        std::stable_partition(effects.begin(), effects.end(), [](const Shader& shader) { return shader.enabled; });
        return position;
    }
    int move(int index, int direction)
    {
        const int next = index + direction;
        if (next >= 0 && next < static_cast<int>(effects.size()))
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
            out << "shader " << std::quoted(definition(shader.effect).id) << ' ' << shader.enabled;
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
        if (!(in >> std::quoted(id) >> enabled) || (enabled != 0 && enabled != 1))
        {
            return;
        }
        const auto entry = std::find_if(definitions.begin(), definitions.end(),
                                        [&](const Definition& definition) { return id == definition.id; });
        if (entry == definitions.end())
        {
            return;
        }
        Shader shader(entry->effect);
        for (std::size_t i = 0; i < shader.parameters.size(); ++i)
        {
            int value;
            const auto& parameter = entry->parameters[i];
            if (!(in >> value) || value < parameter.minimum || value > parameter.maximum)
            {
                return;
            }
            shader.parameters[i] = value;
        }
        in >> std::ws;
        if (!in.eof())
        {
            return;
        }
        shader.enabled = enabled != 0;
        const auto found = std::find_if(effects.begin(), effects.end(),
                                        [&](const Shader& value) { return value.effect == shader.effect; });
        if (found != effects.end())
        {
            effects.erase(found);
        }
        effects.push_back(shader);
    }
};
} // namespace shaders
