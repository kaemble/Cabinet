# Video and performance

## Scaling and resolution

**Configuration → Video** or `--scale MODE` selects scaling:

| Mode | Behavior |
| --- | --- |
| `sharp-bilinear` (default) | Nearest-neighbor integer enlargement, then bilinear sampling to the final size; uses bilinear directly below 2x enlargement. |
| `nearest` | Nearest-neighbor sampling. |
| `bilinear` | Linear filtering. |
| `integer` | Largest whole-number enlargement that fits, centered with borders; shrinks proportionally if the source is too large. |

`--aspect-ratio auto|16:9|4:3|8:7|stretch` overrides the saved aspect setting. Auto uses the core's ratio, fixed ratios fit inside the window, and Stretch fills it. `--stretch` and `--keep-aspect` select Stretch and Auto. Integer scaling preserves source pixel shape and takes precedence over aspect ratio; high-DPI scaling uses drawable pixels.

Internal resolution is available when advertised by the core and increases GPU load and memory use. Dolphin, Mupen64Plus-Next, Flycast, and melonDS DS require a confirmed restart to apply it. melonDS DS uses Software at 1x and OpenGL Compute above 1x. Resolution and other video settings follow [system/game configuration scope](controls.md#settings-scope).

## Shaders

Open **Configuration → Video → Shaders** on an OpenGL, GLES2, or Vulkan renderer. Effects default off and run from top to bottom. Left/right toggles the selected effect, and confirm opens the parameters of an enabled effect. Left/right adjusts a parameter, and confirm opens its value list.

Press North or S to group enabled effects at the top while preserving their order. Press West or M to move an effect with up/down. Confirm or press the move button again to accept, or cancel to restore the order. The bottom-bar actions also support the mouse.

Effects follow system/game settings scope. Each dual-screen viewport filters its own visible crop. Effects normally run at source resolution before scaling, with menus and text drawn afterward. Shader pages keep gameplay undimmed.

| Effect | Controls and behavior |
| --- | --- |
| Color correction | Brightness, contrast, saturation, gamma, and RGB gain. Neutral values are 0% brightness and 100% for the other controls. |
| CAS | AMD Contrast Adaptive Sharpening in linear light. Adaptation defaults to 0% and intensity to 100%. Set intensity to 0% to bypass sharpening. |
| SMAA | Three-pass antialiasing with color or luminance edges, edge threshold, search distances, corner rounding, and debug views. All three passes run at viewport output resolution, which subsequent effects also use. Lookup textures are embedded. |
| CRT-Geom | Curvature, gamma, tilt, overscan, mask, scanline, and interlacing controls. Runs at output resolution, which also becomes the input resolution for subsequent effects. Interlacing applies to inputs of at least 400 lines per viewport. Zero overscan blanks the image, and negative overscan mirrors the axis. |
| Film grain | Gaussian grain with intensity, variance, mean, and signal-to-noise ratio controls. Defaults are 50%, 40%, 50%, and 6. Higher signal-to-noise ratios reduce grain in brighter pixels; zero disables this attenuation. Set intensity to zero to preserve the input. |
| Chromatic aberration | Independent RGB offsets on both axes, from −1% to +1% of the viewport in 0.01% steps. |
| Vignette | Centered falloff with strength from 0% (unchanged) to 100% (full darkening). |

Shader attribution and license notices are recorded in the [effect sources](../assets/shaders/effects/) and [third-party notices](../third_party/).

### Adding effects

Put configurable GLSL in `assets/shaders/effects/` and match the effect ID to the filename. Internal shaders live in `assets/shaders/internal/`. The JSON metadata between `// cabinet-shader-begin` and `// cabinet-shader-end` defines the ID, display name, entry function, and ordered parameters. Prefix metadata lines with `// `. Each parameter specifies a symbol, label, description, aligned integer bounds, step and `initial`, and a scale for the shader float. Use `suffix`, `display_scale`, and `decimals` to format menu values.

The entry function takes RGB and one float per parameter in metadata order and returns RGB. Effects support up to 20 parameters, packed into GL/GLES uniforms and Vulkan push constants. With `"context": true`, the function receives viewport coordinates and a frame counter after RGB. Contextual effects can query `frame_size()`, `output_size()`, and `frame_texel_size()`, or sample the viewport with `sample_frame(position)`. Use `"resolution": "output"` to render at viewport output resolution. Saved values follow metadata order and must match the parameter count.

For multipass effects, enable context and list up to four functions in `"passes"`, ending with the main `"entry"`. These functions return RGBA. Use `CABINET_PASS` for the zero-based pass index and `sample_pass_0(position)` and related helpers for earlier pass results. An effect can declare up to three lookup textures using `name`, `width`, `height`, `channels`, `linear`, and `data` (base64-encoded zlib bytes). See [SMAA](../assets/shaders/effects/smaa.glsl) for a complete multipass example.

Run `cmake --build build` to regenerate the effect catalog, menu metadata, GL/GLES sources, and Vulkan SPIR-V under `build/generated/shaders/`. Installed applications embed the shaders.

## Hardware rendering

Software, OpenGL/OpenGL ES, and Vulkan cores are supported. `--opengl` and `--vulkan` select the preferred hardware API; if both appear, the last wins. Dolphin and Flycast default to Vulkan, while other cores prefer OpenGL Core. A core must implement the selected backend and may also require a renderer option. These flags do not select Home's renderer.

For Mupen64Plus-Next, **Advanced** selects RDP/RSP plugins, GLideN64 threading, and texture filtering. GLideN64 is the default, with N64 3-point filtering. ParaLLEl-RDP selects an LLE RSP and has separate internal-resolution settings. Renderer changes require restart. To select Vulkan explicitly:

```sh
./build/cabinet-runner --rom /games/game.z64 \
  --core ./build/cores/x86_64/mupen64plus_next_libretro.so --vulkan \
  --core-option mupen64plus-rdp-plugin=parallel \
  --core-option mupen64plus-rsp-plugin=parallel
```

Dolphin requires the system root containing `dolphin-emu/Sys`:

```sh
./build/cabinet-runner --rom /games/game.rvz \
  --core ./build/cores/x86_64/dolphin_libretro.so --vulkan \
  --system-dir ./build/cores/x86_64/system
```

Dolphin's **More Core options** includes Dual core and Immediately present XFB. Dual core requires restart and defaults on for aarch64 and off for x86; saved/explicit values override defaults. Immediate XFB applies on resume.

GL/GLES shares textures with the core and keeps its context across menus and window changes. Vulkan shares the device with SDL and copies core frames into runner-owned images for SDR output and paused display. Screenshots can require GPU readback. See [testing](testing.md) for graphics validation.

## Frame pacing

**Advanced → VSync / VRR** applies immediately and follows system/game scope. With frame-based audio, fixed refresh rates within 0.5% of the core rate or a multiple use a small audio resampling adjustment. Other refresh rates use content-rate scheduling. Unknown refresh rates or unavailable VSync use timer pacing.

Pause and resume reset timing and prime audio. While fast-forward is active, pacing, VSync, and audio are disabled.

[Callback audio](runner.md#audio-callbacks) follows playback demand independently of video scheduling. It keeps native timing without queue-feedback speed correction and uses swap interval 1 to allow audio service between low-rate video frames.

`--no-vsync` requests timer pacing. `--vrr` uses content-rate presentation with VSync and assumes VRR is already enabled in the display/driver/compositor; it does not enable or verify hardware VRR.

## Diagnostics and benchmarks

`--timing-stats` prints five-second and final summaries covering core/present CPU time, presentation interval variation, late deadlines, repeated frames, and audio queue feedback. The configuration line shows the accepted swap interval, target rate, and core-clock pacing. `idle_presents` counts presents without a core call; `catchup_presents` counts those with multiple calls. Empty software queues do not establish hardware audio underruns, and SDL submission timing does not measure physical scanout.

For throughput comparisons, use the same ROM, scene, internal resolution, and window size:

```sh
./build/cabinet-runner --rom /games/game.rvz \
  --core ./build/cores/x86_64/dolphin_libretro.so \
  --fast-forward --timing-stats --windowed --window-size 1280x720 --opengl
```

Repeat with `--vulkan`. Reports include `elapsed_s`, `runs_per_s` (core calls), `video_fps` (new frames), and the negotiated `video_api`. Core calls may cover multiple video intervals, so call rate is not necessarily game frame rate. `--load-state FILE` resets timing counters after loading; `--frames` still counts display loops from startup.

**Interface → Show stats** displays new video frames per second, average presentation interval, and core idle percentage. Idle measures time outside synchronous `retro_run()` calls, excluding asynchronous worker activity; it is not total CPU utilization. Measurements pause in menus and restart on resume.
