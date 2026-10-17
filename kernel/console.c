/* console.c — serial console input: receive ring, interrupt keys
 *
 * With receive interrupts on, the trap handler drains the UART into a
 * ring, so the interrupt keys are seen even while a form hogs the CPU or
 * never reads input:
 *   Ctrl-\ or a serial BREAK   always: vm_request_interrupt()
 *   Ctrl-C                     while a form runs and ctrl-c-interrupts is on
 * Either one also drops the bytes typed ahead. beerlang delivers the
 * interrupt to the foreground task (the REPL's form) between quanta.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "scheduler.h"
#include "task.h"
#include "bstring.h"
#include "uart.h"
#include "namespace.h"
#include "native.h"
#include "value.h"
#include "symbol.h"
#include "function.h"
#include "memory.h"
#include "vm.h"

#define RING_SIZE 256           /* power of two */

static volatile uint8_t  ring[RING_SIZE];
static volatile uint32_t ring_head;    /* written by the trap handler */
static volatile uint32_t ring_tail;    /* written by the reader */
static volatile uint32_t drop_before;  /* typed-ahead bytes before this go */
static volatile bool     drop;
static bool irq_mode;

static volatile bool reading;
static volatile bool ctrl_c_interrupts = true;

#define CTRL_C          0x03
#define CTRL_BACKSLASH  0x1C

void console_rx(uint8_t b, bool is_break) {
    if (is_break || b == CTRL_BACKSLASH || (b == CTRL_C && ctrl_c_interrupts && !reading)) {
        drop_before = ring_head;           /* the reader drops them: it owns ring_tail */
        drop = true;
        vm_request_interrupt();
        return;
    }
    uint32_t h = ring_head;
    if (h - ring_tail >= RING_SIZE) return;   /* full: drop */
    ring[h & (RING_SIZE - 1)] = b;
    __asm__ volatile("" ::: "memory");
    ring_head = h + 1;
}

int console_getc_nonblock(char* c) {
    if (!irq_mode) return uart_getc_nonblock(c);
    uint32_t t = ring_tail;
    if (drop) {
        drop = false;
        uint32_t d = drop_before;
        if ((int32_t)(d - t) > 0) t = d;   /* never back over bytes already read */
    }
    if (t == ring_head) { ring_tail = t; return 0; }
    *c = (char)ring[t & (RING_SIZE - 1)];
    __asm__ volatile("" ::: "memory");
    ring_tail = t + 1;
    return 1;
}

void console_set_reading(bool r) { reading = r; }

void console_init(void) {
    irq_mode = uart_rx_irq_enable() != 0;
}

/* ── beer.tty natives ───────────────────────────────────────────────────── */

/* (beer.tty/uart-byte) => next received byte, or nil */
static Value native_uart_byte(VM* vm, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
    char c;
    return console_getc_nonblock(&c) ? make_fixnum((uint8_t)c) : VALUE_NIL;
}

/* (beer.tty/ctrl-c-interrupts! on?) — off for programs that want Ctrl-C as a key */
static Value native_ctrl_c_set(VM* vm, int argc, Value* argv) {
    if (argc != 1) { vm_error(vm, "ctrl-c-interrupts!: takes one argument"); return VALUE_NIL; }
    ctrl_c_interrupts = !is_nil(argv[0]) && !is_false(argv[0]);
    return VALUE_NIL;
}

/* (beer.tty/ctrl-c-interrupts?) => does Ctrl-C interrupt right now */
static Value native_ctrl_c_get(VM* vm, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
    return (ctrl_c_interrupts && !reading) ? VALUE_TRUE : VALUE_FALSE;
}

/* ── per-task output capture ───────────────────────────────────────────── */

/* A task that called capture-output! has its stdout/stderr (print, printf,
 * VM errors) collected here instead of going to the UART; another task
 * drains it with take-output. Keyed by the Task, retained while captured
 * so the pointer can't be reused. LF becomes CR LF, as on the UART. */

#define CAPTURES    8
#define CAPTURE_MAX (64 * 1024)  /* beyond this, output is dropped */

typedef struct {
    Task*  task;
    char*  buf;
    size_t len, cap;
} Capture;

static Capture captures[CAPTURES];

static Capture* capture_for(Task* t) {
    if (!t) return NULL;
    for (int i = 0; i < CAPTURES; i++)
        if (captures[i].task == t) return &captures[i];
    return NULL;
}

static void capture_put(Capture* c, char ch) {
    if (c->len >= c->cap) {
        if (c->cap >= CAPTURE_MAX) return;
        size_t cap = c->cap ? c->cap * 2 : 256;
        char* b = realloc(c->buf, cap);
        if (!b) return;
        c->buf = b; c->cap = cap;
    }
    c->buf[c->len++] = ch;
}

void console_write(const char* s, size_t n) {
    Capture* c = global_scheduler ? capture_for(global_scheduler->current) : NULL;
    for (size_t i = 0; i < n; i++) {
        if (c) {
            if (s[i] == '\n') capture_put(c, '\r');
            capture_put(c, s[i]);
        } else {
            if (s[i] == '\n') uart_putc('\r');
            uart_putc(s[i]);
        }
    }
}

/* (beer.tty/capture-output!) => slot: this task's output goes to the slot */
static Value native_capture_output(VM* vm, int argc, Value* argv) {
    (void)argc; (void)argv;
    Task* t = global_scheduler ? global_scheduler->current : NULL;
    if (!t) { vm_error(vm, "capture-output!: not in a task"); return VALUE_NIL; }
    Capture* c = capture_for(t);
    if (c) return make_fixnum(c - captures);
    for (int i = 0; i < CAPTURES; i++) {
        if (!captures[i].task) {
            object_retain(tag_pointer(t));
            captures[i].task = t;
            captures[i].len = 0;
            return make_fixnum(i);
        }
    }
    vm_error(vm, "capture-output!: no free slot");
    return VALUE_NIL;
}

static Capture* slot_arg(VM* vm, const char* who, int argc, Value* argv) {
    if (argc != 1 || !is_fixnum(argv[0]) || untag_fixnum(argv[0]) < 0 ||
        untag_fixnum(argv[0]) >= CAPTURES) {
        char msg[80];
        snprintf(msg, sizeof msg, "%s: takes a slot from capture-output!", who);
        vm_error(vm, msg);
        return NULL;
    }
    return &captures[untag_fixnum(argv[0])];
}

/* Bytes of an incomplete UTF-8 sequence at the end of b[0..n) */
static size_t utf8_tail(const char* b, size_t n) {
    for (size_t k = 1; k <= 3 && k <= n; k++) {
        uint8_t c = (uint8_t)b[n - k];
        if ((c & 0xC0) == 0x80) continue;          /* continuation byte */
        size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        return need > k ? k : 0;
    }
    return 0;
}

/* (beer.tty/take-output slot) => what the task wrote since, or nil */
static Value native_take_output(VM* vm, int argc, Value* argv) {
    Capture* c = slot_arg(vm, "take-output", argc, argv);
    if (!c || c->len == 0) return VALUE_NIL;
    size_t keep = utf8_tail(c->buf, c->len);
    size_t n = c->len - keep;
    if (n == 0) return VALUE_NIL;
    Value s = string_from_buffer(c->buf, n);
    memmove(c->buf, c->buf + n, keep);
    c->len = keep;
    return s;
}

/* (beer.tty/release-output! slot) — the task's output goes to the UART again */
static Value native_release_output(VM* vm, int argc, Value* argv) {
    Capture* c = slot_arg(vm, "release-output!", argc, argv);
    if (!c || !c->task) return VALUE_NIL;
    Task* t = c->task;
    c->task = NULL;
    c->len = 0;
    object_release(tag_pointer(t));
    return VALUE_NIL;
}

static void reg(Namespace* ns, const char* name, NativeFn fn) {
    Value f = native_function_new(-1, fn, name);
    namespace_define(ns, symbol_intern(name), f);
    object_release(f);
}

void console_register_natives(void) {
    Namespace* ns = namespace_registry_get_or_create(global_namespace_registry, "beer.tty");
    reg(ns, "uart-byte",          native_uart_byte);
    reg(ns, "ctrl-c-interrupts!", native_ctrl_c_set);
    reg(ns, "ctrl-c-interrupts?", native_ctrl_c_get);
    reg(ns, "capture-output!",    native_capture_output);
    reg(ns, "take-output",        native_take_output);
    reg(ns, "release-output!",    native_release_output);
}
