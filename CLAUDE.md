# beeros

Bare-metal OS layer for running the Beerlang REPL directly on embedded hardware — no Linux.

## Relationship to beerlang

- Parent project: `/Users/nvlass/work/beerlang` (the language runtime, compiler, VM)
- beerlang is included here as a git submodule (`beerlang/`)
- beeros provides the platform layer that replaces beerlang's POSIX-specific components

## Architecture

beerlang's POSIX-specific files are replaced by beeros equivalents at link time:

| Replaced file                  | beeros replacement              |
|--------------------------------|---------------------------------|
| `beerlang/src/io/io_reactor.c` | `kernel/io_reactor_baremetal.c` |
| `beerlang/src/io/reactor.c`    | `kernel/reactor_baremetal.c`    |
| `beerlang/src/repl/main.c`     | `kernel/repl_uart.c`            |

Everything else (VM, compiler, GC, types, channels, actors, stdlib) compiles unchanged.

## Directory structure

```
beeros/
├── beerlang/          ← git submodule
├── platform/
│   └── <board>/
│       ├── boot/      ← start.S, linker script
│       └── hal/       ← uart.c, timer.c (board-specific)
├── kernel/            ← board-agnostic bare-metal layer
├── lib/               ← future: beeros.beer stdlib
└── Makefile
```

## Board HAL contract

Every board HAL must implement (see `platform/README.md` for the full header spec):

```c
void     uart_init(uint32_t baud);
void     uart_putc(char c);
void     uart_puts(const char *s);
char     uart_getc(void);            // blocking
int      uart_getc_nonblock(char*);  // returns 1 if char ready, 0 if not
uint64_t timer_read_us(void);        // microseconds since boot
```

Each board also provides `board.mk` exporting `CROSS`, `ARCH_CFLAGS`, `QEMU`, `QEMU_FLAGS`.

## Build & run

```bash
# QEMU targets (no hardware needed)
make BOARD=virt-riscv              # build for RISC-V virt machine
make run  BOARD=virt-riscv         # build + launch in QEMU (Ctrl+A X to quit)

make BOARD=virt-aarch64            # build for AArch64 virt machine
make run  BOARD=virt-aarch64       # build + launch in QEMU

# Real hardware (once sourced)
make BOARD=<name>                  # cross-compile for a physical board
```

Toolchain requirements:
- `virt-riscv`: `riscv64-unknown-elf-gcc` — `brew install riscv-gnu-toolchain`
- `virt-aarch64`: `aarch64-none-elf-gcc` — download from developer.arm.com or distro package

## QEMU virt targets

| Board | QEMU command | UART | Timer |
|---|---|---|---|
| `virt-riscv` | `qemu-system-riscv64 -M virt` | NS16550A @ `0x10000000` | `rdtime` CSR (10 MHz) |
| `virt-aarch64` | `qemu-system-aarch64 -M virt -cpu cortex-a53` | PL011 @ `0x09000000` | `CNTPCT_EL0` (62.5 MHz) |

Both load the ELF via `-kernel beeros.elf` and map UART0 to stdio (`-serial mon:stdio -nographic`).

## Target hardware (TBD)

Physical board not yet sourced. RISC-V candidates under consideration:
- MangoPi MQ-Pro (Allwinner D1, RV64GCV, 512MB DDR3)
- Milk-V Mars (JH7110, 4× SiFive U74, better availability)

The `virt-riscv` HAL (NS16550A UART, `rdtime`) transfers to real RISC-V boards with only
base-address and baud-divisor changes. Add `platform/<board>/` when hardware arrives.

See `docs/beerproc.md` in the beerlang repo for the longer-term BeerProc vision.
