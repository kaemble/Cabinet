# libretro API header

`libretro.h` was obtained on 2026-11-06 from:
https://raw.githubusercontent.com/libretro/libretro-common/master/include/libretro.h

The vendored copy makes builds independent of network access. Its MIT license
and copyright notice are included in the header. Bundled emulator cores have
their own licenses; the header's license does not cover those binaries.

`libretro_vulkan.h` was copied from the supplied RetroArch reference tree on
2026-11-13. It defines rendering interface v5 and context negotiation v2 and
retains its upstream MIT license notice. Vulkan headers are supplied by the
build environment.
