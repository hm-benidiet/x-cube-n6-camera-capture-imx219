# Build/flash/debug environment for the NUCLEO-N657X0-Q camera-capture firmware.
FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG ARM_TOOLCHAIN_VERSION=14.3.rel1

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        make \
        xz-utils \
        gdb-multiarch \
        python3 \
        python3-venv \
        python3-pip \
        libusb-1.0-0 \
        usbutils \
        picocom \
    && rm -rf /var/lib/apt/lists/*

# Arm GNU Toolchain, installed at the same path as on the reference host so
# existing build/ directories and compile_commands.json stay valid.
RUN ARCH="$(dpkg --print-architecture)" \
    && case "$ARCH" in amd64) TC_ARCH=x86_64 ;; arm64) TC_ARCH=aarch64 ;; *) echo "unsupported arch $ARCH" && exit 1 ;; esac \
    && curl -fsSL "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${ARM_TOOLCHAIN_VERSION}-${TC_ARCH}-arm-none-eabi.tar.xz" \
        | tar -xJ -C /opt \
    && mv /opt/arm-gnu-toolchain-${ARM_TOOLCHAIN_VERSION}-${TC_ARCH}-arm-none-eabi /opt/gcc-arm-none-eabi \
    && rm -rf /opt/gcc-arm-none-eabi/share/doc
ENV PATH=/opt/gcc-arm-none-eabi/bin:$PATH

# Python tooling: pyOCD for flashing (cmake/pyocd_load.py) and the deps of the
# IMX219 tuning/diagnostic scripts in cmake/.
ENV VIRTUAL_ENV=/opt/venv
RUN python3 -m venv $VIRTUAL_ENV \
    && $VIRTUAL_ENV/bin/pip install --no-cache-dir \
        pyocd \
        pyserial \
        numpy \
        pillow \
        matplotlib \
        opencv-python-headless
ENV PATH=$VIRTUAL_ENV/bin:$PATH

# Serial console (ST-Link VCP) and USB access for the non-root user.
RUN usermod -aG dialout,plugdev vscode

COPY serial-monitor.sh /usr/local/bin/serial-monitor
RUN chmod +x /usr/local/bin/serial-monitor
