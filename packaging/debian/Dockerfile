# Debian 13 is the minimum supported Linux library baseline.
# Update the image digest and archive snapshot deliberately, then rebuild all presets.
FROM docker.io/library/debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2
ARG TARGET_ARCH=amd64
ARG DEBIAN_SNAPSHOT=20261118T000000Z
RUN rm -f /etc/apt/sources.list.d/debian.sources && \
    printf 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/%s/ trixie main\n' "$DEBIAN_SNAPSHOT" > /etc/apt/sources.list && \
    printf 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/%s/ trixie-security main\n' "$DEBIAN_SNAPSHOT" >> /etc/apt/sources.list && \
    case "$TARGET_ARCH" in amd64|arm64) ;; *) exit 1 ;; esac && \
    if [ "$(dpkg --print-architecture)" != "$TARGET_ARCH" ]; then dpkg --add-architecture "$TARGET_ARCH"; fi && \
    apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates nlohmann-json3-dev build-essential cmake ninja-build python3 pkg-config nasm git glslang-tools \
    wayland-protocols libwayland-bin xvfb \
    && if [ "$(dpkg --print-architecture)" != "$TARGET_ARCH" ]; then \
        test "$TARGET_ARCH" = arm64 && apt-get install -y --no-install-recommends crossbuild-essential-arm64; \
    fi && \
    apt-get install -y --no-install-recommends \
    libfreetype-dev:$TARGET_ARCH libarchive-dev:$TARGET_ARCH libsqlite3-dev:$TARGET_ARCH libjpeg-dev:$TARGET_ARCH \
    libssl-dev:$TARGET_ARCH libvulkan-dev:$TARGET_ARCH libdrm-dev:$TARGET_ARCH \
    libgbm-dev:$TARGET_ARCH libegl-dev:$TARGET_ARCH libgl-dev:$TARGET_ARCH libglu1-mesa-dev:$TARGET_ARCH \
    libwayland-dev:$TARGET_ARCH libx11-dev:$TARGET_ARCH libxext-dev:$TARGET_ARCH \
    libxrandr-dev:$TARGET_ARCH libxcursor-dev:$TARGET_ARCH libxi-dev:$TARGET_ARCH \
    libxfixes-dev:$TARGET_ARCH libxss-dev:$TARGET_ARCH libxtst-dev:$TARGET_ARCH \
    libxkbcommon-dev:$TARGET_ARCH libdecor-0-dev:$TARGET_ARCH libasound2-dev:$TARGET_ARCH \
    libpulse-dev:$TARGET_ARCH libpipewire-0.3-dev:$TARGET_ARCH libudev-dev:$TARGET_ARCH \
    libusb-1.0-0-dev:$TARGET_ARCH libevdev-dev:$TARGET_ARCH libpng-dev:$TARGET_ARCH \
    zlib1g-dev:$TARGET_ARCH libbz2-dev:$TARGET_ARCH liblzma-dev:$TARGET_ARCH libzstd-dev:$TARGET_ARCH \
    libcurl4-openssl-dev:$TARGET_ARCH libmbedtls-dev:$TARGET_ARCH \
    liblz4-dev:$TARGET_ARCH libwebp-dev:$TARGET_ARCH libpcap-dev:$TARGET_ARCH \
    libdbus-1-dev:$TARGET_ARCH libfontconfig-dev:$TARGET_ARCH \
    libavcodec-dev:$TARGET_ARCH libavformat-dev:$TARGET_ARCH libavutil-dev:$TARGET_ARCH \
    libswresample-dev:$TARGET_ARCH libswscale-dev:$TARGET_ARCH libgl1-mesa-dri:$TARGET_ARCH \
    && rm -rf /var/lib/apt/lists/*
# SDL's GLES renderer is needed alongside desktop GL on ARM devices.
RUN apt-get update && apt-get install -y --no-install-recommends libgles-dev:$TARGET_ARCH \
    && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends clang \
    && rm -rf /var/lib/apt/lists/*
COPY packaging/debian/aarch64.cmake packaging/debian/armadaos.cmake packaging/debian/run-build.py /opt/cabinet/
RUN printf '%s\n' "$DEBIAN_SNAPSHOT" > /opt/cabinet/debian-snapshot && \
    printf '%s\n' 'debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2' > /opt/cabinet/base-image
ENV HOME=/tmp XDG_CACHE_HOME=/tmp/cabinet-cache
WORKDIR /src
