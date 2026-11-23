# Home library

## Import and organize

Press **Start** on a controller, **Esc** on the keyboard, or click **Menu** in the bottom bar to open Home's menu. It contains **Add custom entry**, **Settings**, and **Close Cabinet**. Close the running game before closing Cabinet.

**Add custom entry** asks for a core first. Select the **ROM** row to choose a file of any type, or leave it skipped and choose **Add to library** for a contentless core (`supports_no_game = "true"`). The runner checks that the core supports running without content. Custom entries use the generic system and appear under **Others** in the sidebar; that tab is hidden when there are no custom entries. ZIP files added this way are passed directly to the selected core without extraction. The core must support the chosen file.

**Settings** saves Home's confirm/cancel swap and dual-screen preferences in `home.conf` under the data directory. Swapping exchanges the controller's south/east actions in Home and adds `--swap-confirm-cancel` to runner launches. Enabling dual-screen adds `--dual-screen`, which the runner applies to DS/3DS games. Keyboard Enter/Escape retain their usual actions. `CABINET_DUAL_SCREEN=1` also continues to enable dual-screen launches.

Select a system in the sidebar, then choose **Add game** or press **I**. Dragging files onto Home also opens import review and requires a selected system. Review the title, system, and core before choosing **Add to library**; the selected system's bundled core is the default, and a custom core can be selected.

Open a game's details to play, edit its title or core, mark it as a favorite, locate a moved file, or remove it. Removal leaves ROMs and saves untouched. Locate file preserves the game's identity and metadata. Importing an existing canonical path selects its entry; same-named files in different folders remain separate games.

Choose **Edit details → Box art: choose image** to select a PNG, JPEG, or BMP cover. The editor previews the image; **Save changes** replaces the library placeholder or previous cover. Cancelling discards the selection. Cabinet stores its own PNG copy under `~/.local/share/cabinet/boxart/<game identity>.png`, preserving the aspect ratio and limiting the longest side to 1024 pixels. Moving or deleting the source image does not affect the saved cover. Title edits and ROM relocation keep the same art.

The sidebar includes system counts, All games, Favorites, and Recently played. Search with **F** or the search box; Enter/Escape finishes entry. Search and title fields support a movable cursor: Left/Right and Home/End move it, Shift selects text, and Ctrl+Left/Right moves by word. Backspace/Delete remove text around the cursor; holding Ctrl deletes a word. Ctrl+A selects all, and Ctrl+C/X/V copy, cut, and paste. Click to position the cursor or drag to select; long fields scroll to keep the cursor visible. The view button switches grid/list views. Games without box art use placeholders, and view/search choices last for the session.

Arrows, D-pad, or left stick move focus; Enter/south selects, Escape/east goes back, and Tab switches panes. Right from the sidebar enters the grid, and Up from its first row reaches the toolbar. The layout and file browser adapt to the window size.

Home stays open while a separate runner process plays the game. One game can run per home window. Launch failures appear in Home, and Recently played updates after the core and content load successfully. Close the game through its menu before closing Home.

## ZIP archives

Home supports ZIP imports for single-file Game Boy/Color, GBA, NES, SNES, N64, DS, Mega Drive, Master System, and Game Gear cartridges. You can select alternate cores, including cores such as bsnes that request a file path: Home extracts the selected ROM before launching the runner. Home checks custom cores when adding or editing an archived entry. Other systems still require cores that support memory-loaded content; path-based disc systems require unpacked content with their referenced files kept together.

A single matching ROM is selected automatically; multiple matches open a member picker. Each member can have its own library entry, saves, and configuration. Home extracts only the selected member into a private temporary directory for play and removes it after the runner exits. The archive remains untouched.

Encrypted archives, unsafe or duplicate member names, corrupt data, nested ZIPs, multi-file disc bundles, and ROMs over 512 MiB are unsupported. Locate file can switch between an extracted ROM and a ZIP archive or relink a moved archive. Choose the ROM inside the ZIP when multiple files match the game’s system; the game keeps its title, saves, and other metadata. Standalone runner launches require extracted content.

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
