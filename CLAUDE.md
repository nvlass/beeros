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
├── beerlang/          ← git submodule (VM, compiler, stdlib — never modified here)
├── platform/
│   └── <board>/
│       ├── boot/      ← start.S (+ mtvec/MIE setup), linker script
│       └── hal/       ← uart.c, timer.c, plic.c/h, trap.S, gfx_ramfb.c
├── kernel/            ← board-agnostic bare-metal layer
│   ├── mem_natives.h / .c    ← beer.mem namespace (always loaded at boot)
│   ├── gfx.h / gfx.c         ← framebuffer HAL
│   ├── gfx_natives.h / .c    ← beer.gfx namespace + lib/gfx.beer loader
│   └── repl_uart.c            ← REPL entry; boots mem then optional gfx
├── lib/
│   ├── mem.beer       ← MMIO helpers + named MMIO constants
│   └── gfx.beer       ← beerlang drawing library
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

Optional for graphics:
```c
void gfx_init_ramfb(void);   // configure hardware framebuffer; calls gfx_init()
```

Each board also provides `board.mk` exporting `CROSS`, `ARCH_CFLAGS`, `QEMU`, `QEMU_FLAGS`.

## C library: picolibc

beeros uses [picolibc](https://github.com/picolibc/picolibc) as its C library.
picolibc is a maintained bare-metal libc (successor to newlib) that provides
`printf`, `malloc`, `memcpy`, etc. We only supply ~10 syscall stubs in
`kernel/syscall_stubs.c` (`_write` → UART, `_sbrk` → static heap, `_exit` → halt, etc.).

**One-time setup** (run on every new machine, takes ~10 min for picolibc build):
```bash
scripts/setup-toolchain.sh
```

picolibc is installed to `/opt/picolibc-rv64` by default. Override with:
```bash
make BOARD=virt-riscv PICOLIBC_PREFIX=/your/path
```

## Build & run

```bash
# QEMU targets (no hardware needed)
make BOARD=virt-riscv              # build for RISC-V virt machine
make run  BOARD=virt-riscv         # build + launch in QEMU (Ctrl+A X to quit)

make BOARD=virt-aarch64            # build for AArch64 virt machine
make run  BOARD=virt-aarch64       # build + launch in QEMU

# Graphics build (virt-riscv only — opens Cocoa window + REPL on stdio)
make BOARD=virt-riscv BEEROS_GFX=1          # build with beer.gfx
make run-gfx BOARD=virt-riscv               # macOS (Cocoa)
make run-gfx BOARD=virt-riscv DISPLAY_BACKEND=gtk   # Linux (GTK)

# Real hardware (once sourced)
make BOARD=<name>                  # cross-compile for a physical board
```

Toolchain requirements (all installed by `scripts/setup-toolchain.sh`):
- `riscv64-elf-gcc` — RISC-V cross compiler
- `qemu` — system emulator
- `picolibc` at `/opt/picolibc-rv64` — bare-metal C library
- For AArch64: `aarch64-none-elf-gcc` from developer.arm.com (not in Homebrew)

## MMIO driver layer (`beer.mem`)

Always loaded at boot (no build flag). Lets drivers be written entirely in beerlang.

**Natives (16):**
```
read8/16/32/64    (addr) → fixnum          volatile MMIO reads
write8!/16!/32!/64! (addr val) → nil       volatile MMIO writes
fence / fence-i   () → nil                 rw barrier / I-cache flush (arch-guarded)
addr-of           (str-val) → fixnum       physical address of string's data[] — DMA buffers
irq-register!     (n fn) → nil             store beerlang fn as IRQ n handler
irq-handler       (n) → fn|nil             retrieve stored handler
irq-enable!       (n prio) → nil           program PLIC priority + hart enable bit
irq-pending?      (n) → bool               non-blocking pending check
irq-wait!         (n) → nil                cooperative yield until IRQ n fires
```

**`lib/mem.beer` helpers (embedded, compiled at boot):**
```
rmw32!            (addr mask val)           read-modify-write register field
poll-set32        (addr bit)                spin until bit set
poll-clear32      (addr bit)                spin until bit clear
irq-listen!       (n)                       irq-wait! + call handler once
irq-loop!         (n)                       irq-listen! forever (for spawned tasks)
```
Also defines named MMIO constants: `uart0-*`, `fw-cfg-*`, `plic-base`, `ramfb-base`.

**Interrupt model (cooperative):**
- `trap.S` trap vector: saves all 31 GPRs → claims PLIC → sets `irq_pending` bit → completes PLIC → `mret`
- `irq-wait!` native: polls volatile bitmask with `scheduler_run_one_tick` yield; clears bit when set
- Handler calls happen in normal task context — scheduler is never entered from interrupt context
- `start.S` sets `mtvec` and enables `mstatus.MIE` + `mie.MEIE` before `call beeros_main`

**PLIC on QEMU virt-riscv:** base `0x0C000000`, hart 0 M-mode context (context 0).
`plic_enable(irq, priority)` → sets priority register, enable bit, threshold=0, `csrs mie`.

**`addr-of` layout:** assumes beerlang `String` layout: `{struct Object hdr; uint32_t byte_len; uint32_t char_count; char data[];}`. Returns `&data[0]`. Non-string values return 0.

## Graphics driver (`beer.gfx`)

Two-layer architecture, no beerlang submodule changes:

**Layer 1 — C HAL** (`kernel/gfx.h`, `kernel/gfx.c`, `platform/<board>/hal/gfx_ramfb.c`):
- `gfx_init(width, height, fb_addr)` — called by platform once the framebuffer address is known
- `gfx_set_pixel(x, y, color)` / `gfx_fill_rect(x, y, w, h, color)` — XRGB8888 format
- virt-riscv uses QEMU's `ramfb` device: one fw-cfg DMA handshake at boot maps the buffer to `0x84000000`

**Layer 2 — `beer.gfx` namespace** (`kernel/gfx_natives.c`, `lib/gfx.beer`):
- Natives registered at boot: `set-pixel!`, `fill-rect!`, `width`, `height`, `rgb`, `clear!`
- `lib/gfx.beer` embedded at build time via `xxd -i` → `kernel/gfx_beer_blob.h`, then compiled and run using the same reader→compiler→scheduler pattern as the REPL
- Higher-level functions in `lib/gfx.beer`: `line!` (Bresenham), `circle!` (midpoint), `rect!` (outlined), `text!` (8×8 bitmap font), `color`, `clear!`

The blob generation rule: `make kernel/gfx_beer_blob.h` (runs automatically as part of `run-gfx`).

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
