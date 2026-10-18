# beeros

Bare-metal OS layer for running the Beerlang REPL directly on embedded hardware — no Linux.

## Relationship to beerlang

- Parent project: `/Users/nvlass/work/beerlang` (the language runtime, compiler, VM)
- beerlang is included here as a git submodule (`beerlang/`)
- beeros provides the platform layer that replaces beerlang's POSIX-specific components

## Architecture

beerlang is built from its own `beerlang.mk` (sources, flags, feature
switches; `beerlang/docs/EMBEDDING.md`) with `BEER_PLATFORM := none`: a
stub I/O reactor, no `beer.tcp`/`beer.udp`/`beer.shell`. beeros adds only
`kernel/repl_uart.c` (the REPL, instead of `src/repl/main.c`) and the libc
stubs in `kernel/syscall_stubs.c` (`usleep`, `poll`, `_write`, ...). Rule
from EMBEDDING.md: configure, don't patch — if a hook is missing, ask in
beerlang's `TODO.md`.

**Embedded `.beer` libraries.** `beerlang/lib/core.beer` and `lib/*.beer`
are embedded with `xxd`. Boot uses `namespace_init_bare()` (no
`lib/core.beer` search), then `beer_load_lib()` (`kernel/blob_loader.c`)
runs each through beerlang's `core_load_source_ns` (home namespaces,
`*ns*` restore, compiled code kept alive) and records the namespace in
`*loaded-libs*`, so `require` doesn't look for a file. There is no `defn-`
or Java interop (`Math/abs`) — keep `lib/*.beer` to plain beerlang.

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
│   ├── blob_loader.h / .c    ← shared read→compile→run loader for embedded .beer libs
│   ├── virtio_blk.h / .c     ← C virtio-blk driver (virtio-mmio, modern transport)
│   ├── blk_natives.h / .c    ← beer.blk.native namespace + lib/blk.beer loader
│   ├── beer_libs.h / .c      ← loads the pure-beerlang libs (BEER_LIBS in the Makefile)
│   ├── console.h / .c        ← UART receive ring, interrupt keys, beer.tty natives
│   └── repl_uart.c            ← REPL entry; boots core.beer, mem, blk, then optional gfx
├── lib/
│   ├── mem.beer       ← MMIO helpers + named MMIO constants
│   ├── blk.beer       ← beer.blk: block-device maps, registry, error policy
│   ├── virtio.beer    ← beer.virtio: virtio-mmio transport + split virtqueues
│   ├── virtio_blk.beer ← beer.virtio-blk: virtio-blk driver in beerlang (boot default)
│   ├── ext2.beer      ← beer.ext2: read-only ext2 over a block-device map
│   ├── fs.beer        ← beer.fs: mount table, path ops (also inside .tar files), *fs* hook; mounts /
│   ├── ustar.beer     ← beer.ustar: tar index/entries from a ByteBuffer
│   ├── pkg.beer       ← beer.pkg: /pkg tars on *load-path*, run/exec/resource
│   ├── input.beer     ← beer.input: event queue, held keys, key->char
│   ├── virtio_input.beer ← beer.virtio-input: keyboard/mouse/tablet driver
│   ├── tty.beer       ← beer.tty: ttys (keystroke source + ANSI sink); serial tty
│   ├── lineedit.beer  ← beer.lineedit: REPL line editor (history, completion)
│   └── gfx.beer       ← beerlang drawing library
├── rootfs/            ← root filesystem contents (etc/init.beer, etc/beeros.edn, lib/, bin/)
│   └── lib/beer/      ← term.beer (beer.term: VT100 emulator + REPL), font.beer (PSF fonts)
├── packages/<name>/   ← sample packages (beer.edn, src/, assets/) → /pkg/<name>.tar
├── scripts/qemu-drive.py ← scripted REPL + monitor/QMP input for QEMU tests
└── Makefile
```

Embedded libraries: `lib/foo.beer` becomes `kernel/foo_beer_blob.h` (array
`lib_foo_beer`) by a pattern rule. Pure-beerlang libs go in `BEER_LIBS`
(Makefile) and `kernel/beer_libs.c`, in load order.

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
addr-of           (str-val|bytebuffer) → fixnum  physical address of data — DMA buffers
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

**`addr-of`:** dispatches on type. For a `ByteBuffer` (from `beer.bytes/alloc` — the preferred DMA buffer: mutable, not UTF-8-validated, not NUL-padded), returns `bytebuffer_data(v)`. For a `String`, returns `string_cstr(v)` — i.e. `&data[0]` of its inline byte buffer, computed by beerlang's own accessor (do *not* re-mirror the private `String` struct; it has a cached-`hash` field before `data[]`). Other values return 0.

## Block devices (`beer.blk`)

Interface and design: `docs/block-device.md`. A block device is a map with
`:read!`/`:write!` `(lba n buf off)` over ByteBuffers, `:flush!`, and
geometry; filesystems only ever see the map. Always built (no flag).

- `kernel/virtio_blk.c` — C driver: probes all virtio-mmio slots (board
  constants in `platform/<board>/hal/virtio_mmio.h`), needs the modern
  transport (`-global virtio-mmio.force-legacy=false`, in `board.mk`), one
  request in flight, polls for completion.
- `beer.blk.native` — its natives; return `nil` or an error keyword.
- `lib/blk.beer` — wraps them into device maps (throwing `ex-info`),
  registry (`devices`, `device`, `register-device!`), `read-sectors`,
  `write-sectors!`, `flush!`, `use-native-driver!`.
- `lib/virtio_blk.beer` — the same driver in beerlang on `beer.virtio`
  (`lib/virtio.beer`, shared with virtio-input) + `beer.bytes`; the **boot default** (`boot-driver` at the end of the file).
  `(beer.virtio-blk/attach!)` / `(beer.blk/use-native-driver!)` switch at
  the REPL; one driver owns a device at a time.

Attach a disk: `make run BOARD=virt-riscv DISK=disk.img` (raw image), or
`make run-disk` to build `build/disk.img` from `rootfs/` (`make run-gfx`
attaches it by default; `DISK=` for none) (+ beerlang's
`string`/`json`/`test`/`digest`/`crypto`/`tar` libs in `/lib/beer`) and boot with it.

## Filesystem (`beer.ext2`)

ext2, chosen over FAT32/littlefs/etc. — comparison and staged plan in
`docs/filesystem.md`. `lib/ext2.beer` is read-only so far:
`(beer.ext2/mount dev)`, `ls`, `slurp`, `read-bytes`, `stat`/`lstat`,
`readlink`. `lib/fs.beer` (`beer.fs`) mounts the first ext2 device at `/`
at boot and offers `ls`/`slurp`/`stat`/`mount!` on absolute paths. It
installs beerlang's file hook `beer.core/*fs*` and sets `*load-path*` to
`["/lib/"]`, so beerlang's own `require`/`load-file`/`slurp` read from it
(see `docs/filesystem.md`). `/etc/init.beer` runs last at boot
(`beer_run_init()`, after gfx) and starts the terminal REPL. Refuses incompat features other than `filetype` (so ext3/4
images are rejected, not misread). Build images with e2fsprogs
(`brew install e2fsprogs`, keg-only in `/opt/homebrew/opt/e2fsprogs/sbin`):
`mke2fs -t ext2 -d rootdir/ disk.img 8M`. QEMU needs `-m 256M` (from
`board.mk`) when invoked by hand.

## Packages (`beer.pkg`)

Design and API: `docs/packages.md`. A package is a tar with `beer.edn`
at the top (`:name :main :args ...`) and namespaces below it. `beer.fs`
reads paths through tar files (`/pkg/hello.tar/hello/core.beer`), and
`beer.pkg/rescan!` (at boot) puts `/lib/` and every `/pkg/*.tar/` on
`beer.core/*load-path*`. `(beer.pkg/run "hello" & args)` requires
`:main` and calls `-main`; `(beer.pkg/exec "hello" & args)` loads
`/bin/hello.beer` with `beer.pkg/*args*` bound.

## Input (`beer.input`)

Interface and backends: `docs/input.md` (networking plans: `docs/network.md`).
Backends call `beer.input/emit!`; consumers use `poll-event` (non-blocking),
`wait-event` (yields), `down?`, `key->char`. `lib/virtio_input.beer` probes
virtio-input devices at boot, names them `kbd0`/`mouse0`/`tablet0`, and
spawns one polling task each (they run while the REPL waits on the UART).
`make run-gfx` attaches a virtio keyboard + mouse. For headless tests use
`scripts/qemu-drive.py` (monitor `sendkey`/`mouse_move`, QMP
`input-send-event`); `-monitor unix:` paths in the scratchpad are too long
for macOS, hence TCP.

## Console (`beer.tty`, `beer.lineedit`)

Design and keys: `docs/console.md`. The REPL reads every form through
`(beer.lineedit/read-form beer.lineedit/prompt)` (compiled once in
`repl_uart.c`); on an error it disables the editor
(`beer.lineedit/enabled?` false) and uses its C reader, `(beer.lineedit/enable!)`
re-enables. Editor forms skip the C bracket check (the editor knows `\(`
char literals, the C check doesn't). The serial tty decodes VT100/xterm
input and also accepts `beer.input` keyboards.

**Interrupts.** UART receive is interrupt-driven (`kernel/console.c`: ring
buffer, `uart_rx_isr` in the board HAL, PLIC IRQ 10). `Ctrl-\`, a serial
BREAK and Ctrl-Alt-Esc always call `vm_request_interrupt()`; `Ctrl-C` does
while a form runs (`beer.tty/ctrl-c-interrupts!` turns that off). beerlang
unwinds the REPL's form (finally yes, catch no); the REPL then calls
`beer.tty/recover!`. Test BREAK with QMP `chardev-send-break` (id
`serial0`). `mem_trap_handler` halts with mcause/mepc on an exception.

**Linker script.** `.tbss` is NOBITS and takes no address space, but `tp`
points at the one TLS copy, so `virt-riscv.ld` reserves it
(`.tbss_space`); without that `.bss` can start on top of it and `errno`
writes land in the first `.bss` variable.

**Terminal.** `beer.term` (`rootfs/lib/beer/term.beer`, on the disk) is a
VT100-subset emulator on the framebuffer; `/etc/init.beer` starts a REPL
on it (`:console {:term ...}` in `/etc/beeros.edn`) as a background task
next to the serial REPL, independent of it: it binds `*ns*`, `*out*`/`*err*`
(a `beer.tty/capture-stream`, drained by the renderer; `kernel/console.c`
buffers writes to its fd) and `beer.tty/*tty*`, takes the keyboard and its
interrupt keys (`beer.tty/keyboard-interrupt` → `(interrupt! repl-task)`,
survived by `(catch :interrupt e ...)`). docs/console.md "Two REPLs".

## Graphics driver (`beer.gfx`)

Two-layer architecture, no beerlang submodule changes:

**Layer 1 — C HAL** (`kernel/gfx.h`, `kernel/gfx.c`, `platform/<board>/hal/gfx_ramfb.c`):
- `gfx_init(width, height, fb_addr)` — called by platform once the framebuffer address is known
- `gfx_set_pixel(x, y, color)` / `gfx_fill_rect(x, y, w, h, color)` — XRGB8888 format
- virt-riscv uses QEMU's `ramfb` device: one fw-cfg DMA handshake at boot maps the buffer to `__fb_start` (`0x8FBC0000`, 4 MiB reserved below the stack by `virt-riscv.ld`, outside the heap). All fw-cfg registers are big-endian — including the DMA address register, so both halves are written through `be32()`.

**Layer 2 — `beer.gfx` namespace** (`kernel/gfx_natives.c`, `lib/gfx.beer`):
- Natives registered at boot: `set-pixel!`, `fill-rect!`, `width`, `height`, `rgb`, `clear!`,
  `blit-bits!` (1-bit bitmap from a ByteBuffer, scaled; font glyphs), `copy-rect!` (scrolling)
- `lib/gfx.beer` embedded at build time via `xxd -i` → `kernel/gfx_beer_blob.h`, then compiled and run using the same reader→compiler→scheduler pattern as the REPL
- Higher-level functions in `lib/gfx.beer`: `line!` (Bresenham), `circle!` (midpoint), `rect!` (outlined), `text!` (8×8 bitmap font), `color`, `clear!`

The blob generation rule: `make kernel/gfx_beer_blob.h` (runs automatically as part of `run-gfx`).

## QEMU virt targets

| Board | QEMU command | UART | Timer |
|---|---|---|---|
| `virt-riscv` | `qemu-system-riscv64 -M virt` | NS16550A @ `0x10000000` | `rdtime` CSR (10 MHz) |
| `virt-aarch64` | `qemu-system-aarch64 -M virt -cpu cortex-a53` | PL011 @ `0x09000000` | `CNTPCT_EL0` (62.5 MHz) |

Both load the ELF via `-kernel beeros.elf` and map UART0 to stdio (`-serial mon:stdio -nographic`).

## Roadmap

1. **Filesystem support** — block device driver (virtio-blk under QEMU) + a filesystem
   layer so beeros can load code/assets from storage instead of embedded blobs.
2. **"Does it run Doom?"** — after filesystem *and* a keyboard/mouse input driver
   (PS/2 or virtio-input under QEMU) land. Goal is a full engine rewrite in
   idiomatic beerlang (pure functions, Clojure-style data), not a thin C-shim
   port — using id Software's original `linuxdoom-1.10` release as the
   reference implementation and `ozkl/doomgeneric` as the map of where the
   platform boundary sits (5 callback functions: init, draw-frame, sleep,
   get-ticks, get-key).

   **Scope split** (based on file-size analysis of `linuxdoom-1.10`):
   - `p_*` (playsim) + `r_*` (renderer) ≈ 35% of the whole codebase by bytes,
     but ≈ 53% of the code that's actually algorithmic logic once you exclude
     generated data tables (`info.c`/`tables.c`, ~27% of the codebase — those
     translate near-mechanically into beerlang data literals), the platform
     layer (`i_*`, replaced outright by `beer.gfx`/`beer.mem`/input driver),
     and the zone allocator (`z_*`, dropped entirely — beerlang's refcounted
     GC replaces it). **Nikolaos is doing `p_*` and `r_*` personally.**
   - Rough function count in `p_*`+`r_*`: ~250-320 C functions (order of
     magnitude ~280). `p_enemy.c` (monster AI) alone is ~60, mostly
     near-mechanical per-monster-action transliteration. The renderer
     (`r_bsp.c`/`r_segs.c`/`r_things.c`) is fewer, denser, hot-loop-heavy
     functions — that's where idiomatic-pure-functional-without-wrecking-
     performance is the real design problem.
   - Still needed beyond p_*/r_* for a first playable milestone: `w_wad.c`
     (WAD loader), a `g_game.c`-equivalent state machine (level load, game
     state transitions, input→ticcmd), minimal HUD. Menu, automap,
     intermission/finale screens, sound are post-milestone polish.
   - Known hard part: Doom's playsim/renderer lean on mutable state that
     resists naive pure-functional translation — mobjs are a doubly-linked
     list mutated *during* traversal (spawn/remove mid-iteration), the BSP
     walk accumulates into global arrays as it recurses, collision detection
     collects into a shared mutable buffer mid-walk. Expect real per-
     subsystem redesign, not transliteration, in those spots.
   - Bonus: doubles as a real-world compiler/VM benchmark — real-time
     software rasterization with allocation churn stresses tail-call
     handling, GC/refcount overhead, and arithmetic throughput in ways no
     microbenchmark does.

## Target hardware (TBD)

Physical board not yet sourced. RISC-V candidates under consideration:
- MangoPi MQ-Pro (Allwinner D1, RV64GCV, 512MB DDR3)
- Milk-V Mars (JH7110, 4× SiFive U74, better availability)

The `virt-riscv` HAL (NS16550A UART, `rdtime`) transfers to real RISC-V boards with only
base-address and baud-divisor changes. Add `platform/<board>/` when hardware arrives.

See `docs/beerproc.md` in the beerlang repo for the longer-term BeerProc vision.
