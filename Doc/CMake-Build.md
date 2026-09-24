# CMake + pyOCD Build (NUCLEO-N657X0-Q)

This is an additional build path alongside the existing Makefile, EWARM and
STM32CubeIDE projects. It only targets the **NUCLEO-N657X0-Q** board with an
**IMX219** camera module; for STM32N6570-DK or other sensors, use the
Makefile build instead (see the root [Makefile](../Makefile)).

Note: the IMX219 driver ([Lib/Camera_Middleware/sensors/imx219/](../Lib/Camera_Middleware/sensors/imx219/))
and ISP integration (debayering, AE, AWB) are both wired up and confirmed
working on real hardware — the UVC stream produces a real, detailed image.
**Colors currently have a strong magenta/pink cast**: the AWB tuning
([Inc/imx219_isp_param_conf.h](../Inc/imx219_isp_param_conf.h)) is adapted
from IMX335's, not calibrated against a real IMX219 module (that needs the
STM32 ISP IQTune tool and a color chart, which wasn't available here). See
that file's header comment for exactly which fields are real vs. copied
placeholders.

## Checking the sensor is actually capturing (raw dump diagnostic)

A separate, temporary diagnostic bypasses the ISP/pixel-packer pipeline
entirely: it shrinks the sensor's crop to 128x128 and captures raw Bayer10
straight into a small buffer via DCMIPP's "dump pipe" (PIPE0), readable
directly over SWD. This was the main bring-up tool before ISP integration
was in place.

**It's off by default now** (`IMX219_RAW_DUMP_TEST` in
[imx219.h](../Lib/Camera_Middleware/sensors/imx219/imx219.h), set to 0) —
turning it on breaks the real video stream for the rest of that boot. It
suspends PIPE1 (the normal UVC pipe) to let PIPE0 use the sensor without the
two conflicting (they share one sensor readout, see the comment above
`CMW_CAMERA_DebugRawDump()` in cmw_camera.c), and never restores either
afterwards. Only turn it back on when you specifically want to re-check raw
sensor output, not alongside real streaming. To use it:

1. Set `IMX219_RAW_DUMP_TEST` to 1, rebuild and flash, then start a UVC
   stream at least once (e.g. `ffplay /dev/videoN` or any viewer) — this is
   what actually triggers `CMW_CAMERA_DebugRawDump()` and starts PIPE0. The
   stream itself will show nothing useful once this fires, that's expected.
2. While it's running, fetch and view the raw frame:
   ```sh
   pip install pyocd pillow numpy matplotlib   # matplotlib optional
   python3 cmake/imx219_raw_dump.py            # one-shot, saves + shows raw_dump.png
   python3 cmake/imx219_raw_dump.py --watch 1  # live view, refreshes every second
   ```

Since it's raw Bayer, expect a grayscale mosaic, not a real photo — the
point is to confirm the sensor is delivering live, per-frame-varying data
(point it at something bright vs. covering it, and watch the values in
`--watch` mode change) rather than a fixed/stuck value.

## Prerequisites

- CMake >= 3.20
- A generator such as Ninja or Make
- [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (`arm-none-eabi-gcc`) on `PATH`
- [pyOCD](https://pyocd.io/) for flashing: `pip install pyocd`
- The board's onboard ST-Link connected via USB-C to CN10, in **Dev mode**
  boot-switch position (see [Boot-Overview.md](Boot-Overview.md))

## Build

Using the provided [CMakePresets.json](../CMakePresets.json):

```sh
cmake --preset default
cmake --build --preset default
```

Or without presets:

```sh
cmake -B build -G Ninja
cmake --build build
```

`ORIENTATION` can be set to `FRONT` (default) or `REAR`, either via the
`rear` preset (`cmake --preset rear && cmake --build --preset rear`, building
into `build-rear/`) or manually with `-DORIENTATION=REAR`.

This produces `build/Project`, `build/Project.bin` and `build/Project.hex`.

## Flash (load into SRAM and run)

```sh
cmake --build --preset flash
```

(or `flash-rear` for the REAR variant, or `cmake --build build --target flash`
without presets)

### Why this isn't a persistent flash write

The STM32N657 has **no internal flash**. This build links the whole
application into internal AXI SRAM (`Gcc/STM32N657xx_nucleo.ld`), exactly
like the Makefile/EWARM/CubeIDE builds do — this only works with the board's
boot switches in **Dev mode**, and the firmware is lost on power-cycle.

Persisting firmware across power cycles would mean booting from the external
QSPI flash, which needs a First Stage Boot Loader (FSBL) and
STM32CubeProgrammer's external-loader mechanism. Neither is included in this
repository (only unbuilt FSBL templates ship with STM32Cube_FW_N6), and pyOCD
has no equivalent to STM32CubeProgrammer's external loaders. If you need
persistent boot-from-flash, use STM32CubeProgrammer as described in
[Program-Hex-Files-STM32CubeProgrammer.md](Program-Hex-Files-STM32CubeProgrammer.md).

### What `cmake --build build --target flash` actually does

It runs [`cmake/pyocd_load.py`](../cmake/pyocd_load.py), which reproduces the
load sequence found in the IAR debug macro
([`EWARM/NUCLEO-N657X0-Q/fix_load_debug.mac`](../EWARM/NUCLEO-N657X0-Q/fix_load_debug.mac)) —
the only documented reference for what this part actually needs:

1. Attach over SWD, reset the chip and halt the core. The reset is required:
   otherwise the state of the previously running application (active
   exception, PSP/CONTROL, enabled interrupts, running DCMIPP/USB DMA) leaks
   into the new image and crashes it as soon as the memory layout changed.
   The reset re-locks the peripherals, which is why step 2 comes after it.
2. Disable the RISAF2 firewall, the TrustZone SAU, and the fault-handler
   enables, so the debug probe has unrestricted memory access.
3. Write `Project.bin` into SRAM at `0x34180400` (`ORIGIN(AXISRAM2_P2_S)`).
4. Read the initial SP/PC from the image's own vector table and set those
   core registers directly.
5. Resume the core.

pyOCD has no built-in target definition for the STM32N657 (very recent
Cortex-M55/TrustZone part), so the script connects using pyOCD's generic
`cortex_m` target — sufficient here since we only need basic SWD halt/memory
access/register set, not a flash algorithm.

This has been verified end-to-end on real NUCLEO-N657X0-Q hardware: build,
`cmake --build build --target flash`, and the app's boot banner comes up on
the ST-Link virtual COM port (115200 8N1, e.g. `/dev/ttyACM0`).

### Troubleshooting

- **`pyocd` reports "No device connected" / STLink error 5, or
  STM32CubeProgrammer itself also fails to read/write memory on this
  board**: the boot mode jumpers (`JP1`/BOOT0, `JP2`/BOOT1) are not in Dev
  mode. This is not a pyOCD quirk — with the jumpers wrong, even
  STM32CubeProgrammer's own CLI fails to access SRAM the same way. Check
  them against `_htmresc/NUCLEO-N657X0-Q_Dev_mode.png` /
  [Boot-Overview.md](Boot-Overview.md) and retry.
- Confirm `pyocd list --probes` sees the onboard ST-Link.
- If pyOCD can't find/select the Cortex-M55 core (STM32N657 also has a
  Cortex-M0+), check `pyocd list --targets` output and try passing
  `--probe-uid` if multiple probes are attached.
