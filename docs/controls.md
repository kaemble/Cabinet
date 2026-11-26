# Controls and configuration

## Keyboard and gamepads

These keyboard defaults cover Game Boy, GBA, NES, and SNES face buttons. Each system's full mapping appears under **Configuration → Input → Keyboard bindings**.

| Key | Action |
| --- | --- |
| Arrows | D-pad |
| X / Z | A / B |
| Enter / Backspace | Start / Select |
| Escape | Menu / back / resume |
| F11 | Toggle fullscreen |
| F5 / F7 | Save / load state |
| Hold Tab | Fast-forward |
| Window close | Confirm quit |

SDL-recognized gamepads work immediately; the first available pad controls player one. Keyboard input requires game-window focus. Analog systems use WASD for the left stick and IJKL for the right stick/C-buttons; N64 uses Space for Z, Q/E for L/R, and X/Z for A/B.

| System | Default gamepad layout |
| --- | --- |
| GB/GBC, GBA, NES | A east, B south; GBA L/R shoulders |
| SNES, DS | A east, B south, X north, Y west; L/R shoulders |
| N64 | A south, B west, Z left trigger, L/R shoulders; left stick movement, right stick C-buttons |
| Dreamcast | A south, B east, X west, Y north; L/R triggers, both sticks; C/Z shoulders, D Back/View |
| GameCube | A south, B west, X east, Y north, Z right shoulder; L/R triggers, both sticks |
| Mega Drive | A west, B south, C east, X left shoulder, Y north, Z right shoulder; Mode Select |
| Saturn | A west, B south, C east, X left shoulder, Y north, Z right shoulder; L/R triggers |
| Game Gear / Master System | 1 south, 2 east, Start/Pause Start |
| PSP / PlayStation / PlayStation 2 | Cross south, Circle east, Square west, Triangle north; L/R shoulders, sticks and PS1/PS2 L2/R2/L3/R3 |
| PC Engine | I east, II south; III west, IV north, V/VI shoulders |

Digital systems also bind the left stick to D-pad directions. Analog sticks preserve magnitude outside the deadzone, and triggers report pressure. For emulated controllers with analog input, stick and trigger deadzones are independently adjustable from 0% to 30%, defaulting to 5%. Bindings assume the cores' default control modes; changing a core's controller layout may require rebinding.

## Menu and hotkeys

**Escape** or **Start + Select/View** pauses gameplay and audio and opens the menu. Start and Select respond individually before the chord completes. **Interface → Menu shortcut** can instead select L3 + R3 or Home/Guide; the OS may intercept Home/Guide. Home shows a startup shortcut reminder for the first three successful controller launches; standalone callers can request it with `--show-menu-hint`.

Navigate with arrows, D-pad, or left stick. Enter/south confirms, Escape/east goes back, and X/Z also work. **Interface → Swap confirm/cancel** reverses the gamepad menu buttons. Mouse movement highlights rows, left-click activates on release, and the wheel navigates lists. Bottom-bar prompts are clickable. Held controls must be released after entering/leaving the menu before affecting gameplay.

The menu offers Resume, Save state, Load state, Restart game, Quit, Configuration, and Interface. Save/load uses one slot per game and core binary. State actions resume on success and keep the menu open on failure. Restart reinitializes the core and content. Restart and Quit require confirmation; **Interface → Confirm state actions** controls load/overwrite confirmation for menus and hotkeys and defaults on. A first save needs no confirmation.

**Configuration → Input → Hotkeys** edits Save state, Load state, and Fast forward bindings. Gamepad hotkeys start unbound. Save/load act once per press; fast-forward is held and mutes audio. `--fast-forward` keeps it enabled independently of the hotkey.

## Rebinding and devices

Under **Configuration → Input → Gamepad bindings / Keyboard bindings**, select an action, release held controls, then press a key/button or move a stick/trigger. Multiple inputs can drive one action, and one input can drive several actions. Simultaneous bindings use the strongest value. Individual stick directions can be bound, including digital inputs at full magnitude.

Escape or the menu chord cancels capture. Delete clears the selected action's bindings; North also clears gamepad/hotkey bindings. Restore defaults asks for confirmation and affects the current system. Menu navigation, Escape, F11, and Home/Guide remain reserved. Bindings save immediately; gamepad profiles use SDL physical positions across controller models.

**Input → Rumble** defaults on. Pausing, disabling rumble, disconnecting, or exiting stops vibration. N64 defaults to a Rumble Pak unless explicitly overridden. Dreamcast selects Flycast's Full Controller when advertised; Mega Drive defaults to six-button input, and compatible PlayStation cores use DualShock.

For Wii, **Input → Controller** selects Wii Remote + Nunchuck, Sideways Wii Remote, Classic Controller, or GameCube Controller. Changes apply immediately and each device keeps separate bindings. Remote modes include Waggle; pointer input is supported.

For melonDS DS, click or drag on the touchscreen. A drag starting inside stays captured and clamps at its edges until release; clicks outside are ignored. Touch respects letterboxing/scaling and is disabled in menus or while unfocused. `--core-option melonds_touch_mode=joystick` selects the core's controller stylus instead. Azahar handles touch mapping through its own screen-layout options; 3DS bindings include Circle Pad, C-stick, ZL/ZR, Home/Swap screens, and touchscreen press.

Disconnecting the active pad selects another available pad; if none remain, the game pauses and keyboard navigation remains available. Reconnection does not resume automatically. Focus loss pauses by default. **Interface** can disable this or toggle hiding the mouse after five seconds of inactivity. With focus-loss pausing disabled, gamepad input remains active in the background.

## Settings scope

Video, Input, Audio, and Advanced settings inherit from the selected system. **Configuration → Create game config** copies effective settings and bindings into a per-game configuration; subsequent edits affect that game. **Reset to inherited settings** removes its configuration reference after confirmation and restores system settings. Explicit CLI options take precedence.

**Video** controls fullscreen, scaling, aspect ratio, and advertised internal resolution. **Audio** controls volume. **Advanced** contains VSync, VRR, curated N64 controls, and **More Core options**. Core option submenus show accepted values and core-provided descriptions. Options requiring restart need **Restart game**; see [video and performance](video.md).

**Interface** settings are global: menu shortcut, confirm/cancel, state confirmations, focus-loss pausing, cursor hiding, and stats. See [storage](storage.md) for configuration paths.
