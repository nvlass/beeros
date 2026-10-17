#ifndef VIRTIO_MMIO_BOARD_H
#define VIRTIO_MMIO_BOARD_H

/* QEMU virt (aarch64): 32 virtio-mmio transports. Untested — this board
 * has never been built (no aarch64-none-elf-gcc installed). */
#define VIRTIO_MMIO_BASE    0x0a000000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_SLOTS   32

#endif
