/* gfx.h — board-agnostic framebuffer HAL
 *
 * Implemented by kernel/gfx.c.
 * Initialized by platform/<board>/hal/gfx_ramfb.c (or equivalent).
 *
 * Color format: 0x00RRGGBB as a 32-bit little-endian uint32_t.
 */

#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
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

/* Off-screen surfaces (id 1..GFX_SURFACES-1; 0 is the screen) and
 * compositing through the owner map; see gfx.c */
#define GFX_SURFACES 64
int      gfx_surface_new(uint32_t w, uint32_t h);          /* -1 if none left */
void     gfx_surface_free(int id);
void     gfx_fill_rect_on(int id, int64_t x, int64_t y, int64_t w, int64_t h, uint32_t color);
void     gfx_blit_bits_on(int id, int32_t x, int32_t y, const uint8_t* bits, uint32_t w,
                          uint32_t h, uint32_t stride, uint32_t scale, uint32_t fg, int64_t bg);
void     gfx_copy_rect_on(int id, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h,
                          uint32_t dx, uint32_t dy);
void     gfx_own(int id, int64_t x, int64_t y, int64_t w, int64_t h);
int      gfx_owner_at(int64_t x, int64_t y);
void     gfx_present(int id, int64_t sx, int64_t sy, int64_t w, int64_t h, int64_t dx, int64_t dy);
void     gfx_fill_owned(int id, int64_t x, int64_t y, int64_t w, int64_t h, uint32_t color);

/* Software mouse cursor at (x,y), drawn over everything */
void     gfx_cursor(int32_t x, int32_t y, bool show);

/* Query screen dimensions (0 before gfx_init). */
uint32_t gfx_width(void);
uint32_t gfx_height(void);

#endif /* GFX_H */
