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

/* Draw a 1-bit bitmap (MSB = leftmost pixel, stride bytes per row) at
 * (x,y), each bit as a scale x scale square: set bits in fg, clear bits in
 * bg, or left alone when bg < 0. Clipped. */
void     gfx_blit_bits(int32_t x, int32_t y, const uint8_t* bits, uint32_t w, uint32_t h,
                       uint32_t stride, uint32_t scale, uint32_t fg, int64_t bg);

/* Copy a w x h rectangle from (sx,sy) to (dx,dy); overlapping is fine. */
void     gfx_copy_rect(uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy);

/* Query screen dimensions (0 before gfx_init). */
uint32_t gfx_width(void);
uint32_t gfx_height(void);

#endif /* GFX_H */
