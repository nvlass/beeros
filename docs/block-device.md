# Block device interface

Status: interface and C virtio-blk driver implemented (`lib/blk.beer`,
`kernel/virtio_blk.c`, `kernel/blk_natives.c`). The beerlang virtio-blk
driver — intended to become the default, so drivers stay hackable from the
REPL — is next.

The contract between storage drivers (virtio-blk, SD/eMMC, RAM disk) and
everything that reads sectors (tar loader, FAT/littlefs, partition table
parser). A filesystem only ever sees this interface, never a driver, so
drivers can be swapped or used side by side without touching it.

## The device map

A block device is a plain beerlang map (beerlang has no protocols yet):

```clojure
{:name        "virtio0"    ; for logs and the registry
 :sector-size 512          ; bytes; every transfer is whole sectors
 :sectors     2097152      ; device size in sectors
 :read-only?  false
 :read!       (fn [lba n buf off] ...)   ; device -> buf
 :write!      (fn [lba n buf off] ...)   ; buf -> device
 :flush!      (fn [] ...)}               ; optional; nil if writes are synchronous
```

### `:read!` / `:write!`

- `lba` — first sector, 0-based.
- `n` — sector count, `>= 1`.
- `buf` — a ByteBuffer (`beer.bytes`). Never a string.
- `off` — byte offset into `buf` where the data starts or lands.
- Transfers exactly `(* n sector-size)` bytes. Absolute: ignores and does not
  move the buffer's position/limit.
- Returns `nil` on success. Blocks the calling task until the transfer is done
  (polling or `irq-wait!` inside the driver is the driver's business).

Callers must ensure `(+ lba n) <= sectors` and that `buf` has room; drivers
check anyway and throw rather than trusting the caller.

### Errors

Drivers throw `ex-info` (core.beer's error map) with `:data`:

```clojure
{:device "virtio0" :op :read :lba 1024 :n 8
 :reason :io-error}   ; or :out-of-range, :read-only, :timeout, :not-ready
```

No partial transfers: an op either completes fully or throws.

### Alignment and DMA

The interface promises nothing about `buf`'s alignment. A driver that needs
alignment (e.g. a descriptor table, or a controller that wants word-aligned
payloads) copies through its own aligned bounce buffer. virtio-blk payload
buffers need no alignment, so a virtio driver can DMA straight into
`(+ (beer.bytes/addr buf) off)`. On QEMU virt DMA is cache-coherent; real
boards will need cache maintenance inside the driver, not in this interface.

## Implementations

All of these produce the same map. Which one is in use is a boot-time choice.

| Backend | Where | Notes |
|---|---|---|
| virtio-blk (C) | QEMU | **Implemented.** `kernel/virtio_blk.c`, natives in `beer.blk.native`, map built by `native-virtio-device` in `lib/blk.beer`. Reference/fallback. |
| virtio-blk (beerlang) | QEMU | **Next; intended default.** Same map, built entirely on `beer.mem` + `beer.bytes`. |
| SD/eMMC | real board | Per-SoC controller driver (D1 SMHC, JH7110 DW MSHC). |
| RAM disk | anywhere | Backed by one ByteBuffer, e.g. a disk image embedded at build time. Useful for tests and diskless boot. |

### Using it

```clojure
(beer.blk/devices)                        ;=> ("virtio0")
(def d (beer.blk/device "virtio0"))
(beer.blk/read-sectors d 0 1)             ;=> #<bytebuffer 512>
(beer.blk/write-sectors! d 200 1 buf 0)
(beer.blk/flush! d)
```

Run QEMU with a disk: `make run BOARD=virt-riscv DISK=disk.img`. The
transport must be modern virtio-mmio (`-global virtio-mmio.force-legacy=false`,
already in `board.mk`); QEMU's virt machine fills the 8 slots from the top,
so the probe scans all of them.

### C driver vs beerlang driver

Swapping between them is a one-line change in whatever builds the map; nothing
above the interface notices. A C driver registers natives such as
`(beer.blk/read! dev-id lba n buf off)`, and the wrapper is just:

```clojure
(defn virtio-device [id]
  {:name (str "virtio" id)
   :sector-size 512
   :sectors (beer.blk/sectors id)
   :read-only? (beer.blk/read-only? id)
   :read!  (fn [lba n buf off] (beer.blk/read!  id lba n buf off))
   :write! (fn [lba n buf off] (beer.blk/write! id lba n buf off))
   :flush! (fn [] (beer.blk/flush! id))})
```

Both stay in the tree. Once the beerlang driver matches the C one against
the same disk image, boot registers the beerlang one by default and the C
driver remains as a fallback (and as a reference when debugging the
beerlang one). The C natives return `nil` or an error keyword; turning that
into `ex-info` happens in `lib/blk.beer`, so error policy is editable too.

What the beerlang driver needs that the C one gets for free: the descriptor
table, available and used rings must be 16/2/4-byte aligned and never move.
C uses static arrays with `__attribute__((aligned))`; a ByteBuffer's data
start is not 16-byte aligned (see `beerlang-bytebuffer.md`). Options: a
`beer.bytes/alloc-aligned`, a reserved DMA region exposed by `beer.mem`, or
over-allocate a ByteBuffer and round its address up. The payload buffers
themselves need no alignment.

## Partitions

A partition is itself a block device: a map whose `:read!`/`:write!` add a base
LBA and check bounds, wrapping its parent. A small parser (MBR first; GPT
later) takes a device and returns one map per partition. QEMU test images can
skip the table entirely; real SD cards nearly always have one.

```clojure
(partitions dev)  ;=> [{:name "virtio0p1" :sectors ... :read! ...} ...]
```

## Registry

`beer.blk` keeps an atom of registered devices by name, so the REPL and boot
code can find them without wiring:

```clojure
(register-device! dev)   (device "virtio0p1")   (devices)
```

Drivers register at boot; the partition parser registers each partition it
finds.

## Concurrency

Each device has one owner. Drivers that wait with `irq-wait!` yield to the
scheduler mid-request, so two tasks calling `:read!` on the same device could
interleave virtqueue updates. v1 rule: drivers serialise requests internally
(a simple in-flight flag, or a per-device owner task fed through a channel).
The interface itself stays synchronous either way.

## Open questions

- Request queueing / multiple in-flight requests: not needed for v1 (one
  request at a time, blocking). Revisit for performance.
- `:flush!` semantics for write-back caches (virtio `VIRTIO_BLK_F_FLUSH`).
  v1: call it after metadata writes in the FS layer.
- Discard/TRIM: out of scope.
- Error recovery (retry, reset): drivers throw; retrying is the caller's call.
