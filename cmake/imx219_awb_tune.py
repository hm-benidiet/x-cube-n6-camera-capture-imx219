#!/usr/bin/env python3
"""Interactively tune the IMX219 ISP's AWB gains against live video, then
print the final values ready to paste into Inc/imx219_isp_param_conf.h.

Background: ISP_Init() copies the const tuning struct
(ISP_IQParamCacheInit_IMX219) into a live, mutable copy called
`ISP_IQParamCache` (Lib/Camera_Middleware/ISP_Library/isp/Src/
isp_services.c). The AWB algorithm (isp_awb_algo.c) only re-applies gains to
hardware when the estimated color temperature changes OR when
`ISP_IQParamCache.AWBAlgo.enable` is set to the sentinel value 255
(ISP_AWB_ENABLE_RECONFIGURE, isp_core.h) -- otherwise it silently keeps
whatever was last pushed, even if the tuning table changes underneath it.
So writing new gains alone over SWD has no visible effect; this script also
sets that reconfigure flag after every change to force the algorithm to
re-read and re-apply immediately (confirmed against real hardware: without
it, edits are silently ignored).

This only touches the per-channel gains (ispGainR/G/B), not the 3x3 color
correction matrix (coeff) -- a global R/G/B channel imbalance (e.g. the
magenta cast from using IMX335's uncalibrated AWB tuning) is exactly what a
gain trim corrects; the matrix is a separate, harder-to-tune-by-eye lever
for cross-channel color correction, left alone here.

Setup:
  1. Build and flash normally (IMX219_RAW_DUMP_TEST must be 0 -- the
     default), then start a UVC stream and open it in a live viewer
     (ffplay, guvcview, OBS, ...) in a SEPARATE window so you can watch the
     effect of each change immediately.
  2. Run this script alongside it:
       pip install pyocd
       python3 cmake/imx219_awb_tune.py
  3. Type commands to trim each channel (multiplicative, relative to the
     values currently baked into the firmware) and watch the viewer:
       r 0.8       set the red trim to 0.8x
       g 1.1       set the green trim to 1.1x
       b 0.9       set the blue trim to 0.9x
       show        print current trims and the resulting absolute gains
       reset       back to 1.0x on all channels (restores the baked-in gains)
       bake        print the final gain values, ready to paste into
                   Inc/imx219_isp_param_conf.h
       quit        exit (leaves the last-applied values running on the
                   board; they're only in RAM, a reflash reverts to
                   whatever's baked into imx219_isp_param_conf.h)

Requires: pip install pyocd
"""
import argparse
import struct
import subprocess
import sys

LIVE_SYMBOL = "ISP_IQParamCache"                  # what ISP_Algo_AWB_Process reads/reconfigures from
ORIG_SYMBOL = "ISP_IQParamCacheInit_IMX219"       # the untouched compile-time constant
AWB_OFFSET = 0x74                                 # AWBAlgo field within ISP_IQParamTypeDef
NUM_TEMP_SLOTS = 5           # ISP_AWB_COLORTEMP_REF
OFFSET_ENABLE = 0x00         # AWBAlgo.enable (uint8_t) -- write 255 here to force reapply
OFFSET_REF_COLOR_TEMP = 0xa4
OFFSET_GAIN_R = 0xb8
OFFSET_GAIN_G = 0xcc
OFFSET_GAIN_B = 0xe0
GAIN_UNIT = 100000000        # this value in the register = "x1.0"
ISP_AWB_ENABLE_RECONFIGURE = 255


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
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise RuntimeError(f"symbol '{name}' not found in {elf_path}. Did you build with ISP integration "
                        f"(the default), and is it the same build that's flashed?")


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


def read_u32(target, addr):
    return struct.unpack("<I", bytes(target.read_memory_block8(addr, 4)))[0]


def write_u32(target, addr, value):
    target.write_memory_block8(addr, list(struct.pack("<I", value & 0xFFFFFFFF)))


def write_u8(target, addr, value):
    target.write_memory_block8(addr, [value & 0xFF])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", default="build/Project", help="Path to the built ELF (default: build/Project)")
    parser.add_argument("--probe-uid", default=None, help="ST-Link probe unique ID, if more than one is attached")
    args = parser.parse_args()

    try:
        live_base = find_symbol(args.elf, LIVE_SYMBOL) + AWB_OFFSET
        orig_base = find_symbol(args.elf, ORIG_SYMBOL) + AWB_OFFSET
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    try:
        session = connect(args.probe_uid)
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    with session:
        target = session.target

        # Reference values always come from the untouched compile-time
        # constant, never from the live (possibly already-tuned-in-a-
        # previous-session) copy -- otherwise trims would compound across
        # separate runs of this script instead of always being relative to
        # what's actually in imx219_isp_param_conf.h.
        ref_temp = [read_u32(target, orig_base + OFFSET_REF_COLOR_TEMP + 4 * i) for i in range(NUM_TEMP_SLOTS)]
        num_profiles = 0
        for t in ref_temp:
            if t == 0:
                break
            num_profiles += 1
        if num_profiles == 0:
            print("error: no AWB profiles found (all referenceColorTemp entries are 0) -- wrong symbol/offset",
                  file=sys.stderr)
            return 1

        orig_r = [read_u32(target, orig_base + OFFSET_GAIN_R + 4 * i) for i in range(num_profiles)]
        orig_g = [read_u32(target, orig_base + OFFSET_GAIN_G + 4 * i) for i in range(num_profiles)]
        orig_b = [read_u32(target, orig_base + OFFSET_GAIN_B + 4 * i) for i in range(num_profiles)]

        print(f"{LIVE_SYMBOL}.AWBAlgo @ 0x{live_base:08x} (live), {ORIG_SYMBOL}.AWBAlgo @ 0x{orig_base:08x} (reference)")
        print(f"{num_profiles} AWB profile(s) (color temps: {ref_temp[:num_profiles]})")
        print("Baked-in gains (x1.0 = 100000000):")
        for i in range(num_profiles):
            print(f"  profile {i}: R={orig_r[i]} G={orig_g[i]} B={orig_b[i]}")
        print()
        print(__doc__.split("Type commands")[1].split("Requires:")[0])

        trim = {"r": 1.0, "g": 1.0, "b": 1.0}

        def apply():
            for i in range(num_profiles):
                write_u32(target, live_base + OFFSET_GAIN_R + 4 * i, round(orig_r[i] * trim["r"]))
                write_u32(target, live_base + OFFSET_GAIN_G + 4 * i, round(orig_g[i] * trim["g"]))
                write_u32(target, live_base + OFFSET_GAIN_B + 4 * i, round(orig_b[i] * trim["b"]))
            # Force the AWB algorithm to notice and re-push to hardware even
            # though the scene's estimated color temperature hasn't changed
            # -- see the module docstring.
            write_u8(target, live_base + OFFSET_ENABLE, ISP_AWB_ENABLE_RECONFIGURE)

        def show():
            print(f"trim: R={trim['r']:.3f} G={trim['g']:.3f} B={trim['b']:.3f}")
            for i in range(num_profiles):
                print(f"  profile {i}: R={round(orig_r[i]*trim['r'])} G={round(orig_g[i]*trim['g'])} "
                      f"B={round(orig_b[i]*trim['b'])}")

        def bake():
            new_r = [round(orig_r[i] * trim["r"]) for i in range(num_profiles)]
            new_g = [round(orig_g[i] * trim["g"]) for i in range(num_profiles)]
            new_b = [round(orig_b[i] * trim["b"]) for i in range(num_profiles)]
            pad = NUM_TEMP_SLOTS - num_profiles
            print("\nPaste into the AWBAlgo block in Inc/imx219_isp_param_conf.h:\n")
            print(f"        .ispGainR = {{ {', '.join(str(v) for v in new_r)}, {', '.join(['0'] * pad)}, }},")
            print(f"        .ispGainG = {{ {', '.join(str(v) for v in new_g)}, {', '.join(['0'] * pad)}, }},")
            print(f"        .ispGainB = {{ {', '.join(str(v) for v in new_b)}, {', '.join(['0'] * pad)}, }},")
            print(f"\n(trim applied: R={trim['r']:.3f} G={trim['g']:.3f} B={trim['b']:.3f})")

        while True:
            try:
                line = input("awb-tune> ").strip()
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
                trim["r"] = trim["g"] = trim["b"] = 1.0
                apply()
                show()
            elif cmd == "bake":
                bake()
            elif cmd in ("r", "g", "b") and len(parts) == 2:
                try:
                    trim[cmd] = float(parts[1])
                except ValueError:
                    print(f"not a number: {parts[1]}")
                    continue
                apply()
                show()
            else:
                print("commands: r <factor> | g <factor> | b <factor> | show | reset | bake | quit")

    return 0


if __name__ == "__main__":
    sys.exit(main())
