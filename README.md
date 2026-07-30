# beeros

**Bare-metal Beerlang** — boot directly into a Beerlang REPL on embedded hardware.

No Linux. No init system. Just the Beerlang VM talking to the hardware.

## What this is

beeros is a thin platform layer that replaces beerlang's POSIX-specific components
(scheduler threading, epoll/kqueue reactor, REPL stdin/stdout) with bare-metal equivalents.
The VM, compiler, GC, channels, actor system, and stdlib are unchanged — they compile
directly for the target with no OS underneath.

## One-time toolchain setup

beeros requires a cross-compiler and [picolibc](https://github.com/picolibc/picolibc)
(a minimal C library for embedded targets). Run the setup script once per machine — it
installs everything needed:

```bash
scripts/setup-toolchain.sh
```

What it installs (macOS/Homebrew):
- `riscv64-elf-gcc` — RISC-V cross compiler
- `qemu` — system emulator for testing without hardware
- `meson`, `ninja` — build system for picolibc
- **picolibc** (built from source, ~10 min) → `/opt/picolibc-rv64`

If you install picolibc to a different path, pass it to make:

```bash
make BOARD=virt-riscv PICOLIBC_PREFIX=/your/path
```

> **Linux**: `apt install gcc-riscv64-unknown-elf qemu-system-misc`, then run
> `scripts/setup-toolchain.sh` for picolibc (skips the brew steps automatically).

## Quickstart (QEMU — no hardware needed)

After running the setup script:

```bash
make BOARD=virt-riscv          # build
make run  BOARD=virt-riscv     # launch in QEMU  (Ctrl+A X to quit)

make BOARD=virt-aarch64        # AArch64 variant (needs aarch64-none-elf-gcc)
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

## Architecture

```
beerlang/          ← git submodule (the full Beerlang VM, unchanged)
platform/<board>/
  boot/start.S     ← entry: zero BSS, set sp, call beeros_main()
  boot/<board>.ld  ← linker script (RAM origin, heap/stack layout)
  hal/uart.c       ← UART driver (putc/getc/puts)
  hal/timer.c      ← microsecond timer (clock_gettime shim)
kernel/
  syscall_stubs.c  ← picolibc syscall hooks (_write→uart, _sbrk→heap, etc.)
  io_reactor_baremetal.c  ← replaces beerlang's epoll/kqueue reactor
  reactor_baremetal.c
  repl_uart.c      ← REPL entry point over UART
```

The kernel layer provides exactly what picolibc's syscall interface expects (~10
functions). Everything else — `printf`, `malloc`, `memcpy`, `qsort` — comes from
picolibc unchanged.

## Board support

| Board | Architecture | UART | Timer |
|---|---|---|---|
| `virt-riscv` | RV64GC | NS16550A @ 0x10000000 | `rdtime` CSR (10 MHz) |
| `virt-aarch64` | AArch64 | PL011 @ 0x09000000 | `CNTPCT_EL0` |

Physical boards are added under `platform/<name>/` when hardware is sourced.
Adding a new board = new `board.mk` + `start.S` + `<board>.ld` + `hal/` — no other
changes needed.

## License

[MIT](LICENSE)
