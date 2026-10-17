#ifndef VIRTIO_MMIO_BOARD_H
#define VIRTIO_MMIO_BOARD_H

/* QEMU virt (riscv): 8 virtio-mmio transports, PLIC IRQs 1..8.
 * QEMU fills them from the highest slot down. */
#define VIRTIO_MMIO_BASE    0x10001000UL
#define VIRTIO_MMIO_STRIDE  0x1000UL
#define VIRTIO_MMIO_SLOTS   8

#endif
