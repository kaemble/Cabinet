#!/usr/bin/env python3
"""Check shader metadata validation and generated interfaces without a GPU."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_shaders", ROOT / "tools/build_shaders.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def source(metadata, body):
    header = "\n".join("// " + line for line in json.dumps(metadata, indent=2).splitlines())
    return "// cabinet-shader-begin\n" + header + "\n// cabinet-shader-end\n" + body


class ShaderMetadataTest(unittest.TestCase):
    def setUp(self):
        self.metadata, self.body = build.parse_shader((ROOT / "assets/shaders/color_correction.glsl").read_text())

    def test_metadata_changes_reach_generated_definitions(self):
        self.metadata.update(id="edited_shader", name='Edited "shader"', entry="edited_color")
        self.metadata["parameters"][0].update(name="Exposure", minimum=-50, maximum=50, step=10, initial=20, scale=0.1)
        metadata, body = build.parse_shader(source(self.metadata, self.body))
        definition = build.definition(metadata)
        self.assertIn('color_id = "edited_shader"', definition)
        self.assertIn('color_name = "Edited \\"shader\\""', definition)
        self.assertIn('"Exposure", "Add or subtract brightness", -50, 50, 10, 20, 0.1f', definition)
        _, fragment, _, gl_fragment = build.sources(metadata, body)
        self.assertIn("edited_color(texture(", fragment)
        self.assertIn("edited_color(texture2D(", gl_fragment)

    def test_parameter_counts_pack_into_matching_backend_layouts(self):
        for count in (1, 4, 7, 8, 9, 28):
            with self.subTest(count=count):
                metadata = copy.deepcopy(self.metadata)
                metadata["parameters"] = [dict(metadata["parameters"][0], symbol=f"parameter_{i}") for i in range(count)]
                metadata, body = build.parse_shader(source(metadata, self.body))
                vectors = (count + 3) // 4
                self.assertIn(f"parameter_vector_count = {vectors}", build.definition(metadata))
                _, fragment, _, gl_fragment = build.sources(metadata, body)
                for code in (fragment, gl_fragment):
                    self.assertIn(f"vec4 values[{vectors}]", code)
                    self.assertIn(f"values[{(count - 1) // 4}].{'xyzw'[(count - 1) % 4]}", code)
                self.assertIn("p.source.xy", fragment)
                self.assertIn("uv * source.xy", gl_fragment)

    def test_invalid_metadata_is_rejected(self):
        for changes in (
            {"step": 0}, {"step": 3}, {"initial": 101}, {"initial": 1},
            {"minimum": 101}, {"maximum": -100}, {"maximum": 2**31},
            {"scale": 0}, {"scale": float("nan")}, {"scale": float("inf")},
            {"symbol": "not an identifier"},
        ):
            with self.subTest(changes=changes):
                metadata = copy.deepcopy(self.metadata)
                metadata["parameters"][0].update(changes)
                with self.assertRaises(ValueError):
                    build.parse_shader(source(metadata, self.body))
        for count in (0, 29):
            metadata = copy.deepcopy(self.metadata)
            metadata["parameters"] = [dict(metadata["parameters"][0], symbol=f"parameter_{i}") for i in range(count)]
            with self.assertRaises(ValueError):
                build.parse_shader(source(metadata, self.body))
        self.metadata["parameters"][1]["symbol"] = self.metadata["parameters"][0]["symbol"]
        with self.assertRaises(ValueError):
            build.parse_shader(source(self.metadata, self.body))
        with self.assertRaises(ValueError):
            build.parse_shader(self.body)

    def test_checked_in_definition_matches_shader(self):
        self.assertEqual(build.definition(self.metadata), (ROOT / "src/color_correction_definition.h").read_text())

    def test_checked_in_gl_matches_shader(self):
        _, _, vertex, fragment = build.sources(self.metadata, self.body)
        embedded = (ROOT / "src/color_correction_shaders.h").read_text()
        for name, code in (("vertex", vertex), ("fragment", fragment)):
            self.assertIn(f'color_gl_{name} = R"shader({code})shader";', embedded)


if __name__ == "__main__":
    unittest.main()
