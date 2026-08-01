/* mem_natives.c — beer.mem namespace for beeros
 *
 * Provides raw MMIO read/write, memory fences, address-of (for DMA buffers),
 * and cooperative interrupt handling via the RISC-V PLIC.
 *
 * Zero beerlang submodule changes. The 4-line reg() helper is replicated from
 * beerlang/src/runtime/core.c (it is static there, not exported).
 *
 * Interrupt model (cooperative):
 *   - trap.S trap_vector → mem_trap_handler() → sets irq_pending bit → mret
 *   - mem/irq-wait! spins (with memory barrier) until the bit appears, then
 *     clears it and returns; the beerlang caller dispatches the stored handler
 *   - mem/irq-listen! (in lib/mem.beer) wraps irq-wait! + irq-handler + call
 *
 * DMA pattern:
 *   (def buf "...N bytes...") ; allocate a string as a byte buffer
 *   (mem/addr-of buf)         ; returns physical address of buf's char data[]
 *   (mem/write32! dma-addr-reg (mem/addr-of buf))
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
#include "uart.h"
#include "plic.h"

/* Generated at build time: xxd -i lib/mem.beer > kernel/mem_beer_blob.h */
#include "mem_beer_blob.h"

/* ── String object layout (mirrors beerlang/src/types/string.c, private type) */
typedef struct {
    struct Object hdr;
    uint32_t byte_len;
    uint32_t char_count;
    char data[];
} MemString;

/* ── IRQ dispatch table ─────────────────────────────────────────────────── */
#define MAX_IRQS 64
static Value          irq_handlers[MAX_IRQS];
static volatile uint64_t irq_pending;   /* bit n set by trap handler */

/* Called from trap.S trap_vector after registers are saved */
void mem_trap_handler(void) {
    uint32_t irq = plic_claim();
    if (irq > 0 && irq < MAX_IRQS) {
        irq_pending |= (1ULL << irq);
        plic_complete(irq);
    }
    /* Spurious or out-of-range IRQ: just complete with 0 (no-op for PLIC) */
}

/* ── local retain list ──────────────────────────────────────────────────── */
#define MEM_MAX_UNITS 16
static CompiledCode* g_units[MEM_MAX_UNITS];
static Value*        g_consts[MEM_MAX_UNITS];
static int           g_n_units = 0;

static void mem_retain_unit(CompiledCode* code, Value* consts) {
    if (g_n_units < MEM_MAX_UNITS) {
        g_units[g_n_units]  = code;
        g_consts[g_n_units] = consts;
        g_n_units++;
    }
}

/* ── reg() helper — replicated from core.c (static there) ──────────────── */
static void reg(Namespace* ns, const char* name, NativeFn fn) {
    Value f = native_function_new(-1, fn, name);
    namespace_define(ns, symbol_intern(name), f);
    object_release(f);
}

/* ── address extraction helper ──────────────────────────────────────────── */
static inline uintptr_t addr_arg(Value v) {
    return (uintptr_t)(uint64_t)untag_fixnum(v);
}

/* ── MMIO read natives ──────────────────────────────────────────────────── */

static Value native_mem_read8(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 1) return VALUE_NIL;
    return make_fixnum((int64_t)(*(volatile uint8_t*)addr_arg(argv[0])));
}

static Value native_mem_read16(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 1) return VALUE_NIL;
    return make_fixnum((int64_t)(*(volatile uint16_t*)addr_arg(argv[0])));
}

static Value native_mem_read32(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 1) return VALUE_NIL;
    return make_fixnum((int64_t)(*(volatile uint32_t*)addr_arg(argv[0])));
}

static Value native_mem_read64(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 1) return VALUE_NIL;
    return make_fixnum((int64_t)(*(volatile uint64_t*)addr_arg(argv[0])));
}

/* ── MMIO write natives ─────────────────────────────────────────────────── */

static Value native_mem_write8(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 2) return VALUE_NIL;
    *(volatile uint8_t*)addr_arg(argv[0]) = (uint8_t)untag_fixnum(argv[1]);
    return VALUE_NIL;
}

static Value native_mem_write16(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 2) return VALUE_NIL;
    *(volatile uint16_t*)addr_arg(argv[0]) = (uint16_t)untag_fixnum(argv[1]);
    return VALUE_NIL;
}

static Value native_mem_write32(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 2) return VALUE_NIL;
    *(volatile uint32_t*)addr_arg(argv[0]) = (uint32_t)untag_fixnum(argv[1]);
    return VALUE_NIL;
}

static Value native_mem_write64(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 2) return VALUE_NIL;
    *(volatile uint64_t*)addr_arg(argv[0]) = (uint64_t)untag_fixnum(argv[1]);
    return VALUE_NIL;
}

/* ── Memory fence natives ───────────────────────────────────────────────── */

static Value native_mem_fence(VM* vm, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
#ifdef __riscv
    __asm__ volatile("fence rw,rw" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
    return VALUE_NIL;
}

static Value native_mem_fence_i(VM* vm, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
#ifdef __riscv
    __asm__ volatile("fence.i" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("isb" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
    return VALUE_NIL;
}

/* ── DMA: addr-of ───────────────────────────────────────────────────────── */
/* Returns the physical address of a beerlang string's char data[] region.
 * Use a string as a DMA-safe byte buffer: (def buf (make-string n \0))
 * The buffer is stable for its lifetime (refcounting GC, no compaction). */

static Value native_mem_addr_of(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 1 || !is_pointer(argv[0])) return make_fixnum(0);
    if (object_type(argv[0]) != TYPE_STRING) return make_fixnum(0);
    MemString* s = (MemString*)argv[0].as.object;
    return make_fixnum((int64_t)(uintptr_t)s->data);
}

/* ── Interrupt natives ──────────────────────────────────────────────────── */

/* (mem/irq-register! n fn) — store fn as handler for IRQ n */
static Value native_mem_irq_register(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 2 || !is_fixnum(argv[0])) return VALUE_NIL;
    int n = (int)untag_fixnum(argv[0]);
    if (n < 0 || n >= MAX_IRQS) return VALUE_NIL;
    if (is_pointer(irq_handlers[n])) object_release(irq_handlers[n]);
    irq_handlers[n] = argv[1];
    if (is_pointer(argv[1])) object_retain(argv[1]);
    return VALUE_NIL;
}

/* (mem/irq-handler n) — retrieve stored handler (nil if none) */
static Value native_mem_irq_handler(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 1 || !is_fixnum(argv[0])) return VALUE_NIL;
    int n = (int)untag_fixnum(argv[0]);
    if (n < 0 || n >= MAX_IRQS) return VALUE_NIL;
    Value h = irq_handlers[n];
    if (is_pointer(h)) object_retain(h);
    return h;
}

/* (mem/irq-enable! n priority) — configure PLIC for IRQ n */
static Value native_mem_irq_enable(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 2 || !is_fixnum(argv[0]) || !is_fixnum(argv[1])) return VALUE_NIL;
    int n    = (int)untag_fixnum(argv[0]);
    int prio = (int)untag_fixnum(argv[1]);
    plic_enable(n, prio);
    return VALUE_NIL;
}

/* (mem/irq-pending? n) — true if IRQ n has fired and not yet consumed */
static Value native_mem_irq_pending(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 1 || !is_fixnum(argv[0])) return VALUE_FALSE;
    int n = (int)untag_fixnum(argv[0]);
    if (n < 0 || n >= MAX_IRQS) return VALUE_FALSE;
    return (irq_pending & (1ULL << n)) ? VALUE_TRUE : VALUE_FALSE;
}

/* (mem/irq-wait! n) — spin until IRQ n fires; clears the pending bit.
 * Interrupts must be globally enabled (mstatus.MIE) and the source configured
 * via mem/irq-enable! before calling this. The caller is responsible for
 * dispatching the handler (see mem/irq-listen! in lib/mem.beer).
 *
 * Note: this spins the calling task. Use inside a (spawn ...) if you want the
 * REPL to remain responsive while waiting. */
static Value native_mem_irq_wait(VM* vm, int argc, Value* argv) {
    (void)vm;
    if (argc < 1 || !is_fixnum(argv[0])) return VALUE_NIL;
    int n = (int)untag_fixnum(argv[0]);
    if (n < 0 || n >= MAX_IRQS) return VALUE_NIL;
    while (!(irq_pending & (1ULL << n))) {
        /* Yield to the scheduler if we have one; otherwise spin.
         * The trap handler fires between scheduler ticks and sets irq_pending. */
        if (global_scheduler) {
            scheduler_run_one_tick(global_scheduler);
        } else {
            __asm__ volatile("wfi");  /* wait for interrupt — saves power */
        }
    }
    irq_pending &= ~(1ULL << n);
    return VALUE_NIL;
}

/* ── namespace registration ─────────────────────────────────────────────── */

void mem_register_natives(void) {
    /* Initialise handler table to nil */
    for (int i = 0; i < MAX_IRQS; i++) irq_handlers[i] = VALUE_NIL;
    irq_pending = 0;

    Namespace* ns = namespace_registry_get_or_create(
        global_namespace_registry, "beer.mem");

    reg(ns, "read8",          native_mem_read8);
    reg(ns, "read16",         native_mem_read16);
    reg(ns, "read32",         native_mem_read32);
    reg(ns, "read64",         native_mem_read64);
    reg(ns, "write8!",        native_mem_write8);
    reg(ns, "write16!",       native_mem_write16);
    reg(ns, "write32!",       native_mem_write32);
    reg(ns, "write64!",       native_mem_write64);
    reg(ns, "fence",          native_mem_fence);
    reg(ns, "fence-i",        native_mem_fence_i);
    reg(ns, "addr-of",        native_mem_addr_of);
    reg(ns, "irq-register!",  native_mem_irq_register);
    reg(ns, "irq-handler",    native_mem_irq_handler);
    reg(ns, "irq-enable!",    native_mem_irq_enable);
    reg(ns, "irq-pending?",   native_mem_irq_pending);
    reg(ns, "irq-wait!",      native_mem_irq_wait);
}

/* ── load embedded lib/mem.beer ─────────────────────────────────────────── */

void mem_load_library(void) {
    char* src = malloc(lib_mem_beer_len + 1);
    if (!src) return;
    memcpy(src, lib_mem_beer, lib_mem_beer_len);
    src[lib_mem_beer_len] = '\0';

    Reader* reader = reader_new(src, "lib/mem.beer");
    free(src);
    if (!reader) return;

    Value forms = reader_read_all(reader);
    if (reader_has_error(reader)) {
        uart_puts("[mem] read error: ");
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

        Compiler* compiler = compiler_new("lib/mem.beer");
        CompiledCode* code = compile(compiler, form);

        if (compiler_has_error(compiler)) {
            uart_puts("[mem] compile error: ");
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
            uart_puts("[mem] runtime error: ");
            uart_puts(t->vm->error_msg);
            uart_puts("\r\n");
        }

        object_release(tv);
        mem_retain_unit(code, consts);
    }

    object_release(forms);
}
