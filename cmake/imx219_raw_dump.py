#!/usr/bin/env python3
"""Fetch the IMX219 raw-dump diagnostic buffer over SWD and show it as an image.

This only works while the firmware is built with IMX219_RAW_DUMP_TEST enabled
(the default, see Lib/Camera_Middleware/sensors/imx219/imx219.h) and a UVC
stream has been started at least once (e.g. by opening the camera with
ffplay/guvcview/etc. against its second USB port) so that
CMW_CAMERA_DebugRawDump() has actually configured the small 128x128 crop and
started the DCMIPP dump pipe (PIPE0). See Doc/CMake-Build.md for the full
story of why this exists: the sensor's raw Bayer output bypasses the
DCMIPP/ISP pixel-packer pipeline entirely, so it's usable to confirm the
sensor itself is alive independently of ISP integration status.

The buffer holds IMX219_DEBUG_RAW_DUMP_WIDTH x IMX219_DEBUG_RAW_DUMP_HEIGHT
raw10 samples, each stored as an unpacked little-endian 16-bit word (10
significant bits). This script does not attempt to debayer -- it just
displays the raw mosaic as grayscale, which is enough to tell whether the
sensor is delivering live, real, varying data.

Requires: pip install pyocd pillow numpy (matplotlib optional, for --watch
and interactive display; without it the script just saves a PNG).

Usage:
  python3 cmake/imx219_raw_dump.py                  # one-shot, saves + shows raw_dump.png
  python3 cmake/imx219_raw_dump.py --watch 1         # live view, refresh every 1s
  python3 cmake/imx219_raw_dump.py --elf build/Project --out /tmp/frame.png
"""
import argparse
import re
import struct
import subprocess
import sys

WIDTH = 128
HEIGHT = 128
SYMBOL = "raw_dump_buffer"


def find_symbol(elf_path, name):
    import os

    if not os.path.exists(elf_path):
        raise RuntimeError(f"'{elf_path}' not found. Build the project first (cmake --build build), "
                            f"or pass --elf to point at the built ELF.")
    try:
        result = subprocess.run(["arm-none-eabi-nm", elf_path], capture_output=True, text=True)
    except FileNotFoundError:
        raise RuntimeError("arm-none-eabi-nm not found on PATH. Install the Arm GNU Toolchain.")
    if result.returncode != 0:
        raise RuntimeError(f"arm-none-eabi-nm failed on '{elf_path}':\n{result.stderr.strip()}")
    out = result.stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise RuntimeError(
        f"symbol '{name}' not found in {elf_path}. Is IMX219_RAW_DUMP_TEST enabled "
        f"in imx219.h, and did you rebuild?"
    )


def read_frame(address, probe_uid=None):
    from pyocd.core.helpers import ConnectHelper

    size = WIDTH * HEIGHT * 2
    session = ConnectHelper.session_with_chosen_probe(
        unique_id=probe_uid,
        target_override="cortex_m",
        connect_mode="attach",
    )
    if session is None:
        raise RuntimeError("no debug probe found")

    with session:
        data = bytes(session.target.read_memory_block8(address, size))

    words = struct.unpack(f"<{WIDTH * HEIGHT}H", data)
    return words


def to_image_array(words):
    import numpy as np

    arr = np.array(words, dtype=np.uint16).reshape(HEIGHT, WIDTH)
    lo, hi = int(arr.min()), int(arr.max())
    span = max(1, hi - lo)
    scaled = ((arr.astype(np.float32) - lo) / span * 255).astype(np.uint8)
    return scaled, lo, hi


def save_png(scaled, out_path):
    from PIL import Image

    Image.fromarray(scaled, mode="L").resize((WIDTH * 4, HEIGHT * 4), Image.NEAREST).save(out_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", default="build/Project", help="Path to the built ELF (default: build/Project)")
    parser.add_argument("--out", default="raw_dump.png", help="Where to save the image (default: raw_dump.png)")
    parser.add_argument("--probe-uid", default=None, help="ST-Link probe unique ID, if more than one is attached")
    parser.add_argument("--watch", type=float, default=None, metavar="SECONDS",
                         help="Keep re-reading and redrawing every SECONDS (requires matplotlib)")
    args = parser.parse_args()

    try:
        address = find_symbol(args.elf, SYMBOL)
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"{SYMBOL} @ 0x{address:08x} ({WIDTH}x{HEIGHT}, raw10-as-u16)")

    try:
        import matplotlib.pyplot as plt
        have_matplotlib = True
    except ImportError:
        have_matplotlib = False
        if args.watch is not None:
            print("error: --watch requires matplotlib (pip install matplotlib)", file=sys.stderr)
            return 1

    if args.watch is None:
        try:
            words = read_frame(address, args.probe_uid)
        except RuntimeError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        scaled, lo, hi = to_image_array(words)
        save_png(scaled, args.out)
        print(f"raw sample range: {lo}-{hi} (of 0-1023); saved {args.out}")
        if have_matplotlib:
            plt.imshow(scaled, cmap="gray", vmin=0, vmax=255)
            plt.title(f"IMX219 raw dump (range {lo}-{hi}/1023)")
            plt.show()
        return 0

    import numpy as np

    plt.ion()
    fig, ax = plt.subplots()
    im = ax.imshow(np.zeros((HEIGHT, WIDTH), dtype=np.uint8), cmap="gray", vmin=0, vmax=255)
    title = ax.set_title("")
    print("Watching -- close the window or Ctrl+C to stop.")
    try:
        while plt.fignum_exists(fig.number):
            try:
                words = read_frame(address, args.probe_uid)
            except RuntimeError as e:
                print(f"error: {e}", file=sys.stderr)
                return 1
            scaled, lo, hi = to_image_array(words)
            im.set_data(scaled)
            title.set_text(f"IMX219 raw dump (range {lo}-{hi}/1023)")
            print(f"range {lo}-{hi}/1023")
            fig.canvas.draw_idle()
            plt.pause(args.watch)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
