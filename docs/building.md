# Building and packaging

## Local development

Python 3.12+, CMake 3.20+, a C++20 compiler, `glslangValidator` (`glslang-tools` on Debian), and the development packages for FreeType, Vulkan, LibArchive, SQLite, JPEG, libcurl, nlohmann_json, and OpenSSL Crypto are needed. Optional Armada support (`-DCABINET_ARMADA_DRM=ON`) also needs libdrm, GBM, EGL, Wayland, wayland-scanner, and wayland-protocols. Install SDL's platform development dependencies as well if its X11/Wayland/audio backends are wanted. Git is also needed by the core-bundle tests.

```sh
python3 tools/build_sdl.py --jobs 6
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/build/sdl3-static" \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 6
ctest --test-dir build --parallel 4 --output-on-failure
```

An arbitrary install prefix works too:

```sh
python3 tools/build_sdl.py --prefix sdl3-static --jobs 6
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/sdl3-static"
```

When replacing an SDL package already cached by an existing build, add `-USDL3_DIR` to the configure command once, or use a fresh build directory.

The helper verifies the pinned archive, caches sources and objects under `build/sdl-work/`, and installs static SDL, its CMake package, and license. Available platform backends load their optional dependencies dynamically. Use `--archive PATH` for an offline copy of the pinned archive and `--work-dir DIR` to relocate the cache.

The helper also accepts `--toolchain cmake/toolchains/linux-aarch64.cmake` and a separate `--prefix build/sdl3-static-aarch64`. Configure Cabinet with that same toolchain and prefix. Cross builds use host Python and `glslangValidator` to generate shader headers, and require the target development libraries and a suitable sysroot; an x86 SDL prefix cannot serve an aarch64 build.

## Running and installation

The build produces `cabinet` (Home) and `cabinet-runner` (game sessions), with embedded fonts and icons. Installation includes the [font license](../assets/fonts/OFL.txt), SDL license, and shader notices.

```sh
python3 tools/build_cores.py --arch x86_64 --jobs 4
./build/cabinet
./build/cabinet-runner --rom /path/to/game.gbc --core ./build/cores/x86_64/gambatte_libretro.so
cmake --install build --prefix /path/to/install
```

Build cores separately for the target architecture. Installation places executables in `bin/` and Home resources in `share/cabinet/`. Home finds the runner beside its executable and prefers installed resources, falling back to checkout resources for development. See [core builds](cores.md).

## Runner-only build

```sh
cmake -S . -B build/runner-only -DCABINET_BUILD_HOME=OFF -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="$PWD/build/sdl3-static"
cmake --build build/runner-only
cmake --install build/runner-only --prefix /path/to/install
```

The runner-only build requires Python 3.12+, host `glslangValidator`, static SDL3, FreeType, and Vulkan. It installs `cabinet-runner` and licenses. Supply a compatible core separately.

## SDL and Linux runtime baseline

CMake requires the exact SDL version and the `SDL3::SDL3-static` target. The version, release URL, and SHA-256 are pinned in [dependencies.lock.json](../dependencies.lock.json).

## Debian 13 builds with Docker

Debian 13 (trixie) is Cabinet's minimum Linux runtime baseline. Build release binaries and cores together in the container, which pins the Debian image digest and APT snapshot. SDL and core inputs come from the repository lockfiles.

Install Docker Engine (Linux containers) or Docker Desktop and Python 3.9+ on the host. Run from the checkout root with a local Docker daemon.

```sh
python3 packaging/debian/build.py x86_64 cabinet --jobs 6
python3 packaging/debian/build.py aarch64 sdl --jobs 6
python3 packaging/debian/build.py aarch64 cores --jobs 6
python3 packaging/debian/build.py aarch64 mgba --jobs 6
python3 packaging/debian/build.py ArmadaOS cabinet --jobs 6
```

| Preset | Target | Cabinet configuration |
| --- | --- | --- |
| `x86_64` | x86-64 | Armada DRM disabled |
| `aarch64` | ARM64 | Armada DRM disabled |
| `ArmadaOS` | ARM64 | `-DCABINET_ARMADA_DRM=ON` |

On amd64 Docker hosts, the ARM64 presets cross-compile with Debian's GNU toolchain or Clang for cores that require it, using `:arm64` development packages. On arm64 Docker hosts they build natively. Cross-builds work without QEMU. The `x86_64` preset requires an amd64 daemon.

ArmadaOS cross-builds compile Cabinet, static SDL, and the cores with `-O3 -march=armv8.2-a+fp16+dotprod -mtune=cortex-a78`. These target-specific flags do not apply to generic `aarch64` builds, native builds, or core code generators that run on the build host.

`cabinet` builds static SDL and both frontend executables in Release mode with tests disabled. `sdl` builds SDL only. `cores` builds and verifies the locked bundle, and any core name listed by `--help` builds that core alone. Native frontend builds verify Debian runtime dependencies. Verify cross-built executables on Debian 13 ARM64.

Docker core builds strip staged binaries before recording bundle checksums. Rerunning a build also strips completed cores in the selection.

Outputs and resumable work directories are under `release/docker/PRESET/IMAGE_ID/`. The script prints the exact path after a successful build. That directory contains:

- `cabinet/cabinet` and `cabinet/cabinet-runner` (frontend build).
- `sdl3-static/` and `sdl-work/` (installed SDL and source/object cache).
- `cores/` and `core-work/` (verified bundle and source/object cache).
- `debian-packages.txt` and `build-environment.json` (installed package versions, image digest, snapshot date, preset, and cross-compilation mode).
- `package-appimage.py` (executable packaging script for this preset and image).

The container mounts the checkout read-only and writes artifacts as the invoking user. Caches are isolated by preset and image ID. Build `cores` before installing the full bundle. For manual container staging, mount the checkout at `/src` and the output directory at `/out`, then run `cmake --install /out/cabinet --prefix /out/staging/usr`.

To package an AppImage, build both `cabinet` and `cores` for the same preset and image ID, then run the generated script **from that output directory**:

```sh
cd release/docker/x86_64/IMAGE_ID
./package-appimage.py --appimagetool /absolute/path/to/appimagetool
```

Packaging requires Python 3, the checkout at its recorded path, the build image, and a host-native `appimagetool` (also selectable via `APPIMAGETOOL` or `PATH`). The script stages `Cabinet.AppDir` in the build container with verified cores, assets, shared libraries, and license notices. It then produces `Cabinet-PRESET.AppImage` on the host. For offline packaging, pass `--runtime-file /path/to/runtime` for the target architecture.

The AppImage bundles both executables in `usr/bin` and cores in `usr/share/cabinet/cores`. On launch it copies bundled system assets into `~/.local/share/cabinet/system/` only when that directory is empty.

The `ArmadaOS` launcher also evaluates `/usr/libexec/armada/device-env` before starting Cabinet. If `ARMADA_SECONDARY_CONNECTOR` is nonempty, it exports `CABINET_DUAL_SCREEN=1`, enabling dual-screen runner launches on supported devices.

The target system supplies glibc, compiler runtimes, graphics and window-system libraries, GPU drivers, and SDL's dynamically loaded display and audio backends. The Debian 13 runtime baseline still applies.

For Vulkan symbol or version errors, compare with the system's `vulkaninfo` to diagnose driver or layer loading. After changing packaging, rerun any `build.py` target for the preset to regenerate `package-appimage.py` before repackaging.

To refresh the Debian build environment, update the base-image digest and `DEBIAN_SNAPSHOT` in `packaging/debian/Dockerfile` together and rebuild all presets. The repositories use the explicit `trixie` suite, and APT signature verification remains enabled.
