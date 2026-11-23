#!/usr/bin/env python3
"""Exercise Docker orchestration without requiring a daemon or downloading sources."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("docker_build", ROOT / "packaging/debian/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


spec = importlib.util.spec_from_file_location("docker_dispatch", ROOT / "packaging/debian/run-build.py")
dispatch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dispatch)

appimage = types.ModuleType("appimage")
exec(compile(build.APPIMAGE_TEMPLATE.read_text().replace("@CONFIG@", repr({
    "arch": "aarch64", "preset": "ArmadaOS", "root": "/checkout",
    "docker_platform": "linux/amd64", "image_id": "sha256:123abc",
})), str(build.APPIMAGE_TEMPLATE), "exec"), appimage.__dict__)


class AppImageTest(unittest.TestCase):
    def test_dependency_walk_bundles_app_libraries_without_shadowing_host_drivers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            appdir = root / "AppDir"
            (appdir / "usr/bin").mkdir(parents=True)
            (appdir / "usr/bin/cabinet").touch()
            sysroot = root / "sysroot"
            libraries = sysroot / "usr/lib/aarch64-linux-gnu"
            libraries.mkdir(parents=True)
            (libraries / "libarchive.so.13").write_bytes(b"application dependency")
            host_dependencies = [
                "libstdc++.so.6", "libgcc_s.so.1", "libc.so.6", "libmvec.so.1",
                "libvulkan.so.1", "libEGL.so.1", "libdrm_amdgpu.so.1",
                "libwayland-client.so.0", "libX11.so.6", "libX11-xcb.so.1",
                "libxcb.so.1", "libxcb-dri3.so.0", "libxkbcommon.so.0",
            ]

            def inspect(command, **kwargs):
                if command[0] == "dpkg-query":
                    return "libarchive13t64:arm64: /usr/lib/aarch64-linux-gnu/libarchive.so.13\n"
                dependencies = ["libarchive.so.13"] if Path(command[-1]).name == "cabinet" else host_dependencies
                return "\n".join(f"(NEEDED) Shared library: [{name}]" for name in dependencies)

            def path_in_sysroot(path):
                return sysroot / str(path).lstrip("/")

            # Only libarchive exists in this sysroot. Trying to bundle or inspect
            # any host driver dependency would fail the walk.
            with patch.object(appimage, "Path", side_effect=path_in_sysroot), \
                 patch.object(appimage.subprocess, "check_output", side_effect=inspect):
                appimage.bundle_libraries(appdir, "aarch64")
            self.assertEqual([p.name for p in (appdir / "usr/lib").iterdir()], ["libarchive.so.13"])
            self.assertEqual((appdir / "usr/lib/libarchive.so.13").read_bytes(), b"application dependency")

    def test_launcher_environment_arguments_and_system_seeding(self):
        with tempfile.TemporaryDirectory(prefix="cabinet appimage ") as temporary:
            root = Path(temporary)
            appdir = root / "Cabinet.AppDir"
            binaries = appdir / "usr/bin"
            binaries.mkdir(parents=True)
            assets = appdir / "usr/share/cabinet/cores/system"
            assets.mkdir(parents=True)
            (assets / "firmware").write_text("bundled")
            (assets / ".hidden").write_text("hidden asset")
            (assets / "nested").mkdir()
            (assets / "nested/data").write_text("nested asset")
            (binaries / "cabinet").write_text(
                '#!/bin/sh\nprintf "%s\\n" "$PWD" "$LD_LIBRARY_PATH" "$PATH" "$XDG_DATA_DIRS" "$@"\n'
                'test -x "$(dirname "$0")/cabinet-runner"\n')
            (binaries / "cabinet").chmod(0o755)
            (binaries / "cabinet-runner").touch(mode=0o755)
            launcher = appdir / "AppRun"
            launcher.write_text(appimage.app_run("x86_64"))
            launcher.chmod(0o755)
            home = root / "home"
            system = home / ".local/share/cabinet/system"
            env = dict(os.environ, HOME=str(home), LD_LIBRARY_PATH="/custom/lib",
                       XDG_DATA_DIRS="/custom/share")

            def launch():
                return subprocess.check_output([str(launcher), "--data-dir", "profile with spaces"],
                                               cwd=root, env=env, text=True).splitlines()

            result = launch()
            self.assertEqual(result[0], str(root))
            self.assertEqual(result[1], f"{appdir}/usr/lib:/custom/lib")
            self.assertTrue(result[2].startswith(f"{binaries}:"))
            self.assertEqual(result[3], f"{appdir}/usr/share:/custom/share")
            self.assertEqual(result[4:], ["--data-dir", "profile with spaces"])
            self.assertEqual((system / "nested/data").read_text(), "nested asset")
            (system / "firmware").write_text("user firmware")
            (assets / "new-file").touch()
            launch()
            self.assertEqual((system / "firmware").read_text(), "user firmware")
            self.assertFalse((system / "new-file").exists())
            # Even a lone hidden entry means that this is an existing profile.
            import shutil
            shutil.rmtree(system)
            system.mkdir()
            (system / ".keep").touch()
            launch()
            self.assertEqual(list(system.iterdir()), [system / ".keep"])

    def test_armada_launcher_enables_dual_screen_from_device_environment(self):
        with tempfile.TemporaryDirectory(prefix="cabinet armada ") as temporary:
            root = Path(temporary)
            (root / "usr/bin").mkdir(parents=True)
            (root / "usr/share/cabinet/cores/system").mkdir(parents=True)
            cabinet = root / "usr/bin/cabinet"
            cabinet.write_text('#!/bin/sh\nprintf "%s\\n" "${CABINET_DUAL_SCREEN:-unset}" "${ARMADA_DEVICE_TEST:-unset}" "$@"\n')
            cabinet.chmod(0o755)
            helper = root / "device-env"
            helper.touch(mode=0o755)
            launcher = root / "AppRun"
            env = dict(os.environ, HOME=str(root / "home"))
            env.pop("CABINET_DUAL_SCREEN", None)
            env.pop("ARMADA_SECONDARY_CONNECTOR", None)
            env.pop("ARMADA_DEVICE_TEST", None)
            for preset in ("ArmadaOS", "aarch64", "x86_64"):
                script = appimage.app_run(preset)
                self.assertEqual("/usr/libexec/armada/device-env" in script, preset == "ArmadaOS")
                launcher.write_text(script.replace("/usr/libexec/armada/device-env", f'"{helper}"'))
                launcher.chmod(0o755)
                for connector in ("DSI-2", "", None):
                    assignment = f"ARMADA_SECONDARY_CONNECTOR='{connector}'" if connector is not None else "unset ARMADA_SECONDARY_CONNECTOR"
                    helper.write_text('#!/bin/sh\ncat <<\'ENV\'\n' + assignment + '\nexport ARMADA_DEVICE_TEST="device settings"\nENV\n')
                    result = subprocess.check_output([str(launcher), "--data-dir", "profile with spaces"],
                                                     env=env, text=True).splitlines()
                    expected = "1" if preset == "ArmadaOS" and connector else "unset"
                    self.assertEqual(result[0], expected)
                    self.assertEqual(result[1], "device settings" if preset == "ArmadaOS" else "unset")
                    self.assertEqual(result[2:], ["--data-dir", "profile with spaces"])

    def test_packaging_uses_host_tool_and_target_runtime_architecture(self):
        with tempfile.TemporaryDirectory() as temporary, \
             patch.object(appimage.Path, "cwd", return_value=Path(temporary)), \
             patch("sys.argv", ["package-appimage.py", "--runtime-file", "/runtime-aarch64"]), \
             patch.object(appimage.shutil, "which", return_value="/tools/appimagetool"), \
             patch.object(appimage, "run") as run, contextlib.redirect_stdout(io.StringIO()):
            appimage.main()
            container, package = run.call_args_list
            self.assertIn("linux/amd64", container.args[0])
            self.assertIn("sha256:123abc", container.args[0])
            self.assertEqual(container.args[0][-3:], ["python3", "/out/package-appimage.py", "--stage"])
            self.assertEqual(package.kwargs["env"]["ARCH"], "aarch64")
            self.assertEqual(package.kwargs["env"]["APPIMAGE_EXTRACT_AND_RUN"], "1")
            self.assertEqual(package.args[0][:5], ["/tools/appimagetool", "--comp", "zstd",
                                                 "--runtime-file", Path("/runtime-aarch64")])

    def test_missing_target_dependency_fails_packaging(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary)
            (appdir / "usr/bin").mkdir(parents=True)
            (appdir / "usr/bin/cabinet").touch()
            with patch.object(appimage.subprocess, "check_output", return_value=
                              "(NEEDED) Shared library: [libcabinet-nonexistent.so.1]"), \
                 self.assertRaisesRegex(RuntimeError, "Unresolved aarch64 dependency"):
                appimage.bundle_libraries(appdir, "aarch64")

    def test_staging_publishes_complete_layout_and_preserves_previous_on_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            previous = output / "Cabinet.AppDir"
            previous.mkdir()
            (previous / "previous-build").touch()
            for name in ("build-environment.json", "debian-packages.txt"):
                (output / name).write_text("build provenance")

            def install(command):
                self.assertEqual(command[:2], ["cmake", "--install"])
                prefix = Path(command[-1])
                (prefix / "bin").mkdir(parents=True)
                for name in ("cabinet", "cabinet-runner"):
                    (prefix / "bin" / name).touch(mode=0o755)

            with patch.object(appimage, "run", side_effect=install), \
                 patch.object(appimage, "bundle_libraries", side_effect=RuntimeError("missing library")), \
                 self.assertRaisesRegex(RuntimeError, "missing library"):
                appimage.stage(output)
            self.assertTrue((previous / "previous-build").exists())
            with patch.object(appimage, "run", side_effect=install), \
                 patch.object(appimage, "bundle_libraries") as bundle:
                appimage.stage(output)
                self.assertEqual(bundle.call_args.args[1], "aarch64")
            self.assertFalse((previous / "previous-build").exists())
            self.assertTrue((previous / "usr/bin/cabinet-runner").is_file())
            self.assertTrue((previous / "usr/share/cabinet/cores/system").is_dir())
            self.assertEqual((previous / ".DirIcon").read_text(), appimage.ICON)
            self.assertTrue(os.access(previous / "AppRun", os.X_OK))
            self.assertEqual((previous / "AppRun").read_text(), appimage.app_run("ArmadaOS"))


class ContainerDispatchTest(unittest.TestCase):
    def invoke(self, preset, target, machine="x86_64"):
        with patch("sys.argv", ["run-build.py", preset, target, "--jobs", "2"]), \
             patch.object(dispatch.platform, "machine", return_value=machine), \
             patch.object(dispatch.subprocess, "check_output", return_value="libc6:arm64=2.41\n"), \
             patch.object(Path, "write_text"), patch.object(Path, "read_text", return_value="pin"), \
             patch.dict(os.environ), patch.object(dispatch, "run") as run:
            dispatch.main()
            return [list(map(str, call.args)) for call in run.call_args_list]

    def test_armada_cross_build_uses_target_sdl_and_drm(self):
        sdl, configure, compile_command = self.invoke("ArmadaOS", "cabinet")
        self.assertIn("--toolchain", sdl)
        self.assertIn("/opt/cabinet/armadaos.cmake", sdl)
        self.assertIn("-DCABINET_ARMADA_DRM=ON", configure)
        self.assertIn("-DCMAKE_TOOLCHAIN_FILE=/opt/cabinet/armadaos.cmake", configure)
        self.assertIn("-DCABINET_LOCKED_CORES_DIR=/out/cores", configure)
        self.assertIn("--build", compile_command)

    def test_sdl_only_never_builds_cabinet_or_cores(self):
        commands = self.invoke("x86_64", "sdl")
        self.assertEqual(len(commands), 1)
        self.assertIn("tools/build_sdl.py", commands[0])
        self.assertNotIn("--toolchain", commands[0])

    def test_individual_core_is_verified_without_building_sdl(self):
        compile_command, verify = self.invoke("aarch64", "mgba")
        self.assertIn("tools/build_cores.py", compile_command)
        self.assertIn("/opt/cabinet/aarch64.cmake", compile_command)
        self.assertEqual(compile_command[-2:], ["--core", "mgba"])
        self.assertIn("--verify", verify)
        self.assertEqual(verify[-2:], ["--core", "mgba"])

    def test_all_cores(self):
        compile_command, verify = self.invoke("x86_64", "cores")
        self.assertNotIn("--core", compile_command)
        self.assertNotIn("--core", verify)

    def test_armada_cross_cores_use_optimized_toolchain(self):
        for target in ("cores", "mgba", "gambatte"):
            compile_command, verify = self.invoke("ArmadaOS", target)
            self.assertIn("/opt/cabinet/armadaos.cmake", compile_command)
            self.assertIn("--verify", verify)

    def test_native_armada_does_not_use_cross_toolchain(self):
        commands = self.invoke("ArmadaOS", "cabinet", "aarch64")
        self.assertNotIn("--toolchain", commands[0])
        self.assertFalse(any(arg.startswith("-DCMAKE_TOOLCHAIN_FILE=") for arg in commands[1]))

    def test_native_cabinet_checks_runtime(self):
        commands = self.invoke("aarch64", "cabinet", "aarch64")
        self.assertIn("-DCABINET_ARMADA_DRM=OFF", commands[1])
        self.assertIn("packaging/debian/verify-runtime.py", commands[-1])
        self.assertNotIn("--toolchain", commands[0])


class DockerBuildTest(unittest.TestCase):
    def invoke(self, preset="aarch64", target="mgba", machine="x86_64", installed=True, running=True):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        (root / "cores.lock.json").write_text(json.dumps({"cores": {"mgba": {}}}))
        info = {"Architecture": machine, "ServerVersion": "29", "OSType": "linux"} if running else {}
        def fake_run(command):
            if command[1] == "build":
                Path(command[command.index("--iidfile") + 1]).write_text("sha256:123abc\n")

        with patch.object(build, "ROOT", root), \
             patch("sys.argv", ["build.py", preset, target, "--jobs", "2"]), \
             patch.object(build.shutil, "which", return_value="/usr/bin/docker" if installed else None), \
             patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, json.dumps(info), "")), \
             patch.object(build, "run", side_effect=fake_run) as run, contextlib.redirect_stdout(io.StringIO()), \
             contextlib.redirect_stderr(io.StringIO()):
            build.main()
            return [call.args[0] for call in run.call_args_list]

    def test_cross_core_uses_native_image_and_preserves_selection(self):
        image, container = self.invoke()
        self.assertIn("linux/amd64", image)
        self.assertIn("TARGET_ARCH=arm64", image)
        self.assertEqual(container[-4:], ["aarch64", "mgba", "--jobs", 2])
        self.assertTrue(any(str(arg).endswith("dst=/src,readonly") for arg in container))
        self.assertTrue(any("aarch64/123abc" in str(arg) for arg in container))
        self.assertIn("sha256:123abc", container)

    def test_armada_and_native_arm(self):
        image, container = self.invoke("ArmadaOS", "cabinet", "aarch64")
        self.assertIn("linux/arm64", image)
        self.assertIn("TARGET_ARCH=arm64", image)
        self.assertEqual(container[-4:], ["ArmadaOS", "cabinet", "--jobs", 2])

    def test_x86_sdl(self):
        image, container = self.invoke("x86_64", "sdl")
        self.assertIn("TARGET_ARCH=amd64", image)
        self.assertEqual(container[-4:], ["x86_64", "sdl", "--jobs", 2])

    def test_generated_script_records_each_platform(self):
        for preset, arch in (("x86_64", "x86_64"), ("aarch64", "aarch64"), ("ArmadaOS", "aarch64")):
            self.invoke(preset, "cabinet")
            script = Path(self.temporary.name) / "release/docker" / preset / "123abc/package-appimage.py"
            namespace = {"__name__": "generated_appimage"}
            exec(compile(script.read_text(), str(script), "exec"), namespace)
            self.assertEqual(namespace["CONFIG"]["arch"], arch)
            self.assertEqual(namespace["CONFIG"]["preset"], preset)
            self.assertEqual(namespace["CONFIG"]["image_id"], "sha256:123abc")
            self.assertTrue(os.access(script, os.X_OK))

    def test_missing_docker(self):
        with self.assertRaises(SystemExit) as error:
            self.invoke(installed=False)
        self.assertEqual(error.exception.code, 1)

    def test_unavailable_daemon(self):
        with self.assertRaises(SystemExit) as error:
            self.invoke(running=False)
        self.assertEqual(error.exception.code, 1)

    def test_reject_arm_host_for_x86(self):
        with self.assertRaises(SystemExit):
            self.invoke("x86_64", "cabinet", "aarch64")

    def test_reject_unknown_core(self):
        with self.assertRaises(SystemExit):
            self.invoke(target="unknown")


if __name__ == "__main__":
    unittest.main()
