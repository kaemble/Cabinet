#!/usr/bin/env python3
"""Build the locked SDL release as a static CMake package (Python 3.12+)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def run(*command):
    print("+", *map(str, command), flush=True)
    subprocess.run(list(map(str, command)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=ROOT / "build/sdl3-static",
                        help="SDL install prefix (default: build/sdl3-static)")
    parser.add_argument("--work-dir", type=Path, default=ROOT / "build/sdl-work")
    parser.add_argument("--toolchain", type=Path, help="CMake toolchain for a cross build")
    parser.add_argument("--archive", type=Path, help="Use an offline archive; the locked checksum is still required")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    dependency = json.loads((ROOT / "dependencies.lock.json").read_text())["sdl3"]
    version, sha256 = dependency["version"], dependency["sha256"]
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    archive = args.archive.resolve() if args.archive else work / f"SDL3-{version}.tar.gz"
    if not archive.exists():
        if args.archive:
            parser.error(f"Archive does not exist: {archive}")
        with tempfile.TemporaryDirectory(dir=work) as temporary:
            download = Path(temporary) / "SDL.tar.gz"
            print(f"Downloading SDL {version}", flush=True)
            urllib.request.urlretrieve(dependency["url"], download)
            if digest(download) != sha256:
                raise ValueError("SDL archive checksum does not match dependencies.lock.json")
            download.replace(archive)
    if digest(archive) != sha256:
        raise ValueError(f"SDL archive checksum does not match dependencies.lock.json: {archive}")
    source = work / f"source-{sha256}" / f"SDL3-{version}"
    if not source.exists():
        with tempfile.TemporaryDirectory(dir=work) as temporary:
            with tarfile.open(archive) as contents:
                contents.extractall(temporary, filter="data")
            source.parent.mkdir(exist_ok=True)
            (Path(temporary) / f"SDL3-{version}").rename(source)
    configure = ["cmake", "-S", source]
    build_name = f"native-{platform.machine()}"
    if args.toolchain:
        toolchain = args.toolchain.resolve(strict=True)
        build_name = "cross-" + hashlib.sha256(str(toolchain).encode() + toolchain.read_bytes()).hexdigest()[:16]
        configure.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
    build = work / f"build-{version}-{sha256[:12]}-{build_name}"
    run(*configure, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={args.prefix.resolve()}", "-DCMAKE_INSTALL_LIBDIR=lib",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON",
        "-DSDL_DEPS_SHARED=ON", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF")
    run("cmake", "--build", build, "--parallel", args.jobs)
    run("cmake", "--install", build)
    print(f"SDL {version} installed. Configure Cabinet with -DCMAKE_PREFIX_PATH={args.prefix.resolve()}")


if __name__ == "__main__":
    main()
