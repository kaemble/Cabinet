#!/usr/bin/env python3
"""Generate Cabinet shader headers in a build directory. Requires glslangValidator."""

from pathlib import Path
import argparse
import base64
import json
import math
import re
import struct
import subprocess
import tempfile
import zlib


def parse_shader(source):
    header, separator, body = source.partition("// cabinet-shader-end\n")
    if not separator or not header.startswith("// cabinet-shader-begin\n"):
        raise ValueError("Shader must start with a cabinet-shader metadata header")
    lines = header.splitlines()[1:]
    if any(not line.startswith("// ") for line in lines):
        raise ValueError("Shader metadata must use // comments")
    metadata = json.loads("\n".join(line[3:] for line in lines))
    for key in ("id", "entry"):
        if not re.fullmatch(r"[a-z][a-z0-9_]*", metadata[key]):
            raise ValueError(f"Invalid shader {key}")
    if not isinstance(metadata["name"], str) or not metadata["name"]:
        raise ValueError("Shader name must be a nonempty string")
    if type(metadata.get("context", False)) is not bool:
        raise ValueError("Shader context must be a boolean")
    if metadata.get("resolution", "source") not in ("source", "output"):
        raise ValueError("Shader resolution must be source or output")
    passes = metadata.get("passes", [metadata["entry"]])
    if not isinstance(passes, list) or not 1 <= len(passes) <= 4 or any(
            not isinstance(entry, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", entry) for entry in passes):
        raise ValueError("Shader passes must contain one to four entry names")
    if "passes" in metadata and (not metadata.get("context") or passes[-1] != metadata["entry"]):
        raise ValueError("Multipass shaders require context and must end at the shader entry")
    textures = metadata.get("textures", [])
    names = set()
    if not isinstance(textures, list) or len(textures) > 3:
        raise ValueError("Shaders may embed up to three lookup textures")
    for texture in textures:
        name = texture["name"]
        if not re.fullmatch(r"[a-z][a-z0-9_]*", name) or name in names or name == "frame" or name.startswith("pass_"):
            raise ValueError("Invalid or duplicate lookup texture name")
        names.add(name)
        if any(type(texture[key]) is not int or not 1 <= texture[key] <= 2048 for key in ("width", "height")):
            raise ValueError("Invalid lookup texture dimensions")
        if type(texture["channels"]) is not int or not 1 <= texture["channels"] <= 4:
            raise ValueError("Invalid lookup texture channels")
        if type(texture["linear"]) is not bool:
            raise ValueError("Lookup texture filtering must be a boolean")
        texture_data(texture)
    parameters = metadata["parameters"]
    # Reserve three vec4s for source extent, animation and output dimensions within Vulkan's guaranteed 128-byte push constants.
    if not 1 <= len(parameters) <= 20:
        raise ValueError("Shaders must define between 1 and 20 parameters")
    symbols = set()
    for parameter in parameters:
        symbol = parameter["symbol"]
        if not re.fullmatch(r"[a-z][a-z0-9_]*", symbol) or symbol in symbols:
            raise ValueError("Parameter symbols must be unique GLSL identifiers")
        symbols.add(symbol)
        for key in ("name", "description"):
            if not isinstance(parameter[key], str) or not parameter[key]:
                raise ValueError(f"Parameter {key} must be a nonempty string")
        for key in ("minimum", "maximum", "step", "initial"):
            if type(parameter[key]) is not int or not -(2**31) < parameter[key] < 2**31 - 1:
                raise ValueError(f"Parameter {key} must be a 32-bit integer")
        minimum, maximum, step, initial = (parameter[key] for key in ("minimum", "maximum", "step", "initial"))
        if step <= 0 or minimum >= maximum or not minimum <= initial <= maximum:
            raise ValueError("Invalid parameter range, step or default")
        if minimum - step < -(2**31) or maximum + step > 2**31 - 1 or maximum - minimum > 2**31 - 1 - step:
            raise ValueError("Parameter range is too large")
        if (maximum - minimum) % step or (initial - minimum) % step:
            raise ValueError("Parameter range and default must align with its step")
        if not isinstance(parameter.get("suffix", "%"), str):
            raise ValueError("Parameter suffix must be a string")
        display_scale = parameter.get("display_scale", 1)
        if type(display_scale) not in (int, float) or not math.isfinite(display_scale) or display_scale <= 0:
            raise ValueError("Parameter display scale must be finite and positive")
        decimals = parameter.get("decimals", 0)
        if type(decimals) is not int or not 0 <= decimals <= 3:
            raise ValueError("Parameter decimals must be between zero and three")
        scale = parameter["scale"]
        if type(scale) not in (int, float) or not math.isfinite(scale) or scale <= 0:
            raise ValueError("Parameter scale must be finite and positive")
    return metadata, body


def texture_data(texture):
    data = zlib.decompress(base64.b64decode("".join(texture["data"]), validate=True))
    if len(data) != texture["width"] * texture["height"] * texture["channels"]:
        raise ValueError("Lookup texture data does not match its dimensions")
    return data


def pass_entries(metadata):
    return metadata.get("passes", [metadata["entry"]])


def input_names(metadata, stage):
    return [f"pass_{i}" for i in range(stage)] + [texture["name"] for texture in metadata.get("textures", [])]


def effect_name(metadata):
    return "".join(part.title() for part in metadata["id"].split("_"))


def definition(catalog):
    result = """#pragma once
#include <array>
#include <span>
// Generated by tools/build_shaders.py from assets/shaders/effects/*.glsl.
namespace shaders
{
enum class Effect
{
"""
    result += ",\n".join("    " + effect_name(metadata) for metadata in catalog) + "\n};\n"
    result += """struct Parameter
{
    const char* name;
    const char* description;
    int minimum, maximum, step, initial;
    float scale;
    const char* suffix;
    float display_scale;
    int decimals;
};
struct Definition
{
    Effect effect;
    const char* id;
    const char* name;
    std::span<const Parameter> parameters;
    bool output_resolution;
};
"""
    maximum_vectors = max((len(metadata["parameters"]) + 3) // 4 for metadata in catalog)
    result += f"inline constexpr std::size_t maximum_parameter_vectors = {maximum_vectors};\n"
    for metadata in catalog:
        parameters = metadata["parameters"]
        result += f'inline constexpr std::array<Parameter, {len(parameters)}> {metadata["id"]}_parameters{{{{\n'
        for parameter in parameters:
            fields = [json.dumps(parameter[key]) for key in ("name", "description")]
            fields += [str(parameter[key]) for key in ("minimum", "maximum", "step", "initial")]
            fields += [f'{float(parameter["scale"])}f', json.dumps(parameter.get("suffix", "%")),
                       f'{float(parameter.get("display_scale", 1))}f', str(parameter.get("decimals", 0))]
            result += "    {" + ", ".join(fields) + "},\n"
        result += "}};\n"
    result += f"inline constexpr std::array<Definition, {len(catalog)}> definitions{{{{\n"
    for metadata in catalog:
        result += f'    {{Effect::{effect_name(metadata)}, {json.dumps(metadata["id"])}, {json.dumps(metadata["name"])}, {metadata["id"]}_parameters, {str(metadata.get("resolution") == "output").lower()}}},\n'
    result += """}};
inline const Definition& definition(Effect effect)
{
    return definitions.at(static_cast<std::size_t>(effect));
}
} // namespace shaders
"""
    return result


def sources(metadata, body, stage=0):
    count = (len(metadata["parameters"]) + 3) // 4
    vertex = """#version 450
layout(location=0) out vec2 uv;
void main()
{
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
"""
    fragment = """#version 450
layout(binding=0) uniform sampler2D frame;
""" + f"layout(push_constant) uniform Parameters {{ vec4 values[{count}]; vec4 source; vec4 context; vec4 target; }} p;\n" + """layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
"""
    gl_vertex = """attribute vec2 position;
varying vec2 uv;
void main()
{
    uv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
"""
    gl_fragment = """varying vec2 uv;
uniform sampler2D frame;
""" + f"uniform vec4 values[{count}];\n" + "uniform vec4 source;\nuniform vec4 context;\nuniform vec4 target;\n"
    for vulkan in (True, False):
        prefix = "p." if vulkan else ""
        texture = "texture" if vulkan else "texture2D"
        output = "color" if vulkan else "gl_FragColor"
        helper = ""
        for binding, name in enumerate(input_names(metadata, stage), 1):
            declaration = f"layout(binding={binding}) " if vulkan else ""
            helper += f"{declaration}uniform sampler2D {name};\n"
            helper += f"vec4 sample_{name}(vec2 position)\n{{\n    return {texture}({name}, clamp(position, vec2(0.0), vec2(1.0)));\n}}\n"
        if "passes" in metadata:
            helper += f"#define CABINET_PASS {stage}\n"
            for i, parameter in enumerate(metadata["parameters"]):
                helper += f"float {parameter['symbol']}()\n{{\n    return {prefix}values[{i // 4}].{'xyzw'[i % 4]};\n}}\n"
        if metadata.get("context", False):
            helper += f"vec2 frame_texel_size()\n{{\n    return 2.0 * {prefix}source.zw;\n}}\n"
            helper += f"vec2 frame_size()\n{{\n    return {prefix}target.zw;\n}}\n"
            helper += f"vec2 output_size()\n{{\n    return {prefix}target.xy;\n}}\n"
            helper += "vec3 sample_frame(vec2 position)\n{\n"
            helper += f"    return {texture}(frame, {prefix}context.yz + clamp(position, {prefix}source.zw, vec2(1.0) - {prefix}source.zw) * {prefix}source.xy).rgb;\n}}\n"
        arguments = [f"{texture}(frame, {prefix}context.yz + uv * {prefix}source.xy).rgb"]
        if metadata.get("context", False):
            arguments += ["uv", f"{prefix}context.x"]
        arguments += [f"{prefix}values[{i // 4}].{'xyzw'[i % 4]}" for i in range(len(metadata["parameters"]))]
        main = "\nvoid main()\n{\n"
        call = f"{pass_entries(metadata)[stage]}({', '.join(arguments)})"
        result = call if "passes" in metadata else f"vec4({call}, 1.0)"
        main += f"    {output} = {result};\n}}\n"
        if vulkan:
            fragment += helper + body + main
        else:
            gl_fragment += helper + body + main
    return vertex, fragment, gl_vertex, gl_fragment


def load_catalog(root):
    catalog = []
    for path in sorted((root / "assets/shaders/effects").glob("*.glsl")):
        metadata, body = parse_shader(path.read_text())
        if metadata["id"] != path.stem:
            raise ValueError(f"Shader ID must match its filename: {path}")
        catalog.append((metadata, body))
    if not catalog:
        raise ValueError("No effect shaders found")
    names = [effect_name(metadata) for metadata, _ in catalog]
    if len(set(names)) != len(names):
        raise ValueError("Shader IDs must produce unique effect names")
    return catalog


def programs(catalog):
    result = '#pragma once\n#include "shader_definitions.h"\n#include <cstdint>\n'
    result += "// Generated by tools/build_shaders.py from assets/shaders/effects/*.glsl.\n"
    for metadata in catalog:
        result += f'#include "{metadata["id"]}_shaders.h"\n'
    result += """namespace shaders
{
struct Stage
{
    std::span<const std::uint32_t> vertex;
    std::span<const std::uint32_t> fragment;
    const char* gl_vertex;
    const char* gl_fragment;
    std::span<const char* const> inputs;
};
struct Texture
{
    unsigned width, height;
    bool linear;
    std::span<const std::uint8_t> pixels;
};
struct Program
{
    std::span<const Stage> stages;
    std::span<const Texture> textures;
};
"""
    maximum_samplers = max(len(pass_entries(metadata)) + len(metadata.get("textures", [])) for metadata in catalog)
    result += f"inline constexpr std::size_t maximum_sampler_count = {maximum_samplers};\n"
    for metadata in catalog:
        shader_id = metadata["id"]
        for stage in range(len(pass_entries(metadata))):
            names = input_names(metadata, stage)
            result += f"inline constexpr std::array<const char*, {len(names)}> {shader_id}_{stage}_inputs{{{{"
            result += ", ".join(json.dumps(name) for name in names) + "}};\n"
        result += f"inline constexpr std::array<Stage, {len(pass_entries(metadata))}> {shader_id}_stages{{{{\n"
        for stage in range(len(pass_entries(metadata))):
            symbol = f"{shader_id}_{stage}"
            result += f"    {{{symbol}_vert, {symbol}_frag, {symbol}_gl_vertex, {symbol}_gl_fragment, {shader_id}_{stage}_inputs}},\n"
        result += "}};\n"
        textures = metadata.get("textures", [])
        result += f"inline constexpr std::array<Texture, {len(textures)}> {shader_id}_textures{{{{\n"
        for texture in textures:
            result += f"    {{{texture['width']}, {texture['height']}, {str(texture['linear']).lower()}, {shader_id}_{texture['name']}_pixels}},\n"
        result += "}};\n"
    result += f"inline constexpr std::array<Program, {len(catalog)}> programs{{{{\n"
    for metadata in catalog:
        shader_id = metadata["id"]
        result += f"    {{{shader_id}_stages, {shader_id}_textures}},\n"
    return result + """}};
inline const Program& program(Effect effect)
{
    return programs.at(static_cast<std::size_t>(effect));
}
} // namespace shaders
"""


def spirv_array(compiler, directory, name, stage, code):
    path = directory / (name + "." + stage)
    path.write_text(code)
    output = path.with_suffix(".spv")
    subprocess.run([str(compiler), "-V", str(path), "-o", str(output)], check=True)
    data = output.read_bytes()
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    result = f"inline constexpr std::uint32_t {name}[] = {{\n"
    for i in range(0, len(words), 8):
        result += "    " + ", ".join(f"0x{word:08x}" for word in words[i:i + 8]) + ",\n"
    return result + "};\n"


def effect_headers(root, compiler):
    catalog = load_catalog(root)
    outputs = {}
    with tempfile.TemporaryDirectory(prefix="cabinet-shaders-") as directory:
        for metadata, body in catalog:
            shader_id = metadata["id"]
            result = "#pragma once\n#include <cstdint>\n"
            result += f"// Generated by tools/build_shaders.py from assets/shaders/effects/{shader_id}.glsl.\n"
            for stage in range(len(pass_entries(metadata))):
                vertex, fragment, gl_vertex, gl_fragment = sources(metadata, body, stage)
                symbol = f"{shader_id}_{stage}"
                for extension, code in [("vert", vertex), ("frag", fragment)]:
                    result += spirv_array(compiler, Path(directory), f"{symbol}_{extension}", extension, code)
                for name, code in [("vertex", gl_vertex), ("fragment", gl_fragment)]:
                    result += f'inline constexpr const char* {symbol}_gl_{name} = R"shader({code})shader";\n'
            for texture in metadata.get("textures", []):
                data = texture_data(texture)
                channels = texture["channels"]
                pixels = bytearray()
                for i in range(0, len(data), channels):
                    pixel = data[i:i + channels]
                    pixels.extend((pixel + bytes(3))[:3] + (pixel[3:4] if channels == 4 else bytes([255])))
                result += f"inline constexpr std::uint8_t {shader_id}_{texture['name']}_pixels[] = {{\n"
                for i in range(0, len(pixels), 32):
                    result += "    " + ",".join(str(value) for value in pixels[i:i + 32]) + ",\n"
                result += "};\n"
            outputs[f"{shader_id}_shaders.h"] = result
    metadata = [entry for entry, _ in catalog]
    outputs["shader_definitions.h"] = definition(metadata)
    outputs["shader_programs.h"] = programs(metadata)
    return outputs


def internal_headers(root, compiler):
    groups = {
        "armada_display_shaders.h": [("armada_display_vertex", "armada_display.vert"),
                                     ("armada_display_fragment", "armada_display.frag")],
        "vulkan_snapshot_shader.h": [("vulkan_snapshot_shader", "vulkan_snapshot.comp")],
    }
    outputs = {}
    with tempfile.TemporaryDirectory(prefix="cabinet-internal-shaders-") as directory:
        for name, stages in groups.items():
            result = "#pragma once\n#include <cstdint>\n"
            result += "// Generated by tools/build_shaders.py from assets/shaders/internal/.\n"
            for symbol, filename in stages:
                path = root / "assets/shaders/internal" / filename
                result += spirv_array(compiler, Path(directory), symbol, path.suffix[1:], path.read_text())
            outputs[name] = result
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--group", choices=("effects", "internal"), default="effects")
    parser.add_argument("--glslang", default="glslangValidator", help="Host glslangValidator executable")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    outputs = effect_headers(root, args.glslang) if args.group == "effects" else internal_headers(root, args.glslang)
    # Publish outputs only after every shader has validated and compiled successfully.
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, result in outputs.items():
        (args.output_dir / name).write_text(result)


if __name__ == "__main__":
    main()
