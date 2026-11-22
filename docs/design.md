
# Design Document: Cabinet

**Platform:** Linux x86-64 and aarch64
**Rendering and input:** SDL
**Emulation interface:** libretro

## 1. Summary

Cabinet is a library-first (as in game library/collection) libretro frontend: users add games to a browsable collection, select one, and play with minimal setup. It takes inspiration from OpenEmu's unified game library and approachable presentation, while using SDL for its window, rendering, and controller input.

The frontend loads libretro cores as shared libraries and supplies their video, audio, input, and environment callbacks. The core runs the emulated system; Cabinet handles the library, user interface, configuration, and host-device integration. This follows libretro's frontend/core division. [1,2]

## 2. Goals and non-goals

**Goals**
- Make importing and launching a game straightforward.
- Make the library and in-game menus navigable with a game controller.
- Provide practical defaults so a user can start without configuring every core.
- Keep games, core assignments, settings, and save data organized.
- Handle missing cores and unsupported features with understandable messages.

**Non-goals for the initial release**
- Downloading games, firmware, or artwork automatically.
- Netplay, achievements, or a large shader catalog.
- Supporting every core-specific feature from day one.
- Replacing core-specific documentation or configuration options.

## 3. User experience

### Library

The home screen opens to a list of game systems on the left pane and their corresponding libraries on the main view, the main view shows all the games imported for that system with a grid view, optional list view, and simple filters for system, favorites, and recently played. Selecting a game shows its title, system, assigned core, and available actions: **Play**, **Edit Details**, and **Remove from Library**.

Upon launching a game a new fullscreen window is created where the game is rendered on, it is also where the In-game menu should be displayed.

The library-first approach draws on OpenEmu's focus on organizing games, collections, and artwork in one interface. Cabinet keeps these features optional: missing artwork or metadata never blocks launching a game. [3,4]

### Controller-first navigation

All essential screens must work without a mouse:

- **D-pad / left stick:** Move focus.
- **A / south button:** Select or confirm.
- **B / east button:** Back or cancel.
- **Guide / Home:** Reserved for the operating system; do not intercept by default.

Show an on-screen hint for the active confirm and back buttons, since controller labels vary. Keyboard and mouse remain supported. SDL's gamepad API provides standardized gamepad input, while SDL also reports device-add and removal events for hot-plug handling. [5,6]

### In-game menu

A configurable controller action (pressing down both Start + Select/View by default) pauses the game and opens a compact overlay with:

- Resume
- Save state / Load state
- Restart game
- Configuration: Input, Video, and Audio submenus
- Quit

Prompt before destructive actions such as overwriting a save state or discarding unsaved configuration changes. The game pauses while the menu is open.

## 4. Sane defaults

Defaults should prioritize predictable behavior and low setup effort:

- **Video:** Preserve the core's aspect ratio, fit inside the window, and avoid cropping.
- **Scaling:** Use a simple linear or nearest-neighbor scaler. Do not enable shaders by default.
- **Audio:** Enable audio at a comfortable, non-clipping level; provide a master volume control.
- **Input:** Use SDL gamepad mappings where available. Keep keyboard input enabled as a fallback.
- **Core choice:** The user should be able to operate the program and play comfortably without having to know what a libretro core is. Cabinet should automatically detect which core to use from its bundled cores and supported systems.
- **Library data:** Store metadata and settings separately from original game files. Never move or modify a game file without clear user consent.
- **Failure handling:** Preserve the selected game and return to the library with a useful error if a core fails to load.

## 5. Architecture

| Component | Responsibility |
|---|---|
| **Application shell** | SDL initialization, window lifecycle, event loop, shutdown |
| **Library** | Import, index, search, collections, game metadata, recent activity |
| **Core manager** | Discover and load core libraries; resolve core paths and compatibility |
| **Libretro host** | Implement the libretro API callbacks and manage game lifecycle |
| **Input manager** | Translate SDL keyboard/gamepad state into libretro input |
| **Audio output** | Buffer and play audio samples from the core |
| **Video output** | Accept core frames, scale them, and present them through SDL |
| **Settings and storage** | Persist preferences, mappings, save data paths, and library database |
| **UI** | Library, dialogs, settings, and in-game overlay |

Run each game in a separate `cabinet-runner` process. Keep frontend calls into the core and SDL rendering on that process's main thread. Since libretro does not guarantee API thread safety, avoid concurrent frontend calls into a core. [1]

## 6. Core and game lifecycle

A typical launch proceeds as follows:

1. Resolve the selected game and its assigned or compatible core.
2. Load the core library and resolve required libretro entry points.
3. Register the frontend callbacks and query core/system information.
4. Initialize the core, load game content, and obtain audio/video timing information.
5. Start the emulation loop; translate input, run the core, queue audio, and present frames.
6. On exit, unload the game, deinitialize the core, and release its library.

Treat every step as fallible. Show an actionable error—such as “Core file not found” or “This core does not support this game file”—and return the user to a stable UI state.

Support software-rendered frames, OpenGL/OpenGL ES hardware contexts, and Vulkan hardware rendering through SDL. Dolphin and Flycast prefer Vulkan by default; other cores prefer OpenGL, with explicit CLI overrides for both APIs. Vulkan shares the negotiated device with SDL and snapshots core images on the GPU, using fenced frame slots and GPU barriers for synchronization. Reject unsupported graphics APIs with a clear message. Create the negotiated graphics context before querying audio/video information, then allocate the framebuffer using the reported geometry.

## 7. SDL event and input handling

The SDL event loop handles window events, keyboard input, controller input, and controller hot-plugging. On controller addition, open the device and refresh available mappings; on removal, switch to another connected controller if available and notify the user without closing the game. SDL's gamepad events include device-added and device-removed notifications. [5,6]

Separate **frontend navigation** from **gameplay input**. When the in-game menu is open, navigation input belongs to the menu; while playing, mapped controls go to the core. Provide a configurable menu shortcut and prevent that shortcut from also triggering a game action.

## 8. Library and file handling

Import adds a reference to a user-selected game file and records its path, display name, system/core association, and last-played time. Support manual system and core assignment when automatic identification is uncertain, as well as prompting the user to provide files such as a BIOS for the cores that require them. Missing files should be flagged in the library with an option to locate them again.

Keep artwork and metadata optional and editable. Do not assume that a filename uniquely identifies a game, and do not fetch metadata or artwork from external services in the MVP. Do not add files or otherwise pollute the directory where the game files are located. 

## 9. Persistence and saves

Store frontend settings and the game library in a dedicated application-data directory. Keep per-game save data and save states distinct from the original content. Use permanent random library identities independent of readable save-folder names. Preserve the original filename stem for the folder, sanitizing filesystem-unsafe characters and adding a short identity suffix only for collisions. Persist both fields so moving or renaming a game preserves its saves. Keep raw battery saves game-named and core-managed files in their native layout. Keep core-binary hashes in save-state filenames within core-named subfolders, with descriptive metadata sidecars. Pass identity and folder name through `--game-id` and `--save-name`. Standalone launches without an explicit ID use Generic and retain content-hash or canonical-path-hash fallback identity; avoid reading entire disc images merely to identify them. No automatic migration from previous pre-alpha save layouts is required.

On exit, flush the library database and settings. Save-state support should be exposed only when the loaded core offers the relevant capability; otherwise, hide or disable those actions and explain why.

## 10. Accessibility and resilience

- Maintain a visible focus indicator in every controller-navigable screen.
- Support keyboard navigation and remappable actions.
- Provide readable text scaling and high-contrast focus states.
- Avoid relying on color alone to communicate status.
- Keep the UI responsive during imports and core startup.
- Recover cleanly from missing files, missing cores, controller disconnects, and core crashes.
- Avoid using flashy animations if they compromise responsiveness in any way.

## 11. Bundled libretro cores

Cabinet comes bundled with several cores and is already set up to emulate various systems.
The MVP release should be able to handle these systems.

- Game Boy / Game Boy Color (Gambatte)
- Game Boy Advance (VBA-Next)
- GameCube (Dolphin)
- Wii (Dolphin)
- Game Gear (PicoDrive)
- Nintendo Entertainment System / Famicom (FCEUmm)
- Nintendo 64 (Mupen64Plus-Next)
- Nintendo DS (MelonDS)
- Sega Genesis / Mega Drive (PicoDrive)
- Sega Master System (PicoDrive)
- Sega Saturn (Kronos)
- Sony PSP (PPSSPP)
- Sony PlayStation (Swanstation)
- Super Nintendo (Snes9x)
- TurboGrafx-16 / PC Engine (Mednafen)

## 12. MVP acceptance criteria

The first release is ready when a user can:

1. Add a game file to the library and see it in a grid or list.
2. A compatible installed core can be automatically recognized and the game can be launched.
3. Play using a connected SDL-recognized gamepad or keyboard.
4. Open and navigate the in-game menu with a controller.
5. Pause, resume, and return to the library without restarting the application.
6. Change basic video, audio, and input settings.

## 13. Suggested implementation sequence

1. SDL window, event loop, and basic focus-driven UI.
2. Core loading and a minimal libretro callback host.
3. Software video presentation, audio output, and input mapping.
4. Library import, persistence, and core assignment.
5. Controller navigation and in-game menu.
6. Save-state integration, additional settings, and resilience testing.

The key product principle is: **make the common path simple, and keep advanced configuration available without making it mandatory.**

[1] libretro-core-rs/spec/developing-cores.md at main — https://github.com/jefersondaniel/libretro-core-rs/blob/main/spec/developing-cores.md
[2] API – Libretro — https://www.libretro.com/index.php/api/
[3] OpenEmu — https://en.wikipedia.org/wiki/OpenEmu
[4] OpenEmu - Multiple Video Game System — https://openemu.org/
[5] SDL3 Example: input/02-joystick-events — https://examples.libsdl.org/SDL3/input/02-joystick-events/
[6] SDL3/SDL_GamepadDeviceEvent — https://wiki.libsdl.org/SDL3/SDL_GamepadDeviceEvent
