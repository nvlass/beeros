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
- **picolibc** (built from source, ~10 min) → installed into GCC's own specs directory so `--specs=picolibc.specs` works automatically with no extra flags

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

## Raw hardware access (`beer.mem`)

`beer.mem` is loaded at every boot — no build flag needed. It gives beerlang code
direct access to hardware so you can write a new device driver entirely in beer
without touching C.

```clojure
;; MMIO reads and writes (8/16/32/64-bit, volatile)
(beer.mem/read8  0x10000005)        ;=> 96  (UART LSR: TX empty)
(beer.mem/write8! 0x10000000 65)    ; writes 'A' directly to UART TX register

;; Memory ordering
(beer.mem/fence)                    ; RISC-V fence rw,rw  (dmb sy on AArch64)
(beer.mem/fence-i)                  ; RISC-V fence.i      (isb on AArch64)

;; DMA — get the physical address of a string's data region
(def buf "...16 bytes...")
(beer.mem/addr-of buf)              ;=> physical address, stable for buf's lifetime
;; Pass that address to a DMA descriptor register via write32!

;; Interrupts (cooperative — handler runs between REPL ticks)
(beer.mem/irq-register! 10 (fn [] (println "UART RX ready")))
(beer.mem/irq-enable! 10 1)                        ; priority 1 at the PLIC
(await (spawn (fn [] (beer.mem/irq-loop! 10))))    ; background listener task
```

Helper macros from `lib/mem.beer` (also always available):
```clojure
(beer.mem/rmw32! base-addr mask val)   ; read-modify-write a register field
(beer.mem/poll-set32   addr bit)       ; spin until bit is set
(beer.mem/poll-clear32 addr bit)       ; spin until bit is clear
(beer.mem/irq-listen! n)               ; wait for one IRQ, call handler, return
(beer.mem/irq-loop! n)                 ; wait + call handler, repeat forever
```

Named MMIO constants are also defined: `beer.mem/uart0-base`, `beer.mem/plic-base`,
`beer.mem/fw-cfg-base`, `beer.mem/ramfb-base`, etc.

**Writing a driver entirely in beer:**
```clojure
;; Minimal NS16550A UART init — no C required
(defn my-uart-init! [base baud]
  (beer.mem/write8! (+ base 3) 0x80)             ; LCR: DLAB=1
  (beer.mem/write8! base (quot 115200 baud))      ; DLL
  (beer.mem/write8! (+ base 1) 0)                 ; DLH
  (beer.mem/write8! (+ base 3) 0x03))             ; LCR: 8N1
```

**How interrupts work:** the M-mode trap handler (`platform/virt-riscv/hal/trap.S`)
saves all 31 GPRs, claims the PLIC IRQ, sets a bit in a `volatile uint64_t` bitmask,
completes the PLIC claim, and returns via `mret` — no beerlang call from interrupt
context. `irq-wait!` polls that bitmask cooperatively (yielding to the scheduler),
clears the bit when set, and returns to the beerlang caller. `irq-listen!` then
calls the stored handler in normal task context.

## Graphics (QEMU virt-riscv)

beeros includes a two-layer graphics stack built entirely in the beeros repo —
no changes to the beerlang submodule.

```bash
make run-gfx BOARD=virt-riscv   # opens a 1280×720 Cocoa window + REPL on stdio
                                 # Linux: make run-gfx BOARD=virt-riscv DISPLAY_BACKEND=gtk
```

Once the REPL appears, the `beer.gfx` namespace is ready:

```clojure
(require 'beer.gfx)

(gfx/clear! (gfx/rgb 0 0 32))                       ; dark blue background
(gfx/fill-rect! 100 100 300 200 (gfx/rgb 200 50 50)) ; filled red rectangle
(gfx/rect!   400 100 300 200 (gfx/rgb 255 200 0))    ; outlined yellow rectangle
(gfx/line!   0 0 1279 719 (gfx/rgb 255 255 255))     ; white diagonal
(gfx/circle! 640 360 200 (gfx/rgb 0 255 128))        ; green circle outline
(gfx/text!   50 50 "beeros!" (gfx/rgb 255 255 255))  ; 8×8 bitmap text
```

**How it works:**

Layer 1 — C HAL (`kernel/gfx.h`, `kernel/gfx.c`): a board-agnostic framebuffer
API (`gfx_set_pixel`, `gfx_fill_rect`, `gfx_width/height`). The platform
implementation (`platform/virt-riscv/hal/gfx_ramfb.c`) does a one-shot fw-cfg
DMA handshake to configure QEMU's `ramfb` device at 1280×720 XRGB8888.

Layer 2 — `beer.gfx` namespace (`kernel/gfx_natives.c`, `lib/gfx.beer`): six
native primitives are registered at boot, then `lib/gfx.beer` is compiled and
run in-place — Bresenham `line!`, midpoint `circle!`, outlined `rect!`, 8×8
bitmap `text!`, `color`, and `clear!` are pure beerlang on top of those
primitives. The `.beer` source is embedded in the binary at build time via `xxd -i`.

## Architecture

```
beerlang/          ← git submodule (the full Beerlang VM, unchanged)
platform/<board>/
  boot/start.S     ← entry: stack, FPU, TLS, mtvec, MIE, call beeros_main()
  boot/<board>.ld  ← linker script (RAM origin, heap/stack, TLS sections)
  hal/uart.c       ← UART driver (putc/getc/puts)
  hal/timer.c      ← microsecond timer (clock_gettime shim)
  hal/plic.c/h     ← (virt-riscv) RISC-V PLIC init (irq enable, claim, complete)
  hal/trap.S       ← (virt-riscv) M-mode trap handler (save/restore 31 GPRs, mret)
  hal/gfx_ramfb.c  ← (virt-riscv) QEMU ramfb init via fw-cfg DMA
kernel/
  syscall_stubs.c  ← picolibc syscall hooks (_write→uart, _sbrk→heap, etc.)
  io_reactor_baremetal.c  ← replaces beerlang's epoll/kqueue reactor
  reactor_baremetal.c
  repl_uart.c      ← REPL entry point; boots mem + optional gfx namespaces
  mem_natives.c    ← beer.mem namespace: MMIO, fence, addr-of, IRQ primitives
  gfx.h / gfx.c   ← board-agnostic framebuffer HAL
  gfx_natives.c    ← beer.gfx namespace registration + lib/gfx.beer loader
lib/
  mem.beer         ← MMIO helpers: rmw32!, poll-set/clear32, irq-listen!, constants
  gfx.beer         ← drawing library (line!, circle!, text!, …)
```

The kernel layer provides exactly what picolibc's syscall interface expects (~10
functions). Everything else — `printf`, `malloc`, `memcpy`, `qsort` — comes from
picolibc unchanged.

## Board support

| Board | Architecture | UART | Timer | PLIC | Graphics |
|---|---|---|---|---|---|
| `virt-riscv` | RV64GC | NS16550A @ 0x10000000 | `rdtime` CSR (10 MHz) | 0x0C000000 | ramfb via fw-cfg |
| `virt-aarch64` | AArch64 | PL011 @ 0x09000000 | `CNTPCT_EL0` | — | — |

Physical boards are added under `platform/<name>/` when hardware is sourced.
Adding a new board = new `board.mk` + `start.S` + `<board>.ld` + `hal/` — no other
changes needed.

## License

[MIT](LICENSE)
