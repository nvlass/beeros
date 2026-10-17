/* gfx.c — linear framebuffer HAL
 *
 * The framebuffer is a flat array of 32-bit XRGB8888 pixels at fb_addr.
 * gfx_init() is called once by the platform-specific init code after the
 * hardware has been told where the buffer lives.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "gfx.h"

static volatile uint32_t* g_fb   = (volatile uint32_t*)0;
static uint32_t            g_w    = 0;
static uint32_t            g_h    = 0;

void gfx_init(uint32_t width, uint32_t height, uintptr_t fb_addr) {
    g_w  = width;
    g_h  = height;
    g_fb = (volatile uint32_t*)fb_addr;
}

uint32_t gfx_width(void)  { return g_w; }
uint32_t gfx_height(void) { return g_h; }

void gfx_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_fb || x >= g_w || y >= g_h) return;
    g_fb[y * g_w + x] = color;
}

void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!g_fb) return;
    /* Clip to screen */
    if (x >= g_w || y >= g_h) return;
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    for (uint32_t row = y; row < y + h; row++) {
        volatile uint32_t* line = g_fb + row * g_w + x;
        for (uint32_t col = 0; col < w; col++) {
            line[col] = color;
        }
    }
}

void gfx_blit_bits(int32_t x, int32_t y, const uint8_t* bits, uint32_t w, uint32_t h,
                   uint32_t stride, uint32_t scale, uint32_t fg, int64_t bg) {
    if (!g_fb || scale == 0) return;
    for (uint32_t r = 0; r < h; r++) {
        const uint8_t* row = bits + r * stride;
        for (uint32_t sy = 0; sy < scale; sy++) {
            int64_t py = (int64_t)y + r * scale + sy;
            if (py < 0 || py >= g_h) continue;
            volatile uint32_t* line = g_fb + py * g_w;
            for (uint32_t c = 0; c < w; c++) {
                bool on = row[c >> 3] & (0x80 >> (c & 7));
                if (!on && bg < 0) continue;
                uint32_t color = on ? fg : (uint32_t)bg;
                for (uint32_t sx = 0; sx < scale; sx++) {
                    int64_t px = (int64_t)x + c * scale + sx;
                    if (px >= 0 && px < g_w) line[px] = color;
                }
            }
        }
    }
}

void gfx_copy_rect(uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy) {
    if (!g_fb) return;
    if (sx >= g_w || sy >= g_h || dx >= g_w || dy >= g_h) return;
    uint32_t maxx = (sx > dx ? sx : dx), maxy = (sy > dy ? sy : dy);
    if (maxx + w > g_w) w = g_w - maxx;
    if (maxy + h > g_h) h = g_h - maxy;
    /* rows in the order that doesn't overwrite rows still to be copied */
    for (uint32_t i = 0; i < h; i++) {
        uint32_t r = dy > sy ? h - 1 - i : i;
        memmove((void*)(g_fb + (dy + r) * g_w + dx),
                (const void*)(g_fb + (sy + r) * g_w + sx), w * 4);
    }
}
