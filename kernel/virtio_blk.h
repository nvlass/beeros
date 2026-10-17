#ifndef BEEROS_VIRTIO_BLK_H
#define BEEROS_VIRTIO_BLK_H

/* Minimal virtio-blk driver (virtio 1.x over MMIO, one split virtqueue).
 * One request in flight per device; each call blocks until it completes.
 * Sectors are always 512 bytes (virtio-blk's unit, whatever the backing
 * device's block size). Addresses are physical == virtual on beeros. */

#include <stdbool.h>
#include <stdint.h>

#define VIRTIO_BLK_SECTOR 512
#define VIRTIO_BLK_MAX_DEVS 4

typedef enum {
    VBLK_OK = 0,
    VBLK_ERR_IO,            /* device reported VIRTIO_BLK_S_IOERR */
    VBLK_ERR_UNSUPPORTED,   /* VIRTIO_BLK_S_UNSUPP, or flush not offered */
    VBLK_ERR_TIMEOUT,       /* no completion within the deadline */
    VBLK_ERR_RANGE,         /* lba + n past the end of the device */
    VBLK_ERR_READ_ONLY,
    VBLK_ERR_NO_DEVICE,
} vblk_status;

/* Scan the board's virtio-mmio slots and initialise every block device.
 * Returns the number found. Safe to call once at boot. */
int virtio_blk_probe(void);

int      virtio_blk_count(void);
uint64_t virtio_blk_sectors(int id);
bool     virtio_blk_read_only(int id);
bool     virtio_blk_can_flush(int id);

/* Transfer n sectors starting at lba to/from buf (n * 512 bytes). */
vblk_status virtio_blk_rw(int id, bool write, uint64_t lba, uint32_t n, void* buf);
vblk_status virtio_blk_flush(int id);

#endif
