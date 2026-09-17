#!/usr/bin/env python3
"""Interactively tune the DCMIPP hardware YUV color-conversion matrix against
live video, then print the final values ready to paste into
Src/app_cam.c's CAM_EnableYuv().

Background: the ISP's AWB gain (imx219_awb_tune.py) turned out to have no
effect on the real image -- ISP_Algo_AWB_Process only pushes new gains to
hardware when the estimated scene color temperature changes, and forcing a
reconfigure was confirmed (by zeroing entire matrix rows and measuring pixel
output) to still produce no visible change. The demosaic Bayer-pattern fix
(RGGB -> GRBG, matching the mirrored sensor orientation) *did* produce a
measurable improvement, but a magenta cast remains.

The actual RGB->YUV color-space conversion is done by DCMIPP hardware
registers (P1YUVRR1/RR2/GR1/GR2/BR1/BR2 for pipe1), configured once at stream
start by CAM_EnableYuv() in Src/app_cam.c via
HAL_DCMIPP_PIPE_SetYUVConversionConfig(). This is confirmed to be the real,
active signal path (there is no other color transform between demosaiced RGB
and the YUV output over UVC). This script pokes those registers directly so
changes show up live, instead of the disconnected ISP AWB parameters.

The 3x3 matrix already baked into CAM_EnableYuv() (RR=131 RG=-119 RB=-12 /
GR=55 GG=183 GB=18 / BR=-30 BG=-101 BB=131) is a standard, sensor-independent
RGB->YCbCr (BT.601-ish) encoding matrix -- it's not the source of a
sensor-specific color cast by itself, it just re-encodes whatever RGB it's
given. Since there's no other active white-balance in the real signal path,
the magenta cast most likely comes from an R/B-heavy demosaiced RGB feeding
into this matrix. A correct way to fix that *through this same matrix*
(without adding new code) is to pre-scale the matrix's *columns*: column 1
(RR, GR, BR) is entirely input-R's contribution to the three outputs, column
2 (RG, GG, BG) is input-G's, column 3 (RB, GB, BB) is input-B's. Scaling a
column by k is mathematically identical to scaling that input channel by k
before the matrix multiply -- i.e. exactly a white-balance gain. The "A"
terms (RA/GA/BA) are fixed per-output offsets (mid-scale/chroma bias, not
white balance) and are left untouched.

Registers hold each signed coefficient as a two's-complement value truncated
to 11 bits (RR/RG/RB/GR/GG/GB/BR/BG/BB) or 10 bits (RA/GA/BA), packed two per
32-bit register:
    P1YUVRR1 = RG_11 << 16 | RR_11        P1YUVRR2 = RA_10 << 16 | RB_11
    P1YUVGR1 = GG_11 << 16 | GR_11        P1YUVGR2 = GA_10 << 16 | GB_11
    P1YUVBR1 = BG_11 << 16 | BR_11        P1YUVBR2 = BA_10 << 16 | BB_11
(reverse-engineered from stm32n6xx_hal_dcmipp.c's MATRIX_VALUE11/
MATRIX_VALUE10 macros and the DCMIPP_TypeDef field masks in the CMSIS
device header; verified by decoding a value of 0 back to 0).

These are fixed hardware register addresses (DCMIPP peripheral, not
compiled-code symbols), so no ELF/nm lookup is needed here -- unlike
imx219_awb_tune.py. They only hold meaningful data once CAM_EnableYuv() has
actually run at least once, i.e. after a YUV-format stream has started.

Setup:
  1. Build and flash normally, then start a UVC stream and open it in a live
     viewer (ffplay, guvcview, OBS, ...) in a SEPARATE window so you can
     watch the effect of each change immediately. This must be done AFTER
     flashing/reset and BEFORE running this script, so CAM_EnableYuv() has
     already written its initial values into the registers this script reads
     and modifies.
  2. Run this script alongside it:
       pip install pyocd
       python3 cmake/imx219_yuv_matrix_tune.py
  3. Type commands and watch the viewer:
       r 0.8        scale input-R's contribution (column 1) by 0.8x
       g 1.1        scale input-G's contribution (column 2) by 1.1x
       b 0.9        scale input-B's contribution (column 3) by 0.9x
       set RG -50   directly set one matrix coefficient by name
                    (RR RG RB GR GG GB BR BG BB)
       show         print current matrix and trims
       reset        back to the values read at startup
       bake         print the final matrix, ready to paste into
                    Src/app_cam.c's CAM_EnableYuv()
       quit         exit (leaves the last-applied values running on the
                    board; they're only in registers, a reflash/reset
                    reverts to whatever's baked into app_cam.c)

Requires: pip install pyocd
"""
import struct
import sys

DCMIPP_BASE = 0x58002000
REG = {
    "RR1": DCMIPP_BASE + 0x984,
    "RR2": DCMIPP_BASE + 0x988,
    "GR1": DCMIPP_BASE + 0x98c,
    "GR2": DCMIPP_BASE + 0x990,
    "BR1": DCMIPP_BASE + 0x994,
    "BR2": DCMIPP_BASE + 0x998,
}

# (register, field name, is_low_half, bits) for each of the 12 coefficients.
# "low half" = bits [10:0] or [9:0]; "high half" = bits [26:16] or [25:16].
FIELDS = {
    "RR": ("RR1", True, 11), "RG": ("RR1", False, 11),
    "RB": ("RR2", True, 11), "RA": ("RR2", False, 10),
    "GR": ("GR1", True, 11), "GG": ("GR1", False, 11),
    "GB": ("GR2", True, 11), "GA": ("GR2", False, 10),
    "BR": ("BR1", True, 11), "BG": ("BR1", False, 11),
    "BB": ("BR2", True, 11), "BA": ("BR2", False, 10),
}
MATRIX_FIELDS = ["RR", "RG", "RB", "GR", "GG", "GB", "BR", "BG", "BB"]
COLUMN = {"r": ["RR", "GR", "BR"], "g": ["RG", "GG", "BG"], "b": ["RB", "GB", "BB"]}


def encode(value, bits):
    mask = (1 << bits) - 1
    return value & mask


def decode(field, bits):
    field &= (1 << bits) - 1
    sign_bit = 1 << (bits - 1)
    return field - (sign_bit << 1) if field & sign_bit else field


def read_u32(target, addr):
    return struct.unpack("<I", bytes(target.read_memory_block8(addr, 4)))[0]


def write_u32(target, addr, value):
    target.write_memory_block8(addr, list(struct.pack("<I", value & 0xFFFFFFFF)))


def read_all(target):
    raw = {name: read_u32(target, addr) for name, addr in REG.items()}
    values = {}
    for coeff, (reg, low, bits) in FIELDS.items():
        half = raw[reg] & 0xFFFF if low else (raw[reg] >> 16) & 0xFFFF
        values[coeff] = decode(half, bits)
    return values


def write_all(target, values):
    for reg_name in REG:
        low_coeff = [c for c, (r, low, b) in FIELDS.items() if r == reg_name and low][0]
        high_coeff = [c for c, (r, low, b) in FIELDS.items() if r == reg_name and not low][0]
        _, _, low_bits = FIELDS[low_coeff]
        _, _, high_bits = FIELDS[high_coeff]
        low_enc = encode(values[low_coeff], low_bits)
        high_enc = encode(values[high_coeff], high_bits)
        write_u32(target, REG[reg_name], (high_enc << 16) | low_enc)


def connect(probe_uid=None):
    from pyocd.core.helpers import ConnectHelper

    session = ConnectHelper.session_with_chosen_probe(
        unique_id=probe_uid,
        target_override="cortex_m",
        connect_mode="attach",
    )
    if session is None:
        raise RuntimeError("no debug probe found")
    session.open()
    return session


def main():
    import argparse

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--probe-uid", default=None, help="ST-Link probe unique ID, if more than one is attached")
    args = parser.parse_args()

    try:
        session = connect(args.probe_uid)
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    with session:
        target = session.target
        original = read_all(target)

        if all(original[c] == 0 for c in MATRIX_FIELDS):
            print("warning: matrix is all-zero -- CAM_EnableYuv() probably hasn't run yet.",
                  file=sys.stderr)
            print("Start a UVC stream first (this is what calls CAM_EnableYuv()), then re-run this script.",
                  file=sys.stderr)
            return 1

        current = dict(original)

        print("Read from live DCMIPP registers (pipe1 YUV conversion matrix):")
        print(f"  RR={original['RR']:4d} RG={original['RG']:4d} RB={original['RB']:4d} RA={original['RA']:4d}")
        print(f"  GR={original['GR']:4d} GG={original['GG']:4d} GB={original['GB']:4d} GA={original['GA']:4d}")
        print(f"  BR={original['BR']:4d} BG={original['BG']:4d} BB={original['BB']:4d} BA={original['BA']:4d}")
        print()
        print(__doc__.split("Type commands")[1].split("Requires:")[0])

        def show():
            print(f"  RR={current['RR']:4d} RG={current['RG']:4d} RB={current['RB']:4d} RA={current['RA']:4d}")
            print(f"  GR={current['GR']:4d} GG={current['GG']:4d} GB={current['GB']:4d} GA={current['GA']:4d}")
            print(f"  BR={current['BR']:4d} BG={current['BG']:4d} BB={current['BB']:4d} BA={current['BA']:4d}")

        def apply_column(channel, factor):
            for coeff in COLUMN[channel]:
                current[coeff] = round(original[coeff] * factor)
            write_all(target, current)
            show()

        def bake():
            print("\nPaste into CAM_EnableYuv() in Src/app_cam.c:\n")
            print("  DCMIPP_ColorConversionConfTypeDef color_conf = {")
            print("    .ClampOutputSamples = ENABLE,")
            print("    .OutputSamplesType = DCMIPP_CLAMP_YUV,")
            print(f"    .RR = {current['RR']}, .RG = {current['RG']}, .RB = {current['RB']}, .RA = {current['RA']},")
            print(f"    .GR = {current['GR']}, .GG = {current['GG']}, .GB = {current['GB']}, .GA = {current['GA']},")
            print(f"    .BR = {current['BR']}, .BG = {current['BG']}, .BB = {current['BB']}, .BA = {current['BA']},")
            print("  };")

        while True:
            try:
                line = input("yuv-tune> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if not line:
                continue
            parts = line.split()
            cmd = parts[0].lower()
            if cmd in ("quit", "exit", "q"):
                break
            elif cmd == "show":
                show()
            elif cmd == "reset":
                current.clear()
                current.update(original)
                write_all(target, current)
                show()
            elif cmd == "bake":
                bake()
            elif cmd in ("r", "g", "b") and len(parts) == 2:
                try:
                    factor = float(parts[1])
                except ValueError:
                    print(f"not a number: {parts[1]}")
                    continue
                apply_column(cmd, factor)
            elif cmd == "set" and len(parts) == 3:
                field = parts[1].upper()
                if field not in MATRIX_FIELDS:
                    print(f"unknown field '{field}', must be one of: {', '.join(MATRIX_FIELDS)}")
                    continue
                try:
                    current[field] = int(parts[2])
                except ValueError:
                    print(f"not an integer: {parts[2]}")
                    continue
                write_all(target, current)
                show()
            else:
                print("commands: r <factor> | g <factor> | b <factor> | set <FIELD> <value> | show | reset | bake | quit")

    return 0


if __name__ == "__main__":
    sys.exit(main())
