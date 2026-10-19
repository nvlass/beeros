/* gfx.c — linear framebuffer HAL, off-screen surfaces, compositing
 *
 * The framebuffer is a flat array of 32-bit XRGB8888 pixels at fb_addr.
 * gfx_init() is called once by the platform-specific init code after the
 * hardware has been told where the buffer lives.
 *
 * Surfaces are off-screen pixel buffers (id 1..GFX_SURFACES-1; id 0 is the
 * screen itself). A window system draws each window into its own surface
 * and presents it: the owner map says, per screen pixel, which surface
 * may show there, so overlapping windows never draw over each other.
 *
 * The mouse cursor is drawn by software on top: every screen write that
 * touches it hides it first and redraws it afterwards.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

typedef struct {
    uint32_t* px;
    uint32_t  w, h;
} Target;

static volatile uint32_t* g_fb   = (volatile uint32_t*)0;
static uint32_t            g_w    = 0;
static uint32_t            g_h    = 0;
static Target              surfaces[GFX_SURFACES];   /* [0] = the screen */
static uint8_t*            owner;                    /* g_w * g_h surface ids */

void gfx_init(uint32_t width, uint32_t height, uintptr_t fb_addr) {
    g_w  = width;
    g_h  = height;
    g_fb = (volatile uint32_t*)fb_addr;
    surfaces[0] = (Target){ (uint32_t*)fb_addr, width, height };
    owner = calloc((size_t)width * height, 1);
}

uint32_t gfx_width(void)  { return g_w; }
uint32_t gfx_height(void) { return g_h; }

/* ── cursor ────────────────────────────────────────────────────────────── */

#define CUR_W 11
#define CUR_H 16

/* Arrow: '#' black outline, '.' white fill */
static const char* cursor_art[CUR_H] = {
    "#          ",
    "##         ",
    "#.#        ",
    "#..#       ",
    "#...#      ",
    "#....#     ",
    "#.....#    ",
    "#......#   ",
    "#.......#  ",
    "#........# ",
    "#.....#####",
    "#..#..#    ",
    "#.# #..#   ",
    "##  #..#   ",
    "#    #..#  ",
    "      ##   ",
};

static int32_t  cur_x, cur_y;
static bool     cur_shown;
static uint32_t cur_saved[CUR_W * CUR_H];

static void cursor_draw(void) {
    for (int r = 0; r < CUR_H; r++) {
        for (int c = 0; c < CUR_W; c++) {
            int32_t x = cur_x + c, y = cur_y + r;
            if (x < 0 || y < 0 || x >= (int32_t)g_w || y >= (int32_t)g_h) continue;
            volatile uint32_t* p = &g_fb[(uint32_t)y * g_w + (uint32_t)x];
            cur_saved[r * CUR_W + c] = *p;
            char a = cursor_art[r][c];
            if (a == '#') *p = 0x000000;
            else if (a == '.') *p = 0xFFFFFF;
        }
    }
}

static void cursor_restore(void) {
    for (int r = 0; r < CUR_H; r++) {
        for (int c = 0; c < CUR_W; c++) {
            int32_t x = cur_x + c, y = cur_y + r;
            if (x < 0 || y < 0 || x >= (int32_t)g_w || y >= (int32_t)g_h) continue;
            g_fb[(uint32_t)y * g_w + (uint32_t)x] = cur_saved[r * CUR_W + c];
        }
    }
}

/* Around a screen write to (x,y,w,h): hide the cursor if they overlap */
static bool cursor_hide_for(int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!cur_shown) return false;
    if (x >= cur_x + CUR_W || y >= cur_y + CUR_H || x + w <= cur_x || y + h <= cur_y)
        return false;
    cursor_restore();
    return true;
}

static void cursor_show_again(bool hidden) { if (hidden) cursor_draw(); }

void gfx_cursor(int32_t x, int32_t y, bool show) {
    if (!g_fb) return;
    if (cur_shown) cursor_restore();
    cur_x = x; cur_y = y; cur_shown = show;
    if (show) cursor_draw();
}

/* ── targets ───────────────────────────────────────────────────────────── */

static Target* target(int id) {
    if (id < 0 || id >= GFX_SURFACES || !surfaces[id].px) return NULL;
    return &surfaces[id];
}

int gfx_surface_new(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return -1;
    for (int i = 1; i < GFX_SURFACES; i++) {
        if (!surfaces[i].px) {
            uint32_t* px = calloc((size_t)w * h, 4);
            if (!px) return -1;
            surfaces[i] = (Target){ px, w, h };
            return i;
        }
    }
    return -1;
}

void gfx_surface_free(int id) {
    if (id <= 0 || id >= GFX_SURFACES || !surfaces[id].px) return;
    free(surfaces[id].px);
    surfaces[id] = (Target){ NULL, 0, 0 };
    if (owner)
        for (size_t i = 0; i < (size_t)g_w * g_h; i++)
            if (owner[i] == id) owner[i] = 0;
}

/* Clip (x,y,w,h) to a w_max x h_max area; false if nothing is left */
static bool clip(int64_t* x, int64_t* y, int64_t* w, int64_t* h, uint32_t w_max, uint32_t h_max) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > w_max) *w = (int64_t)w_max - *x;
    if (*y + *h > h_max) *h = (int64_t)h_max - *y;
    return *w > 0 && *h > 0;
}

void gfx_fill_rect_on(int id, int64_t x, int64_t y, int64_t w, int64_t h, uint32_t color) {
    Target* t = target(id);
    if (!t || !clip(&x, &y, &w, &h, t->w, t->h)) return;
    bool hid = id == 0 && cursor_hide_for(x, y, w, h);
    for (int64_t row = y; row < y + h; row++) {
        volatile uint32_t* line = (volatile uint32_t*)t->px + row * t->w + x;
        for (int64_t col = 0; col < w; col++) line[col] = color;
    }
    cursor_show_again(hid);
}

void gfx_blit_bits_on(int id, int32_t x, int32_t y, const uint8_t* bits, uint32_t w, uint32_t h,
                      uint32_t stride, uint32_t scale, uint32_t fg, int64_t bg) {
    Target* t = target(id);
    if (!t || scale == 0) return;
    bool hid = id == 0 && cursor_hide_for(x, y, (int64_t)w * scale, (int64_t)h * scale);
    for (uint32_t r = 0; r < h; r++) {
        const uint8_t* row = bits + r * stride;
        for (uint32_t sy = 0; sy < scale; sy++) {
            int64_t py = (int64_t)y + r * scale + sy;
            if (py < 0 || py >= t->h) continue;
            volatile uint32_t* line = (volatile uint32_t*)t->px + py * t->w;
            for (uint32_t c = 0; c < w; c++) {
                bool on = row[c >> 3] & (0x80 >> (c & 7));
                if (!on && bg < 0) continue;
                uint32_t color = on ? fg : (uint32_t)bg;
                for (uint32_t sx = 0; sx < scale; sx++) {
                    int64_t px = (int64_t)x + c * scale + sx;
                    if (px >= 0 && px < t->w) line[px] = color;
                }
            }
        }
    }
    cursor_show_again(hid);
}

void gfx_copy_rect_on(int id, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h,
                      uint32_t dx, uint32_t dy) {
    Target* t = target(id);
    if (!t) return;
    if (sx >= t->w || sy >= t->h || dx >= t->w || dy >= t->h) return;
    uint32_t maxx = (sx > dx ? sx : dx), maxy = (sy > dy ? sy : dy);
    if (maxx + w > t->w) w = t->w - maxx;
    if (maxy + h > t->h) h = t->h - maxy;
    bool hid = id == 0 && (cursor_hide_for(sx, sy, w, h) | cursor_hide_for(dx, dy, w, h));
    /* rows in the order that doesn't overwrite rows still to be copied */
    for (uint32_t i = 0; i < h; i++) {
        uint32_t r = dy > sy ? h - 1 - i : i;
        memmove(t->px + (dy + r) * t->w + dx, t->px + (sy + r) * t->w + sx, w * 4);
    }
    cursor_show_again(hid);
}

/* ── screen (surface 0) ────────────────────────────────────────────────── */

void gfx_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_fb || x >= g_w || y >= g_h) return;
    bool hid = cursor_hide_for(x, y, 1, 1);
    g_fb[y * g_w + x] = color;
    cursor_show_again(hid);
}

void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    gfx_fill_rect_on(0, x, y, w, h, color);
}

void gfx_blit_bits(int32_t x, int32_t y, const uint8_t* bits, uint32_t w, uint32_t h,
                   uint32_t stride, uint32_t scale, uint32_t fg, int64_t bg) {
    gfx_blit_bits_on(0, x, y, bits, w, h, stride, scale, fg, bg);
}

void gfx_copy_rect(uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy) {
    gfx_copy_rect_on(0, sx, sy, w, h, dx, dy);
}

/* ── compositing ───────────────────────────────────────────────────────── */

void gfx_own(int id, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!owner || id < 0 || id >= GFX_SURFACES || !clip(&x, &y, &w, &h, g_w, g_h)) return;
    for (int64_t row = y; row < y + h; row++)
        memset(owner + row * g_w + x, id, (size_t)w);
}

int gfx_owner_at(int64_t x, int64_t y) {
    if (!owner || x < 0 || y < 0 || x >= g_w || y >= g_h) return -1;
    return owner[y * g_w + x];
}

/* Copy surface id's (sx,sy,w,h) to the screen at (dx,dy), only where the
 * owner map says id */
void gfx_present(int id, int64_t sx, int64_t sy, int64_t w, int64_t h, int64_t dx, int64_t dy) {
    Target* s = target(id);
    if (!s || id == 0 || !owner) return;
    /* clip to the surface, then to the screen, moving the other side along */
    int64_t ox = sx, oy = sy;
    if (!clip(&sx, &sy, &w, &h, s->w, s->h)) return;
    dx += sx - ox; dy += sy - oy;
    ox = dx; oy = dy;
    if (!clip(&dx, &dy, &w, &h, g_w, g_h)) return;
    sx += dx - ox; sy += dy - oy;
    bool hid = cursor_hide_for(dx, dy, w, h);
    for (int64_t r = 0; r < h; r++) {
        const uint32_t* src = s->px + (sy + r) * s->w + sx;
        volatile uint32_t* dst = g_fb + (dy + r) * g_w + dx;
        const uint8_t* own = owner + (dy + r) * g_w + dx;
        for (int64_t c = 0; c < w; c++)
            if (own[c] == id) dst[c] = src[c];
    }
    cursor_show_again(hid);
}

/* Fill the screen pixels of (x,y,w,h) that the owner map gives to id
 * (0: the desktop) */
void gfx_fill_owned(int id, int64_t x, int64_t y, int64_t w, int64_t h, uint32_t color) {
    if (!g_fb || !owner || !clip(&x, &y, &w, &h, g_w, g_h)) return;
    bool hid = cursor_hide_for(x, y, w, h);
    for (int64_t r = y; r < y + h; r++) {
        volatile uint32_t* dst = g_fb + r * g_w;
        const uint8_t* own = owner + r * g_w;
        for (int64_t c = x; c < x + w; c++)
            if (own[c] == id) dst[c] = color;
    }
    cursor_show_again(hid);
}

/* ── images ────────────────────────────────────────────────────────────── */

/* Copy a w x h block of XRGB pixels (stride bytes per row) to surface id
 * at (x,y); pixels equal to key are skipped when key >= 0. Clipped. */
void gfx_blit_pixels_on(int id, int64_t x, int64_t y, const uint8_t* px, int64_t w, int64_t h,
                        int64_t stride, int64_t key) {
    Target* t = target(id);
    if (!t) return;
    int64_t ox = x, oy = y;
    if (!clip(&x, &y, &w, &h, t->w, t->h)) return;
    px += (y - oy) * stride + (x - ox) * 4;
    bool hid = id == 0 && cursor_hide_for(x, y, w, h);
    for (int64_t r = 0; r < h; r++) {
        const uint32_t* src = (const uint32_t*)(px + r * stride);
        uint32_t* dst = t->px + (y + r) * t->w + x;
        if (key < 0) memcpy(dst, src, (size_t)w * 4);
        else
            for (int64_t c = 0; c < w; c++)
                if ((src[c] & 0xFFFFFF) != (uint32_t)key) dst[c] = src[c];
    }
    cursor_show_again(hid);
}

/* Copy surface src's (sx,sy,w,h) to surface dst at (dx,dy), each pixel as
 * a scale x scale square (dst may be 0, the screen) */
void gfx_blit_surface(int src, int64_t sx, int64_t sy, int64_t w, int64_t h,
                      int dst, int64_t dx, int64_t dy, int64_t scale) {
    Target* s = target(src);
    Target* d = target(dst);
    if (!s || !d || src == dst || scale < 1) return;
    if (!clip(&sx, &sy, &w, &h, s->w, s->h)) return;
    bool hid = dst == 0 && cursor_hide_for(dx, dy, w * scale, h * scale);
    for (int64_t r = 0; r < h; r++) {
        const uint32_t* row = s->px + (sy + r) * s->w + sx;
        for (int64_t k = 0; k < scale; k++) {
            int64_t py = dy + r * scale + k;
            if (py < 0 || py >= d->h) continue;
            uint32_t* out = d->px + py * d->w;
            if (scale == 1) {
                int64_t c0 = dx < 0 ? -dx : 0, c1 = dx + w > d->w ? d->w - dx : w;
                if (c1 > c0) memcpy(out + dx + c0, row + c0, (size_t)(c1 - c0) * 4);
                continue;
            }
            for (int64_t c = 0; c < w; c++) {
                uint32_t v = row[c];
                int64_t px0 = dx + c * scale;
                for (int64_t m = 0; m < scale; m++) {
                    int64_t pxx = px0 + m;
                    if (pxx >= 0 && pxx < d->w) out[pxx] = v;
                }
            }
        }
    }
    cursor_show_again(hid);
}
