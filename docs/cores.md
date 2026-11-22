# Core builds

[`cores.lock.json`](../cores.lock.json) pins the upstream revisions, archive checksums, licenses, and build options for all 14 catalog cores: Gambatte, mGBA, FCEUmm, Snes9x, Mupen64Plus-Next, melonDS DS, PicoDrive, Beetle PCE, SwanStation, Kronos, PPSSPP, Dolphin, Flycast, and Azahar. Builds use verified sources rather than moving branches or nightly binaries. Refer to the lockfile for exact versions.

Home discovers cores from the installed bundle or its configured development bundle, defaulting to `build/cores/<architecture>/`. It does not implicitly use the unmanaged `cores/` directory or copy it during installation. Explicit custom core paths remain supported.

## Build requirements

Requires Linux, Python 3.12+, CMake, Make, a C/C++ compiler, pkg-config, and NASM for x86 assembly. Azahar requires CMake 3.25+. Hardware cores also need target OpenGL/EGL/GLX and other system development libraries; the [CI workflow](../.github/workflows/cores.yml) lists native build packages. The [Debian build environment](building.md#debian-13-builds-with-docker) supplies a consistent release baseline.

Core dependencies and required submodules are pinned individually. Recipes use bundled static utility libraries where available, while runtime graphics/device interfaces remain system dependencies. PPSSPP uses its pinned FFmpeg archives for x86_64 and aarch64. No upstream source patches are applied.

## Native and cross builds

```sh
python3 tools/build_cores.py --arch x86_64 --jobs 4
python3 tools/build_cores.py --arch x86_64 --verify
```

On a native ARM64 host, use `--arch aarch64`. Cross-builds require a target compiler, libc/sysroot, and target system development libraries:

```sh
python3 tools/build_cores.py --arch aarch64 \
  --toolchain cmake/toolchains/linux-aarch64.cmake --jobs 4
```

A custom CMake toolchain is also supported. `--arch` alone does not select a cross compiler, and the builder checks the resulting ELF architecture. Cross-building Cabinet additionally requires a target static SDL build and its other development dependencies. Build frontend and cores against the same runtime baseline.

Outputs are isolated under `build/cores/x86_64/` and `build/cores/aarch64/`. `--output DIR` changes the bundle location; configure Cabinet with `-DCABINET_LOCKED_CORES_DIR=/absolute/path/to/bundle` to use it. `--work-dir DIR` changes the source/object cache, and `--offline` requires all pinned archives in that cache.

Normal frontend compilation does not download or build cores. After configuring Cabinet, `cmake --build build --target cabinet-cores` builds the configured architecture's bundle; `--target verify-cabinet-cores` only verifies it.

## Incremental builds and recovery

`--core NAME` is repeatable and updates only selected cores, preserving the rest of the bundle:

```sh
python3 tools/build_cores.py --arch x86_64 --core picodrive --jobs 4
python3 tools/build_cores.py --arch x86_64 --core picodrive --verify
```

Each successful core is published immediately. Rerunning the same command skips verified unchanged cores and resumes failed compilation under `build/core-work/builds/ARCH/NAME/INPUT-HASH/`. Work trees are isolated by recipe, architecture, toolchain, builder, and environment.

`--rebuild` retains compiler intermediates; `--clean` starts selected cores from fresh source/build trees while keeping downloaded archives. Use `--clean` after editing cached sources or changing a compiler/sysroot in place. Failed rebuilds preserve the last published core. Bundle replacement is staged, recoverable, and serialized for builds sharing an output directory.

Verification with `--core` checks only selected recipes. Full verification and installation require every locked core to be present and current. Recipe hashes prevent outdated cores from appearing current after a lockfile change. `--reuse DIRECTORY` can import verified per-core bundles from `DIRECTORY/NAME`, preserving provenance; it is unnecessary for ordinary build resumption.

## Core-specific behavior

### Nintendo DS and 3DS

melonDS DS enables OpenGL classic/compute rendering and native JIT support on both architectures. Cabinet uses Software rendering at 1x and OpenGL Compute at higher internal resolutions; changes require restart. The cursor defaults to Never. See [controls](controls.md) for stylus input and [runner](runner.md#dual-screen-ds-and-3ds) for dual-screen mode.

Azahar supports OpenGL and Vulkan with architecture-appropriate Dynarmic backends. Home opens `3ds`, `3dsx`, `cci`, `cxi`, `app`, `axf`, `elf`, `z3dsx`, `zcci`, and `zcxi` content directly by path. ZIP extraction is disabled for this system. Internal resolution is in Video; other advertised options are under Advanced → More Core options.

Azahar's recipe writes upstream `GIT-COMMIT` and `GIT-TAG` metadata from the lockfile. The core embeds the commit in save-state headers and checks it when loading; keeping this metadata is required for state roundtrips. Bundled dependencies include shader tools, JIT support, and crypto/audio libraries; desktop frontends are disabled.

### Hardware renderers

Flycast and Dolphin prefer Vulkan by default. Mupen64Plus-Next includes GLideN64/HLE and ParaLLEl-RDP/RSP; ParaLLEl-RDP needs an LLE RSP. Kronos uses the Linux `platform=unix` recipe with SSE disabled on ARM64, and its Musashi generator runs with the host compiler. See [video and performance](video.md) for runtime selection and settings.

## Installation and releases

```sh
cmake --install build --prefix /path/to/staging/usr
```

Installation verifies the complete bundle against the checkout's lockfile and target architecture, rejecting stale recipes, changed files, unexpected `.so` files, and wrong-architecture cores. Files install under `share/cabinet/cores/`. Runner-only installs do not ship cores or require the Python core builder.

The bundle contains core libraries, dependency license notices, the exact lockfile, and a `manifest.json` recording source revisions, options, architecture, compiler/CMake details, builder/toolchain checksums, checkout revision/dirty status, and file SHA-256 checksums.

Redistributable assets include `system/PPSSPP/` and `system/dolphin-emu/Sys/`; proprietary BIOS files are excluded. Standalone launches can use `--system-dir` pointing to the bundle's `system/`. Home uses its writable user system directory; copy the asset folders there for development.

Source locks do not pin the whole compiler/runtime environment or promise byte-identical builds. Use the [Debian container and AppImage workflow](building.md) for releases, archive manifests/artifacts, and publish their checksums. The native x86-64/aarch64 CI artifacts are not automatically published releases.

To update a core, verify its source revision and archive checksum, update its recipe, build both architectures, and test launch, input, battery saves, state roundtrips, hardware rendering, and assets as applicable. New cores also require verified license/asset handling. Core fingerprints separate runner state slots; preserve original binaries when retaining important states.
