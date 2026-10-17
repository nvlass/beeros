#ifndef BLK_NATIVES_H
#define BLK_NATIVES_H

/* Register the beer.blk.native namespace (the C driver's primitives).
 * Doesn't touch the hardware. */
void blk_register_natives(void);

/* Compile and run the embedded lib/blk.beer (beer.blk: device maps,
 * registry, error policy) and lib/virtio_blk.beer (the beerlang driver),
 * which also picks the boot driver and registers the devices. */
void blk_load_library(void);

#endif
