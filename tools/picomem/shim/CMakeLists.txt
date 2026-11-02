# libpmhost - the PicoMEM firmware's own device code, built for the host.
# docs/plans/completed/PICOMEM-EMU-PLAN.md section 4. Driven by ../build.sh, which
# passes FW (the patched firmware tree) and BOARD (v1 or v2).
#
# What is compiled is the firmware's: its command dispatcher and main loop
# (pm_cmd.cpp, picomem.cpp's main), its devices, its IRQ multiplexer and
# virtual DMA, its disk and FatFS code. What is replaced is the RP2040: the
# PIO bus loop (pmhost_isa.h), the SDK (include/), the SD card's SPI driver
# (pmhost_sd.cpp) and the I2S output (pmhost_audio.cpp).
#
# GPL-2.0-or-later, like the firmware.
cmake_minimum_required(VERSION 3.16)
project(pmhost C CXX)

if(NOT FW)
  message(FATAL_ERROR "FW= the patched firmware's src/ directory")
endif()
if(NOT BOARD)
  set(BOARD v1)
endif()

set(SHIM ${CMAKE_CURRENT_LIST_DIR})

# ---- the firmware's files ---------------------------------------------------
# Taken from the firmware's own CMakeLists (src/ and each subdirectory),
# minus the files that ARE the hardware: the SD SPI driver, glue.c and
# ff15/diskio.c (pm_disk.cpp is the disk layer and calls sd_disk_*, which
# pmhost_sd.cpp provides), the I2S DMA driver (audio_i2s.c), the OLED and
# front panel, the USB host, and dev_ltemm.cpp, which the firmware's own
# CMake leaves out too (dev_memory.cpp carries the EMS).
set(FW_SOURCES
  picomem.cpp pm_cmd.cpp dosbox/bios_disk.cpp
  pm_libs/pm_libs.cpp pm_libs/isa_dma.cpp pm_libs/isa_irq.cpp
  pm_libs/pico_rtc.cpp pm_libs/pm_disk.cpp pm_libs/pm_apps.cpp
  pm_libs/pm_pccmd.cpp
  isa_devices/dev_memory.cpp isa_devices/dev_picomem_io.cpp
  isa_devices/dev_post.cpp isa_devices/dev_irq.cpp
  isa_devices/dev_sbdsp.cpp isa_devices/dev_adlib.cpp
  isa_devices/dev_cms.cpp isa_devices/dev_tandy.cpp
  isa_devices/dev_mindscape.cpp isa_devices/dev_covox.cpp
  isa_devices/dev_joystick.cpp isa_devices/dev_mouse.cpp
  isa_devices/dev_dma.cpp isa_devices/dev_rtc.cpp
  audio_devices/pico_pic.c audio_devices/audio_fifo.c
  audio_devices/dev_audiomix.cpp audio_devices/square/square.cpp
  audio_devices/emu2149/emu2149.c audio_devices/sbdsp/sbdsp.cpp
  audio_devices/covox/covox_dac.cpp audio_devices/volctrl.cpp
  audio_devices/opl/emu8950.c audio_devices/opl/slot_render.cpp
  audio_devices/opl/dbopl/dbopl.cpp audio_devices/opl/opl_dbopl.cpp
  pm_boards/pmdev_pico.cpp
  lib/my_etherdfs/ethersrv-pico.c lib/my_etherdfs/fs.c
  lib/pm_audio/pm_audio_i2s/pm_audio.cpp
  qwiic/pm_i2c.c qwiic/i2c_ht16k33.c qwiic/i2c_pfc85263.cpp
  lib/FatFs_SPI/ff15/source/ff.c lib/FatFs_SPI/ff15/source/ffsystem.c
  lib/FatFs_SPI/ff15/source/ffunicode.c lib/FatFs_SPI/src/f_util.c
  ne2000/ne2000.c
)
list(TRANSFORM FW_SOURCES PREPEND ${FW}/)

set(FW_INCLUDES
  . audio_devices audio_devices/covox audio_devices/emu2149 audio_devices/opl
  audio_devices/sbdsp audio_devices/square dosbox isa_devices
  lib/FatFs_SPI/ff15/source lib/FatFs_SPI/include lib/FatFs_SPI/sd_driver
  lib/FatFs_SPI/sd_driver/SPI lib/my_etherdfs lib/pm_audio/pm_audio_i2s
  ne2000 pm_boards pm_libs pm_libs/inputs qwiic)
list(TRANSFORM FW_INCLUDES PREPEND ${FW}/)

# ---- the BIOS ROM -----------------------------------------------------------
# The firmware links src/rom/pm{,2}bios.bin as a binary object; this is the
# same bytes as a C array under the same symbol (_binary_pmbios_bin_start).
if(BOARD STREQUAL "v2")
  set(BIOS_NAME pm2bios)
else()
  set(BIOS_NAME pmbios)
endif()
set(BIOS_BIN ${FW}/rom/${BIOS_NAME}.bin)
set(BIOS_C ${CMAKE_CURRENT_BINARY_DIR}/${BIOS_NAME}_bin.c)
file(READ ${BIOS_BIN} BIOS_HEX HEX)
string(LENGTH "${BIOS_HEX}" BIOS_HEXLEN)
math(EXPR BIOS_LEN "${BIOS_HEXLEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BIOS_BYTES "${BIOS_HEX}")
file(WRITE ${BIOS_C}
  "/* generated from ${BIOS_NAME}.bin by tools/picomem/shim/CMakeLists.txt */\n"
  "#include <stdint.h>\n"
  "uint8_t _binary_${BIOS_NAME}_bin_start[${BIOS_LEN}] = {${BIOS_BYTES}};\n"
  "const uint8_t _binary_${BIOS_NAME}_bin_size[1];\n")

# ---- the shim ---------------------------------------------------------------
set(SHIM_SOURCES
  ${SHIM}/pmhost.cpp ${SHIM}/pmhost_sd.cpp ${SHIM}/pmhost_audio.cpp
  ${SHIM}/pmhost_wifi.cpp
  ${BIOS_C})

add_library(pmhost SHARED ${FW_SOURCES} ${SHIM_SOURCES})
# .so on every host, Darwin's included: os88build.have_pmhost, pmhost.py,
# os88marty, MartyPC's patch and the vm/ profiles all name libpmhost-<b>.so
set_target_properties(pmhost PROPERTIES OUTPUT_NAME pmhost-${BOARD}
  SUFFIX ".so")
target_include_directories(pmhost PRIVATE ${SHIM}/include ${FW_INCLUDES})
target_compile_options(pmhost PRIVATE
  -include ${SHIM}/include/pmhost_pico.h -fms-extensions -w
  -fno-strict-aliasing -fvisibility=hidden)
target_compile_options(pmhost PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
set_target_properties(pmhost PROPERTIES C_STANDARD 11 CXX_STANDARD 17)

# The firmware's own defines for each board: src/CMakeLists.txt's PICOMEM
# (1.x, what firmware/picomem1/v1board.patch selects) and PICOMEM_20 blocks,
# then its common block. What differs from the iron is marked HOST.
set(COMMON_DEFS
  PMHOST=1 BOARD_PICOMEM=1 ROM_BIOS=0x0D0000u USE_RTC=1 USE_SDCARD=1
  SD_CRC_ENABLED=0 USE_AUDIO=1 USE_EMU8950_OPL=1 EMU8950_NO_TLL=1
  EMU8950_NO_TEST_FLAG=1 EMU8950_NO_RATECONV USE_ALARM=1
  UART_PIN0=0 CFG_TUSB_DEBUG=0 SD_DEBUG=0 USE_PRINTF=0 USE_DBG_PRINTF=0
  DISP32=0 CORE1_ALIVETEST=0 TEST_PSRAM=0 TEST_SDCARD=0 TEST_DFS=0
  TEST_OTHER=0 DO_FAST_RAM=0 ASYNC_UART=0 USB_UART=0 USE_SBDSP=1
  USE_GUS=0 USE_MPU=0 USE_CDROM=0 USE_DEV_IRQ=0 DBG_PIN_AUDIO=0
  USE_SDIO=0
  EMU8950_ASM=0       # HOST: the RP2040 slot renderer is ARM assembly
  PM_ENABLE_STDIO=0   # HOST: logging goes through the host's log callback
  USE_USBHOST=0       # HOST: the mouse arrives through pm_mouse (later wave)
  PM_WIFI=1 USE_NE2000=1  # the Pico W's: its Wi-Fi is pmhost_wifi.cpp
  USE_AUDIO_I2S=1 PICO_AUDIO_I2S_PIO=1 PICO_AUDIO_I2S_SM=3
  PICO_AUDIO_I2S_DMA_IRQ=1)
if(BOARD STREQUAL "v2")
  # PICOMEM_20 on a Pimoroni Pico Plus 2 (RP2350): the 2.x's four IRQ lines
  # and its real DMA (pmhost_isa2.h), QSPI PSRAM mapped into the address
  # space (a host array at __psram_start__). GUS and MPU-401 off, the
  # owner's decision; USE_OLED off HOST - a display nobody can see.
  set(BOARD_DEFS BOARD_PM20=1 MUX_V2=1 PICO_RP2350=1
    PIMORONI_PICO_PLUS2_RP2350=1 PM_SYS_CLK=360 CYW43_PIO_CLOCK_DIV_INT=6
    MAX_PMRAM=16 USE_SPI_PSRAM=0 USE_SPI_PSRAM_DMA=0 PSRAM_HEAP=1
    I2S_CLK_INVERT=1 PICO_AUDIO_I2S_DATA_PIN=15
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=16 ONBOARD_RTC=1 USE_OLED=0
    USE_HWIRQ=1 USE_HWDMA=1)
else()
  set(BOARD_DEFS BOARD_PM=1 PICO_RP2040=1 RASPBERRYPI_PICO_W=1 PM_WIFI_LED=1
    PM_SYS_CLK=280 USE_HWIRQ=0 USE_HWDMA=0 USE_SPI_PSRAM=1
    USE_SPI_PSRAM_DMA=1 PSRAM_HEAP=0 CYW43_PIO_CLOCK_DIV_INT=4
    PICO_AUDIO_I2S_DATA_PIN=26 PICO_AUDIO_I2S_CLOCK_PIN_BASE=27
    PSRAM_PIN_CS=5 PSRAM_PIN_SCK=6 PSRAM_PIN_MOSI=7 PSRAM_PIN_MISO=4
    PSRAM_ASYNC=1)
endif()
# PMH_PRINTF=ON: the firmware's own PM_INFO/PM_ERROR trace, on stdout - what
# the iron prints on its debug UART. For a session chasing a hang; never a row.
option(PMH_PRINTF "the firmware's debug printf" OFF)
if(PMH_PRINTF)
  list(APPEND COMMON_DEFS PM_PRINTF=1)
else()
  list(APPEND COMMON_DEFS PM_PRINTF=0)
endif()
target_compile_definitions(pmhost PRIVATE ${COMMON_DEFS} ${BOARD_DEFS})
find_package(Threads REQUIRED)
target_link_libraries(pmhost PRIVATE Threads::Threads)
if(APPLE)
  target_link_options(pmhost PRIVATE -Wl,-undefined,error)   # Apple ld's
else()
  target_link_options(pmhost PRIVATE -Wl,--no-undefined)
endif()
