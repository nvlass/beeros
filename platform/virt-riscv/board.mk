CROSS       = riscv64-elf
ARCH_CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany
QEMU        = qemu-system-riscv64
QEMU_FLAGS  = -M virt -m 256M -nographic -bios none -serial mon:stdio
