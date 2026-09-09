# Framebuffer-scale memory ops: why byte-at-a-time beerlang loses

Status: design note
Context: how far "pure beerlang drivers" scale, and what native support
a beerlang virtio-gpu / heavy `beer.gfx` path would need.

## The numbers

QEMU virt `ramfb` default is 1280×800 XRGB8888 = **4.1 MB per frame**.
Doom's internal buffer is 320×200×1 (palettized) = 64 KB, blitted +
scaled to the window each frame.

A beerlang loop doing one `mem/write32!` per pixel:

- Each `mem/write32!` call = VM dispatch + native call frame + argument
  boxing/unboxing (two `Value` structs, 16 bytes each) + bounds/tag
  checks + a single `sw` instruction.
- Ballpark **20–50 VM-level "instructions" of overhead per stored word**.
- Full-screen clear = 1.05 M words × ~30 = ~30 M VM ops **per clear**.
  At a few tens of MIPS effective on the interpreter that's
  ~1 second per clear. Unusable.
- Even Doom's 64 KB blit (16 K words) × 30 ≈ 500 K VM ops per frame just
  to copy, before any rendering work. That alone caps you well under
  30 fps.

`beer.gfx` today hides this because `fill-rect!` / `clear!` are **C
natives** (`kernel/gfx_natives.c` → `gfx_fill_rect` in `kernel/gfx.c`),
so the inner loop is compiled C. The moment a driver or effect wants a
memory operation that isn't already a native, it falls off a cliff.

## Where this bites

| Operation                                     | Size          | Byte-loop viable?            |
|-----------------------------------------------|---------------|------------------------------|
| virtio-blk: read a 512 B sector into a buffer | 128 words     | yes (barely) — but see below |
| virtio-blk: read a 4 KB FS block              | 1 K words     | marginal, ~30 K VM ops       |
| virtio-input: event ring drain                | tens of bytes | yes, trivially               |
| Load a 2 MB tar image off disk                | 512 K words   | **no** — needs bulk copy     |
| `beer.gfx` software blit / scale (Doom)       | 16–64 K words | **no**                       |
| Full framebuffer clear / fade / melt effect   | 1 M words     | **no**                       |
| virtio-gpu: transfer_to_host_2d staging copy  | 1 M+ words    | **no**                       |

The blk sector case is only "viable" if the device DMAs **directly into
the buffer's backing store** (via `bytes/addr`) so there is no copy at
all. If you have to stage through an intermediate region and then
`mem/read8` it out byte by byte, even 512 bytes × per-call overhead adds
up across thousands of sectors during a FAT scan.

## What native support closes it

Ranked by how much they matter:

### 1. Bulk copy / fill natives (required for anything past blk)

```
(bytes/copy!  dst dst-off src src-off len)   ; buffer ↔ buffer
(bytes/fill!  buf off len val)
(mem/copy!    dst-addr src-addr len)          ; raw phys ↔ phys
(mem/fill!    addr len val32)
(bytes/blit-from-addr! buf off addr len)      ; DMA staging → buffer
(bytes/blit-to-addr!   buf off addr len)
```

Each is a single `memcpy` / `memset` under the hood (QEMU virt is
cache-coherent, so no maintenance needed). Cost drops from ~30 VM
ops/word to ~0 — one native call amortized over the whole span.

This makes: tar-image load, FS block reads, and simple full-screen
fills/clears fast.

### 2. A framebuffer-aware blit native (required for Doom-class rendering)

Doom needs *scaled* / *masked* blits, not just straight copies:

```
(gfx/blit! src-buf sw sh  dst-x dst-y  scale)          ; nearest-neighbour
(gfx/blit-indexed! src-buf palette-buf sw sh dx dy)    ; 8-bit → XRGB via LUT
(gfx/blit-masked! src-buf sw sh dx dy transparent-key)
```

These are the `i_video.c` equivalents. The renderer produces a small
palettized buffer in beerlang (that part *is* the "idiomatic beerlang"
work), then hands it to one native call for presentation. This is
exactly the platform boundary `doomgeneric` draws — `DG_DrawFrame`.

### 3. Column / span writers (nice-to-have, renderer hot path)

Doom's rasterizer is column-major (`R_DrawColumn`, `R_DrawSpan`). If the
beerlang renderer wants to emit directly into the framebuffer rather
than a staging buffer:

```
(gfx/draw-column! x y-top count src-buf src-off step)
(gfx/draw-span!   y x1 x2 src-buf u v du dv)
```

This is the "keep it pure without wrecking performance" tension from the
roadmap — the honest answer is the innermost 2–3 rasterizer primitives
probably stay as C natives with a beerlang-shaped interface, and
everything above them (BSP walk, visplane assembly, sprite sorting,
clipping) is beerlang.

## Guidance

- **blk + input drivers: pure beerlang is fine**, provided (a) the
  device DMAs straight into a `bytes/` buffer and (b) `bytes/copy!` /
  `bytes/fill!` exist for the occasional staging move.
- **beer.gfx effects and virtio-gpu: need bulk `mem/copy!` + `mem/fill!`**
  at minimum, plus a present/blit native for anything animated.
- **Doom renderer: bulk ops + an indexed-blit present native + likely
  2–3 span/column natives.** The playsim (`p_*`) has no framebuffer-scale
  memory pressure and is pure-beerlang all the way down.

## Fixed: `mem/addr-of` off-by-4

`kernel/mem_natives.c` used to mirror the private `String` struct and
missed its `uint32_t hash` field, so `mem/addr-of` returned `data[] - 4`
(verified: offset 32 vs 36). Fixed by calling `string_cstr()` instead of
mirroring — beerlang computes the offset. Verified in QEMU:
`(beer.mem/read8 (beer.mem/addr-of "ABCD"))` → 65.

The structural lesson stands: don't re-mirror runtime-private structs.
The `TYPE_BYTEBUFFER` proposal (`docs/beerlang-bytebuffer.md`) removes
the hazard for good by giving DMA targets a documented layout and a
first-class `bytes/addr` native.
