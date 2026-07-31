/* gfx.c — linear framebuffer HAL
 *
 * The framebuffer is a flat array of 32-bit XRGB8888 pixels at fb_addr.
 * gfx_init() is called once by the platform-specific init code after the
 * hardware has been told where the buffer lives.
 */

#include <stdint.h>
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
