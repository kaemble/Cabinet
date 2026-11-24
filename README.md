# Cabinet

Cabinet is an SDL3 game-library frontend and libretro runner. Browse and organize games in the home window, then play in a separate window with gamepad and keyboard input, audio, save states, and an in-game menu.

## Build and run

Requires Python 3.12+, CMake 3.20+, a C++20 compiler, pinned static SDL3, and development packages for FreeType, Vulkan, SQLite3, LibArchive, JPEG, libcurl, nlohmann_json, and OpenSSL Crypto. See [building](docs/building.md) for platform dependencies and release builds.

```sh
python3 tools/build_sdl.py --jobs 6
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$PWD/build/sdl3-static"
cmake --build build --parallel 6
python3 tools/build_cores.py --arch x86_64 --jobs 6
./build/cabinet
```

Use `--arch aarch64` for a native ARM64 core build. Cores are built separately from the frontend; firmware must be supplied by the user. In Home, select a system and choose **Add game**. During play, **Escape** or **Start + Select/View** opens the menu. Both executables provide `--help`.

## Documentation

- [Building and packaging](docs/building.md): dependencies, runner-only builds, installation, Debian containers, and AppImages.
- [Home library](docs/library.md): importing games, ZIP archives, firmware, and library management.
- [Standalone runner](docs/runner.md): launching content, command-line options, and dual-screen output.
- [Controls and configuration](docs/controls.md): keyboard/gamepad mappings, menus, hotkeys, and per-game settings.
- [Saves and storage](docs/storage.md): data locations, game identity, backups, and state compatibility.
- [Video and performance](docs/video.md): scaling, hardware rendering, and frame pacing.
- [Core builds](docs/cores.md): pinned sources, architectures, bundle verification, and core-specific requirements.
- [Testing](docs/testing.md): automated tests, screenshots, and hardware checks.
- [Armada dual-screen output](docs/armada-dual-screen.md): direct panel rendering and device validation.
- [Coding style](docs/style.md) and [design](docs/design.md).

Cabinet supports player-one input, DS/3DS touch, and Wii pointer input. Multiplayer mapping and Direct3D rendering are not implemented. Compatibility depends on the core, content, and graphics driver.
