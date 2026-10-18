/* gfx_natives.c — beer.gfx native namespace for beeros
 *
 * Registers the primitive ops (set-pixel!, fill-rect!, width, height,
 * rgb, clear!, blit-bits!, copy-rect!) in the beer.gfx namespace, then compiles and runs the
 * embedded lib/gfx.beer source (Bresenham lines, circles, outlined rects,
 * 8×8 bitmap text).
 *
 * No modifications to the beerlang submodule: we replicate the 4-line
 * register helper from core.c (it's static there, not exported).
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "namespace.h"
#include "native.h"
#include "symbol.h"
#include "memory.h"
#include "value.h"
#include "reader.h"
#include "compiler.h"
#include "task.h"
#include "function.h"
#include "vm.h"
#include "scheduler.h"
#include "vector.h"
#include "bytebuffer.h"
#include "gfx.h"
#include "uart.h"
#include "blob_loader.h"

/* Generated at build time: xxd -i lib/gfx.beer > kernel/gfx_beer_blob.h */
#include "gfx_beer_blob.h"

/* ── 4-line helper replicated from core.c (static there) ──────────────── */
static void reg(Namespace* ns, const char* name, NativeFn fn) {
    Value f = native_function_new(-1, fn, name);
    namespace_define(ns, symbol_intern(name), f);
    object_release(f);
}

/* ── native implementations ────────────────────────────────────────────── */

/* (set-pixel! x y color) */
static Value native_gfx_set_pixel(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc != 3) return VALUE_NIL;
    uint32_t x = (uint32_t)untag_fixnum(argv[0]);
    uint32_t y = (uint32_t)untag_fixnum(argv[1]);
    uint32_t c = (uint32_t)untag_fixnum(argv[2]);
    gfx_set_pixel(x, y, c);
    return VALUE_NIL;
}

/* (fill-rect! x y w h color) */
static Value native_gfx_fill_rect(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc != 5) return VALUE_NIL;
    uint32_t x = (uint32_t)untag_fixnum(argv[0]);
    uint32_t y = (uint32_t)untag_fixnum(argv[1]);
    uint32_t w = (uint32_t)untag_fixnum(argv[2]);
    uint32_t h = (uint32_t)untag_fixnum(argv[3]);
    uint32_t c = (uint32_t)untag_fixnum(argv[4]);
    gfx_fill_rect(x, y, w, h, c);
    return VALUE_NIL;
}

/* (width) -> fixnum */
static Value native_gfx_width(VM* vm, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
    return make_fixnum((int64_t)gfx_width());
}

/* (height) -> fixnum */
static Value native_gfx_height(VM* vm, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
    return make_fixnum((int64_t)gfx_height());
}

/* (rgb r g b) -> fixnum color — 0x00RRGGBB */
static Value native_gfx_rgb(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc != 3) return make_fixnum(0);
    uint32_t r = (uint32_t)untag_fixnum(argv[0]) & 0xFF;
    uint32_t g = (uint32_t)untag_fixnum(argv[1]) & 0xFF;
    uint32_t b = (uint32_t)untag_fixnum(argv[2]) & 0xFF;
    return make_fixnum((int64_t)((r << 16) | (g << 8) | b));
}

/* (clear! color) — fill entire screen */
static Value native_gfx_clear(VM* vm, int argc, Value* argv) {
    (void)vm;
    uint32_t c = (argc >= 1) ? (uint32_t)untag_fixnum(argv[0]) : 0u;
    gfx_fill_rect(0, 0, gfx_width(), gfx_height(), c);
    return VALUE_NIL;
}

/* All fixnum arguments in argv[from..to) (skipping skip, or -1) */
static bool fixnums(VM* vm, const char* who, Value* argv, int from, int to, int skip) {
    for (int i = from; i < to; i++) {
        if (i != skip && !is_fixnum(argv[i])) {
            char msg[96];
            snprintf(msg, sizeof msg, "%s: numbers expected", who);
            vm_error(vm, msg);
            return false;
        }
    }
    return true;
}

#define I(k) untag_fixnum(argv[k])

static Value blit_bits(VM* vm, const char* who, int id, Value* argv) {
    if (!is_bytebuffer(argv[2])) { vm_error(vm, "blit-bits!: a ByteBuffer expected"); return VALUE_NIL; }
    if (!fixnums(vm, who, argv, 0, 9, 2)) return VALUE_NIL;
    int64_t off = I(3);
    uint32_t h = (uint32_t)I(5), stride = (uint32_t)I(6);
    if (off < 0 || (uint64_t)off + (uint64_t)h * stride > bytebuffer_capacity(argv[2])) {
        vm_error(vm, "blit-bits!: bitmap outside the buffer");
        return VALUE_NIL;
    }
    int64_t bg = is_fixnum(argv[9]) ? I(9) : -1;
    gfx_blit_bits_on(id, (int32_t)I(0), (int32_t)I(1), bytebuffer_data(argv[2]) + off,
                     (uint32_t)I(4), h, stride, (uint32_t)I(7), (uint32_t)I(8), bg);
    return VALUE_NIL;
}

/* (blit-bits! x y buf off w h stride scale fg bg) — 1-bit bitmap from a
 * ByteBuffer at off (font glyphs), on the screen; bg nil = transparent */
static Value native_gfx_blit_bits(VM* vm, int argc, Value* argv) {
    if (argc != 10) { vm_error(vm, "blit-bits!: (blit-bits! x y buf off w h stride scale fg bg)"); return VALUE_NIL; }
    return blit_bits(vm, "blit-bits!", 0, argv);
}

/* (blit-bits-on! surface x y buf off w h stride scale fg bg) */
static Value native_gfx_blit_bits_on(VM* vm, int argc, Value* argv) {
    if (argc != 11 || !is_fixnum(argv[0])) {
        vm_error(vm, "blit-bits-on!: (blit-bits-on! surface x y buf off w h stride scale fg bg)");
        return VALUE_NIL;
    }
    return blit_bits(vm, "blit-bits-on!", (int)untag_fixnum(argv[0]), argv + 1);
}

static Value copy_rect(VM* vm, const char* who, int id, Value* argv) {
    if (!fixnums(vm, who, argv, 0, 6, -1)) return VALUE_NIL;
    for (int i = 0; i < 6; i++) if (I(i) < 0) return VALUE_NIL;
    gfx_copy_rect_on(id, (uint32_t)I(0), (uint32_t)I(1), (uint32_t)I(2), (uint32_t)I(3),
                     (uint32_t)I(4), (uint32_t)I(5));
    return VALUE_NIL;
}

/* (copy-rect! sx sy w h dx dy) — e.g. scroll a text area */
static Value native_gfx_copy_rect(VM* vm, int argc, Value* argv) {
    if (argc != 6) { vm_error(vm, "copy-rect!: (copy-rect! sx sy w h dx dy)"); return VALUE_NIL; }
    return copy_rect(vm, "copy-rect!", 0, argv);
}

/* (copy-rect-on! surface sx sy w h dx dy) */
static Value native_gfx_copy_rect_on(VM* vm, int argc, Value* argv) {
    if (argc != 7 || !is_fixnum(argv[0])) {
        vm_error(vm, "copy-rect-on!: (copy-rect-on! surface sx sy w h dx dy)");
        return VALUE_NIL;
    }
    return copy_rect(vm, "copy-rect-on!", (int)untag_fixnum(argv[0]), argv + 1);
}

/* (fill-rect-on! surface x y w h color) */
static Value native_gfx_fill_rect_on(VM* vm, int argc, Value* argv) {
    if (argc != 6 || !fixnums(vm, "fill-rect-on!", argv, 0, 6, -1)) {
        if (argc != 6) vm_error(vm, "fill-rect-on!: (fill-rect-on! surface x y w h color)");
        return VALUE_NIL;
    }
    gfx_fill_rect_on((int)I(0), I(1), I(2), I(3), I(4), (uint32_t)I(5));
    return VALUE_NIL;
}

/* (surface w h) => id of a new off-screen surface (black), or nil */
static Value native_gfx_surface(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !fixnums(vm, "surface", argv, 0, 2, -1)) {
        if (argc != 2) vm_error(vm, "surface: (surface w h)");
        return VALUE_NIL;
    }
    if (I(0) <= 0 || I(1) <= 0) return VALUE_NIL;
    int id = gfx_surface_new((uint32_t)I(0), (uint32_t)I(1));
    return id < 0 ? VALUE_NIL : make_fixnum(id);
}

/* (surface-free! id) — also gives its screen pixels back to the desktop */
static Value native_gfx_surface_free(VM* vm, int argc, Value* argv) {
    if (argc != 1 || !fixnums(vm, "surface-free!", argv, 0, 1, -1)) return VALUE_NIL;
    gfx_surface_free((int)I(0));
    return VALUE_NIL;
}

/* (own! id x y w h) — screen pixels in the rect show surface id (0: desktop) */
static Value native_gfx_own(VM* vm, int argc, Value* argv) {
    if (argc != 5 || !fixnums(vm, "own!", argv, 0, 5, -1)) {
        if (argc != 5) vm_error(vm, "own!: (own! id x y w h)");
        return VALUE_NIL;
    }
    gfx_own((int)I(0), I(1), I(2), I(3), I(4));
    return VALUE_NIL;
}

/* (owner-at x y) => surface id shown at that screen pixel (0: desktop) */
static Value native_gfx_owner_at(VM* vm, int argc, Value* argv) {
    if (argc != 2 || !fixnums(vm, "owner-at", argv, 0, 2, -1)) return VALUE_NIL;
    int id = gfx_owner_at(I(0), I(1));
    return id < 0 ? VALUE_NIL : make_fixnum(id);
}

/* (present! id sx sy w h dx dy) — copy the surface rect to the screen at
 * (dx,dy), where the owner map says id */
static Value native_gfx_present(VM* vm, int argc, Value* argv) {
    if (argc != 7 || !fixnums(vm, "present!", argv, 0, 7, -1)) {
        if (argc != 7) vm_error(vm, "present!: (present! id sx sy w h dx dy)");
        return VALUE_NIL;
    }
    gfx_present((int)I(0), I(1), I(2), I(3), I(4), I(5), I(6));
    return VALUE_NIL;
}

/* (fill-owned! id x y w h color) — fill the screen pixels owned by id */
static Value native_gfx_fill_owned(VM* vm, int argc, Value* argv) {
    if (argc != 6 || !fixnums(vm, "fill-owned!", argv, 0, 6, -1)) {
        if (argc != 6) vm_error(vm, "fill-owned!: (fill-owned! id x y w h color)");
        return VALUE_NIL;
    }
    gfx_fill_owned((int)I(0), I(1), I(2), I(3), I(4), (uint32_t)I(5));
    return VALUE_NIL;
}

/* (cursor! x y show?) — the software mouse pointer */
static Value native_gfx_cursor(VM* vm, int argc, Value* argv) {
    if (argc != 3 || !fixnums(vm, "cursor!", argv, 0, 2, -1)) {
        if (argc != 3) vm_error(vm, "cursor!: (cursor! x y show?)");
        return VALUE_NIL;
    }
    gfx_cursor((int32_t)I(0), (int32_t)I(1), !is_nil(argv[2]) && !is_false(argv[2]));
    return VALUE_NIL;
}

#undef I

/* ── namespace registration ─────────────────────────────────────────────── */

void gfx_register_natives(void) {
    Namespace* ns = namespace_registry_get_or_create(
        global_namespace_registry, "beer.gfx");
    reg(ns, "set-pixel!",  native_gfx_set_pixel);
    reg(ns, "fill-rect!",  native_gfx_fill_rect);
    reg(ns, "width",       native_gfx_width);
    reg(ns, "height",      native_gfx_height);
    reg(ns, "rgb",         native_gfx_rgb);
    reg(ns, "clear!",      native_gfx_clear);
    reg(ns, "blit-bits!",  native_gfx_blit_bits);
    reg(ns, "copy-rect!",  native_gfx_copy_rect);
    reg(ns, "surface",       native_gfx_surface);
    reg(ns, "surface-free!", native_gfx_surface_free);
    reg(ns, "fill-rect-on!", native_gfx_fill_rect_on);
    reg(ns, "blit-bits-on!", native_gfx_blit_bits_on);
    reg(ns, "copy-rect-on!", native_gfx_copy_rect_on);
    reg(ns, "own!",          native_gfx_own);
    reg(ns, "owner-at",      native_gfx_owner_at);
    reg(ns, "present!",      native_gfx_present);
    reg(ns, "fill-owned!",   native_gfx_fill_owned);
    reg(ns, "cursor!",       native_gfx_cursor);
}

/* ── load embedded lib/gfx.beer ─────────────────────────────────────────── */

void gfx_load_library(void) {
    beer_load_lib(lib_gfx_beer, lib_gfx_beer_len, "lib/gfx.beer", "beer.gfx");
}
