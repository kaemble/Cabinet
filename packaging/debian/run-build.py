#!/usr/bin/env python3
"""Container-side dispatcher; use build.py from the host."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess


def run(*command):
    print("+", *map(str, command), flush=True)
    subprocess.run(list(map(str, command)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("preset", choices=("x86_64", "aarch64", "ArmadaOS"))
    parser.add_argument("target")
    parser.add_argument("--jobs", type=int, required=True)
    args = parser.parse_args()
    arch = "x86_64" if args.preset == "x86_64" else "aarch64"
    cross = arch != platform.machine()
    toolchain_path = "/opt/cabinet/armadaos.cmake" if args.preset == "ArmadaOS" else "/opt/cabinet/aarch64.cmake"
    toolchain = ["--toolchain", toolchain_path] if cross else []
    cmake_toolchain = [f"-DCMAKE_TOOLCHAIN_FILE={toolchain_path}"] if cross else []
    os.environ.update(HOME="/tmp", XDG_CACHE_HOME="/tmp/cabinet-cache")
    if cross:
        os.environ["PKG_CONFIG_LIBDIR"] = "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
        os.environ["PKG_CONFIG_PATH"] = ""
    out = Path("/out")
    # Record the actual installed package revisions alongside the artifacts.
    packages = subprocess.check_output(["dpkg-query", "-W", "-f=${Package}:${Architecture}=${Version}\\n"], text=True)
    (out / "debian-packages.txt").write_text(packages)
    (out / "build-environment.json").write_text(json.dumps({
        "preset": args.preset, "architecture": arch, "cross": cross,
        "base_image": Path("/opt/cabinet/base-image").read_text().strip(),
        "debian_snapshot": Path("/opt/cabinet/debian-snapshot").read_text().strip(),
    }, indent=2) + "\n")
    if args.target in ("sdl", "cabinet"):
        run("python3", "tools/build_sdl.py", "--prefix", out / "sdl3-static",
            "--work-dir", out / "sdl-work", "--jobs", args.jobs, *toolchain)
    if args.target == "cabinet":
        run("cmake", "-S", "/src", "-B", out / "cabinet", "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
            f"-DCMAKE_PREFIX_PATH={out / 'sdl3-static'}",
            f"-DCABINET_LOCKED_CORES_DIR={out / 'cores'}",
            f"-DCABINET_ARMADA_DRM={'ON' if args.preset == 'ArmadaOS' else 'OFF'}", *cmake_toolchain)
        run("cmake", "--build", out / "cabinet", "--parallel", args.jobs)
        if not cross:
            run("python3", "packaging/debian/verify-runtime.py", out / "cabinet/cabinet", out / "cabinet/cabinet-runner")
    elif args.target != "sdl":
        selected = [] if args.target == "cores" else ["--core", args.target]
        strip = "aarch64-linux-gnu-strip" if cross else "strip"
        run("python3", "tools/build_cores.py", "--arch", arch, "--output", out / "cores",
            "--work-dir", out / "core-work", "--jobs", args.jobs, "--strip", strip, *toolchain, *selected)
        run("python3", "tools/build_cores.py", "--verify", "--arch", arch,
            "--output", out / "cores", *selected)


if __name__ == "__main__":
    main()
