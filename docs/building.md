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

The helper verifies the downloaded archive, caches its source and build files under `build/sdl-work/`, disables SDL's shared library and test/example targets, and installs the static library plus its CMake package and license. SDL's native platform backends remain enabled when their development dependencies are present; their optional system libraries are loaded dynamically (`SDL_DEPS_SHARED=ON`). Use `--archive PATH` to point to an offline copy of the same checksum-pinned archive and `--work-dir` to change the cache location.

The helper also accepts `--toolchain cmake/toolchains/linux-aarch64.cmake` and a separate `--prefix build/sdl3-static-aarch64`. Configure Cabinet with that same toolchain and prefix. Cross builds use host Python and `glslangValidator` to generate shader headers, and require the target development libraries and a suitable sysroot; an x86 SDL prefix cannot serve an aarch64 build.

## Running and installation

The build produces `cabinet` (Home) and `cabinet-runner` (independent sessions). Both embed Atkinson Hyperlegible Next fonts and icon assets. The font's [SIL Open Font License](../assets/fonts/OFL.txt), SDL license, and shader license notices are installed with either build.

```sh
python3 tools/build_cores.py --arch x86_64 --jobs 4
./build/cabinet
./build/cabinet-runner --rom /path/to/game.gbc --core ./build/cores/x86_64/gambatte_libretro.so
cmake --install build --prefix /path/to/install
```

Use the matching core architecture on ARM64. Core builds are separate from frontend compilation; see [core builds](cores.md) for cross-compilation and bundle verification. Installation places executables in `bin/` and Home resources in `share/cabinet/`. Home locates the runner beside itself and prefers installed resources, with checkout paths as a development fallback. Runtime FreeType, Vulkan, and core dependencies must be available on the target system.

## Runner-only build

```sh
cmake -S . -B build/runner-only -DCABINET_BUILD_HOME=OFF -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="$PWD/build/sdl3-static"
cmake --build build/runner-only
cmake --install build/runner-only --prefix /path/to/install
```

This build needs Python 3.12+, the host `glslangValidator`, static SDL3, FreeType, and Vulkan, but does not discover or require SQLite, LibArchive, OpenSSL Crypto, or a core bundle. It installs `cabinet-runner` and licenses. Supply a compatible core separately.

## SDL and Linux runtime baseline

SDL is statically linked. Its exact version, release URL, and SHA-256 are recorded in [`dependencies.lock.json`](../dependencies.lock.json). CMake requires that exact version and the `SDL3::SDL3-static` target; a shared SDL installation is not a fallback. Other libraries may remain dynamically linked.

## Debian 13 builds with Docker

Debian **stable** (Debian 13 trixie as of writing this) is Cabinet's minimum supported Linux library baseline. Build release binaries and libretro cores in the same environment; building on a newer host can introduce newer glibc, libstdc++, or other shared-library requirements. The container pins a Debian image by digest and APT repositories to a dated [Debian snapshot](https://snapshot.debian.org/). SDL and core source versions and checksums come from the committed lockfiles. This fixes the build inputs; it does not promise byte-identical binaries across machines.

Install Docker Engine (Linux containers) or Docker Desktop and Python 3.9+ on the host. The script checks that Docker is installed and its daemon is accessible; it does not install Docker, start services, or invoke sudo. From the repository root:

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

On an amd64 Docker daemon, the ARM64 presets use Debian's GNU aarch64 cross compiler by default, or Clang targeting aarch64 for cores that require it, with `:arm64` development packages including graphics and audio libraries. No ARM machine or host QEMU/binfmt setup is needed. On an arm64 daemon they build natively. The `x86_64` preset requires an amd64 daemon. The daemon architecture, rather than the client machine's architecture, determines the build host. Use a local Docker daemon: the checkout is bind-mounted from the client's filesystem.

ArmadaOS cross-builds compile Cabinet, static SDL, and the cores with `-O3 -march=armv8.2-a+fp16+dotprod -mtune=cortex-a78`. These target-specific flags do not apply to generic `aarch64` builds, native builds, or core code generators that run on the build host.

`cabinet` builds the pinned static SDL first, then `cabinet` and `cabinet-runner` in Release mode with tests disabled. It does not build cores. `sdl` builds only SDL. `cores` builds the complete locked bundle; any core name shown by `--help` builds just that core, preserving other completed cores in the bundle. A core-only build does not build SDL. The existing core builder verifies the bundle's checksums and ELF architecture. Native Cabinet builds also run the Debian runtime dependency verifier. Cross-built executables need runtime verification on Debian 13 ARM64; `ldd` on the x86 build host cannot validate them.

Outputs and resumable work directories are under `release/docker/PRESET/IMAGE_ID/`. The script prints the exact path after a successful build. That directory contains:

- `cabinet/cabinet` and `cabinet/cabinet-runner` (frontend build).
- `sdl3-static/` and `sdl-work/` (installed SDL and source/object cache).
- `cores/` and `core-work/` (verified bundle and source/object cache).
- `debian-packages.txt` and `build-environment.json` (installed package versions, image digest, snapshot date, preset, and cross-compilation mode).
- `package-appimage.py` (executable packaging script for this preset and image).

The checkout is mounted read-only; only the output directory is writable. Files are created with the invoking user's UID/GID on Unix hosts. Presets and image IDs have separate caches so host builds or an updated container cannot supply stale objects or core bundles. Both Cabinet and the core builder use the same `cores/` path within that preset. Build `cores` separately before installing a full Cabinet bundle; installation verifies every locked core. To stage it inside the container, mount the printed output directory at `/out` and the checkout at `/src`, then run `cmake --install /out/cabinet --prefix /out/staging/usr`.

To package an AppImage, build both `cabinet` and `cores` for the same preset and image ID, then run the generated script **from that output directory**:

```sh
cd release/docker/x86_64/IMAGE_ID
./package-appimage.py --appimagetool /absolute/path/to/appimagetool
```

Packaging needs Python 3, the original checkout at its recorded location, the Docker build image, and a host-native `appimagetool` (also selectable with `APPIMAGETOOL`, or found on `PATH`). No target emulation is needed to package ARM64 on x86-64. The script stages `Cabinet.AppDir` inside the same build container, verifies and installs every locked core and its assets, and recursively bundles target shared libraries and their Debian license notices. It then runs `appimagetool` on the host with zstd compression and `ARCH=x86_64` or `ARCH=aarch64` (including ArmadaOS), producing `Cabinet-PRESET.AppImage`. AppImage-form appimagetool can extract and run without FUSE. To package offline, supply `--runtime-file /path/to/runtime` with a runtime for the **target** architecture; otherwise appimagetool downloads its runtime.

The AppDir keeps `cabinet` and `cabinet-runner` together in `usr/bin` and cores in `usr/share/cabinet/cores`, so the existing default discovery works without flags. `AppRun` prepends the bundled binary, library, and shared-data directories to the environment and preserves the caller's working directory and arguments. On launch it creates `~/.local/share/cabinet/system/`, checks for any entries (including hidden files), and copies bundled `cores/system/` contents only if the directory is empty. Copying never overwrites existing entries. A populated directory is left alone on subsequent launches and upgrades.

The `ArmadaOS` launcher also evaluates `/usr/libexec/armada/device-env` before starting Cabinet. If `ARMADA_SECONDARY_CONNECTOR` is nonempty, it exports `CABINET_DUAL_SCREEN=1`, enabling dual-screen runner launches on supported devices.

The AppImage retains the Debian 13 minimum runtime baseline. glibc, the compiler runtimes (`libstdc++` and `libgcc_s`), and graphics/window-system libraries (GL/EGL, Vulkan, GBM, DRM, Wayland, X11/XCB and xkbcommon) remain supplied by the target system, as do SDL's dynamically loaded display/audio backends and GPU drivers. Keeping only the Vulkan loader outside the bundle is insufficient: an older bundled compiler or Wayland runtime can hide symbols required by a newer system Vulkan driver or layer.

Missing symbol/version errors in the log indicate a driver or layer loading failure; the SDL message alone does not establish that the GPU lacks surface support. Compare with the system's `vulkaninfo` before changing the build baseline. After updating packaging, regenerate `package-appimage.py` by running any `build.py` target for that preset, then rerun the generated packaging script. Staging replaces the previous AppDir, so obsolete bundled libraries are removed.

To update the baseline deliberately, change the `FROM` digest and `DEBIAN_SNAPSHOT` in `packaging/debian/Dockerfile` together and rebuild all presets. Keep the suite named `trixie`, rather than the moving `stable` alias. The timestamp freezes package revisions, including security updates; refreshing it is an explicit maintenance task. Snapshot sources disable only their expiration check, retaining APT signature verification ([Debian source options](https://manpages.debian.org/trixie/apt/sources.list.5.en.html)).
