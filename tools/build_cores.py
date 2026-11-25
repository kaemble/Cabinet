#!/usr/bin/env python3
"""Build or verify Cabinet's locked Linux cores (Python 3.12+)."""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MACHINES = {"x86_64": 62, "aarch64": 183}


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def architecture(value):
    value = {"amd64": "x86_64", "arm64": "aarch64"}.get(value.lower(), value.lower())
    if value not in MACHINES:
        raise ValueError(f"Unsupported architecture: {value}")
    return value


def elf_check(path, arch):
    with path.open("rb") as source:
        header = source.read(20)
    if len(header) != 20 or header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", header, 16) != (3, MACHINES[arch]):
        raise ValueError(f"{path}: expected a Linux {arch} ELF64 shared library")


def relative_path(value):
    return bool(value) and not Path(value).is_absolute() and ".." not in Path(value).parts


def check_source(source):
    if not re.fullmatch(r"[0-9a-f]{40}", source["commit"]) or not re.fullmatch(r"[0-9a-f]{64}", source["sha256"]):
        raise ValueError("Invalid source pin")
    if not source["source"].startswith("https://") or not relative_path(source["source_directory"]) or Path(source["source_directory"]).name != source["source_directory"]:
        raise ValueError("Invalid source archive")


def load_lock(path):
    lock = json.loads(path.read_text())
    if lock["schema"] != 1 or lock["platform"] != "linux" or set(lock["architectures"]) != set(MACHINES):
        raise ValueError("Unsupported core lockfile schema/platform/architectures")
    artifacts = set()
    for name, core in lock["cores"].items():
        if not re.fullmatch(r"[a-z0-9_]+", name):
            raise ValueError(f"Invalid core name: {name}")
        check_source(core)
        if "archive_git_metadata" in core and not isinstance(core["archive_git_metadata"], bool):
            raise ValueError(f"Invalid archive Git metadata setting: {name}")
        for relative in core["license_files"]:
            if not relative_path(relative):
                raise ValueError(f"Invalid license path: {name}")
        if Path(core["artifact"]).name != core["artifact"] or not core["artifact"].endswith(".so") or core["artifact"] in artifacts:
            raise ValueError(f"Invalid or duplicate artifact: {name}")
        artifacts.add(core["artifact"])
        if core.get("build_system", "cmake") not in ("cmake", "make"):
            raise ValueError(f"Unsupported build system: {name}")
        for field in ("build_directory", "artifact_path", "makefile"):
            if field in core and not relative_path(core[field]):
                raise ValueError(f"Invalid {field}: {name}")
        if "architectures" in core and set(core["architectures"]) != set(MACHINES):
            raise ValueError(f"Missing architecture recipe: {name}")
        for change in core.get("source_changes", []):
            if (not relative_path(change["path"]) or not isinstance(change["before"], str) or
                    not change["before"] or not isinstance(change["after"], str)):
                raise ValueError(f"Invalid source change: {name}")
        paths = set()
        for dependency in core.get("submodules", []):
            check_source(dependency)
            if not relative_path(dependency["path"]) or dependency["path"] in paths:
                raise ValueError(f"Invalid or duplicate submodule: {name}")
            paths.add(dependency["path"])
        for asset in core.get("assets", []):
            if not relative_path(asset["source"]) or not relative_path(asset["destination"]) or not asset["destination"].startswith("system/"):
                raise ValueError(f"Invalid asset path: {name}")
    return lock


def recipe_digest(core):
    return hashlib.sha256(json.dumps(core, sort_keys=True).encode()).hexdigest()


def owned_files(name, core, files):
    prefixes = [f"licenses/{name}/"] + [asset["destination"] + "/" for asset in core.get("assets", [])]
    return {relative: checksum for relative, checksum in files.items()
            if relative == core["artifact"] or any(relative.startswith(prefix) for prefix in prefixes)}


def read_bundle(bundle, arch):
    """Check integrity and retain each core's own recipe and file inventory."""
    lock_path = bundle / "cores.lock.json"
    lock = load_lock(lock_path)
    manifest = json.loads((bundle / "manifest.json").read_text())
    if manifest["schema"] != 1 or manifest["architecture"] != arch or manifest["lock_sha256"] != digest(lock_path):
        raise ValueError("Core bundle architecture or recorded lockfile does not match")
    if any(p.is_symlink() for p in bundle.rglob("*")):
        raise ValueError("Core bundles must not contain symbolic links")
    actual_files = {p.relative_to(bundle).as_posix() for p in bundle.rglob("*") if p.is_file() and p != bundle / "manifest.json"}
    if actual_files != set(manifest["files"]):
        raise ValueError("Core bundle file inventory does not match")
    for relative, checksum in manifest["files"].items():
        path = bundle / relative
        if not relative_path(relative) or not path.resolve().is_relative_to(bundle.resolve()) or digest(path) != checksum:
            raise ValueError(f"Core bundle checksum mismatch: {relative}")
    claimed = {"cores.lock.json"}
    artifacts = set()
    for name, receipt in manifest["cores"].items():
        required = {"artifact", "recipe_sha256", "files", "toolchain_sha256"}
        if name not in lock["cores"] or not required.issubset(receipt):
            raise ValueError(f"Invalid core receipt: {name}")
        for relative, checksum in receipt["files"].items():
            if relative in claimed or manifest["files"].get(relative) != checksum:
                raise ValueError(f"Invalid core file ownership: {relative}")
            claimed.add(relative)
        if receipt["artifact"] not in receipt["files"]:
            raise ValueError(f"Missing library for {name}")
        elf_check(bundle / receipt["artifact"], arch)
        artifacts.add(receipt["artifact"])
    if claimed != set(manifest["files"]) or {p.name for p in bundle.glob("*.so")} != artifacts:
        raise ValueError("Core bundle contains missing or unmanaged files")
    return manifest


def verify(bundle, lock_path, arch, selected=None):
    lock = load_lock(lock_path)
    manifest = read_bundle(bundle, arch)
    names = set(selected or lock["cores"])
    if not names <= set(manifest["cores"]) or (selected is None and set(manifest["cores"]) != names):
        raise ValueError("Core bundle selection is incomplete or unexpected")
    if selected is None and manifest["lock_sha256"] != digest(lock_path):
        raise ValueError("Core bundle lockfile does not match")
    for name in names:
        core = lock["cores"][name]
        receipt = manifest["cores"][name]
        if receipt["recipe_sha256"] != recipe_digest(core) or receipt["artifact"] != core["artifact"]:
            raise ValueError(f"Stale core recipe: {name}")
        for asset in core.get("assets", []):
            if not any(relative.startswith(asset["destination"] + "/") for relative in receipt["files"]):
                raise ValueError(f"Missing packaged assets: {name}")
    return manifest


def run(command, ceiling, cwd=None):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True,
                   env={**os.environ, "GIT_CEILING_DIRECTORIES": str(ceiling)}, cwd=cwd)


def extract_source(pin, cache, destination, offline):
    archive = cache / (pin["sha256"] + ".tar.gz")
    if not archive.exists():
        if offline:
            raise ValueError(f"Missing cached source: {archive}")
        partial = archive.with_suffix(".download")
        print(f"Downloading {pin['source']}", flush=True)
        with urllib.request.urlopen(pin["source"], timeout=60) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
        if digest(partial) != pin["sha256"]:
            partial.unlink()
            raise ValueError(f"Source checksum mismatch: {pin['repository']}")
        partial.replace(archive)
    if digest(archive) != pin["sha256"]:
        raise ValueError(f"Cached source checksum mismatch: {archive}")
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as source:
        source.extractall(destination, filter="data")
    return destination / pin["source_directory"]


def make_compilers(toolchain, temporary):
    if not toolchain:
        return {"CC": os.environ.get("CC", "cc"), "CXX": os.environ.get("CXX", "c++"), "AR": os.environ.get("AR", "ar")}
    probe = temporary / "compiler-probe"
    probe.mkdir()
    (probe / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(probe C CXX)\n'
        'file(WRITE "${CMAKE_BINARY_DIR}/compilers.txt" "${CMAKE_C_COMPILER}\\n${CMAKE_CXX_COMPILER}\\n${CMAKE_AR}\\n${CMAKE_C_COMPILER_TARGET}\\n${CMAKE_C_FLAGS}\\n${CMAKE_CXX_FLAGS}\\n${CABINET_CORE_COMPILE_FLAGS}\\n")\n')
    build = probe / "build"
    run(["cmake", "-S", probe, "-B", build, f"-DCMAKE_TOOLCHAIN_FILE={toolchain}"], temporary)
    cc, cxx, ar, target, cflags, cxxflags, core_flags = (build / "compilers.txt").read_text().splitlines()
    # Make cores take compiler commands rather than CMake toolchains. Reject
    # target/flag-only toolchains instead of silently producing host binaries.
    if target or cflags or cxxflags:
        raise ValueError("Make core cross-builds require compiler drivers encoding the target/sysroot; flag-only toolchains are unsupported")
    return {"CC": cc, "CXX": cxx, "AR": ar, **({"CABINET_CORE_COMPILE_FLAGS": core_flags} if core_flags else {})}


def copy_notices(source, destination):
    for path in source.rglob("*"):
        if path.is_file() and path.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE", "COPYRIGHT")):
            target = destination / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


def apply_source_changes(source, core):
    for change in core.get("source_changes", []):
        path = source / change["path"]
        if not path.resolve().is_relative_to(source.resolve()):
            raise ValueError(f"Source change escapes the core: {change['path']}")
        text = path.read_text()
        if text.count(change["before"]) != 1:
            raise ValueError(f"Source change must match exactly once: {change['path']}")
        path.write_text(text.replace(change["before"], change["after"], 1))


def build_core(name, core, arch, toolchain, compilers, cache, temporary, stage, jobs, offline):
    print(f"Building {name} for {arch}", flush=True)
    source = temporary / name / core["source_directory"]
    ready = temporary / "sources-ready"
    if not ready.exists():
        source = extract_source(core, cache, temporary / name, offline)
        for index, pin in enumerate(core.get("submodules", [])):
            extracted = extract_source(pin, cache, temporary / f"{name}-dependency-{index}", offline)
            target = source / pin["path"]
            target.mkdir(parents=True, exist_ok=True)
            shutil.copytree(extracted, target, dirs_exist_ok=True, symlinks=True)
        apply_source_changes(source, core)
        ready.write_text("Verified sources prepared\n")
    # Azahar's archive builds consume these upstream-supported metadata files.
    # Write them on resumed builds too, before CMake generates the core identity.
    if core.get("archive_git_metadata"):
        for filename, value in (("GIT-COMMIT", core["commit"]), ("GIT-TAG", core["version"])):
            path = source / filename
            contents = value + "\n"
            if not path.exists() or path.read_text() != contents:
                path.write_text(contents)
    build = temporary / (name + "-build")
    recipe = core.get("architectures", {}).get(arch, {})
    directory = source / core.get("build_directory", ".")
    if core.get("build_system", "cmake") == "cmake":
        options = {**core["cmake"], **recipe}
        arguments = {key: str(value).replace("{source}", str(source)) for key, value in options.items()}
        command = ["cmake", "-S", directory, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
                   *[f"-D{key}={value}" for key, value in sorted(arguments.items())]]
        if toolchain:
            command.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
        run(command, temporary)
        run(["cmake", "--build", build, "--target", core["target"], "--parallel", jobs], temporary)
        artifact = build / core.get("artifact_path", core["artifact"])
        cache_lines = (build / "CMakeCache.txt").read_text().splitlines()
        compiler = [line for line in cache_lines if re.match(r"CMAKE_(C|CXX)_COMPILER(:|_)|CMAKE_(C|CXX)_FLAGS[:_]", line)]
    else:
        options = {**core.get("make", {}), **recipe, **compilers}
        core_flags = options.pop("CABINET_CORE_COMPILE_FLAGS", "")
        extra_makefiles = []
        if core_flags:
            # Append after upstream flags without replacing required defines or PIC.
            flags_file = temporary / "cabinet-flags.mk"
            flags_file.write_text(f"override CFLAGS += {core_flags}\noverride CXXFLAGS += {core_flags}\n")
            extra_makefiles = ["-f", flags_file]
            options["CABINET_CORE_COMPILE_FLAGS"] = core_flags
        for target in core.get("prepare_targets", []):
            # Code generators execute on the build host, also during ARM cross-builds.
            run(["make", "-C", directory, "-f", core["makefile"], target,
                 "CC=" + os.environ.get("CC_FOR_BUILD", "cc"),
                 "CXX=" + os.environ.get("CXX_FOR_BUILD", "c++")], temporary)
        command = ["make", "-C", directory, "-f", core["makefile"], *extra_makefiles, f"-j{jobs}", core["target"],
                   *[f"{key}={value}" for key, value in sorted(options.items())]]
        run(command, temporary)
        artifact = directory / core.get("artifact_path", core["artifact"])
        compiler = options
    elf_check(artifact, arch)
    shutil.copy2(artifact, stage / core["artifact"])
    notices = stage / "licenses" / name
    for relative in core["license_files"]:
        destination = notices / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if (source / relative).is_dir():
            shutil.copytree(source / relative, destination)
        else:
            shutil.copy2(source / relative, destination)
    copy_notices(source, notices / "source-notices")
    for asset in core.get("assets", []):
        destination = stage / asset["destination"]
        if destination.exists():
            raise ValueError(f"Duplicate asset destination: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source / asset["source"], destination)
    return {"version": core["version"], "commit": core["commit"], "repository": core["repository"],
            "source_sha256": core["sha256"], "build_system": core.get("build_system", "cmake"),
            "options": options, "compiler": compiler, "submodules": core.get("submodules", [])}


def reuse_core(directory, name, core, arch, toolchain, stage):
    try:
        return reuse_verified_core(directory, name, core, arch, toolchain, stage)
    except (ValueError, OSError, KeyError) as error:
        print(f"Ignoring invalid {name} cache; rebuilding: {error}", flush=True)
        return None


def reuse_verified_core(directory, name, core, arch, toolchain, stage):
    if not directory.exists():
        return None
    pinned = load_lock(directory / "cores.lock.json")
    if pinned["cores"].get(name) != core:
        return None
    manifest = verify(directory, directory / "cores.lock.json", arch, [name])
    receipt = manifest["cores"][name]
    if receipt["toolchain_sha256"] != (digest(toolchain) if toolchain else None):
        return None
    for relative in receipt["files"]:
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(directory / relative, target)
    receipt["provenance"] = {key: manifest.get(key) for key in
                            ("cabinet_revision", "cabinet_dirty", "builder_sha256", "cmake_version", "toolchain_sha256")}
    print(f"Reusing verified {name} for {arch}", flush=True)
    return receipt


@contextlib.contextmanager
def output_lock(output):
    output.parent.mkdir(parents=True, exist_ok=True)
    with (output.parent / ("." + output.name + ".lock")).open("a") as guard:
        fcntl.flock(guard, fcntl.LOCK_EX)
        yield


def recover_output(output, arch):
    previous = output.with_name("." + output.name + ".previous")
    if previous.exists():
        read_bundle(previous, arch)
        if not output.exists():
            previous.rename(output)
        else:
            read_bundle(output, arch)
            shutil.rmtree(previous)


def publish_core(output, name, receipt, core_stage, lock_bytes, arch, provenance):
    """Publish one complete core, retaining all other cores and their receipts."""
    existing = read_bundle(output, arch) if output.exists() else None
    with tempfile.TemporaryDirectory(prefix="publish-", dir=output.parent) as temporary:
        stage = Path(temporary) / "bundle"
        if existing:
            # Hard-link unchanged files; replace links before writing any new data.
            shutil.copytree(output, stage, copy_function=os.link)
        else:
            stage.mkdir()
        receipts = dict(existing["cores"]) if existing else {}
        # A removed recipe retires only that core's owned files. This also
        # handles core replacements without leaving an unpackageable bundle.
        retired = set(receipts) - set(json.loads(lock_bytes)["cores"])
        for retired_name in retired:
            for relative in receipts.pop(retired_name)["files"]:
                (stage / relative).unlink()
        for relative in receipts.get(name, {}).get("files", {}) if name else ():
            (stage / relative).unlink()
        if name:
            new_files = {p.relative_to(core_stage).as_posix(): digest(p) for p in core_stage.rglob("*") if p.is_file()}
            for relative in new_files:
                target = stage / relative
                if target.exists():
                    raise ValueError(f"Core files overlap: {relative}")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(core_stage / relative, target)
            receipt = dict(receipt, files=new_files)
            receipts[name] = receipt
        for common in ("cores.lock.json", "manifest.json"):
            (stage / common).unlink(missing_ok=True)
        (stage / "cores.lock.json").write_bytes(lock_bytes)
        files = {"cores.lock.json": digest(stage / "cores.lock.json")}
        for entry in receipts.values():
            files.update(entry["files"])
        manifest = dict(provenance, schema=1, architecture=arch,
                        lock_sha256=hashlib.sha256(lock_bytes).hexdigest(), cores=receipts, files=files)
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if name:
            verify(stage, stage / "cores.lock.json", arch, [name])
        else:
            read_bundle(stage, arch)
        previous = output.with_name("." + output.name + ".previous")
        if output.exists():
            output.rename(previous)
        try:
            stage.rename(output)
        except BaseException:
            if previous.exists():
                previous.rename(output)
            raise
        if previous.exists():
            shutil.rmtree(previous)


def build_bundle(args, output):
    toolchain = args.toolchain.resolve() if args.toolchain else None
    toolchain_sha = digest(toolchain) if toolchain else None
    lock_bytes = args.lock.read_bytes()
    lock = load_lock(args.lock)
    if json.loads(lock_bytes) != lock:
        raise ValueError("Lockfile changed while starting the build")
    names = list(dict.fromkeys(args.core or lock["cores"]))
    if set(names) - set(lock["cores"]):
        raise ValueError(f"Unknown cores: {sorted(set(names) - set(lock['cores']))}")
    work = args.work_dir.resolve()
    cache = work / "downloads"
    cache.mkdir(parents=True, exist_ok=True)
    recover_output(output, args.arch)
    current = read_bundle(output, args.arch) if output.exists() else None
    revision = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True)
    dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain"], capture_output=True, text=True)
    provenance = {"cabinet_revision": revision.stdout.strip() if revision.returncode == 0 else None,
                  "cabinet_dirty": bool(dirty.stdout.strip()) if dirty.returncode == 0 else None,
                  "builder_sha256": digest(Path(__file__)), "toolchain_sha256": toolchain_sha,
                  "cmake_version": subprocess.check_output(["cmake", "--version"], text=True).splitlines()[0]}
    compilers = None
    for name in names:
        core = lock["cores"][name]
        prior = current["cores"].get(name) if current else None
        if not args.rebuild and not args.clean and prior and prior["recipe_sha256"] == recipe_digest(core) and prior["toolchain_sha256"] == toolchain_sha:
            print(f"Already built and verified {name} for {args.arch}", flush=True)
            continue
        inputs = {"recipe": core, "arch": args.arch, "toolchain": toolchain_sha,
                  "builder": provenance["builder_sha256"],
                  "environment": {key: os.environ.get(key) for key in
                                  ("CC", "CXX", "AR", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "PKG_CONFIG_LIBDIR", "PKG_CONFIG_SYSROOT_DIR")}}
        identity = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
        tree = work / "builds" / args.arch / name / identity
        if args.clean and tree.exists():
            shutil.rmtree(tree)
        tree.mkdir(parents=True, exist_ok=True)
        print(f"Build files: {tree}", flush=True)
        with tempfile.TemporaryDirectory(prefix=f"{name}-stage-", dir=output.parent) as stage_dir:
            stage = Path(stage_dir)
            receipt = reuse_core(args.reuse / name, name, core, args.arch, toolchain, stage) if args.reuse and not args.clean and not args.rebuild else None
            try:
                if not receipt:
                    if core.get("build_system") == "make" and compilers is None:
                        with tempfile.TemporaryDirectory(prefix="probe-", dir=work) as probe:
                            compilers = make_compilers(toolchain, Path(probe))
                    receipt = build_core(name, core, args.arch, toolchain, compilers or {}, cache,
                                         tree, stage, args.jobs, args.offline)
                receipt.update(artifact=core["artifact"], recipe_sha256=recipe_digest(core), toolchain_sha256=toolchain_sha)
                publish_core(output, name, receipt, stage, lock_bytes, args.arch, provenance)
                current = read_bundle(output, args.arch)
            except BaseException:
                print(f"{name} failed; previous outputs are unchanged. Build files retained at {tree}", file=sys.stderr)
                raise
    # Refresh the recorded lock snapshot even when only unrelated lock metadata
    # changed. Each core's independent recipe hash still exposes stale entries.
    if current and current["lock_sha256"] != hashlib.sha256(lock_bytes).hexdigest():
        publish_core(output, None, None, None, lock_bytes, args.arch, provenance)
    if not args.core:
        verify(output, args.lock, args.arch)
    else:
        verify(output, args.lock, args.arch, names)
    print(f"Built and verified {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=architecture, default=architecture(platform.machine()))
    parser.add_argument("--lock", type=Path, default=ROOT / "cores.lock.json")
    parser.add_argument("--work-dir", type=Path, default=ROOT / "build/core-work")
    parser.add_argument("--output", type=Path, help="Default: build/cores/ARCH")
    parser.add_argument("--toolchain", type=Path, help="CMake toolchain; required when cross-compiling")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--offline", action="store_true", help="Require cached source archives")
    parser.add_argument("--reuse", type=Path, help="Reuse verified per-core bundles from DIRECTORY/NAME when their recipes and toolchains match")
    parser.add_argument("--core", action="append", help="Update selected cores only, preserving the rest; repeat for multiple cores")
    parser.add_argument("--rebuild", action="store_true", help="Run selected builds again, retaining compiler intermediates")
    parser.add_argument("--clean", action="store_true", help="Rebuild selected cores from fresh source/build trees")
    parser.add_argument("--verify", action="store_true", help="Verify an existing bundle without building or downloading")
    args = parser.parse_args()
    args.lock = args.lock.resolve()
    output = (args.output or ROOT / "build/cores" / args.arch).resolve()
    if args.verify:
        verify(output, args.lock, args.arch, args.core)
        print(f"Verified {output}")
        return
    if platform.system() != "Linux" or args.jobs < 1:
        raise ValueError("Build on Linux with a positive job count")
    if args.arch != architecture(platform.machine()) and not args.toolchain:
        raise ValueError("Cross-compilation requires --toolchain; the requested architecture alone does not select a compiler")
    with output_lock(output):
        build_bundle(args, output)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
