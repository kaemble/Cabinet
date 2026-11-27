# Standalone runner

`cabinet-runner` runs independently of Home, SQLite, and the library database. It embeds SDL and font/icon assets, and requires FreeType, the Vulkan loader, and the selected core's runtime libraries. See [building](building.md) for a runner-only build.

## Launch content

```sh
./build/cabinet-runner \
  --rom "/games/My Game.gbc" \
  --core ./build/cores/x86_64/gambatte_libretro.so \
  --data-dir /profiles/player-one \
  --windowed --window-size 800x600 --scale nearest --volume 80
```

Supply `--core PATH` and `--rom PATH`. For cores that support contentless sessions, omit `--rom` in a standalone launch or use **Add custom entry** in [Home](library.md#import-and-organize). Standalone contentless sessions default to the core name for the title and `Generic/core-<core filename stem>/` for saves and states.

`--system ID` selects an input/settings profile; otherwise the runner infers one from the ROM/core. Use `--system wii` for Wii content with shared disc extensions such as ISO or RVZ. `--help` lists supported IDs and all options.

## Session options

The runner starts fullscreen unless the system has a saved windowed preference. `--windowed` overrides it, and `--window-size WxH` sets the windowed size. Fullscreen changes are saved per system on exit.

| Option | Purpose |
| --- | --- |
| `--title TEXT` | Override the window title. |
| `--system-dir DIR` | Select firmware and system assets. |
| `--save-dir DIR`, `--state-dir DIR` | Override save/state roots, retaining identity subfolders. |
| `--state-file FILE` | Use an exact save/load slot; mutually exclusive with `--state-dir`. |
| `--load-state FILE` | Load a state on startup without changing the save destination. |
| `--paused` | Start in the menu after the first video frame or startup state load. |
| `--core-option KEY=VALUE` | Set a core option before initialization; repeat for multiple options. |
| `--no-audio`, `--volume 0..100` | Disable audio or set volume. |
| `--menu-shortcut start-select\|l3-r3\|home` | Choose the gamepad menu shortcut. Escape always works. |
| `--swap-confirm-cancel` | Swap menu confirm/back buttons. |
| `--core-log-level LEVEL` | Core messages: `debug`, `info`, `warn` (default), `error`, or `off`. |
| `--fast-forward` | Run uncapped with muted audio; disables VSync and pacing waits. |

Explicit command-line settings override saved preferences. See [controls](controls.md), [storage](storage.md), and [video](video.md) for configuration scope, save identity, and rendering options. Debug core logs can substantially slow emulation.

## Audio callbacks

The runner services libretro's optional `RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK` on the runner thread, including between video frames. SDL converts the sample rate, and the runner targets a 60 ms queue.

The producer is inactive while paused, fast-forwarding, or running without an audio device, and is stopped before save/load operations and core shutdown. Frontend run-ahead is unavailable for cores using callback audio.

## Dual-screen DS and 3DS

For melonDS DS (`--system nds`) and Azahar (`--system 3ds`), `--dual-screen` places the top screen in the main window and the bottom screen in a second resizable window. The bottom window opens on another display when available. Both views share one emulation clock; only the main window controls pacing and displays menus/stats.

Either window can retain gameplay focus. Closing either opens quit confirmation. Scaling and aspect settings apply to both views, and touch follows the bottom view with captured drags clamped to its edges. The mode temporarily fixes the core to a top/bottom layout with swapping disabled; saved layout preferences remain available without the flag.

Enable dual-screen in Home's **Settings** or set `CABINET_DUAL_SCREEN=1` to pass the flag to runner launches. Builds with `-DCABINET_ARMADA_DRM=ON` can lease the bottom panel directly in Armada Game Mode. See [Armada dual-screen output](armada-dual-screen.md).
