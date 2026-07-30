# beeros

**Bare-metal Beerlang** — boot directly into a Beerlang REPL on embedded RISC-V hardware.

No Linux. No init system. Just the Beerlang VM talking to the hardware.

## What this is

beeros is a thin platform layer that replaces beerlang's POSIX-specific components
(scheduler threading, epoll/kqueue reactor, REPL stdin/stdout) with bare-metal equivalents.
The VM, compiler, GC, channels, actor system, and stdlib are unchanged — they compile
directly for the target with no OS underneath.

## Status

QEMU virt targets working (`virt-riscv`, `virt-aarch64`). Physical hardware TBD.

## Quickstart (QEMU — no hardware needed)

```bash
# RISC-V (requires riscv64-unknown-elf-gcc)
brew install riscv-gnu-toolchain        # macOS
make BOARD=virt-riscv
make run  BOARD=virt-riscv              # Ctrl+A X to quit QEMU

# AArch64 (requires aarch64-none-elf-gcc from developer.arm.com)
make BOARD=virt-aarch64
make run  BOARD=virt-aarch64
```

Expected output:

```
[beeros] starting...
[beeros] ready
beeros 0.1 / beerlang 0.x.x
Type (exit) to quit

beer:1>
```

## Board support

Board-specific code lives in `platform/<board>/`. Each board provides:
- `board.mk` — toolchain (`CROSS`, `ARCH_CFLAGS`) and QEMU settings
- `boot/start.S` — entry point, stack setup, secondary core parking
- `boot/<board>.ld` — linker script
- `hal/uart.c` — UART driver (HAL contract: init/putc/puts/getc/getc_nonblock)
- `hal/timer.c` — microsecond timer

Current boards:

| Board | Architecture | Notes |
|---|---|---|
| `virt-riscv` | RV64GC | QEMU virt machine, NS16550A UART |
| `virt-aarch64` | AArch64 | QEMU virt machine, PL011 UART |

Physical boards added under `platform/<name>/` when hardware is sourced.

## License

[MIT](LICENSE)
