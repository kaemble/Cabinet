# Home library

## Import and organize

Press **Start** on a controller, **Esc** on the keyboard, or click **Menu** in the bottom bar to open Home's menu. It contains **Add custom entry**, **Settings**, **Close Cabinet**, and **SteamGridDB API key**. Close the running game before closing Cabinet.

**Add custom entry** opens the core picker first. Select **ROM** to choose content, or skip it for a core that supports running without content. Custom entries appear under **Others** and pass ZIP files directly to the selected core.

**Settings** saves Home's confirm/cancel swap and dual-screen preferences in `home.conf`. The swap exchanges the controller's south/east actions in Home and the runner. Dual-screen applies to DS/3DS and can also be enabled with `CABINET_DUAL_SCREEN=1`.

Select a system in the sidebar, then choose **Add game** or press **I**. Dragging files onto Home also opens import review and requires a selected system. Review the title, system, and core before choosing **Add to library**; the selected system's bundled core is the default, and a custom core can be selected.

Open a game's details to play, edit its title or core, mark it as a favorite, locate a moved file, or remove it. Removal leaves ROMs and saves untouched. Locate file preserves the game's identity and metadata. Importing an existing canonical path selects its entry; same-named files in different folders remain separate games.

In **Edit details**, use **Box art: choose image** to preview a PNG, JPEG, or BMP cover, then select **Save changes**. Cabinet stores a PNG copy with a maximum side of 1024 pixels. The copy stays with the entry when you rename the game or move its ROM.

Choose **Menu → SteamGridDB API key** to save, replace, or remove your [SteamGridDB API key](https://www.steamgriddb.com/profile/preferences/api). Cabinet stores it locally in `steamgriddb/key.conf` under the data directory, inside a directory accessible only to your user account. The key is masked in the menu and is sent only to the API, never to artwork downloads.

With a key configured, Home searches during import using the game title or selected ZIP member. It prefers an exact title match and square artwork, with Alternate square grids first. Save the preview with **Add to library**. Select the loading SteamGridDB row to cancel a lookup. Failed or cancelled lookups leave the import available, and duplicate imports keep their artwork.

The **SteamGridDB: find box art** row in import review and Edit details searches with the current title. You can edit the search query and choose a matching game. The preferred cover is staged until you save; cancelling the editor discards it. Existing games can still use the local image chooser.

The sidebar shows system counts and filters for All games, Favorites, and Recently played. Use **F** or the search box to search, and the view button to switch between grid and list. View and search choices last for the session.

Arrows, D-pad, or left stick move focus; Enter/south selects, Escape/east goes back, and Tab switches panes. Right from the sidebar enters the grid, and Up from its first row reaches the toolbar. The layout and file browser adapt to the window size.

Home stays open while a separate runner process plays the game. One game can run per home window. Launch failures appear in Home, and Recently played updates after the core and content load successfully. Close the game through its menu before closing Home.

## ZIP archives

Home supports ZIP imports for single-file Game Boy/Color, GBA, NES, SNES, N64, DS, Mega Drive, Master System, and Game Gear cartridges. You can select alternate cores, including cores such as bsnes that request a file path: Home extracts the selected ROM before launching the runner. Home checks custom cores when adding or editing an archived entry. Other systems still require cores that support memory-loaded content; path-based disc systems require unpacked content with their referenced files kept together.

A single matching ROM is selected automatically; multiple matches open a member picker. Each member can have its own library entry, saves, and configuration. Home extracts only the selected member into a private temporary directory for play and removes it after the runner exits. The archive remains untouched.

Encrypted archives, unsafe or duplicate member names, corrupt data, nested ZIPs, multi-file disc bundles, and ROMs over 512 MiB are unsupported. Locate file can switch between an extracted ROM and a ZIP archive or relink a moved archive. Choose the ROM inside the ZIP when multiple files match the game’s system; the game keeps its title, saves, and other metadata. Standalone runner launches require extracted content.

## Firmware and system assets

Place firmware under `~/.local/share/cabinet/system/`, or `DIR/system/` with `--data-dir DIR`. Home prompts for a compatible BIOS when importing PlayStation (PS1), PlayStation 2, or Saturn content; setup can be skipped or reopened through **BIOS files** in game details. PS1 and Saturn images are checked against known 512 KiB BIOS checksums. PS2 dumps (4–8 MiB) are checked for a valid ROM directory and console version metadata, then copied into `system/pcsx2/bios/` with a stable checksum-based filename. Existing PS2 dumps in that folder are also detected. Proprietary BIOS files are never downloaded or bundled.

For development, copy the bundled `system/PPSSPP/`, `system/dolphin-emu/`, and `system/pcsx2/resources/` folders into Home's system directory. See [building and packaging](building.md) for AppImage setup.

For Dreamcast, import a `.cue` or `.gdi` descriptor with its referenced tracks kept together; `.cdi`, `.chd`, and `.m3u` are also recognized. Flycast supports an HLE BIOS; an optional original BIOS belongs at `system/dc/dc_boot.bin`. It defaults to per-game VMU cards. Nintendo 3DS content opens directly by path and does not support ZIP imports.

See [core builds](cores.md#playstation-2) for PlayStation 2 formats, architecture-specific cores, and memory-card locations.

## Launch options and logging

```sh
./build/cabinet --data-dir /path/to/profile
./build/cabinet --system gb --import "/path/to/game.gbc"
./build/cabinet --log-level info
```

Home finds `cabinet-runner` beside its executable; `--runner PATH` overrides it. Bundled cores resolve against the current installation, so moving an installation or AppImage preserves selections. Custom cores retain their paths and must be located in Edit details if moved.

`--log-level` accepts `off`, `error`, `warn` (default), `info`, and `debug`. Info prints a shell-quoted runner command and its exit status; debug adds paths, entry counts, and readiness. Core verbosity is controlled separately by the runner's `--core-log-level`.
