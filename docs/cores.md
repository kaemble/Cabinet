# Core builds

[cores.lock.json](../cores.lock.json) lists the catalog and pins each core's revision, archive checksum, license, and build options. The builder verifies the pinned sources.

Home uses installed cores or the configured development bundle, which defaults to `build/cores/<architecture>/`. You can also select a custom core by path.

## Build requirements

Requires Linux, Python 3.12+, CMake, Make, a C/C++ compiler, pkg-config, and NASM for x86 assembly. Azahar requires CMake 3.25+; the PS2 shader dependencies require CMake 3.22.1+. Hardware cores also need target OpenGL/EGL/GLX and other development libraries. The [Debian Dockerfile](../packaging/debian/Dockerfile) lists the packages used for [release builds](building.md#debian-13-builds-with-docker).

Recipes pin dependencies and submodules and use bundled static libraries where available. Runtime graphics and device interfaces come from the target system. PPSSPP uses pinned FFmpeg archives for both architectures.

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
python3 tools/build_cores.py --arch x86_64 --core blastem --jobs 4
python3 tools/build_cores.py --arch x86_64 --core blastem --verify
```

The builder publishes each successful core immediately. Rerunning skips verified cores and resumes compilation under `build/core-work/builds/ARCH/NAME/INPUT-HASH/`.

`--rebuild` retains compiler intermediates; `--clean` starts selected cores from fresh source/build trees while keeping downloaded archives. Use `--clean` after editing cached sources or changing a compiler/sysroot in place. Failed rebuilds preserve the last published core. Bundle replacement is staged, recoverable, and serialized for builds sharing an output directory.

Verification with `--core` checks selected recipes. Full verification and installation require every core supported on the target architecture. Use `--reuse DIRECTORY` to import verified per-core bundles from `DIRECTORY/NAME`.

## Core-specific behavior

### Sega cartridges

BlastEm is the default for Mega Drive, Master System, and Game Gear cartridges. Home extracts ZIP imports before launch. The pinned libretro branch uses the x86 JIT on x86-64 and generated CPU cores on ARM64. Its ROM database is embedded in the core. Mega Drive defaults to a six-button pad.

### Super Nintendo and Sega Saturn

bsnes loads cartridge content by path; Home extracts single-file SNES ZIP imports before launch. Beetle Saturn builds as `mednafen_saturn_libretro.so` and uses software rendering. Its disc formats are CUE, CCD, CHD, TOC, and M3U.

### Nintendo DS and 3DS

melonDS DS enables OpenGL classic/compute rendering and native JIT support on both architectures. Cabinet uses Software rendering at 1x and OpenGL Compute at higher internal resolutions; changes require restart. The cursor defaults to Never. See [controls](controls.md) for stylus input and [runner](runner.md#dual-screen-ds-and-3ds) for dual-screen mode.

Azahar supports OpenGL and Vulkan with architecture-appropriate Dynarmic backends. Home opens `3ds`, `3dsx`, `cci`, `cxi`, `app`, `axf`, `elf`, `z3dsx`, `zcci`, and `zcxi` content directly by path. ZIP extraction is disabled for this system. Internal resolution is in Video; other advertised options are under Advanced → More Core options.

Azahar's recipe writes upstream `GIT-COMMIT` and `GIT-TAG` metadata from the lockfile. The core embeds the commit in save-state headers and checks it when loading; keeping this metadata is required for state roundtrips. Bundled dependencies include shader tools, JIT support, and crypto/audio libraries; desktop frontends are disabled.

### PlayStation 2

PlayStation 2 uses [PCEE2](https://github.com/WizzardSK/pcee2-libretro) on x86_64 and [ARMSX2](https://github.com/ARMSX2/ARMSX2) on aarch64. Each bundle builds and verifies only its architecture's PS2 core. Disc images and ELF content load by path; ZIP extraction is disabled. The available extensions follow the selected core, including ISO, CHD, CSO, ZSO, and M3U on both architectures.

Both cores default to Vulkan and use the PS2 controls profile, including both analog sticks, L2/R2 pressure, and L3/R3. Internal resolution is available in Video; other core options are under Advanced → More Core options.

See [Home's firmware setup](library.md#firmware-and-system-assets) for BIOS installation. Standalone launches look in `pcsx2/bios/` under `--system-dir`. Copy the bundle's `system/pcsx2/resources/` into the writable system directory. Include `pcsx2/memcards/` from that directory in save backups. Save states are specific to the core and architecture.

ARMSX2 requires Clang for its ARM64 calling conventions. Its recipe selects Clang and the aarch64 target for both the core and its dependencies; the Debian image includes the native Clang compiler alongside GCC’s target libraries.

The recipes build pinned static SDL3, plutovg, plutosvg, and shaderc dependencies; PCEE2 also builds pinned rapidyaml and its nested dependencies. System development packages must provide PNG 1.6.40+, Zstd 1.5.5+, LZ4, WebP, FreeType 2.12+, libcurl, libpcap, fontconfig, and D-Bus. The Debian build image supplies these packages.

## Run-ahead

Configuration → Advanced → Run-ahead offers off (the default), 1, 2, or 3 frames. The setting follows the current system or game configuration. Frontend run-ahead is available for Gambatte, mGBA, FCEUmm, Beetle PCE, and Beetle Saturn, which report `savestate_features = "deterministic"` in the pinned [Libretro core metadata](../third_party/core-info.json). Updating that snapshot changes eligibility without changing core build recipes.

bsnes and SwanStation use the same menu entry as an alias for `bsnes_run_ahead_frames` and `swanstation_Main_RunaheadFrameCount`. Changes through More Core options stay synchronized; Cabinet does not add frontend run-ahead to either core.

Frontend run-ahead retains one frame of progress, executes the requested extra frames with repeated input, presents only the final frame's output, and restores the retained state in memory. It is suspended during fast-forward. Ordinary startup frames are allowed before serialization becomes available. Hardware rendering and incomplete serialization disable frontend run-ahead. A failed snapshot disables the setting; an unrecoverable rollback stops the session without writing speculative battery RAM.

Higher values require more CPU time and may cause stuttering when execution exceeds the frame budget. Run-ahead reduces emulated input delay; display and audio buffering still contribute latency.

## Installation and releases

```sh
cmake --install build --prefix /path/to/staging/usr
```

Installation verifies the complete bundle against the checkout's lockfile and target architecture, rejecting stale recipes, changed files, unexpected `.so` files, and wrong-architecture cores. Files install under `share/cabinet/cores/`. Runner-only installs do not ship cores or require the Python core builder.

The bundle contains core libraries, dependency license notices, the exact lockfile, and a `manifest.json` recording source revisions, options, architecture, compiler/CMake details, builder/toolchain checksums, checkout revision/dirty status, and file SHA-256 checksums.

Redistributable assets include `system/PPSSPP/`, `system/dolphin-emu/Sys/`, and `system/pcsx2/resources/`; proprietary BIOS files are excluded. Standalone launches can use `--system-dir` pointing to the bundle's `system/`. Home uses its writable user system directory; copy the asset folders there for development.

For releases, use the [Debian container and AppImage workflow](building.md#debian-13-builds-with-docker) and retain the bundle manifest and artifact checksums.

To update a core, verify its source revision and archive checksum, update its recipe, build both architectures, and test launch, input, battery saves, state roundtrips, hardware rendering, and assets as applicable. New cores also require verified license/asset handling. Core fingerprints separate runner state slots; preserve original binaries when retaining important states.
