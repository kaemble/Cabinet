# ArmadaOS direct dual-screen output

With `--dual-screen`, Cabinet uses a direct DRM lease for the bottom screen in Armada Game Mode. In other environments it retains the existing second SDL window. Without the flag it never calls the Armada factory, probes a lease, or opens the bottom touchscreen.

## Runtime path

The factory checks `/etc/os-release` for `ID=armada` and requires a primary `GAMESCOPE_WAYLAND_DISPLAY` named `gamescope-*`. It opens a separate Wayland connection, discovers `wp_drm_lease_device_v1`, and selects the single offered internal connector. It does not assume connector numbers or read `armada-bottom-env`. Without an offered internal panel it falls back to the usual SDL second window. Once a connector is selected, a rejected lease or initialization failure is reported rather than silently moving both screens onto the top panel.

Armada's bottom gamescope uses `--drm-lease-yield`: it suspends for a protocol lease holder and resumes when the holder releases its lease. Cabinet does not stop services, launch a compositor, or use `armada-run-bottom`.

The backend discovers the preferred mode, panel orientation, leased CRTC and XRGB8888 plane. Atomic KMS is required: Armada can lease a plane other than the CRTC's usual primary plane. After the first modeset, two scanout buffers alternate using nonblocking page flips. A pending flip drops a bottom presentation instead of adding another display pacing clock. Opaque flip cookies reject stale events from the companion's shared DRM event queue.

For SDL's `opengl` and `opengles2` renderers, renderable GBM buffers are imported through EGL DMA-BUF images into SDL render targets. The GPU draws the existing bottom crop, scaling, rotation and menu dimming directly into scanout storage. This path has no per-frame readback, CPU pixel conversion, CPU rotation or `glFinish`. Sharp Bilinear retains its existing GPU intermediate pass.

Allocation intersects the KMS plane's `IN_FORMATS` modifiers with EGL-importable modifiers that are not external-texture-only. GBM allocates storage suitable for both rendering and scanout, and KMS registration and FBO completeness validate it. A linear allocation is attempted if the initial allocation/import fails. SDL's GL target is created before attaching the EGL image, because SDL allocates texture storage even when wrapping an existing GL texture.

When supported, an EGL native fence is passed to KMS through `IN_FENCE_FD`. Otherwise the render fence is polled with zero timeout before the nonblocking page flip is submitted. Buffers are not reused while rendering or flipping is pending. GPU work is finished synchronously only during resource teardown.

Armada DS/3DS dual-screen launches prefer EGL before SDL initialization, including when the top window uses X11. An explicit `SDL_VIDEO_FORCE_EGL` setting takes precedence. Ordinary launches and other operating systems do not change their GL context preference.

For Vulkan, `VulkanVideo` enables `VK_KHR_display`, `VK_EXT_direct_mode_display` and `VK_EXT_acquire_drm_display` before instance creation, exclusively for Armada DS/3DS dual-screen sessions. The native presenter acquires the leased connector through that same instance/device and creates a separate display swapchain. Its graphics pipeline samples Cabinet's exported Vulkan texture and draws the bottom crop, scaling, rotation, source flip and menu dimming directly into the swapchain images. Sharp Bilinear uses a fragment-shader equivalent of nearest integer enlargement followed by bilinear sampling, with no intermediate target.

Native bottom rendering submits after the top SDL renderer's submission on the same graphics queue. A GPU barrier makes source writes visible to the fragment shader. Each frame slot owns an acquire semaphore and render-completion fence; present semaphores belong to swapchain images, so reacquiring an image proves its prior presentation wait has finished. Acquires and fence checks use zero timeout and drop bottom frames when resources are busy. The native path does not switch SDL render targets, copy images, read pixels back, or wait for the device to idle in its frame loop. WSI owns the lease's DRM event queue; Cabinet does not drain that queue itself while Vulkan presentation is active.

Software cores explicitly selecting `SDL_RENDER_DRIVER=vulkan` also use the frontend-provided Vulkan instance and native presenter. XRGB8888 uploads retain their byte layout in a native BGRA texture. Other supported core pixel formats use the upload conversion SDL would otherwise perform behind an opaque wrapper; there is no full-panel CPU conversion. Hardware Vulkan cores use the existing frontend snapshot image, including libretro's producer synchronization.

Unsupported Vulkan drivers, the SDL software renderer, and GL contexts without usable EGL imports retain the CPU readback fallback, with an explicit warning when GPU initialization fails. The startup log identifies `Vulkan direct display`, `GBM/EGL GPU scanout` or `CPU readback`.

Touch uses the `bottom_touchscreen` evdev device, `openat` and `EVIOCGRAB`, as in the reference emulators. Slot zero supplies the single emulated stylus. Raw axes are normalized, inverse panel rotation is applied, and Cabinet's existing viewport/crop mapping handles letterboxes and captured drags. Device loss or input overflow releases the contact; the device is retried every two seconds. An unavailable digitizer produces a warning. Menus and focus handling remain on the main window; a leased panel has no window focus of its own.

Lease revocation, Wayland disconnect, DRM failure or a two-second flip timeout ends the session through normal error cleanup. Shutdown releases the digitizer, disables scanout when the lease is valid, frees buffers and closes the lease fd before destroying the protocol lease. There is no mid-session reacquisition after a compositor restart. Vulkan surface loss, swapchain invalidation or device loss also ends the session through normal cleanup; there is no live swapchain recreation. Vulkan destroys its swapchain and releases the display before the lease fd is closed and the protocol lease is released.

## Build and validation

Armada support is disabled by default. Enable it explicitly with `-DCABINET_ARMADA_DRM=ON` on Linux. It requires libdrm, GBM, EGL, wayland-client, wayland-protocols and wayland-scanner development packages. On Debian these are `libdrm-dev`, `libgbm-dev`, `libegl-dev`, `libwayland-dev`, `wayland-protocols` and `libwayland-bin`.

```sh
cmake -S . -B build-dual-screen -G Ninja -DBUILD_TESTING=ON -DCABINET_ARMADA_DRM=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build/sdl3-static"
cmake --build build-dual-screen
ctest --test-dir build-dual-screen --output-on-failure
```

The geometry/input test covers environment and connector selection, all rotations with padded strides, inverse touch mapping, evdev slots, release and overflow. It compares SDL's GPU draw transform with the CPU rotation at every pixel for all orientations, letterboxes and vertical source flips. Runner tests trap both Armada video configuration and the lease factory to prove ordinary launches never reach either, and exercise the second-window fallback.

`armada_drm_display_test` is a manual hardware test, outside CTest: it leases the panel for three seconds and draws red/blue/green/white logical quadrants. An optional renderer name (`opengl`, `opengles2`, `vulkan`, `software`) selects the SDL backend. GL tests require GPU scanout, verify the imported target's actual colors/rotation with a single test-only readback, and trap readback calls to prove that production GPU presentation never reads pixels back. The Vulkan test injects a one-time swapchain capture through test-only linker wrappers and checks the actual display pixels. `armada_drm_display_test vulkan sharp` compares shader filtering, vertical flip and dimming with an independent pixel reference. `armada_drm_display_test vulkan bridge` sends the pattern through the real libretro Vulkan snapshot bridge before native presentation. CMake compiles embedded SPIR-V from `assets/shaders/internal/armada_display.vert` and `.frag` with the host `glslangValidator`, writing headers under the build directory’s `generated/shaders/`. The installed application needs no shader compiler. Hardware tests require access to the real gamescope session. Validate panel orientation, touch input, both renderer paths, and real DS/3DS content on the target device; synthetic tests alone do not establish gameplay compatibility.
