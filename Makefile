# beeros Makefile
# Cross-compile Beerlang for bare-metal targets
#
# Usage:
#   make BOARD=virt-riscv          build + run in QEMU (RISC-V)
#   make BOARD=virt-aarch64        build + run in QEMU (AArch64)
#   make BOARD=<real-board>        cross-compile for hardware
#   make run BOARD=<name>          launch in QEMU (virt targets only)
#   make clean
#
# Each platform/$(BOARD)/board.mk sets CROSS, ARCH_CFLAGS, QEMU, QEMU_FLAGS.

BOARD ?= $(error Please set BOARD=<name> — e.g. make BOARD=virt-riscv)

# Load board-specific toolchain and QEMU settings (sets CROSS, ARCH_CFLAGS, QEMU, QEMU_FLAGS)
-include platform/$(BOARD)/board.mk

# Defaults — overridden by board.mk
CROSS       ?= riscv64-unknown-elf
ARCH_CFLAGS ?= -march=rv64gc -mabi=lp64d
QEMU        ?= qemu-system-riscv64
QEMU_FLAGS  ?= -M virt -m 256M -nographic -serial mon:stdio

# Allow toolchain prefix override for non-Homebrew installs
CC_PREFIX ?=

CC      = $(CC_PREFIX)$(CROSS)-gcc
AS      = $(CC_PREFIX)$(CROSS)-gcc
LD      = $(CC_PREFIX)$(CROSS)-gcc
OBJCOPY = $(CC_PREFIX)$(CROSS)-objcopy
OBJDUMP = $(CC_PREFIX)$(CROSS)-objdump

BEER_ROOT = beerlang

# Auto-initialise the beerlang submodule if the working tree is empty.
# The sentinel file is the first header that the rest of the build needs.
$(BEER_ROOT)/include/beerlang.h:
	git submodule update --init $(BEER_ROOT)

# Beerlang source files — exclude the three files beeros replaces
BEER_EXCLUDE = \
	$(BEER_ROOT)/src/io/io_reactor.c \
	$(BEER_ROOT)/src/io/reactor.c \
	$(BEER_ROOT)/src/repl/main.c

BEER_SRCS = $(filter-out $(BEER_EXCLUDE), \
	$(wildcard $(BEER_ROOT)/src/vm/*.c) \
	$(wildcard $(BEER_ROOT)/src/types/*.c) \
	$(wildcard $(BEER_ROOT)/src/memory/*.c) \
	$(wildcard $(BEER_ROOT)/src/reader/*.c) \
	$(wildcard $(BEER_ROOT)/src/compiler/*.c) \
	$(wildcard $(BEER_ROOT)/src/runtime/*.c) \
	$(wildcard $(BEER_ROOT)/src/scheduler/*.c) \
	$(wildcard $(BEER_ROOT)/src/task/*.c) \
	$(wildcard $(BEER_ROOT)/src/channel/*.c) \
	$(wildcard $(BEER_ROOT)/src/io/*.c) \
	$(wildcard $(BEER_ROOT)/src/lib/*.c) \
	$(BEER_ROOT)/vendor/mini-gmp.c \
	$(BEER_ROOT)/vendor/ulog.c)

PLATFORM_DIR = platform/$(BOARD)

PLATFORM_SRCS = \
	$(PLATFORM_DIR)/boot/start.S \
	$(PLATFORM_DIR)/hal/uart.c \
	$(PLATFORM_DIR)/hal/timer.c

KERNEL_SRCS = \
	kernel/io_reactor_baremetal.c \
	kernel/reactor_baremetal.c \
	kernel/repl_uart.c \
	kernel/posix_stubs.c \
	kernel/clock_shim.c

ALL_SRCS = $(BEER_SRCS) $(PLATFORM_SRCS) $(KERNEL_SRCS)

BOARD_UPPER = $(shell echo $(BOARD) | tr 'a-z-' 'A-Z_')

CFLAGS = \
	$(ARCH_CFLAGS) \
	-O2 -ffreestanding -nostdlib -fno-strict-aliasing \
	-Wall -Wextra \
	-I$(BEER_ROOT)/include \
	-I$(BEER_ROOT)/vendor \
	-I$(PLATFORM_DIR)/hal \
	-Ikernel \
	-D_DEFAULT_SOURCE \
	-DBEEROS \
	-D_BEEROS_$(BOARD_UPPER)

LDFLAGS = \
	-T $(PLATFORM_DIR)/boot/$(BOARD).ld \
	-nostdlib \
	-lgcc

BUILD_DIR = build/$(BOARD)

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(filter %.c,$(ALL_SRCS))) \
       $(patsubst %.S,$(BUILD_DIR)/%.o,$(filter %.S,$(ALL_SRCS)))

.PHONY: all clean disasm run

all: beeros.bin

run: beeros.elf
	$(QEMU) $(QEMU_FLAGS) -kernel beeros.elf

beeros.elf: $(BEER_ROOT)/include/beerlang.h $(OBJS)
	$(LD) $(CFLAGS) -o $@ $(filter-out %.h,$^) $(LDFLAGS)

beeros.bin: beeros.elf
	$(OBJCOPY) -O binary $< $@
	@ls -lh beeros.bin

disasm: beeros.elf
	$(OBJDUMP) -d $< | less

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build/ beeros.elf beeros.bin
