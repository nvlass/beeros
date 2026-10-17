# beeros Makefile
# Cross-compile Beerlang for bare-metal targets via picolibc.
#
# One-time setup (macOS):
#   scripts/setup-toolchain.sh
#
# Usage:
#   make BOARD=virt-riscv          build for RISC-V QEMU
#   make BOARD=virt-aarch64        build for AArch64 QEMU
#   make run  BOARD=<name>         launch in QEMU
#   make clean
#
# Override picolibc location if you installed elsewhere:
#   make BOARD=virt-riscv PICOLIBC_PREFIX=/my/picolibc

.DEFAULT_GOAL := all

BOARD ?= $(error Please set BOARD=<name> — e.g. make BOARD=virt-riscv)

# ── toolchain ──────────────────────────────────────────────────────────
-include platform/$(BOARD)/board.mk

CROSS       ?= riscv64-elf
ARCH_CFLAGS ?= -march=rv64gc -mabi=lp64d
QEMU        ?= qemu-system-riscv64
QEMU_FLAGS  ?= -M virt -m 256M -nographic -bios none -serial mon:stdio

CC_PREFIX ?=
CC      = $(CC_PREFIX)$(CROSS)-gcc
AS      = $(CC_PREFIX)$(CROSS)-gcc
LD      = $(CC_PREFIX)$(CROSS)-gcc
OBJCOPY = $(CC_PREFIX)$(CROSS)-objcopy
OBJDUMP = $(CC_PREFIX)$(CROSS)-objdump

# ── picolibc ───────────────────────────────────────────────────────────
# picolibc.specs is installed into GCC's own specs directory by setup-toolchain.sh,
# so --specs=picolibc.specs works without any path. We just check GCC can find it.
_check_picolibc:
	@$(CC) --print-file-name=picolibc.specs | grep -q picolibc.specs || { \
	  echo ""; \
	  echo "  picolibc not found. Run:  scripts/setup-toolchain.sh"; \
	  echo ""; \
	  exit 1; }

# ── beerlang submodule ─────────────────────────────────────────────────
BEER_ROOT = beerlang

$(BEER_ROOT)/include/beerlang.h:
	git submodule update --init $(BEER_ROOT)

BEER_EXCLUDE = \
	$(BEER_ROOT)/src/io/io_reactor.c \
	$(BEER_ROOT)/src/io/reactor.c \
	$(BEER_ROOT)/src/repl/main.c \
	$(BEER_ROOT)/src/runtime/tcp.c \
	$(BEER_ROOT)/src/runtime/udp.c \
	$(BEER_ROOT)/src/runtime/shell.c

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

# ── platform + kernel sources ──────────────────────────────────────────
PLATFORM_DIR  = platform/$(BOARD)

PLATFORM_SRCS = \
	$(PLATFORM_DIR)/boot/start.S \
	$(PLATFORM_DIR)/hal/uart.c \
	$(PLATFORM_DIR)/hal/timer.c \
	$(PLATFORM_DIR)/hal/plic.c \
	$(PLATFORM_DIR)/hal/trap.S

KERNEL_SRCS = \
	kernel/io_reactor_baremetal.c \
	kernel/reactor_baremetal.c \
	kernel/repl_uart.c \
	kernel/syscall_stubs.c \
	kernel/libgcc_shim.c \
	kernel/blob_loader.c \
	kernel/mem_natives.c \
	kernel/virtio_blk.c \
	kernel/blk_natives.c \
	kernel/beer_libs.c

ifdef BEEROS_GFX
KERNEL_SRCS   += kernel/gfx.c kernel/gfx_natives.c
PLATFORM_SRCS += $(PLATFORM_DIR)/hal/gfx_ramfb.c
endif

ALL_SRCS = $(BEER_SRCS) $(PLATFORM_SRCS) $(KERNEL_SRCS)

# ── compiler flags ─────────────────────────────────────────────────────
BOARD_UPPER = $(shell echo $(BOARD) | tr 'a-z-' 'A-Z_')

CFLAGS = \
	$(ARCH_CFLAGS) \
	-O2 -ffreestanding -nostartfiles \
	-ffunction-sections -fdata-sections \
	-Wall -Wextra \
	--specs=picolibc.specs \
	-I$(BEER_ROOT)/include \
	-I$(BEER_ROOT)/vendor \
	-I$(PLATFORM_DIR)/hal \
	-Ikernel \
	-D_GNU_SOURCE \
	-D_POSIX_MONOTONIC_CLOCK=200112L \
	-DBEEROS \
	-D_BEEROS_$(BOARD_UPPER) \
	-DLOG_DISABLED \
	$(if $(BEEROS_GFX),-DBEEROS_GFX)

LDFLAGS = \
	-T $(PLATFORM_DIR)/boot/$(BOARD).ld \
	-nostartfiles \
	-Wl,--gc-sections \
	-Wl,--no-warn-rwx-segments \
	-lgcc

# ── build rules ────────────────────────────────────────────────────────
# Separate objects per flag set: -DBEEROS_GFX changes repl_uart.o
BUILD_DIR = build/$(BOARD)$(if $(BEEROS_GFX),-gfx)

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(filter %.c,$(ALL_SRCS))) \
       $(patsubst %.S,$(BUILD_DIR)/%.o,$(filter %.S,$(ALL_SRCS)))

# ── blob headers for embedded .beer libs ──────────────────────────────────
# lib/foo.beer → kernel/foo_beer_blob.h (array lib_foo_beer)
kernel/%_beer_blob.h: lib/%.beer
	xxd -i $< > $@

# Pure-beerlang libraries loaded by kernel/beer_libs.c, in load order
BEER_LIBS = virtio virtio_blk ext2 fs input virtio_input tty lineedit
BEER_LIB_BLOBS = $(patsubst %,kernel/%_beer_blob.h,$(BEER_LIBS))

# Fixed array name: xxd -i would derive it from the submodule path
kernel/core_beer_blob.h: $(BEER_ROOT)/lib/core.beer
	{ echo 'unsigned char core_beer[] = {'; xxd -i < $<; echo '};'; \
	  echo "unsigned int core_beer_len = $$(wc -c < $< | tr -d ' ');"; } > $@

.PHONY: all clean disasm run run-gfx disk run-disk _check_picolibc

all: _check_picolibc beeros.bin

# ── root filesystem image ─────────────────────────────────────────────────
# rootfs/ (checked in) + pure-beerlang libs from beerlang/lib/beer → ext2.
MKE2FS     ?= $(firstword $(wildcard /opt/homebrew/opt/e2fsprogs/sbin/mke2fs /usr/local/opt/e2fsprogs/sbin/mke2fs) mke2fs)
DISK_SIZE  ?= 16M
DISK_IMG    = build/disk.img
ROOTFS_STAGE = build/rootfs
ROOTFS_BEERLANG_LIBS = string json test digest crypto tar
ROOTFS_DIRS = bin etc lib/beer share/fonts var

disk: $(DISK_IMG)

$(DISK_IMG): Makefile $(shell find rootfs -type f) $(patsubst %,$(BEER_ROOT)/lib/beer/%.beer,$(ROOTFS_BEERLANG_LIBS))
	rm -rf $(ROOTFS_STAGE) $@
	mkdir -p $(addprefix $(ROOTFS_STAGE)/,$(ROOTFS_DIRS))
	cp -R rootfs/. $(ROOTFS_STAGE)/
	cp $(patsubst %,$(BEER_ROOT)/lib/beer/%.beer,$(ROOTFS_BEERLANG_LIBS)) $(ROOTFS_STAGE)/lib/beer/
	$(MKE2FS) -q -t ext2 -L beeros -d $(ROOTFS_STAGE) $@ $(DISK_SIZE)

# make run-disk: build the rootfs image and boot with it attached
run-disk: disk
	$(MAKE) run BOARD=$(BOARD) DISK=$(DISK_IMG)

# DISK=path/to/image.img attaches a raw image as a virtio-blk device
DISK_FLAGS = $(if $(DISK),-drive file=$(DISK),if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0)
# Keyboard + mouse for the display window (virtio-input)
INPUT_FLAGS = -device virtio-keyboard-device -device virtio-mouse-device

run: _check_picolibc beeros.elf
	$(QEMU) $(QEMU_FLAGS) $(DISK_FLAGS) -kernel beeros.elf

# run-gfx: opens a display window + REPL on stdio
# Uses -display cocoa on macOS; change to -display gtk on Linux.
DISPLAY_BACKEND ?= cocoa
run-gfx: _check_picolibc kernel/gfx_beer_blob.h
	$(MAKE) BOARD=$(BOARD) BEEROS_GFX=1
	$(QEMU) -M virt -m 256M -bios none \
	  -device ramfb -display $(DISPLAY_BACKEND) \
	  -serial stdio -global virtio-mmio.force-legacy=false $(DISK_FLAGS) $(INPUT_FLAGS) \
	  -kernel beeros.elf

beeros.elf: $(BEER_ROOT)/include/beerlang.h $(OBJS)
	$(LD) $(CFLAGS) -o $@ $(filter-out %.h,$^) $(LDFLAGS)

beeros.bin: beeros.elf
	$(OBJCOPY) -O binary $< $@
	@ls -lh beeros.bin

disasm: beeros.elf
	$(OBJDUMP) -d $< | less

$(BUILD_DIR)/kernel/gfx_natives.o: kernel/gfx_beer_blob.h
$(BUILD_DIR)/kernel/mem_natives.o: kernel/mem_beer_blob.h
$(BUILD_DIR)/kernel/repl_uart.o: kernel/core_beer_blob.h
$(BUILD_DIR)/kernel/blk_natives.o: kernel/blk_beer_blob.h
$(BUILD_DIR)/kernel/beer_libs.o: $(BEER_LIB_BLOBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build/ beeros.elf beeros.bin kernel/*_beer_blob.h
