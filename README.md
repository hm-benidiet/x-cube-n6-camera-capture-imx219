# x-cube-n6-camera-capture Application

Note: This is a fork which is adapted to work with a IMX219 instead of IMX355. 


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
