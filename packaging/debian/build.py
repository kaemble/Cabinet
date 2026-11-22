#!/usr/bin/env python3
"""Build Cabinet's Debian 13 baseline with Docker (host needs Python 3)."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
APPIMAGE_TEMPLATE = Path(__file__).with_name("package-appimage.py.in")


def write_appimage_script(output, preset, arch, host, image_id):
    config = dict(root=str(ROOT), preset=preset, arch=arch,
                  docker_platform=f"linux/{host}", image_id=image_id)
    script = output / "package-appimage.py"
    script.write_text(APPIMAGE_TEMPLATE.read_text().replace("@CONFIG@", repr(config)))
    script.chmod(0o755)


def run(command, **kwargs):
    print("+", " ".join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def main():
    cores = json.loads((ROOT / "cores.lock.json").read_text())["cores"]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("preset", choices=("x86_64", "aarch64", "ArmadaOS"))
    parser.add_argument("target", choices=("cabinet", "sdl", "cores", *cores),
                        help="Cabinet (including SDL), SDL only, all cores, or a locked core name")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if not shutil.which("docker"):
        parser.exit(1, "Docker is not installed or is not on PATH. Install Docker Engine or Docker Desktop first.\n")
    try:
        info = subprocess.run(["docker", "info", "--format", "{{json .}}"],
                              capture_output=True, text=True)
        server = json.loads(info.stdout) if info.returncode == 0 else {}
        if not server.get("ServerVersion"):
            parser.exit(1, "Docker is not running or its daemon is inaccessible. Start Docker and check your socket permissions/context.\n" + info.stderr)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Cannot query the Docker daemon: {error}\n")
    machine = server.get("Architecture")
    if machine in ("x86_64", "amd64"):
        host = "amd64"
    elif machine in ("aarch64", "arm64"):
        host = "arm64"
    else:
        parser.error(f"Unsupported Docker server architecture: {machine}")
    if server.get("OSType") != "linux":
        parser.error("Select a Linux Docker daemon/container mode")
    arch = "x86_64" if args.preset == "x86_64" else "aarch64"
    if host == "arm64" and arch == "x86_64":
        parser.error("The x86_64 preset needs an amd64 Docker daemon; ARM-to-x86 cross-compilation is not supported")
    target_arch = "amd64" if arch == "x86_64" else "arm64"
    # Read the ID produced by this build; shared mutable tags race when presets
    # build concurrently and can select a different environment for the cache.
    with tempfile.TemporaryDirectory(prefix="cabinet-docker-") as temporary:
        iidfile = Path(temporary) / "image-id"
        run(["docker", "build", "--platform", f"linux/{host}", "--build-arg", f"TARGET_ARCH={target_arch}",
             "--iidfile", iidfile, "-f", ROOT / "packaging/debian/Dockerfile", ROOT])
        image_id = iidfile.read_text().strip()
    # An updated image must never reuse cores/objects compiled against an older environment.
    output = ROOT / "release/docker" / args.preset / image_id.removeprefix("sha256:")
    output.mkdir(parents=True, exist_ok=True)
    command = ["docker", "run", "--rm", "--platform", f"linux/{host}",
               "--mount", f"type=bind,src={ROOT},dst=/src,readonly",
               "--mount", f"type=bind,src={output},dst=/out", "-w", "/src"]
    if hasattr(os, "getuid"):
        command += ["--user", f"{os.getuid()}:{os.getgid()}"]
    run([*command, image_id, "python3", "/opt/cabinet/run-build.py",
         args.preset, args.target, "--jobs", args.jobs])
    write_appimage_script(output, args.preset, arch, host, image_id)
    print(f"Build output: {output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
