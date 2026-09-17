cmake_minimum_required(VERSION 3.20)

# Apply the cross toolchain by default; can still be overridden with
# -DCMAKE_TOOLCHAIN_FILE=... on the command line.
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi-gcc.cmake)
endif()

project(nucleo-n657x0-q-camera-capture C ASM)

# This CMake build only targets the NUCLEO-N657X0-Q board with the IMX335
# camera module. For STM32N6570-DK or other sensors, use the Makefile build
# (see Makefile / mks/).
set(ORIENTATION "FRONT" CACHE STRING "Camera orientation: FRONT (selfie-like) or REAR")
set_property(CACHE ORIENTATION PROPERTY STRINGS FRONT REAR)

set(FW_DIR ${CMAKE_SOURCE_DIR}/STM32Cube_FW_N6)
set(CMW_DIR ${CMAKE_SOURCE_DIR}/Lib/Camera_Middleware)
set(ISP_DIR ${CMW_DIR}/ISP_Library)
set(FREERTOS_DIR ${CMAKE_SOURCE_DIR}/Lib/FreeRTOS)
set(UVCL_DIR ${CMAKE_SOURCE_DIR}/Lib/uvcl)
set(USBX_DIR ${FW_DIR}/Middlewares/ST/usbx)

# ---------------------------------------------------------------------------
# Application sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
  Src/main.c
  Src/app.c
  Src/app_jpg.c
  Src/app_cvt.c
  Src/app_fuseprogramming.c
  Src/stm32n6xx_it.c
  Src/app_cam.c
  Src/freertos_bsp.c
)

# ---------------------------------------------------------------------------
# GCC-specific glue (console retargeting, syscalls, startup file)
# ---------------------------------------------------------------------------
set(GCC_SOURCES
  Gcc/Src/console.c
  Gcc/Src/freertos_libc.c
  Gcc/Src/syscalls.c
  ${FW_DIR}/Drivers/CMSIS/Device/ST/STM32N6xx/Source/Templates/gcc/startup_stm32n657xx.s
)

# ---------------------------------------------------------------------------
# STM32Cube_FW_N6: HAL drivers + Nucleo BSP + external flash BSP component
# ---------------------------------------------------------------------------
set(FW_SOURCES
  ${FW_DIR}/Drivers/CMSIS/Device/ST/STM32N6xx/Source/Templates/system_stm32n6xx_fsbl.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cortex.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dcmipp.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma2d.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_gpio.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ltdc.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ltdc_ex.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rif.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ramcfg.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cacheaxi.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr_ex.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc_ex.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_bsec.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pcd.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pcd_ex.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_usb.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_jpeg.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_exti.c
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_tim.c
  ${FW_DIR}/Drivers/BSP/STM32N6xx_Nucleo/stm32n6xx_nucleo.c
  ${FW_DIR}/Drivers/BSP/STM32N6xx_Nucleo/stm32n6xx_nucleo_bus.c
  ${FW_DIR}/Drivers/BSP/STM32N6xx_Nucleo/stm32n6xx_nucleo_xspi.c
  ${FW_DIR}/Drivers/BSP/Components/mx25um51245g/mx25um51245g.c
)

# ---------------------------------------------------------------------------
# Camera middleware + ISP library (IMX219 sensor only)
#
# The ISP_Library sources are still required even though the IMX219 hardware
# driver doesn't call into the ISP (see cmw_imx219.c): cmw_camera.c's
# restart-state API (ISP_EnableRestartState/ISP_DisableRestartState) calls
# into it unconditionally, regardless of which sensor is selected.
# ---------------------------------------------------------------------------
set(CMW_SOURCES
  ${CMW_DIR}/cmw_camera.c
  ${CMW_DIR}/cmw_utils.c
  ${CMW_DIR}/sensors/cmw_imx219.c
  ${CMW_DIR}/sensors/imx219/imx219.c
  ${CMW_DIR}/sensors/imx219/imx219_reg.c
  ${ISP_DIR}/isp/Src/isp_algo.c
  ${ISP_DIR}/isp/Src/isp_awb_algo.c
  ${ISP_DIR}/isp/Src/isp_ae_algo.c
  ${ISP_DIR}/isp/Src/isp_cmd_parser.c
  ${ISP_DIR}/isp/Src/isp_core.c
  ${ISP_DIR}/isp/Src/isp_services.c
  ${ISP_DIR}/isp/Src/isp_tool_com.c
)

# ---------------------------------------------------------------------------
# FreeRTOS (GCC Cortex-M55 non-secure port)
# ---------------------------------------------------------------------------
set(FREERTOS_SOURCES
  ${FREERTOS_DIR}/Source/croutine.c
  ${FREERTOS_DIR}/Source/event_groups.c
  ${FREERTOS_DIR}/Source/list.c
  ${FREERTOS_DIR}/Source/queue.c
  ${FREERTOS_DIR}/Source/tasks.c
  ${FREERTOS_DIR}/Source/timers.c
  ${FREERTOS_DIR}/Source/portable/GCC/ARM_CM55_NTZ/non_secure/port.c
  ${FREERTOS_DIR}/Source/portable/GCC/ARM_CM55_NTZ/non_secure/portasm.c
)

# ---------------------------------------------------------------------------
# UVC library over USBX (device stack), FreeRTOS-backed, DMA enabled
# ---------------------------------------------------------------------------
set(UVCL_SOURCES
  ${UVCL_DIR}/Src/uvcl.c
  ${UVCL_DIR}/Src/uvcl_desc.c
  ${UVCL_DIR}/Src/usbx/uvcl_usbx.c
)

# USBX itself is ST-vendored, third-party code; globbed the same way the
# original Makefile does it (see Lib/uvcl/mks/uvc_lib_usbx.mk), rather than
# hand-transcribing ~100 filenames.
file(GLOB USBX_SOURCES
  ${USBX_DIR}/common/core/src/ux_system_*.c
  ${USBX_DIR}/common/core/src/ux_utility_*.c
  ${USBX_DIR}/common/core/src/ux_device_*.c
  ${USBX_DIR}/common/usbx_stm32_device_controllers/ux_dcd_stm32_*.c
  ${USBX_DIR}/common/usbx_device_classes/src/ux_device_class_video_*.c
)
list(SORT USBX_SOURCES)

add_executable(Project
  ${APP_SOURCES}
  ${GCC_SOURCES}
  ${FW_SOURCES}
  ${CMW_SOURCES}
  ${FREERTOS_SOURCES}
  ${UVCL_SOURCES}
  ${USBX_SOURCES}
)

target_include_directories(Project PRIVATE
  Inc
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc
  ${FW_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc/Legacy
  ${FW_DIR}/Drivers/CMSIS/Device/ST/STM32N6xx/Include
  ${FW_DIR}/Drivers/CMSIS/Include
  ${FW_DIR}/Drivers/CMSIS/DSP/Include
  ${FW_DIR}/Drivers/BSP/Components/Common
  ${FW_DIR}/Drivers/BSP/STM32N6xx_Nucleo
  ${FW_DIR}/Drivers/BSP/Components/aps256xx
  ${CMW_DIR}
  ${CMW_DIR}/sensors
  ${CMW_DIR}/sensors/imx219
  ${ISP_DIR}/isp/Inc
  ${FREERTOS_DIR}/Source/include
  ${FREERTOS_DIR}/Source/portable/GCC/ARM_CM55_NTZ/non_secure
  ${UVCL_DIR}/Inc
  ${UVCL_DIR}/Src
  ${UVCL_DIR}/Inc/usbx
  ${UVCL_DIR}/Src/usbx
  ${USBX_DIR}/common/core/inc
  ${USBX_DIR}/ports/generic/inc
  ${USBX_DIR}/common/usbx_device_classes/inc
  ${USBX_DIR}/common/usbx_stm32_device_controllers
)

target_compile_definitions(Project PRIVATE
  STM32N657xx
  USE_FULL_ASSERT
  USE_FULL_LL_DRIVER
  VECT_TAB_SRAM
  USE_IMX219_SENSOR
  STM32N6570_NUCLEO_REV
  TX_MAX_PARALLEL_NETWORKS=1
  FEAT_FREERTOS
  UVC_LIB_USE_DMA
  UVC_LIB_USE_USBX
  UX_INCLUDE_USER_DEFINE_FILE
  USBL_PACKET_PER_MICRO_FRAME=3
  UX_STANDALONE
  UVCL_USBX_USE_FREERTOS
  $<$<STREQUAL:${ORIENTATION},FRONT>:CAMERA_SELFY=1>
  $<$<STREQUAL:${ORIENTATION},REAR>:CAMERA_SELFY=0>
)

set(COMMON_C_FLAGS -Wall -fstack-usage -fdata-sections -ffunction-sections)

target_compile_options(Project PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-Os;-g3;-std=gnu11;${COMMON_C_FLAGS}>
  $<$<COMPILE_LANGUAGE:ASM>:-Os;-g3;${COMMON_C_FLAGS}>
)

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/Gcc/STM32N657xx_nucleo.ld)

target_link_options(Project PRIVATE
  -T${LINKER_SCRIPT}
  -specs=nano.specs
  -Wl,-Map=$<TARGET_FILE_DIR:Project>/Project.map,--cref
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segments
  -Wl,--print-memory-usage
  -u _printf_float
)
set_target_properties(Project PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
target_link_libraries(Project PRIVATE c m nosys)

# ---------------------------------------------------------------------------
# Post-build: .bin/.hex, matching the Makefile build's artifacts
# ---------------------------------------------------------------------------
set(PROJECT_BIN $<TARGET_FILE_DIR:Project>/Project.bin)
set(PROJECT_HEX $<TARGET_FILE_DIR:Project>/Project.hex)

add_custom_command(TARGET Project POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:Project> ${PROJECT_BIN}
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:Project> ${PROJECT_HEX}
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:Project>
  COMMENT "Generating Project.bin / Project.hex"
)

# ---------------------------------------------------------------------------
# flash: load Project.bin into SRAM over the on-board ST-Link via pyOCD.
# See cmake/pyocd_load.py and Doc/CMake-Build.md for why this is a SRAM
# load-and-run rather than a persistent flash write (the STM32N657 has no
# internal flash, and this repo does not include an FSBL/external-loader
# project for the external QSPI flash).
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)

# Load address = ORIGIN(AXISRAM2_P2_S) in Gcc/STM32N657xx_nucleo.ld, where the
# vector table (.isr_vector) is placed.
set(FLASH_LOAD_ADDRESS 0x34180400)

if(Python3_Interpreter_FOUND)
  add_custom_target(flash
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/pyocd_load.py
            --bin ${PROJECT_BIN}
            --address ${FLASH_LOAD_ADDRESS}
    DEPENDS Project
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Loading Project.bin into SRAM over ST-Link (pyOCD)"
  )
endif()
