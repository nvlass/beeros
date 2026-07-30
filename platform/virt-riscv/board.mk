CROSS       = riscv64-elf
ARCH_CFLAGS = -march=rv64gc -mabi=lp64d
QEMU        = qemu-system-riscv64
QEMU_FLAGS  = -M virt -m 256M -nographic -serial mon:stdio
