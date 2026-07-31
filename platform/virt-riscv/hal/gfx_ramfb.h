#ifndef GFX_RAMFB_H
#define GFX_RAMFB_H

/* Initialize the QEMU ramfb device via fw-cfg DMA, then call gfx_init().
 * Must be called after BSS is cleared and the scheduler is running. */
void gfx_init_ramfb(void);

#endif /* GFX_RAMFB_H */
