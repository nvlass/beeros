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

/* (blit-bits! x y buf off w h stride scale fg bg) — 1-bit bitmap from a
 * ByteBuffer at off (font glyphs); bg nil = transparent */
static Value native_gfx_blit_bits(VM* vm, int argc, Value* argv) {
    if (argc != 10 || !is_bytebuffer(argv[2])) {
        vm_error(vm, "blit-bits!: (blit-bits! x y buf off w h stride scale fg bg)");
        return VALUE_NIL;
    }
    for (int i = 0; i < 9; i++) {
        if (i != 2 && !is_fixnum(argv[i])) { vm_error(vm, "blit-bits!: numbers expected"); return VALUE_NIL; }
    }
    int64_t off = untag_fixnum(argv[3]);
    uint32_t h = (uint32_t)untag_fixnum(argv[5]), stride = (uint32_t)untag_fixnum(argv[6]);
    if (off < 0 || (uint64_t)off + (uint64_t)h * stride > bytebuffer_capacity(argv[2])) {
        vm_error(vm, "blit-bits!: bitmap outside the buffer");
        return VALUE_NIL;
    }
    int64_t bg = is_fixnum(argv[9]) ? untag_fixnum(argv[9]) : -1;
    gfx_blit_bits((int32_t)untag_fixnum(argv[0]), (int32_t)untag_fixnum(argv[1]),
                  bytebuffer_data(argv[2]) + off, (uint32_t)untag_fixnum(argv[4]), h, stride,
                  (uint32_t)untag_fixnum(argv[7]), (uint32_t)untag_fixnum(argv[8]), bg);
    return VALUE_NIL;
}

/* (copy-rect! sx sy w h dx dy) — e.g. scroll a text area */
static Value native_gfx_copy_rect(VM* vm, int argc, Value* argv) {
    if (argc != 6) { vm_error(vm, "copy-rect!: (copy-rect! sx sy w h dx dy)"); return VALUE_NIL; }
    for (int i = 0; i < 6; i++) {
        if (!is_fixnum(argv[i]) || untag_fixnum(argv[i]) < 0) {
            vm_error(vm, "copy-rect!: non-negative numbers expected");
            return VALUE_NIL;
        }
    }
    gfx_copy_rect((uint32_t)untag_fixnum(argv[0]), (uint32_t)untag_fixnum(argv[1]),
                  (uint32_t)untag_fixnum(argv[2]), (uint32_t)untag_fixnum(argv[3]),
                  (uint32_t)untag_fixnum(argv[4]), (uint32_t)untag_fixnum(argv[5]));
    return VALUE_NIL;
}

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
}

/* ── load embedded lib/gfx.beer ─────────────────────────────────────────── */

void gfx_load_library(void) {
    beer_load_lib(lib_gfx_beer, lib_gfx_beer_len, "lib/gfx.beer", "beer.gfx");
}
