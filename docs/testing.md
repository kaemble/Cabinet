# Testing

## Automated suite

```sh
ctest --test-dir build --parallel 4 --output-on-failure
```

The default suite covers CLI validation, library imports, input, settings inheritance, save-state recovery, frame pacing, and runner behavior. Tests use a synthetic libretro core and isolated data under `build/test-work/`. Graphics tests use separate Xvfb displays and Mesa software GL when available.

`firmware` tests PS2 BIOS validation, installation, and discovery with synthetic fixtures. `home_window` covers the BIOS setup and import flow.

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

Shader tests cover generated metadata and parameter layouts, effect controls and ordering, and GPU output against CPU references. With Xvfb available, `shader_opengl` and `shader_opengles2` check filtering, format conversion, cropped and flipped textures, independent dual-screen viewports, and resource release. `CABINET_TEST_VULKAN=ON` also enables `shader_vulkan`, which requires a Vulkan device or a software driver such as Lavapipe.

See [Armada dual-screen output](armada-dual-screen.md#build-and-validation) for panel and touch validation. Automated checks do not establish full gameplay compatibility; test actual content and target drivers before release.
