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

#include "console.h"
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
}
