#!/usr/bin/env python3
"""Check shader metadata validation and generated interfaces without a GPU."""

import argparse
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
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
        self.metadata, self.body = build.parse_shader((ROOT / "assets/shaders/effects/color_correction.glsl").read_text())

    def test_metadata_changes_reach_generated_definitions(self):
        self.metadata.update(id="edited_shader", name='Edited "shader"', entry="edited_color")
        self.metadata["parameters"][0].update(name="Exposure", minimum=-50, maximum=50, step=10, initial=20, scale=0.1)
        metadata, body = build.parse_shader(source(self.metadata, self.body))
        definition = build.definition([metadata])
        self.assertIn('Effect::EditedShader, "edited_shader"', definition)
        self.assertIn(json.dumps(metadata["name"]), definition)
        self.assertIn('"Exposure", "Add or subtract brightness", -50, 50, 10, 20, 0.1f', definition)
        _, fragment, _, gl_fragment = build.sources(metadata, body)
        self.assertIn("edited_color(texture(", fragment)
        self.assertIn("edited_color(texture2D(", gl_fragment)

    def test_parameter_counts_pack_into_matching_backend_layouts(self):
        for count in (1, 4, 7, 8, 9, 24):
            with self.subTest(count=count):
                metadata = copy.deepcopy(self.metadata)
                metadata["parameters"] = [dict(metadata["parameters"][0], symbol=f"parameter_{i}") for i in range(count)]
                metadata, body = build.parse_shader(source(metadata, self.body))
                vectors = (count + 3) // 4
                self.assertIn(f"std::array<Parameter, {count}>", build.definition([metadata]))
                _, fragment, _, gl_fragment = build.sources(metadata, body)
                for code in (fragment, gl_fragment):
                    self.assertIn(f"vec4 values[{vectors}]", code)
                    self.assertIn(f"values[{(count - 1) // 4}].{'xyzw'[(count - 1) % 4]}", code)
                self.assertIn("p.source.xy", fragment)
                self.assertIn("context.yz + uv * source.xy", gl_fragment)

    def test_invalid_metadata_is_rejected(self):
        for changes in (
            {"step": 0}, {"step": 3}, {"initial": 101}, {"initial": 1},
            {"minimum": 101}, {"maximum": -100}, {"maximum": 2**31},
            {"scale": 0}, {"scale": float("nan")}, {"scale": float("inf")},
            {"symbol": "not an identifier"}, {"suffix": 1},
        ):
            with self.subTest(changes=changes):
                metadata = copy.deepcopy(self.metadata)
                metadata["parameters"][0].update(changes)
                with self.assertRaises(ValueError):
                    build.parse_shader(source(metadata, self.body))
        for count in (0, 25):
            metadata = copy.deepcopy(self.metadata)
            metadata["parameters"] = [dict(metadata["parameters"][0], symbol=f"parameter_{i}") for i in range(count)]
            with self.assertRaises(ValueError):
                build.parse_shader(source(metadata, self.body))
        self.metadata["parameters"][1]["symbol"] = self.metadata["parameters"][0]["symbol"]
        with self.assertRaises(ValueError):
            build.parse_shader(source(self.metadata, self.body))
        with self.assertRaises(ValueError):
            build.parse_shader(self.body)

    def test_generated_definition_matches_shaders(self):
        catalog = build.load_catalog(ROOT)
        metadata = [entry for entry, _ in catalog]
        self.assertEqual(build.definition(metadata), (generated_dir / "shader_definitions.h").read_text())
        self.assertEqual(build.programs(metadata), (generated_dir / "shader_programs.h").read_text())
        self.assertEqual([entry["id"] for entry in metadata], ["chromatic_aberration", "color_correction", "film_noise", "vignette"])

    def test_generated_gl_matches_shaders(self):
        for metadata, body in build.load_catalog(ROOT):
            _, _, vertex, fragment = build.sources(metadata, body)
            shader_id = metadata["id"]
            embedded = (generated_dir / f"{shader_id}_shaders.h").read_text()
            for name, code in (("vertex", vertex), ("fragment", fragment)):
                self.assertIn(f'{shader_id}_gl_{name} = R"shader({code})shader";', embedded)

    def test_context_supplies_animation_and_cropped_sampling(self):
        metadata, body = build.parse_shader((ROOT / "assets/shaders/effects/film_noise.glsl").read_text())
        _, fragment, _, gl_fragment = build.sources(metadata, body)
        self.assertIn("uv, p.context.x,", fragment)
        self.assertIn("uv, context.x,", gl_fragment)
        self.assertIn("p.context.yz + clamp(position, p.source.zw, vec2(1.0) - p.source.zw) * p.source.xy", fragment)
        self.assertIn("sample_frame(vec2 position)", gl_fragment)
        self.assertIn('"Grain strength"', build.definition([entry for entry, _ in build.load_catalog(ROOT)]))

    def test_discovery_ignores_internal_shaders_and_checks_ids(self):
        with tempfile.TemporaryDirectory(prefix="cabinet-shader-catalog-") as directory:
            root = Path(directory)
            effects = root / "assets/shaders/effects"
            internal = root / "assets/shaders/internal"
            effects.mkdir(parents=True)
            internal.mkdir()
            (internal / "not_an_effect.glsl").write_text("Internal shaders have no metadata")
            path = effects / "color_correction.glsl"
            path.write_text(source(self.metadata, self.body))
            self.assertEqual(len(build.load_catalog(root)), 1)
            self.metadata["id"] = "wrong_filename"
            path.write_text(source(self.metadata, self.body))
            with self.assertRaisesRegex(ValueError, "match its filename"):
                build.load_catalog(root)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument("--generated-dir", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    generated_dir = args.generated_dir
    unittest.main(argv=[sys.argv[0], *remaining])
