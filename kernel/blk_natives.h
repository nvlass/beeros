#ifndef BLK_NATIVES_H
#define BLK_NATIVES_H

/* Probe virtio-blk devices and register the beer.blk.native namespace
 * (the C driver's primitives). */
void blk_register_natives(void);

/* Compile and run the embedded lib/blk.beer (beer.blk: device maps,
 * registry, error policy). Registers every probed virtio-blk device. */
void blk_load_library(void);

#endif
