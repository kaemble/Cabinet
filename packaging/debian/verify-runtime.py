#!/usr/bin/env python3
"""Check Cabinet ELF dependencies inside the Debian 13 build environment."""
import argparse
from pathlib import Path
import platform
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaries", type=Path, nargs="+", help="Cabinet executables to check for static SDL and runtime dependencies")
    parser.add_argument("--cores", type=Path, help="Also check a native core bundle's shared-library dependencies")
    args = parser.parse_args()
    release = platform.freedesktop_os_release()
    if release.get("ID") != "debian" or release.get("VERSION_ID") != "13":
        parser.error("Run this check inside Debian 13; the host's newer libraries cannot prove baseline compatibility")
    paths = [(path, True) for path in args.binaries]
    if args.cores:
        cores = sorted(args.cores.glob("*_libretro.so"))
        if not cores:
            parser.error(f"No cores found in {args.cores}")
        paths.extend((path, False) for path in cores)
    failed = False
    for path, frontend in paths:
        path = path.resolve(strict=True)
        dynamic = subprocess.check_output(["readelf", "-d", str(path)], text=True)
        if frontend and re.search(r"Shared library: \[libSDL3[^]]*\]", dynamic):
            print(f"FAIL {path.name}: SDL3 is dynamically linked")
            failed = True
        dependencies = subprocess.run(["ldd", "-r", str(path)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if dependencies.returncode or re.search(r"not found|undefined symbol", dependencies.stdout):
            print(f"FAIL {path.name}:\n{dependencies.stdout}")
            failed = True
        else:
            print(f"Resolved Debian 13 runtime dependencies: {path.name}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
