/* gfx_natives.c — beer.gfx native namespace for beeros
 *
 * Registers the six primitive ops (set-pixel!, fill-rect!, width, height,
 * rgb, clear!) in the beer.gfx namespace, then compiles and runs the
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
#include "gfx.h"
#include "uart.h"

/* Generated at build time: xxd -i lib/gfx.beer > kernel/gfx_beer_blob.h */
#include "gfx_beer_blob.h"

/* ── local retain list (same leak-over-UAF trade-off as repl_uart.c) ──── */
#define GFX_MAX_UNITS 32
static CompiledCode* g_units[GFX_MAX_UNITS];
static Value*        g_consts[GFX_MAX_UNITS];
static int           g_n_units = 0;

static void gfx_retain_unit(CompiledCode* code, Value* consts) {
    if (g_n_units < GFX_MAX_UNITS) {
        g_units[g_n_units]  = code;
        g_consts[g_n_units] = consts;
        g_n_units++;
    }
}

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
}

/* ── load embedded lib/gfx.beer ─────────────────────────────────────────── */

void gfx_load_library(void) {
    /* xxd -i produces `lib_gfx_beer[]` (no NUL); make a NUL-terminated copy */
    char* src = malloc(lib_gfx_beer_len + 1);
    if (!src) return;
    memcpy(src, lib_gfx_beer, lib_gfx_beer_len);
    src[lib_gfx_beer_len] = '\0';

    Reader* reader = reader_new(src, "lib/gfx.beer");
    free(src);
    if (!reader) return;

    Value forms = reader_read_all(reader);
    if (reader_has_error(reader)) {
        uart_puts("[gfx] read error: ");
        uart_puts(reader_error_msg(reader));
        uart_puts("\r\n");
        reader_free(reader);
        object_release(forms);
        return;
    }
    reader_free(reader);

    size_t n = vector_length(forms);
    for (size_t fi = 0; fi < n; fi++) {
        Value form = vector_get(forms, fi);

        Compiler* compiler = compiler_new("lib/gfx.beer");
        CompiledCode* code = compile(compiler, form);

        if (compiler_has_error(compiler)) {
            uart_puts("[gfx] compile error: ");
            uart_puts(compiler_error_msg(compiler));
            uart_puts("\r\n");
            compiled_code_free(code);
            compiler_free(compiler);
            continue;
        }
        compiler_free(compiler);

        int nc = (int)vector_length(code->constants);
        Value* consts = malloc((size_t)nc * sizeof(Value));
        if (!consts) { compiled_code_free(code); continue; }

        for (int i = 0; i < nc; i++) consts[i] = vector_get(code->constants, i);
        for (int i = 0; i < nc; i++) {
            if (is_function(consts[i])) {
                function_set_code(consts[i], code->bytecode, (int)code->code_size,
                                  consts, nc);
                object_make_immortal(consts[i]);
            }
        }

        Value tv = task_new_from_code(code->bytecode, (int)code->code_size,
                                      consts, nc, global_scheduler);
        Task* t = task_get(tv);
        scheduler_run_task_to_completion(global_scheduler, t);

        if (t->vm->error) {
            uart_puts("[gfx] runtime error: ");
            uart_puts(t->vm->error_msg);
            uart_puts("\r\n");
        }

        object_release(tv);
        gfx_retain_unit(code, consts);
    }

    object_release(forms);
}
