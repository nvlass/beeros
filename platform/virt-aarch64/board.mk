CROSS       = aarch64-none-elf
ARCH_CFLAGS = -mcpu=cortex-a53
QEMU        = qemu-system-aarch64
QEMU_FLAGS  = -M virt -cpu cortex-a53 -m 256M -nographic -serial mon:stdio
