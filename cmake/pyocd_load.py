#!/usr/bin/env python3
"""Load a flat binary image into STM32N657 SRAM over the on-board ST-Link and run it.

The STM32N657 has no internal flash. This project links the whole
application into AXI SRAM (see Gcc/STM32N657xx_nucleo.ld) and expects to be
started in "Dev mode" (see Doc/Boot-Overview.md) -- the same way
STM32CubeProgrammer or an IAR/CubeIDE debug session would load it.

The load sequence below mirrors EWARM/NUCLEO-N657X0-Q/fix_load_debug.mac,
which is the only documented reference for what actually has to happen on
this part:

  1. Reset the chip and halt the core. Without the reset, state of the
     previously running application leaks into the new one (active
     exception, PSP/CONTROL, enabled interrupts, running DCMIPP/USB DMA),
     which crashes as soon as the memory layout changed between builds.
     The reset re-locks RISAF2, which is why it happens before step 2.
  2. Disable RISAF2 (resource isolation firewall) so the debug probe (and
     later the application) has unrestricted access to the memory it needs.
  3. Disable the SAU (TrustZone Security Attribution Unit) and the
     configurable fault handlers, matching the "cmse" build of this app.
  4. Write the image into SRAM at its link address.
  5. Read the initial SP/PC out of the image's own vector table and set
     those core registers directly, instead of doing a normal reset-and-run
     (which would not re-apply steps 2-3).
  6. Resume the core.

Requires: pip install pyocd
"""
import argparse
import struct
import sys

from pyocd.core.helpers import ConnectHelper

# Resource Isolation Slave for AXI Flexible memory (RISAF2) region config
# registers. Zeroing these lifts the default access restrictions on SRAM.
RISAF2_DISABLE_REGS = [
    0x54028040, 0x54028080, 0x540280C0, 0x54028100,
    0x54028140, 0x54028180, 0x540281C0,
]
SAU_CTRL = 0xE000EDD0    # SAU->CTRL
SHCSR = 0xE000ED24       # SCB->SHCSR (fault handler enables)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, help="Flat binary image to load (objcopy -O binary output)")
    parser.add_argument("--address", required=True, type=lambda x: int(x, 0),
                         help="SRAM load address; must match the image's own vector table base")
    parser.add_argument("--probe-uid", default=None,
                         help="Unique ID of the ST-Link probe to use, if more than one is connected")
    args = parser.parse_args()

    with open(args.bin, "rb") as f:
        data = f.read()

    if len(data) < 8:
        print(f"error: {args.bin} is too small to contain a vector table", file=sys.stderr)
        return 1

    print("Connecting to ST-Link...")
    session = ConnectHelper.session_with_chosen_probe(
        unique_id=args.probe_uid,
        target_override="cortex_m",
        connect_mode="halt",
    )
    if session is None:
        print("error: no debug probe found", file=sys.stderr)
        return 1

    with session:
        target = session.target
        core = target.selected_core

        print("Resetting and halting the core...")
        target.reset_and_halt()

        print("Disabling RISAF2, SAU and fault handlers (see fix_load_debug.mac)...")
        for reg in RISAF2_DISABLE_REGS:
            target.write32(reg, 0)
        target.write32(SAU_CTRL, 0)
        target.write32(SHCSR, 0)

        print(f"Writing {len(data)} bytes to 0x{args.address:08x}...")
        target.write_memory_block8(args.address, data)

        sp, pc = struct.unpack_from("<II", data, 0)
        print(f"Setting MSP=0x{sp:08x} PC=0x{pc:08x} and resuming...")
        core.write_core_register("msp", sp)
        core.write_core_register("pc", pc)
        core.resume()

    print("Application running. Board must stay in Dev mode; power-cycling loses it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
