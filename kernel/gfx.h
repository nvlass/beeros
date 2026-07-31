/* gfx.h — board-agnostic framebuffer HAL
 *
 * Implemented by kernel/gfx.c.
 * Initialized by platform/<board>/hal/gfx_ramfb.c (or equivalent).
 *
 * Color format: 0x00RRGGBB as a 32-bit little-endian uint32_t.
 */

#ifndef GFX_H
#define GFX_H

#include <stdint.h>

/* Called by platform init code once the framebuffer address is known. */
void     gfx_init(uint32_t width, uint32_t height, uintptr_t fb_addr);

/* Draw a single pixel. No-op if (x,y) is out of bounds. */
void     gfx_set_pixel(uint32_t x, uint32_t y, uint32_t color);

/* Fill a rectangle with a solid color. Clipped to screen bounds. */
void     gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* Query screen dimensions (0 before gfx_init). */
uint32_t gfx_width(void);
uint32_t gfx_height(void);

#endif /* GFX_H */
