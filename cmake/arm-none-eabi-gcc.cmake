# Toolchain file for the Arm GNU Toolchain (arm-none-eabi-gcc), targeting the
# Cortex-M55 core of the STM32N657X0H3Q on the NUCLEO-N657X0-Q board.
#
# Usage: cmake -B build -G Ninja
# (this file is applied automatically by the top-level CMakeLists.txt)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Skip compiler checks that try to link and run a test executable: a bare-metal
# cross toolchain can compile/link but the result cannot run on this host.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX arm-none-eabi-)

find_program(ARM_NONE_EABI_GCC ${TOOLCHAIN_PREFIX}gcc)
if(NOT ARM_NONE_EABI_GCC)
  message(FATAL_ERROR "${TOOLCHAIN_PREFIX}gcc not found. Install the Arm GNU Toolchain and ensure it is on PATH.")
endif()

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)

set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy CACHE FILEPATH "")
set(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump CACHE FILEPATH "")
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size CACHE FILEPATH "")

# cortex-m55 core flags, shared between compile and link steps.
set(MCU_FLAGS "-mcpu=cortex-m55 -mcmse -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
