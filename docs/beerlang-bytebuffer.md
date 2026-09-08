# ByteBuffer — a mutable binary buffer type for beerlang

Status: proposal / design note
Driver: beeros needs a real byte buffer to write drivers (virtio-blk,
virtio-input, virtio-gpu) in beerlang instead of C.

## Why strings don't cut it

Today the only byte container in beerlang is `TYPE_STRING`
(`beerlang/src/types/string.c`):

```c
typedef struct {
    struct Object header;
    uint32_t byte_len;
    uint32_t char_count;
    uint32_t hash;
    char     data[];        // NUL-terminated, UTF-8-validated
} String;
```

Problems for driver / DMA use:

1. **Immutable at the language level.** No `string-set!`. A driver that
   DMAs a sector into a buffer and then reads fields out of it is lying
   to the runtime — every "mutation" has to go through `mem/write8!` on a
   raw address, and every read through `mem/read8`.
2. **UTF-8 validation on construction.** `string_from_buffer` rejects
   non-UTF-8 input (`string_is_valid_utf8`). Binary data with high bytes
   fails to construct at all. You have to pre-size with `\0` and never
   let a real string constructor see the bytes.
3. **`char_count` / `hash` are meaningless** for binary payloads but
   still computed / carried.
4. **No length/position cursor.** virtio descriptor walking, FAT
   directory parsing, WAD lump reads all want a NIO-style
   position/limit cursor.
5. **`mem/addr-of` already has a latent bug because of the layout.**
   `kernel/mem_natives.c` mirrors the `String` struct but **omits the
   `hash` field**, so the address it returns is 4 bytes short of the
   real `data[]`. A dedicated type with a documented layout removes the
   "mirror a private struct" hazard entirely. (Fix `mem_natives.c`
   regardless — see `docs/framebuffer-scale.md`.)

## Design goals

- Small, self-contained new object type. **No VM opcode changes, no
  reader/printer syntax changes required for v1.** Everything through
  natives.
- Contiguous, stable backing store (fine — refcounted GC, no compaction).
- Explicit endianness accessors (LE and BE), because every wire format
  wants them and hand-rolling from bytes is where driver bugs live.
- Directly usable as a DMA target: one call returns the physical address
  of byte 0.
- NIO-ish ergonomics (position / limit / remaining) but also plain
  absolute indexed get/put so you can ignore the cursor when you don't
  want it.

## Object layout

New `ObjectType`:

```c
TYPE_BYTEBUFFER = 0x13,   /* next to TYPE_STRING = 0x12 */
```

```c
typedef struct {
    struct Object header;   /* header.size = capacity in bytes */
    uint32_t capacity;
    uint32_t position;
    uint32_t limit;
    uint8_t  data[];        /* NOT NUL-terminated, NOT validated */
} ByteBuffer;
```

`object_alloc(TYPE_BYTEBUFFER, sizeof(ByteBuffer) + capacity)` — same
one-shot flexible-array allocation as `String`, so the backing store is
part of the object and dies with it. `data[]` starts at a fixed,
documented offset; `object_alloc` gives 8-byte alignment (16 on most
malloc). If a driver needs stronger alignment for a virtqueue it uses
the reserved DMA region instead (see framebuffer-scale doc) — ByteBuffer
is for payloads, not rings.

Destructor: none needed (no owned sub-resources). Register a no-op or
skip.

## Native API (`beer.bytes` namespace)

Construction / info:

```
(bytes/alloc n)            → zeroed buffer, capacity n, position 0, limit n
(bytes/from-string s)      → buffer copy of s's bytes (bypasses UTF-8 path)
(bytes/capacity b)         → fixnum
(bytes/position b)         → fixnum
(bytes/position! b i)      → nil
(bytes/limit b)            → fixnum
(bytes/limit! b i)         → nil
(bytes/remaining b)        → limit - position
(bytes/rewind! b)          → position := 0
(bytes/clear! b)           → position := 0, limit := capacity
(bytes/flip! b)            → limit := position, position := 0
```

Absolute accessors (no cursor movement) — the workhorses:

```
(bytes/u8  b i)            → fixnum          (bytes/u8!  b i v)  → nil
(bytes/u16le b i) / u16be                    (bytes/u16le! …)
(bytes/u32le b i) / u32be                    (bytes/u32le! …)
(bytes/u64le b i) / u64be                    (bytes/u64le! …)
(bytes/i8/i16le/… )        signed variants
```

Relative accessors (advance position by the width):

```
(bytes/get-u8 b) / (bytes/put-u8! b v)
(bytes/get-u16le b) / (bytes/put-u16le! b v)   … etc
```

Bulk:

```
(bytes/fill! b start len val)          → nil
(bytes/copy! dst dst-off src src-off len) → nil   ; buffer↔buffer
(bytes/blit-from-addr! b off addr len) → nil      ; MMIO/phys → buffer
(bytes/blit-to-addr!   b off addr len) → nil      ; buffer → MMIO/phys
(bytes/slice b start len)              → new buffer sharing? NO — copy
(bytes/->string b)                     → string (validates UTF-8, may fail)
(bytes/hex b)                          → debug hex dump string
```

DMA:

```
(bytes/addr b)             → fixnum, physical address of data[0]
```

Replaces `mem/addr-of` on strings. Keep `mem/addr-of` working on
ByteBuffer too (dispatch on type) so `lib/mem.beer` helpers accept both.

## What this is NOT (v1 scope cuts)

- No reader literal (`#bytes[...]`) — add later if it's annoying.
- No `slice` that aliases the parent's store — always a copy. Aliasing
  needs an offset+parent-ref and complicates the destructor story.
- No growable buffers — capacity is fixed at alloc. FAT/WAD code sizes
  up front; drivers use fixed rings.
- No atomics / volatile semantics on buffer access. If a driver needs a
  volatile view of a ring it lives in the DMA region and uses
  `mem/read*` / `mem/write*`, which are already volatile.
- No endianness *mode* on the buffer (NIO's `order()`). Just name the
  width+endianness in the accessor. Fewer footguns.

## VM / runtime impact checklist

- `value.h`: one enum entry.
- new `beerlang/src/types/bytebuffer.c` + header, wired into the build
  and the `beerlang.h` umbrella include.
- `value_print` / `value_print_readable`: one `case` → `#<bytebuffer N>`.
- `value_equal`: bytewise compare (optional — could leave as identity).
- `object.c` destructor registration: none / no-op.
- native registration: new `beer.bytes` namespace, same pattern as
  `beer.tar` in `runtime/core.c`.
- GC: nothing special — flexible-array store, refcounted like `String`.

No compiler changes. No opcode changes. No scheduler interaction.

## Open questions

- `bytes/->string` on invalid UTF-8: return nil (like `string_from_buffer`)
  or a lossy replacement-char string? Prefer nil + a separate
  `bytes/->string-lossy`.
- Should `beer.tar` / `require` learn to read from a ByteBuffer directly
  so a tar image pulled off disk needs no string round-trip? Yes,
  eventually — `load_from_buffer` already takes a `char*`, so it's a
  thin overload.
- Bounds checking: hard `vm_error` on out-of-range index (safe, costs a
  compare per access) vs. debug-only assert. Start with hard errors;
  profile later.
