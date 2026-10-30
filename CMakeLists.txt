cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# ---------------------------
# Version info: generated at build time via version.h
# ---------------------------

find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE FW_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE FW_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_SHA_RESULT
    )
    if(NOT GIT_DESCRIBE_RESULT EQUAL 0)
        set(FW_GIT_DESCRIBE "unknown")
    endif()
    if(NOT GIT_SHA_RESULT EQUAL 0)
        set(FW_GIT_SHA "unknown")
    endif()
else()
    set(FW_GIT_DESCRIBE "unknown")
    set(FW_GIT_SHA "unknown")
endif()

string(TIMESTAMP FW_BUILD_TIME_UTC "%Y-%m-%d %H:%M:%S UTC" UTC)

# version.h is configured below, once FDA_MODE sets the build variant.

# Set the project name
set(CMAKE_PROJECT_NAME lifu-console-fw)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Select linker script based on the BARE_METAL option:
#   BARE_METAL=OFF (default) : image runs under the secure bootloader from the
#                              active slot (linked at 0x08010400). This is the
#                              normal Debug/Release build — the image that gets
#                              signed and bundled into the production firmware.
#   BARE_METAL=ON            : standalone image at 0x08000000 (no bootloader).
# An explicit -DFLASH_LINKER_SCRIPT=... still overrides this selection.
option(BARE_METAL "Build a bare-metal image (no custom bootloader; runs at 0x08000000)" OFF)
# FDA_MODE (below) also sizes the app and the preset region in the linker script.
option(FDA_MODE "The preset's HV entry, linked in at page 61" OFF)
if(NOT DEFINED FLASH_LINKER_SCRIPT)
    if(BARE_METAL)
        set(FLASH_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/STM32F072XX_FLASH.ld")
    else()
        set(FLASH_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/STM32F072XX_FLASH_BL.ld")
    endif()
endif()
if(FDA_MODE)
    set(FDA_LAYOUT 1)
else()
    set(FDA_LAYOUT 0)
endif()
message(STATUS "Linker script: ${FLASH_LINKER_SCRIPT} (FDA layout: ${FDA_LAYOUT})")
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -T "${FLASH_LINKER_SCRIPT}"
    "-Wl,--defsym=FDA_MODE=${FDA_LAYOUT}"
)

# Debug console output (USART3). The board brings out no serial header, so
# printf and the DMA log ring in logging.c are dead weight and are compiled
# out by default. See Core/Inc/dbg_print.h.
# Must be set before add_subdirectory() so the CubeMX sources see it too.
option(ENABLE_DEBUG_PRINT "Emit USART3 debug output (printf + DMA log ring)" OFF)
if(ENABLE_DEBUG_PRINT)
    add_compile_definitions(ENABLE_DEBUG_PRINT)
endif()
message(STATUS "Debug print : ${ENABLE_DEBUG_PRINT}")

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    USB_DEVICE/App/usb_events.c
    Core/Src/ads8678.c
    Core/Src/rgb_led.c
    Core/Src/rgb_effects.c
    Core/Src/fan_driver.c
    Core/Src/flash_eeprom.c
    Core/Src/hv_calibration_coeffs.c
    Core/Src/hv_supply.c
    Core/Src/hv_presets.c
    Core/Src/i2c_master.c
    Core/Src/if_commands.c
    Core/Src/jsmn.c
    Core/Src/lifu_config.c
    Core/Src/logging.c
    Core/Src/lwrb.c
    Core/Src/max31875.c
    Core/Src/rgb.c
    Core/Src/uart_comms.c
    Core/Src/utils.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_BINARY_DIR}/generated
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# ---------------------------
# FDA mode
# ---------------------------
# FDA_MODE links the presets' HV entries into page 61, the end of the
# bootloader's slot. The console applies the selected preset's voltage
# when the host selects it, and refuses OW_POWER_SET_HV and the raw DAC
# commands (Core/Inc/hv_presets.h).
#
# A presets-only (FDA_MODE) image reports which preset set it carries:
# "fda" (the released preset) or "test" (the test presets). It must match a
# preset context in openlifu-diathermy-application.
set(FW_PRESET_VARIANT "fda" CACHE STRING "Preset set in a presets-only image: fda or test")
set_property(CACHE FW_PRESET_VARIANT PROPERTY STRINGS fda test)
if(NOT FW_PRESET_VARIANT MATCHES "^(fda|test)$")
    message(FATAL_ERROR "FW_PRESET_VARIANT must be fda or test, not '${FW_PRESET_VARIANT}'.")
endif()

# One preset is linked from fda_preset.h; several from preset_hv_table.h
# (the SDK's generate_console_presets), whose records carry their own voltage
# ranges. Only a "test" image may link the table: an "fda" image keeps the
# one preset and the 35-45 V window (Core/Inc/hv_presets.h).
if(FDA_MODE)
    set(FDA_PRESET_HEADER "${CMAKE_SOURCE_DIR}/Core/Inc/presets/fda_preset.h")
    set(PRESET_HV_TABLE_HEADER "${CMAKE_SOURCE_DIR}/Core/Inc/presets/preset_hv_table.h")
    if(EXISTS "${FDA_PRESET_HEADER}" AND EXISTS "${PRESET_HV_TABLE_HEADER}")
        message(FATAL_ERROR "Core/Inc/presets holds both fda_preset.h and preset_hv_table.h; "
                            "keep only the one for this preset set.")
    elseif(EXISTS "${PRESET_HV_TABLE_HEADER}")
        if(NOT FW_PRESET_VARIANT STREQUAL "test")
            message(FATAL_ERROR "preset_hv_table.h is for FW_PRESET_VARIANT=test, not '${FW_PRESET_VARIANT}'.")
        endif()
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PRESET_HV_TABLE)
        message(STATUS "FDA mode    : ON (preset table at page 61)")
    elseif(EXISTS "${FDA_PRESET_HEADER}")
        message(STATUS "FDA mode    : ON (preset at page 61)")
    else()
        message(FATAL_ERROR "FDA_MODE needs ${FDA_PRESET_HEADER} or ${PRESET_HV_TABLE_HEADER}: "
                            "generate it with the openlifu SDK.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE FDA_MODE)
    set(FW_BUILD_VARIANT "${FW_PRESET_VARIANT}")
else()
    set(FW_BUILD_VARIANT "")
    message(STATUS "FDA mode    : OFF (host sets the HV voltage)")
endif()

# OW_CMD_VERSION returns FW_VERSION. A presets-only image appends
# "+<FW_PRESET_VARIANT>" (semver build metadata), so the host can tell which
# preset set it carries without writing to the device. Any other build has no
# suffix: all commands are enabled and the host treats it as a development or
# service image. The suffixes match the transmitter firmware's.
if(FW_BUILD_VARIANT)
    set(FW_VERSION_STRING "${FW_GIT_DESCRIBE}+${FW_BUILD_VARIANT}")
else()
    set(FW_VERSION_STRING "${FW_GIT_DESCRIBE}")
endif()

message(STATUS "FW version   : ${FW_VERSION_STRING}")
message(STATUS "FW git sha   : ${FW_GIT_SHA}")
message(STATUS "FW build time: ${FW_BUILD_TIME_UTC}")

configure_file(
    ${CMAKE_SOURCE_DIR}/version.h.in
    ${CMAKE_BINARY_DIR}/generated/version.h
    @ONLY
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# Generate hex and bin files after build
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating .hex and .bin files"
)
