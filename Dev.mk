# Dev.mk - personal helper Makefile for STM32 projects
# Usage examples:
#   make -f Dev.mk build
#   make -f Dev.mk flash
#   make -f Dev.mk debug
#   make -f Dev.mk serial PORT=/dev/ttyUSB0
#
# Keep this file outside CubeMX auto-generation. Do NOT replace the generated Makefile.

# -----------------------------------------------------------------------------
# Generated project Makefile
# -----------------------------------------------------------------------------
GEN_MAKEFILE ?= Makefile

# -----------------------------------------------------------------------------
# Project output settings
# If the generated Makefile defines TARGET and BUILD_DIR, these defaults match it.
# You can override them from command line:
#   make -f Dev.mk flash TARGET=LCB BUILD_DIR=build
# -----------------------------------------------------------------------------
TARGET    ?= $(notdir $(CURDIR))
BUILD_DIR ?= build
ELF       ?= $(BUILD_DIR)/$(TARGET).elf
BIN       ?= $(BUILD_DIR)/$(TARGET).bin
HEX       ?= $(BUILD_DIR)/$(TARGET).hex

# -----------------------------------------------------------------------------
# Programmer selection
# Supported PROGRAMMER values:
#   stlink     - OpenOCD ST-Link
#   cmsis-dap  - OpenOCD CMSIS-DAP / DAPLink
#   jlink      - J-Link Commander
#   dfu        - dfu-util, for chips/bootloaders that support USB DFU
# -----------------------------------------------------------------------------
PROGRAMMER ?= cmsis-dap

# OpenOCD config
OPENOCD          ?= openocd
OPENOCD_TARGET   ?= target/stm32g0x.cfg
ADAPTER_SPEED    ?= 500 

# OpenOCD interface is selected from PROGRAMMER unless explicitly overridden.
ifeq ($(PROGRAMMER),stlink)
OPENOCD_INTERFACE ?= interface/stlink.cfg
else ifeq ($(PROGRAMMER),cmsis-dap)
OPENOCD_INTERFACE ?= interface/cmsis-dap.cfg
else
OPENOCD_INTERFACE ?= interface/stlink.cfg
endif

# J-Link config
JLINK       ?= JLinkExe
JLINK_DEVICE ?= STM32G030F6
JLINK_IF     ?= SWD
JLINK_SPEED  ?= 4000

# Serial monitor
PORT ?= /dev/ttyUSB0
BAUD ?= 115200
SERIAL_TOOL ?= picocom

# Build command. We call the generated Makefile instead of including it, so this
# file will not conflict with auto-generated targets.
MAKE_GEN = $(MAKE) -f $(GEN_MAKEFILE)

.PHONY: help build all clean rebuild flash flash-openocd flash-jlink flash-dfu \
        erase reset halt debug connect gdb serial size find-elf show cfg

help:
	@echo "Personal STM32 helper Makefile"
	@echo ""
	@echo "Build:"
	@echo "  make -f Dev.mk build        Build with generated Makefile"
	@echo "  make -f Dev.mk clean        Clean generated build output"
	@echo "  make -f Dev.mk rebuild      Clean then build"
	@echo ""
	@echo "Flash/debug:"
	@echo "  make -f Dev.mk flash        Build and flash, PROGRAMMER=$(PROGRAMMER)"
	@echo "  make -f Dev.mk erase        Mass erase through OpenOCD"
	@echo "  make -f Dev.mk reset        Reset target through OpenOCD"
	@echo "  make -f Dev.mk debug        Start OpenOCD server"
	@echo "  make -f Dev.mk gdb          Build and start arm-none-eabi-gdb"
	@echo ""
	@echo "Programmer examples:"
	@echo "  make -f Dev.mk flash PROGRAMMER=stlink"
	@echo "  make -f Dev.mk flash PROGRAMMER=cmsis-dap ADAPTER_SPEED=500"
	@echo "  make -f Dev.mk flash PROGRAMMER=jlink"
	@echo "  make -f Dev.mk flash PROGRAMMER=dfu"
	@echo ""
	@echo "Serial:"
	@echo "  make -f Dev.mk serial PORT=/dev/ttyUSB0 BAUD=115200"
	@echo ""
	@echo "Config:"
	@echo "  make -f Dev.mk show"

all: build

build:
	$(MAKE_GEN) all

clean:
	$(MAKE_GEN) clean

rebuild: clean build

flash: build
ifeq ($(PROGRAMMER),jlink)
	$(MAKE) -f Dev.mk flash-jlink
else ifeq ($(PROGRAMMER),dfu)
	$(MAKE) -f Dev.mk flash-dfu
else
	$(MAKE) -f Dev.mk flash-openocd
endif

flash-openocd:
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "adapter speed $(ADAPTER_SPEED); init; reset halt; program $(ELF) verify reset exit"

# J-Link Commander script is generated into build directory.
flash-jlink:
	@mkdir -p $(BUILD_DIR)
	@printf "si $(JLINK_IF)\nspeed $(JLINK_SPEED)\ndevice $(JLINK_DEVICE)\nconnect\nhalt\nloadfile $(ELF)\nr\ng\nqc\n" > $(BUILD_DIR)/flash.jlink
	$(JLINK) -CommanderScript $(BUILD_DIR)/flash.jlink

flash-dfu:
	dfu-util -a 0 -D $(BIN) -s 0x08000000:leave

erase:
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "adapter speed $(ADAPTER_SPEED); init; reset halt; stm32g0x mass_erase 0; reset run; shutdown"

reset:
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "adapter speed $(ADAPTER_SPEED); init; reset run; shutdown"

halt:
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "adapter speed $(ADAPTER_SPEED); init; reset halt; shutdown"

connect: halt

# Keep this running in one terminal, then use another terminal for GDB.
debug:
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "adapter speed $(ADAPTER_SPEED)"

gdb: build
	arm-none-eabi-gdb $(ELF) \
		-ex "target extended-remote localhost:3333" \
		-ex "monitor reset halt" \
		-ex "load" \
		-ex "monitor reset halt"

serial:
	$(SERIAL_TOOL) -b $(BAUD) $(PORT)

size: build
	arm-none-eabi-size $(ELF)

find-elf:
	@find . -type f -name "*.elf" -o -name "*.bin" -o -name "*.hex"

show:
	@echo "GEN_MAKEFILE       = $(GEN_MAKEFILE)"
	@echo "TARGET             = $(TARGET)"
	@echo "BUILD_DIR          = $(BUILD_DIR)"
	@echo "ELF                = $(ELF)"
	@echo "BIN                = $(BIN)"
	@echo "PROGRAMMER         = $(PROGRAMMER)"
	@echo "OPENOCD            = $(OPENOCD)"
	@echo "OPENOCD_INTERFACE  = $(OPENOCD_INTERFACE)"
	@echo "OPENOCD_TARGET     = $(OPENOCD_TARGET)"
	@echo "ADAPTER_SPEED      = $(ADAPTER_SPEED)"
	@echo "JLINK_DEVICE       = $(JLINK_DEVICE)"
	@echo "PORT               = $(PORT)"
	@echo "BAUD               = $(BAUD)"

cfg: show
