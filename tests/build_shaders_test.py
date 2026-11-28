#!/usr/bin/env python3
"""Check shader metadata validation and generated interfaces without a GPU."""

import argparse
import copy
import importlib.util
import json
import hashlib
import zlib
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


LOOKUP_HASHES = ['35065cef2a02cabcad711d6bf430239ae64e27d71c4e4fa06f29cce2c992f0d2', '3694eae5e9d44b8ebb4415a13f8c7b94dc08a2fc86658434d771c4610fe5744d']


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
        for count in (1, 4, 7, 8, 9, 20):
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
            {"display_scale": 0}, {"display_scale": float("nan")}, {"decimals": -1}, {"decimals": 4},
        ):
            with self.subTest(changes=changes):
                metadata = copy.deepcopy(self.metadata)
                metadata["parameters"][0].update(changes)
                with self.assertRaises(ValueError):
                    build.parse_shader(source(metadata, self.body))
        for count in (0, 21):
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
        self.assertEqual([entry["id"] for entry in metadata], ["cas", "chromatic_aberration", "color_correction", "crt_geom", "film_noise", "smaa", "vignette"])

    def test_generated_gl_matches_shaders(self):
        for metadata, body in build.load_catalog(ROOT):
            shader_id = metadata["id"]
            embedded = (generated_dir / f"{shader_id}_shaders.h").read_text()
            for stage in range(len(build.pass_entries(metadata))):
                _, _, vertex, fragment = build.sources(metadata, body, stage)
                symbol = f"{shader_id}_{stage}"
                for name, code in (("vertex", vertex), ("fragment", fragment)):
                    self.assertTrue(f'{symbol}_gl_{name} = R"shader({code})shader";' in embedded, f"Generated {symbol} {name} is stale")

    def test_smaa_passes_and_embedded_lookup_tables(self):
        metadata, body = build.parse_shader((ROOT / "assets/shaders/effects/smaa.glsl").read_text())
        self.assertEqual(build.pass_entries(metadata), ["smaa_detect", "smaa_weights", "smaa_blend"])
        self.assertEqual(len(metadata["parameters"]), 6)
        self.assertEqual(metadata["parameters"][0]["maximum"], 1)
        self.assertFalse(any("depth" in parameter["symbol"] or "predication" in parameter["symbol"]
                             for parameter in metadata["parameters"]))
        self.assertEqual([(texture["width"], texture["height"], texture["channels"], texture["linear"])
                          for texture in metadata["textures"]], [(160, 560, 2, True), (64, 16, 1, False)])
        for texture, digest in zip(metadata["textures"], LOOKUP_HASHES):
            self.assertEqual(hashlib.sha256(build.texture_data(texture)).hexdigest(), digest)
        for stage in range(3):
            _, fragment, _, gl_fragment = build.sources(metadata, body, stage)
            for code in (fragment, gl_fragment):
                self.assertIn(f"#define CABINET_PASS {stage}", code)
                self.assertIn("float edge_threshold()", code)
                self.assertIn(f"= {metadata['passes'][stage]}(", code)
                self.assertIn("sample_area_texture(vec2 position)", code)
                for previous in range(stage):
                    self.assertIn(f"sample_pass_{previous}(vec2 position)", code)
            self.assertIn(f"layout(binding={stage + 1}) uniform sampler2D area_texture;", fragment)
            self.assertIn(f"layout(binding={stage + 2}) uniform sampler2D search_texture;", fragment)

    def test_invalid_passes_and_textures_are_rejected(self):
        metadata, body = build.parse_shader((ROOT / "assets/shaders/effects/smaa.glsl").read_text())
        for passes in ([], ["bad entry"], [1], "smaa_blend", ["smaa_detect"] * 5, ["wrong_final_entry"]):
            changed = dict(metadata, passes=passes)
            with self.assertRaises(ValueError):
                build.parse_shader(source(changed, body))
        with self.assertRaises(ValueError):
            build.parse_shader(source(dict(metadata, context=False), body))
        for changes in ({"width": 0}, {"height": 2049}, {"channels": 5}, {"channels": True},
                        {"linear": "yes"}, {"name": "frame"}, {"name": "pass_0"}, {"data": ["invalid"]},
                        {"width": 159}):
            changed = copy.deepcopy(metadata)
            changed["textures"][0].update(changes)
            with self.assertRaises((ValueError, zlib.error)):
                build.parse_shader(source(changed, body))
        for textures in ([metadata["textures"][0]] * 2, metadata["textures"] * 2):
            with self.assertRaises(ValueError):
                build.parse_shader(source(dict(metadata, textures=textures), body))

    def test_context_supplies_animation_and_cropped_sampling(self):
        metadata, body = build.parse_shader((ROOT / "assets/shaders/effects/film_noise.glsl").read_text())
        _, fragment, _, gl_fragment = build.sources(metadata, body)
        self.assertIn("uv, p.context.x,", fragment)
        self.assertIn("uv, context.x,", gl_fragment)
        self.assertIn("p.context.yz + clamp(position, p.source.zw, vec2(1.0) - p.source.zw) * p.source.xy", fragment)
        self.assertIn("sample_frame(vec2 position)", gl_fragment)
        self.assertIn("return 2.0 * p.source.zw;", fragment)
        self.assertIn("return 2.0 * source.zw;", gl_fragment)
        self.assertIn('"Signal-to-noise ratio"', build.definition([entry for entry, _ in build.load_catalog(ROOT)]))

    def test_output_resolution_and_display_units(self):
        catalog = [metadata for metadata, _ in build.load_catalog(ROOT)]
        crt = next(entry for entry in catalog if entry["id"] == "crt_geom")
        self.assertEqual(len(crt["parameters"]), 18)
        self.assertEqual(crt["resolution"], "output")
        self.assertIn("crt_geom_parameters, true", build.definition(catalog))
        metadata, body = build.parse_shader((ROOT / "assets/shaders/effects/crt_geom.glsl").read_text())
        _, fragment, _, gl_fragment = build.sources(metadata, body)
        self.assertIn("return p.target.xy;", fragment)
        self.assertIn("return p.target.zw;", fragment)
        self.assertIn("return target.xy;", gl_fragment)
        self.metadata["resolution"] = "invalid"
        with self.assertRaises(ValueError):
            build.parse_shader(source(self.metadata, self.body))

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
