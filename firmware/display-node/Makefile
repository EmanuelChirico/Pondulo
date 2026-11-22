##############################################################################
# Build global options
# NOTE: Can be overridden externally.
#

# Compiler options here.
ifeq ($(USE_OPT),)
  USE_OPT = -O2 -ggdb -fomit-frame-pointer -falign-functions=16
endif

# C specific options here (added to USE_OPT).
ifeq ($(USE_COPT),)
  USE_COPT = 
endif

# C++ specific options here (added to USE_OPT).
ifeq ($(USE_CPPOPT),)
  USE_CPPOPT = -fno-rtti
endif

# Enable this if you want the linker to remove unused code and data.
ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

# Linker extra options here.
ifeq ($(USE_LDOPT),)
  USE_LDOPT = 
endif

# Enable this if you want link time optimizations (LTO).
ifeq ($(USE_LTO),)
  USE_LTO = yes
endif

# Enable this if you want to see the full log while compiling.
ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

# If enabled, this option makes the build process faster by not compiling
# modules not used in the current configuration.
ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

#
# Build global options
##############################################################################

##############################################################################
# Architecture or project specific options
#

# Stack size to be allocated to the Cortex-M process stack. This stack is
# the stack used by the main() thread.
# Was 0x400 (1024 bytes) - too small (2026-09-05): main()'s gesture-dispatch
# loop calls uiMacelleriaHandleGesture() -> fireSelection() -> the same
# enterCarousel()/enterBilancia()/enterReceipt() draw functions that forced
# UiTickThread's own stack up to 1536 bytes (see that thread's working area
# comment in this file - same gdisp + multi-value snprintf calls). With
# CH_DBG_ENABLE_STACK_CHECK off (cfg/chconf.h), an overflow here corrupts
# adjacent RAM silently instead of halting cleanly - this was always a risk
# but rarely hit while gestures only came from the broken B1 button or
# manual SD2 keystrokes. Real sensor-driven gestures plus the shorter
# GESTURE_DELAY_MS (1400ms -> 200ms) exercise this same path far more often
# now, which is almost certainly why the screen started freezing under
# active pointer/gesture use. Bumped again 2026-09-05 (0x800 -> 0x1000)
# to match UiTickThread's own bump to 4096 in main.c - RAM is nowhere near
# tight here (~117KB free of 128KB total), so there's no reason to be
# stingy while chasing this class of bug.
ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x1000
endif

# Stack size to the allocated to the Cortex-M main/exceptions stack. This
# stack is used for processing interrupts and exceptions.
ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x400
endif

# Enables the use of FPU (no, softfp, hard).
ifeq ($(USE_FPU),)
  USE_FPU = no
endif

# FPU-related options.
ifeq ($(USE_FPU_OPT),)
  USE_FPU_OPT = -mfloat-abi=$(USE_FPU) -mfpu=fpv4-sp-d16
endif

#
# Architecture or project specific options
##############################################################################

##############################################################################
# Project, target, sources and paths
#

# Define project name here
PROJECT = ch

# Target settings.
MCU  = cortex-m4

# Imported source files and paths.
CHIBIOS ?= ../../external/chibios2111
CONFDIR  := ./cfg
BUILDDIR := ./build
DEPDIR   := ./.dep

GFXLIB = ../../external/ugfx

# Licensing files.
include $(CHIBIOS)/os/license/license.mk
# Startup files.
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32g4xx.mk
# HAL-OSAL files (optional).
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/STM32/STM32G4xx/platform.mk
include $(CHIBIOS)/os/hal/boards/ST_NUCLEO64_G474RE/board.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk
# RTOS files (optional).
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMv7-M-ALT/compilers/GCC/mk/port.mk
#include $(CHIBIOS)/os/common/ports/ARMv7-M/compilers/GCC/mk/port.mk
# Auto-build files in ./source recursively.
include $(CHIBIOS)/tools/mk/autobuild.mk
# Other files (optional).
include $(GFXLIB)/gfx.mk
include $(GFXLIB)/drivers/gdisp/ILI9341/driver.mk                  # display driver

# Define linker script file here
LDSCRIPT= $(STARTUPLD)/STM32G474xE.ld

# C sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CSRC = $(ALLCSRC) \
       $(TESTSRC) \
       $(GFXSRC) \
       $(CHIBIOS)/os/various/syscalls.c \
       gesture_link/src/gesture_link.c \
       pointer_link/src/xy_link.c \
       weight_adc/src/weight_adc.c \
       ui_macelleria/src/cursor_glide.c \
       ui_macelleria/src/mp3_player.c \
       ui_macelleria/src/ui_macelleria.c \
       main.c

# C++ sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CPPSRC = $(ALLCPPSRC)

# List ASM source files here.
ASMSRC = $(ALLASMSRC)

# List ASM with preprocessor source files here.
ASMXSRC = $(ALLXASMSRC)

# Inclusion directories.
INCDIR = $(CONFDIR) $(ALLINC) $(TESTINC) $(GFXINC) gesture_link/include pointer_link/include weight_adc/include ui_macelleria/include

# Define C warning options here.
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes -Wcast-align=strict

# Define C++ warning options here.
CPPWARN = -Wall -Wextra -Wundef

#
# Project, target, sources and paths
##############################################################################

##############################################################################
# Start of user section
#

# List all user C define here, like -D_DEBUG=1
# Routes newlib's retargeted I/O (printf/scanf/etc, via os/various/syscalls.c)
# through the SD2 serial driver, i.e. USART2 -> the Nucleo's ST-LINK VCP.
UDEFS = -DSTDOUT_SD=SD2 -DSTDIN_SD=SD2

# Define ASM defines here
UADEFS =

# List all user directories here
UINCDIR =

# List the user directory to look for the libraries here
ULIBDIR =

# List all user libraries here
ULIBS =

#
# End of user section
##############################################################################

##############################################################################
# Common rules
#

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/arm-none-eabi.mk
include $(RULESPATH)/rules.mk

#
# Common rules
##############################################################################

##############################################################################
# Custom rules
#

#
# Custom rules
##############################################################################
