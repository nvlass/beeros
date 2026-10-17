/* virtio_blk.c — virtio-blk over virtio-mmio (virtio 1.x, split virtqueue)
 *
 * Reference / fallback driver. The intended default is a beerlang driver
 * built on beer.mem + beer.bytes; both sit behind the same block-device
 * map (docs/block-device.md).
 *
 * Needs a modern (version 2) transport: QEMU must run with
 * -global virtio-mmio.force-legacy=false.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "virtio_blk.h"
#include "virtio_mmio.h"
#include "timer.h"
#include "uart.h"

/* ── MMIO register offsets (virtio 1.x §4.2.2) ─────────────────────────── */
#define R_MAGIC            0x000
#define R_VERSION          0x004
#define R_DEVICE_ID        0x008
#define R_DEV_FEATURES     0x010
#define R_DEV_FEATURES_SEL 0x014
#define R_DRV_FEATURES     0x020
#define R_DRV_FEATURES_SEL 0x024
#define R_QUEUE_SEL        0x030
#define R_QUEUE_NUM_MAX    0x034
#define R_QUEUE_NUM        0x038
#define R_QUEUE_READY      0x044
#define R_QUEUE_NOTIFY     0x050
#define R_INT_STATUS       0x060
#define R_INT_ACK          0x064
#define R_STATUS           0x070
#define R_QUEUE_DESC_LO    0x080
#define R_QUEUE_DESC_HI    0x084
#define R_QUEUE_AVAIL_LO   0x090
#define R_QUEUE_AVAIL_HI   0x094
#define R_QUEUE_USED_LO    0x0a0
#define R_QUEUE_USED_HI    0x0a4
#define R_CONFIG_GEN       0x0fc
#define R_CONFIG           0x100   /* virtio_blk_config: u64 capacity first */

#define VIRTIO_MAGIC       0x74726976u   /* "virt" */
#define VIRTIO_DEV_BLOCK   2

#define ST_ACKNOWLEDGE     1u
#define ST_DRIVER          2u
#define ST_DRIVER_OK       4u
#define ST_FEATURES_OK     8u
#define ST_FAILED          128u

#define F_BLK_RO           (1u << 5)
#define F_BLK_FLUSH        (1u << 9)
#define F_VERSION_1_HI     (1u << 0)     /* feature bit 32, in word 1 */

#define DESC_F_NEXT        1
#define DESC_F_WRITE       2             /* device writes this buffer */

#define REQ_IN             0             /* read */
#define REQ_OUT            1             /* write */
#define REQ_FLUSH          4

#define BLK_S_OK           0
#define BLK_S_UNSUPP       2

#define QSIZE              8
#define TIMEOUT_US         2000000ULL

/* ── virtqueue layout (virtio 1.x §2.7) ────────────────────────────────── */
struct vq_desc  { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct vq_avail { uint16_t flags; uint16_t idx; uint16_t ring[QSIZE]; uint16_t used_event; };
struct vq_elem  { uint32_t id; uint32_t len; };
struct vq_used  { uint16_t flags; uint16_t idx; struct vq_elem ring[QSIZE]; uint16_t avail_event; };
struct blk_hdr  { uint32_t type; uint32_t reserved; uint64_t sector; };

typedef struct {
    struct vq_desc  desc[QSIZE] __attribute__((aligned(16)));
    struct vq_avail avail       __attribute__((aligned(2)));
    struct vq_used  used        __attribute__((aligned(4)));
    struct blk_hdr  hdr;
    uint8_t         status;
    uintptr_t       base;
    uint64_t        sectors;
    bool            read_only;
    bool            can_flush;
    uint16_t        used_seen;
} VBlk;

static VBlk devs[VIRTIO_BLK_MAX_DEVS];
static int  n_devs;

static inline uint32_t rd(uintptr_t base, uint32_t off) {
    return *(volatile uint32_t*)(base + off);
}
static inline void wr(uintptr_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(base + off) = v;
}
static inline void wr_addr(uintptr_t base, uint32_t lo, uint32_t hi, const void* p) {
    uint64_t a = (uint64_t)(uintptr_t)p;
    wr(base, lo, (uint32_t)a);
    wr(base, hi, (uint32_t)(a >> 32));
}

static bool init_device(VBlk* d, uintptr_t base) {
    d->base = base;
    wr(base, R_STATUS, 0);                                   /* reset */
    uint32_t st = ST_ACKNOWLEDGE | ST_DRIVER;
    wr(base, R_STATUS, st);

    wr(base, R_DEV_FEATURES_SEL, 0);
    uint32_t f0 = rd(base, R_DEV_FEATURES);
    wr(base, R_DEV_FEATURES_SEL, 1);
    uint32_t f1 = rd(base, R_DEV_FEATURES);
    if (!(f1 & F_VERSION_1_HI)) goto fail;

    uint32_t want0 = f0 & (F_BLK_RO | F_BLK_FLUSH);
    wr(base, R_DRV_FEATURES_SEL, 0);
    wr(base, R_DRV_FEATURES, want0);
    wr(base, R_DRV_FEATURES_SEL, 1);
    wr(base, R_DRV_FEATURES, F_VERSION_1_HI);
    st |= ST_FEATURES_OK;
    wr(base, R_STATUS, st);
    if (!(rd(base, R_STATUS) & ST_FEATURES_OK)) goto fail;

    wr(base, R_QUEUE_SEL, 0);
    if (rd(base, R_QUEUE_READY) != 0) goto fail;
    uint32_t max = rd(base, R_QUEUE_NUM_MAX);
    if (max < QSIZE) goto fail;
    wr(base, R_QUEUE_NUM, QSIZE);
    wr_addr(base, R_QUEUE_DESC_LO,  R_QUEUE_DESC_HI,  d->desc);
    wr_addr(base, R_QUEUE_AVAIL_LO, R_QUEUE_AVAIL_HI, &d->avail);
    wr_addr(base, R_QUEUE_USED_LO,  R_QUEUE_USED_HI,  &d->used);
    wr(base, R_QUEUE_READY, 1);

    /* 64-bit config field: two 32-bit reads, retried if config changed */
    uint32_t gen, lo, hi;
    do {
        gen = rd(base, R_CONFIG_GEN);
        lo  = rd(base, R_CONFIG);
        hi  = rd(base, R_CONFIG + 4);
    } while (gen != rd(base, R_CONFIG_GEN));
    d->sectors   = ((uint64_t)hi << 32) | lo;
    d->read_only = (want0 & F_BLK_RO) != 0;
    d->can_flush = (want0 & F_BLK_FLUSH) != 0;

    st |= ST_DRIVER_OK;
    wr(base, R_STATUS, st);
    return true;

fail:
    wr(base, R_STATUS, st | ST_FAILED);
    return false;
}

int virtio_blk_probe(void) {
    n_devs = 0;
    for (int s = 0; s < VIRTIO_MMIO_SLOTS && n_devs < VIRTIO_BLK_MAX_DEVS; s++) {
        uintptr_t base = VIRTIO_MMIO_BASE + (uintptr_t)s * VIRTIO_MMIO_STRIDE;
        if (rd(base, R_MAGIC) != VIRTIO_MAGIC) continue;
        if (rd(base, R_DEVICE_ID) != VIRTIO_DEV_BLOCK) continue;
        if (rd(base, R_VERSION) != 2) {
            uart_puts("[virtio-blk] legacy transport; run QEMU with "
                      "-global virtio-mmio.force-legacy=false\r\n");
            continue;
        }
        if (init_device(&devs[n_devs], base)) n_devs++;
        else uart_puts("[virtio-blk] device init failed\r\n");
    }
    return n_devs;
}

int virtio_blk_count(void)           { return n_devs; }
static VBlk* dev(int id)             { return (id >= 0 && id < n_devs) ? &devs[id] : NULL; }
uint64_t virtio_blk_sectors(int id)  { VBlk* d = dev(id); return d ? d->sectors : 0; }
bool virtio_blk_read_only(int id)    { VBlk* d = dev(id); return d && d->read_only; }
bool virtio_blk_can_flush(int id)    { VBlk* d = dev(id); return d && d->can_flush; }

/* Publish descriptor chain 0, kick the device, spin until it completes */
static vblk_status submit(VBlk* d) {
    d->status = 0xff;
    d->avail.ring[d->avail.idx % QSIZE] = 0;
    __sync_synchronize();               /* descriptors before the index */
    d->avail.idx++;
    __sync_synchronize();               /* index before the notify */
    wr(d->base, R_QUEUE_NOTIFY, 0);

    uint64_t deadline = timer_read_us() + TIMEOUT_US;
    while (*(volatile uint16_t*)&d->used.idx == d->used_seen) {
        if (timer_read_us() > deadline) return VBLK_ERR_TIMEOUT;
    }
    __sync_synchronize();               /* used index before the data */
    d->used_seen++;
    wr(d->base, R_INT_ACK, rd(d->base, R_INT_STATUS));

    uint8_t s = *(volatile uint8_t*)&d->status;
    if (s == BLK_S_OK)     return VBLK_OK;
    if (s == BLK_S_UNSUPP) return VBLK_ERR_UNSUPPORTED;
    return VBLK_ERR_IO;
}

vblk_status virtio_blk_rw(int id, bool write, uint64_t lba, uint32_t n, void* buf) {
    VBlk* d = dev(id);
    if (!d) return VBLK_ERR_NO_DEVICE;
    if (n == 0 || lba > d->sectors || n > d->sectors - lba) return VBLK_ERR_RANGE;
    if (write && d->read_only) return VBLK_ERR_READ_ONLY;

    d->hdr = (struct blk_hdr){ write ? REQ_OUT : REQ_IN, 0, lba };
    d->desc[0] = (struct vq_desc){ (uintptr_t)&d->hdr, sizeof d->hdr, DESC_F_NEXT, 1 };
    d->desc[1] = (struct vq_desc){ (uintptr_t)buf, n * VIRTIO_BLK_SECTOR,
                                   (uint16_t)(DESC_F_NEXT | (write ? 0 : DESC_F_WRITE)), 2 };
    d->desc[2] = (struct vq_desc){ (uintptr_t)&d->status, 1, DESC_F_WRITE, 0 };
    return submit(d);
}

vblk_status virtio_blk_flush(int id) {
    VBlk* d = dev(id);
    if (!d) return VBLK_ERR_NO_DEVICE;
    if (!d->can_flush) return VBLK_ERR_UNSUPPORTED;
    d->hdr = (struct blk_hdr){ REQ_FLUSH, 0, 0 };
    d->desc[0] = (struct vq_desc){ (uintptr_t)&d->hdr, sizeof d->hdr, DESC_F_NEXT, 1 };
    d->desc[1] = (struct vq_desc){ (uintptr_t)&d->status, 1, DESC_F_WRITE, 0 };
    return submit(d);
}
