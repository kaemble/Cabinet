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

**Configuration → Video → Shaders** contains Cabinet's built-in effects. All effects default off, with disabled rows grayed out. Enable an effect before confirming to open its parameters. Left/right adjusts a parameter immediately; confirm opens its value list. The shader pages keep the game undimmed, including the second screen.

North toggles the selected effect; West starts moving it with up/down. West or confirm accepts the order, and cancel restores the previous order. Enabled effects stay above disabled effects, and moving stays within the selected group. Keyboard equivalents are Delete to toggle and M to start/finish moving. Bottom-bar actions also support the mouse. Effects execute from top to bottom and follow system/game configuration scope.

Color correction provides brightness, contrast, saturation, gamma, and red/green/blue gain. Defaults are neutral: brightness 0%, all other parameters 100%. Saturation 0% produces grayscale, and higher gamma brightens midtones. Processing runs at source resolution before the selected scaling mode; menus and text are drawn afterward. OpenGL, GLES2, and Vulkan are supported. With no effects enabled, the existing presentation path runs without shader programs, intermediate effect textures, or effect passes.

Film grain is ported from hunterk’s public-domain `film_noise.glsl`. It provides animated grain (default strength 8, range 0–16), a centered hotspot, and occasional film scratches. Hotspot and scratches default off. The port uses procedural scratches and dust instead of the reference’s external `noise1` texture. Grain animation advances per rendered frame. Setting all controls to zero preserves the input. Like color correction, film grain defaults off.

Chromatic aberration provides independent red/green/blue X/Y offsets as a separate effect. All six offsets start at zero. Each step shifts a channel by 0.01% of the corresponding frame dimension; the range is −1% to +1%. Offset sampling stays inside the visible frame, including cropped textures.

Vignette is a separate effect with a centered, symmetric gradual falloff. Strength ranges from 0% (unchanged input) to 100% (full darkening), with a default of 100%. The effect defaults off and can be ordered independently of film grain and chromatic aberration.

Each configurable effect keeps its GLSL source and configuration together in `assets/shaders/effects/`; the generator discovers the `.glsl` files in that directory. Internal display shaders live separately in `assets/shaders/internal/`. A JSON header between `// cabinet-shader-begin` and `// cabinet-shader-end`, with each line prefixed by `// `, defines the saved ID, display name, entry function, and ordered parameters. Each parameter declares its symbol, label, description, integer minimum/maximum/step/default (`initial`), optional display `suffix` (defaults to `%`), and the scale used to convert menu values to shader floats. For example, a scale of `0.01` converts 100% to `1.0`.

The entry function accepts the input RGB color followed by one float per parameter, in header order, and returns RGB. Keep the function arguments in that order when editing parameters. The generator packs values into vec4 uniforms for GL/GLES and matching Vulkan push constants; shaders may define 1–24 parameters, reserving two vec4s for source and animation data within Vulkan’s guaranteed 128-byte push constants. Effects with `"context": true` receive normalized frame coordinates and a frame counter after the RGB argument, before their parameters. They can call `sample_frame(position)` to read the input with coordinates clamped to the visible frame, including cropped native textures. Ranges and defaults must align with their steps. Saved settings use the current parameter order and must contain exactly one value per parameter.

The shader ID must match its filename. After adding or editing a GLSL file, run `cmake --build build`; CMake automatically generates the effect catalog, menu metadata, GL/GLES sources, and Vulkan SPIR-V headers under `build/generated/shaders/`. It also compiles the internal shaders from `assets/shaders/internal/`. Generated headers belong to the build directory and are not checked in. Builds require Python 3.12+ and the host `glslangValidator` tool (`glslang-tools` on Debian). Installed applications embed the shaders and need no shader compiler or preset files. `shader_settings.h` handles settings state and persistence using the generated metadata.

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

GL/GLES frames remain on the GPU through a shared context and texture; the core context survives menus and window changes. Vulkan shares the negotiated device with SDL and snapshots core frames into runner-owned RGBA8 images, preserving paused frames and converting supported formats. Rotating fenced slots and queue synchronization protect image lifetimes. Screenshots can require readback. Vulkan output is SDR; HDR and device-loss recovery are not implemented. See [testing](testing.md) for graphics validation.

## Frame pacing

**Advanced → VSync / VRR** applies immediately and follows system/game scope. With frame-based audio, the runner keeps native emulation speed, matching close fixed-refresh rates within 0.5% (including integer multiples) with a small audio resampling adjustment. Other refresh rates use content-rate scheduling; slower displays present the latest image while bounded catch-up preserves game speed. Unknown refresh or unavailable VSync uses timer pacing.

Audio starts with a small primed buffer. A media clock handles cores producing several video intervals per call, while bounded resampling feedback stabilizes the queue. Pause/resume resets timing and primes audio. Fast-forward disables pacing and VSync and mutes playback without changing saved preferences.

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
