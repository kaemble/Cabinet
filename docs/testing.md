# Testing

## Automated suite

```sh
ctest --test-dir build --parallel 4 --output-on-failure
```

The default suite covers CLI validation, library/import flows, input and controller lifecycle, configuration inheritance, save-state failures/rollback, frame pacing, and runner behavior with a synthetic libretro core. The synthetic core is test-only and requires no commercial ROM. Graphics tests use independent Xvfb displays and Mesa software GL when available; test working directories and temporary data are isolated under `build/test-work/`.

To add real-core launch, save/load, and pause checks with a user-supplied ROM:

```sh
cmake -S . -B build -DCABINET_TEST_ROM="/path/to/game.gbc"
cmake --build build
ctest --test-dir build --parallel 4 --output-on-failure
```

Test saves remain in the ignored build directory. No ROM is bundled.

## Screenshots and smoke tests

```sh
./build/cabinet-runner --rom "/path/to/game.gbc" \
  --core ./build/cores/x86_64/gambatte_libretro.so \
  --frames 360 --screenshot /tmp/cabinet.bmp
./build/cabinet --frames 60 --screenshot /tmp/cabinet-library.bmp
```

The runner captures the final rendered window, reports received video frames, and fails if no frames arrive. Screenshots require a frame limit. `--frames` counts display-loop iterations, including paused loops and duplicate frames. Add `--paused` to capture the menu after the first video frame.

## Graphics and device checks

GL/GLES integration tests cover legacy/core GL and GLES2/3/3.1, orientation, resizing, context preservation, save/load, menus, fullscreen transitions, and teardown.

Builds with testing enabled also produce `vulkan_video_test`. Configure with `-DCABINET_TEST_VULKAN=ON` to add it to CTest on a machine with a display and Vulkan driver. It checks pixels, deferred commands, semaphore handoff, image lifetime, duplicate frames, resizing, frame-slot reuse, SDL drawing, fullscreen initialization, and teardown.

`shader_generation` validates shader metadata, the definitions emitted into the build directory, and matching GL/Vulkan parameter layouts, including counts that cross vec4 boundaries. `shader_controls` checks effect parameter editing and defaults, keyboard/controller toggles in place, manual stable sorting with selection retained, reordering across active/inactive effects, move cancellation and boundaries, invalid settings rejection, and mixed-stack order persistence. With Xvfb available, `shader_opengl` and `shader_opengles2` compare GPU pixels against a CPU reference, including neutral defaults, channel gains, multipass order, SDL format conversion, unaffected overlays, and resource release after disabling. CAS checks compare GPU pixels with a linear-light CPU reference across both controls, black/white/flat/detailed images, one-pixel views, cropped and flipped textures, RGB565 input, and color-correction ordering. CRT checks cover fractional controls, output-resolution mask/scanline/gamma comparisons, zero-tilt symmetry, curvature and tilt, zero overscan, interlaced fields, cropped/flipped RGB565 input, and effects before/after CRT. Effect checks also include film-grain animation, neutral settings, standalone chromatic offsets, crop boundaries, independent vignette strength and symmetry, hotspot values, occasional scratches, mixed-effect order, and cached program replacement. Viewport checks cover independent DS/3DS main and bottom windows, padded and flipped input textures, RGB565 conversion, chromatic/color/vignette stacks, scaling modes, and bypass after disabling effects. `CABINET_TEST_VULKAN=ON` also enables `shader_vulkan`; it needs a Vulkan device or software driver such as Lavapipe. The hardware video tests exercise neutral color correction with shared GL/GLES core textures, cropped views, resizing, and secondary windows.

See [Armada dual-screen output](armada-dual-screen.md#build-and-validation) for panel and touch validation. Automated checks do not establish full gameplay compatibility; test actual content and target drivers before release.
