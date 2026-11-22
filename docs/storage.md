# Saves and storage

## Locations

| Data | Default location |
| --- | --- |
| Library database | `~/.local/share/cabinet/library.sqlite3` |
| Box art | `~/.local/share/cabinet/boxart/<game identity>.png` |
| Firmware and system assets | `~/.local/share/cabinet/system/` |
| Battery and core-managed saves | `~/.local/share/cabinet/saves/` |
| Save states | `~/.local/share/cabinet/states/` |
| Global runner preferences | `~/.config/cabinet/runner.conf` |
| System settings and bindings | `~/.config/cabinet/{video,input}/` |
| Per-game configuration | `~/.config/cabinet/games/<system>/<game identity>/` |

`--data-dir DIR` places game data under `DIR` and all configuration under `DIR/config/`. Home passes this root to its runner. Explicit `--system-dir`, `--save-dir`, and `--state-dir` override individual locations; the ROM directory is never implicitly used for saves, and ROMs are not modified.

## Identity and layout

Library games have permanent random 128-bit IDs. Their readable save-folder names derive from the original ROM filename, are sanitized and limited to 160 UTF-8 bytes, and receive an identity suffix on collisions. Title edits and ROM relocation preserve the ID and folder. Removed folders stay reserved to protect leftover saves.

```text
~/.local/share/cabinet/
  saves/<system>/<save name>/
    <game name>.srm
    <game name>.rtc
    cabinet-game.txt
  states/<system>/<save name>/
    cabinet-game.txt
    <core>/<core fingerprint>.state
    <core>/<core fingerprint>.state.txt
```

Core-managed files retain their native structure, such as Flycast VMU files. Flycast and SwanStation basename-based cards are copied to expected names after relocation without overwriting destinations; other custom core layouts may need manual renaming.

Home passes `--game-id ID --save-name NAME` to the runner. Standalone launches can supply the same values. Without `--save-name`, the ID names the folder. Without `--game-id`, identity falls back to a content hash or canonical-path hash under `Generic`, even with `--system`; moving path-identified content changes that identity.

Plain-text manifests record identity, title, content basename, system, core version, and core fingerprint. State sidecars also record timestamps. Fingerprints are FNV-1a compatibility identifiers, not security checks. Disc images are not read in full solely for metadata.

## Persistence and recovery

Battery RAM and RTC load automatically and save every ten seconds and on exit. Incompatible save sizes cause a launch error. Writes use temporary files followed by rename; unchanged data is not rewritten. Abrupt termination can lose progress since the last checkpoint. Loading a state also changes the battery memory saved afterward.

Cabinet retains one previous complete version with a `.bak` suffix, including state metadata. Load a backup state with `--load-state FILE`; to restore battery/RTC data, close the runner and copy the backup over the live file. Files written directly by cores are managed by those cores.

There is one default state slot per game and core binary, with payloads up to 512 MiB. Changing the core binary selects a separate slot. An explicit state load rejects a conflicting fingerprint when metadata is present. Preserve the matching ROM and core binary for important states; matching version labels do not guarantee compatibility across builds or architectures. Battery-save compatibility between cores also varies.

Home stores total active playtime in the library and supplies it to the runner. The runner checkpoints it in `cabinet-game.txt` every 15 seconds and on exit when changed. Paused time is excluded; a crash can lose time since the last checkpoint.
