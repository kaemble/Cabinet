# Home library

## Import and organize

Select a system in the sidebar, then choose **Add game** or press **I**. Dragging files onto Home also opens import review and requires a selected system. Review the title, system, and core before choosing **Add to library**; the selected system's bundled core is the default, and a custom core can be selected.

Open a game's details to play, edit its title or core, mark it as a favorite, locate a moved file, or remove it. Removal leaves ROMs and saves untouched. Locate file preserves the game's identity and metadata. Importing an existing canonical path selects its entry; same-named files in different folders remain separate games.

The sidebar includes system counts, All games, Favorites, and Recently played. Search with **F** or the search box; Enter/Escape finishes entry and Ctrl+A clears it. The view button switches grid/list views. Artwork uses placeholders, and view/search choices last for the session.

Arrows, D-pad, or left stick move focus; Enter/south selects, Escape/east goes back, and Tab switches panes. Right from the sidebar enters the grid, and Up from its first row reaches the toolbar. The layout and file browser adapt to the window size.

Home stays open while a separate runner process plays the game. One game can run per home window. Launch failures appear in Home, and Recently played updates after the core and content load successfully. Close the game through its menu before closing Home.

## ZIP archives

Home supports ZIP imports for single-file Game Boy/Color, GBA, NES, SNES, N64, DS, Mega Drive, Master System, and Game Gear cartridges. You can select alternate cores, including cores such as bsnes that request a file path: Home extracts the selected ROM before launching the runner. Home checks custom cores when adding or editing an archived entry. Other systems still require cores that support memory-loaded content; path-based disc systems require unpacked content with their referenced files kept together.

A single matching ROM is selected automatically; multiple matches open a member picker. Each member can have its own library entry, saves, and configuration. Home extracts only the selected member into a private temporary directory for play and removes it after the runner exits. The archive remains untouched.

Encrypted archives, unsafe or duplicate member names, corrupt data, nested ZIPs, multi-file disc bundles, and ROMs over 512 MiB are unsupported. Locate file can relink a renamed or moved archive, but the member must retain its path within the archive. Standalone runner launches require extracted content.

## Firmware and system assets

Place firmware under `~/.local/share/cabinet/system/`, or `DIR/system/` with `--data-dir DIR`. Home prompts for a compatible BIOS when importing PlayStation or Saturn content; setup can be skipped or reopened through **BIOS files** in game details. Recognized 512 KiB images are validated and copied to the expected filename. Proprietary BIOS files are never downloaded or bundled.

The verified core bundle includes redistributable PPSSPP and Dolphin assets. For development, copy `system/PPSSPP/` and `system/dolphin-emu/` from the bundle into Home's system directory. AppImage setup is described in [building and packaging](building.md).

For Dreamcast, import a `.cue` or `.gdi` descriptor with its referenced tracks kept together; `.cdi`, `.chd`, and `.m3u` are also recognized. Flycast supports an HLE BIOS; an optional original BIOS belongs at `system/dc/dc_boot.bin`. It defaults to per-game VMU cards. Nintendo 3DS content opens directly by path and does not support ZIP imports.

## Launch options and logging

```sh
./build/cabinet --data-dir /path/to/profile
./build/cabinet --system gb --import "/path/to/game.gbc"
./build/cabinet --log-level info
```

Home finds `cabinet-runner` beside its executable; `--runner PATH` overrides it. Bundled cores resolve against the current installation, so moving an installation or AppImage preserves selections. Custom cores retain their paths and must be located in Edit details if moved.

`--log-level` accepts `off`, `error`, `warn` (default), `info`, and `debug`. Info prints a shell-quoted runner command and its exit status; debug adds paths, entry counts, and readiness. Core verbosity is controlled separately by the runner's `--core-log-level`.
