#!/usr/bin/env python3
"""Verify package integrity and architecture rejection without downloading cores."""
import copy
import ctypes
import platform
import importlib.util
import json
import re
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_cores", ROOT / "tools/build_cores.py")
cores = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cores)


class BundleTests(unittest.TestCase):
    def test_system_catalog_is_fully_locked(self):
        lock = cores.load_lock(ROOT / "cores.lock.json")
        catalog = (ROOT / "src/library.cpp").read_text()
        pattern = r'\{\s*"[^"]+",\s*system_display_name\("[^"]+"\),\s*"[^"]+",\s*"([^"]+)"'
        names = set(re.findall(pattern, catalog))
        self.assertTrue(names)
        self.assertEqual(names, set(lock["cores"]))

    def test_archive_build_embeds_pinned_revision_on_fresh_and_resumed_builds(self):
        recipe = copy.deepcopy(cores.load_lock(ROOT / "cores.lock.json")["cores"]["azahar"])
        self.assertTrue(recipe["archive_git_metadata"])
        recipe.update(source_directory="source", target="metadata_probe", artifact="metadata_probe.so",
                      artifact_path="metadata_probe.so", license_files=[], cmake={})
        recipe.pop("architectures", None)
        recipe.pop("submodules", None)
        with tempfile.TemporaryDirectory(prefix="cabinet metadata ") as directory:
            root = Path(directory)
            source = root / "work/probe/source"
            source.mkdir(parents=True)
            (source / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.20)
project(metadata_probe C)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/GIT-COMMIT" REVISION)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/GIT-TAG" VERSION)
string(STRIP "${REVISION}" REVISION)
string(STRIP "${VERSION}" VERSION)
configure_file(probe.c.in probe.c @ONLY)
add_library(metadata_probe MODULE "${CMAKE_CURRENT_BINARY_DIR}/probe.c")
set_target_properties(metadata_probe PROPERTIES PREFIX "")
""")
            (source / "probe.c.in").write_text(
                'const char* revision(void) { return "@REVISION@"; }\n'
                'const char* version(void) { return "@VERSION@"; }\n')
            stage = root / "stage"
            stage.mkdir()
            # Exercise both initial extraction and the sources-ready resume path.
            with mock.patch.object(cores, "extract_source", return_value=source):
                for attempt in range(2):
                    if attempt:
                        (source / "GIT-COMMIT").write_text("UNKNOWN\n")
                        (source / "GIT-TAG").unlink()
                    receipt = cores.build_core("probe", recipe, cores.architecture(platform.machine()), None, {},
                                               root / "cache", root / "work", stage, 1, True)
                    self.assertEqual((source / "GIT-COMMIT").read_text().strip(), recipe["commit"])
                    self.assertEqual((source / "GIT-TAG").read_text().strip(), recipe["version"])
                    self.assertEqual(receipt["source_sha256"], recipe["sha256"])
            library = ctypes.CDLL(str(stage / recipe["artifact"]))
            library.revision.restype = ctypes.c_char_p
            library.version.restype = ctypes.c_char_p
            self.assertEqual(library.revision().decode(), recipe["commit"])
            self.assertEqual(library.version().decode(), recipe["version"])

    def test_partial_bundle_cannot_be_installed_and_reuse_checks_recipe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = root / "cached"
            bundle.mkdir()
            lock_path = ROOT / "cores.lock.json"
            lock = cores.load_lock(lock_path)
            name = "mgba"
            artifact = bundle / lock["cores"][name]["artifact"]
            header = bytearray(64)
            header[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<HH", header, 16, 3, 62)
            artifact.write_bytes(header)
            (bundle / "cores.lock.json").write_bytes(lock_path.read_bytes())
            manifest = {"schema": 1, "architecture": "x86_64", "lock_sha256": cores.digest(lock_path),
                        "cores": {name: {"artifact": artifact.name, "recipe_sha256": cores.recipe_digest(lock["cores"][name]),
                                         "files": {artifact.name: cores.digest(artifact)}, "toolchain_sha256": None, "provenance": {}}},
                        "toolchain_sha256": None,
                        "files": {p.name: cores.digest(p) for p in bundle.iterdir()}}
            (bundle / "manifest.json").write_text(json.dumps(manifest))
            cores.verify(bundle, lock_path, "x86_64", [name])
            for field in ("artifact", "recipe_sha256", "files", "toolchain_sha256"):
                incomplete = copy.deepcopy(manifest)
                del incomplete["cores"][name][field]
                (bundle / "manifest.json").write_text(json.dumps(incomplete))
                with self.assertRaisesRegex(ValueError, "Invalid core receipt"):
                    cores.read_bundle(bundle, "x86_64")
            (bundle / "manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                cores.verify(bundle, lock_path, "x86_64")
            stage = root / "stage"
            stage.mkdir()
            self.assertIsNotNone(cores.reuse_core(bundle, name, lock["cores"][name], "x86_64", None, stage))
            self.assertEqual((stage / artifact.name).read_bytes(), header)
            changed = copy.deepcopy(lock["cores"][name])
            changed["commit"] = "0" * 40
            self.assertIsNone(cores.reuse_core(bundle, name, changed, "x86_64", None, stage))

    def test_incremental_build_preserves_outputs_and_resumes_failed_work(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock = cores.load_lock(ROOT / "cores.lock.json")
            lock["cores"] = {name: lock["cores"][name] for name in ("mgba", "gambatte")}
            lock_path = root / "lock.json"
            lock_path.write_text(json.dumps(lock))
            output, work = root / "output", root / "work"
            calls = []
            fail = {"name": "gambatte"}
            version = {"value": 1}
            saw_resume = []

            def build(name, recipe, arch, toolchain, compilers, cache, tree, stage, jobs, offline):
                calls.append(name)
                marker = tree / "compiled-object"
                if marker.exists():
                    saw_resume.append(name)
                marker.write_bytes(b"compiler work")
                if fail["name"] == name:
                    raise ValueError("simulated compiler failure")
                header = bytearray(64)
                header[:6] = b"\x7fELF\x02\x01"
                struct.pack_into("<HH", header, 16, 3, 62)
                (stage / recipe["artifact"]).write_bytes(header + bytes([version["value"]]))
                notice = stage / "licenses" / name / "NOTICE"
                notice.parent.mkdir(parents=True)
                notice.write_text(name)
                return {"version": recipe["version"]}

            base = ["build_cores.py", "--arch", "x86_64", "--lock", str(lock_path),
                    "--output", str(output), "--work-dir", str(work), "--offline", "--jobs", "1"]

            def invoke(*extra):
                with mock.patch.object(sys, "argv", base + list(extra)):
                    cores.main()

            with mock.patch.object(cores, "build_core", side_effect=build):
                with self.assertRaisesRegex(ValueError, "simulated compiler failure"):
                    invoke()
                self.assertTrue((output / "mgba_libretro.so").exists())
                self.assertFalse((output / "gambatte_libretro.so").exists())
                cores.verify(output, lock_path, "x86_64", ["mgba"])
                with self.assertRaises(ValueError):
                    cores.verify(output, lock_path, "x86_64")
                first_bytes = (output / "mgba_libretro.so").read_bytes()
                fail["name"] = None
                invoke()
                self.assertEqual(calls, ["mgba", "gambatte", "gambatte"])
                self.assertIn("gambatte", saw_resume)
                cores.verify(output, lock_path, "x86_64")
                first_receipt = cores.read_bundle(output, "x86_64")["cores"]["mgba"]
                version["value"] = 2
                invoke("--core", "gambatte", "--rebuild")
                self.assertEqual((output / "mgba_libretro.so").read_bytes(), first_bytes)
                self.assertEqual(cores.read_bundle(output, "x86_64")["cores"]["mgba"], first_receipt)
                cores.verify(output, lock_path, "x86_64")
                snapshot = {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()}
                rename = Path.rename

                def failed_publish(path, destination):
                    if path.name == "bundle" and Path(destination) == output:
                        raise OSError("simulated publish failure")
                    return rename(path, destination)

                with mock.patch.object(Path, "rename", autospec=True, side_effect=failed_publish):
                    with self.assertRaisesRegex(OSError, "simulated publish failure"):
                        invoke("--core", "gambatte", "--rebuild")
                self.assertEqual(snapshot, {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()})
                # Simulate interruption between moving the old output aside and
                # installing the staged directory. The next build recovers it.
                output.rename(output.with_name("." + output.name + ".previous"))
                invoke("--core", "gambatte")
                self.assertEqual(snapshot, {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()})
                fail["name"] = "gambatte"
                with self.assertRaises(ValueError):
                    invoke("--core", "gambatte", "--rebuild")
                self.assertEqual(snapshot, {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()})
                fail["name"] = None
                saw_resume.clear()
                invoke("--core", "gambatte", "--clean")
                self.assertNotIn("gambatte", saw_resume)
                # A changed recipe must not make untouched cores appear current.
                lock["cores"]["mgba"]["version"] = "changed"
                lock_path.write_text(json.dumps(lock))
                invoke("--core", "gambatte")
                with self.assertRaises(ValueError):
                    cores.verify(output, lock_path, "x86_64")
                invoke()
                cores.verify(output, lock_path, "x86_64")

                # Retiring a core removes only its managed files, preserving
                # the other core and a bundle that can still be installed.
                preserved = (output / "gambatte_libretro.so").read_bytes()
                del lock["cores"]["mgba"]
                lock_path.write_text(json.dumps(lock))
                invoke("--core", "gambatte")
                cores.verify(output, lock_path, "x86_64")
                self.assertFalse((output / "mgba_libretro.so").exists())
                self.assertFalse(any((output / "licenses/mgba").rglob("*")))
                self.assertEqual((output / "gambatte_libretro.so").read_bytes(), preserved)

    def test_integrity_and_architecture(self):
        for arch, machine in cores.MACHINES.items():
            with self.subTest(arch=arch), tempfile.TemporaryDirectory() as directory:
                bundle = Path(directory)
                lock = ROOT / "cores.lock.json"
                data = cores.load_lock(lock)
                (bundle / "cores.lock.json").write_bytes(lock.read_bytes())
                for recipe in data["cores"].values():
                    header = bytearray(64)
                    header[:6] = b"\x7fELF\x02\x01"
                    struct.pack_into("<HH", header, 16, 3, machine)
                    (bundle / recipe["artifact"]).write_bytes(header)
                for recipe in data["cores"].values():
                    for asset in recipe.get("assets", []):
                        target = bundle / asset["destination"] / "fixture"
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(b"asset fixture")
                manifest = {"schema": 1, "architecture": arch, "lock_sha256": cores.digest(lock),
                            "cores": {},
                            "files": {p.relative_to(bundle).as_posix(): cores.digest(p) for p in bundle.rglob("*") if p.is_file()}}
                for name, recipe in data["cores"].items():
                    manifest["cores"][name] = {"artifact": recipe["artifact"], "recipe_sha256": cores.recipe_digest(recipe),
                                               "files": cores.owned_files(name, recipe, manifest["files"]),
                                               "toolchain_sha256": None, "provenance": {}}
                (bundle / "manifest.json").write_text(json.dumps(manifest))
                cores.verify(bundle, lock, arch)
                with self.assertRaises(ValueError):
                    cores.verify(bundle, lock, "aarch64" if arch == "x86_64" else "x86_64")
                changed = bundle / next(iter(manifest["files"]))
                original = changed.read_bytes()
                changed.write_bytes(original + b"tampered")
                with self.assertRaises(ValueError):
                    cores.verify(bundle, lock, arch)
                changed.write_bytes(original)
                (bundle / "unmanaged.so").write_bytes(original)
                with self.assertRaises(ValueError):
                    cores.verify(bundle, lock, arch)
                (bundle / "unmanaged.so").unlink()
                altered_lock = copy.deepcopy(data)
                altered_lock["cores"][next(iter(data["cores"]))]["version"] = "changed"
                other = bundle / "other-lock.json"
                other.write_text(json.dumps(altered_lock))
                with self.assertRaises(ValueError):
                    cores.verify(bundle, other, arch)
                # A manifest claiming ARM cannot make an x86 ELF valid (or vice versa).
                other.unlink()
                header = bytearray(original)
                struct.pack_into("<H", header, 18, 183 if machine == 62 else 62)
                changed.write_bytes(header)
                manifest["files"][changed.name] = cores.digest(changed)
                (bundle / "manifest.json").write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    cores.verify(bundle, lock, arch)


if __name__ == "__main__":
    unittest.main()
