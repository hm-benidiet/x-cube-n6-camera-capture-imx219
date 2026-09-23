# x-cube-n6-camera-capture Application

Note: This is a fork which is adapted to work with a IMX219 instead of IMX355. 


## Devcontainer (recommended)

The repo ships a VS Code devcontainer ([.devcontainer/](.devcontainer/)) with the Arm GNU
toolchain, CMake/Ninja, pyOCD, gdb and a serial terminal. It needs a Linux host so the
ST-Link can be passed through over USB.

1. One-time, on the **host**: install the udev rules so the container user can access the ST-Link:
   ```bash
   sudo cp .devcontainer/99-stlink.rules /etc/udev/rules.d/
   sudo udevadm control --reload-rules && sudo udevadm trigger
   ```
2. In VS Code: *Dev Containers: Reopen in Container*.
3. Use *Terminal → Run Task…*:
   - `Build (FRONT)` / `Build (REAR)`
   - `Flash (FRONT)` / `Flash (REAR)`: build and load into SRAM
   - `Run (FRONT)` / `Run (REAR)`: flash, then open the serial console (115200 8N1)
   - `Serial monitor`: console only (`serial-monitor` in a terminal)
4. Debugging: the `Debug (FRONT|REAR)` launch configurations (Cortex-Debug + pyOCD) load the
   image into SRAM and stop at `main`. `Attach` connects to firmware that is already running.

From a terminal: `cmake --workflow --preset build-and-flash` builds and flashes in one step.

## Manual build

To try the software, change the jumpers to devmode, and run the following commands:

```bash
cmake --preset default
cmake --build --preset default
cmake --build --preset flash
```

This will load the firmware  into the SRAM. It will be lost with a power cycle!

To check the camera, plug in the board via the USB connector (in addition to the CN1) and run the following:

```bash
v4l2-ctl --list-devices
```

This should list the camera e.g. 

```text
STM32 uvc (usb-0000:00:14.0-9):
	/dev/video34
	/dev/video35
	/dev/media2
```

Then run 

```bash
ffplay /dev/video34
``` 

## Work in progress

Note that this is absolute work in progress. I just wanted to share a setup working with IMX219. 
