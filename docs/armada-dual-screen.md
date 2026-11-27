# ArmadaOS direct dual-screen output

With `--dual-screen`, DS and 3DS sessions can lease the bottom panel directly in Armada Game Mode. Cabinet checks for `ID=armada` in `/etc/os-release` and a `GAMESCOPE_WAYLAND_DISPLAY` named `gamescope-*`, then looks for a single internal connector through the Wayland DRM lease protocol. Bottom gamescope yields the panel during the lease and resumes when Cabinet releases it.

Other environments and sessions without an offered internal connector use a second SDL window. Once Cabinet selects a connector, lease rejection or initialization failure ends the launch with an error. Launches without `--dual-screen` skip the lease and bottom digitizer.

## Rendering and touch

The startup log identifies the active bottom-panel renderer.

| Renderer | Presentation |
| --- | --- |
| Vulkan direct display | A display swapchain on the runner's Vulkan instance and device samples the frontend texture. Works with Vulkan cores and with software cores using `SDL_RENDER_DRIVER=vulkan`. |
| GBM/EGL GPU scanout | SDL's OpenGL and OpenGL ES 2 renderers draw into GBM buffers imported through EGL. Cabinet prefers EGL for Armada dual-screen sessions unless `SDL_VIDEO_FORCE_EGL` is set. |
| CPU readback | Fallback for the SDL software renderer or unavailable GPU scanout. GPU initialization failures log a warning. |

GPU paths apply the bottom crop, rotation, scaling, and menu dimming directly on the GPU and skip bottom frames when presentation resources are busy. The main window controls pacing. The EGL path uses atomic KMS, alternating scanout buffers, and fences. The Vulkan path presents on the same graphics queue as the main window.

Cabinet grabs the `bottom_touchscreen` evdev device and maps slot zero to the emulated stylus, accounting for panel rotation, letterboxing, and captured drags. Device loss or input overflow releases the contact. Cabinet retries an unavailable device every two seconds. Menus and focus stay on the main window.

Lease loss, Wayland disconnect, DRM failure, a two-second flip timeout, or Vulkan display/device failure ends the session and releases the panel and digitizer.

## Build and validation

Armada support is off by default. Enable it on Linux with `-DCABINET_ARMADA_DRM=ON`. On Debian, install `libdrm-dev`, `libgbm-dev`, `libegl-dev`, `libwayland-dev`, `wayland-protocols`, and `libwayland-bin`.

```sh
cmake -S . -B build-dual-screen -G Ninja -DBUILD_TESTING=ON -DCABINET_ARMADA_DRM=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build/sdl3-static"
cmake --build build-dual-screen
ctest --test-dir build-dual-screen --output-on-failure
```

Automated tests cover lease eligibility, second-window fallback, display transforms, and touch mapping. In the target gamescope session, run `./build-dual-screen/armada_drm_display_test [renderer]` to lease the panel for three seconds and check colored quadrants. The optional renderer is `opengl`, `opengles2`, `vulkan`, or `software`. Use `vulkan sharp` to check filtering, flip, and dimming or `vulkan bridge` to exercise the libretro Vulkan snapshot path. Validate panel orientation, touch, and gameplay with real DS and 3DS content on the target device.
