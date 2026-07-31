/* repl_uart.c — bare-metal Beerlang REPL over UART
 *
 * Replaces beerlang/src/repl/main.c for the beeros target.
 * Entry point: beeros_main(), called from platform/<board>/boot/start.S
 * after the stack and BSS are set up.
 *
 * Phase 1 note: core.beer macros (defn, let, cond, ->, ...) are NOT
 * available until LittleFS storage is wired (Phase 2). Special forms
 * (def, if, fn, do, loop, recur, try, spawn, ...) and native functions
 * work immediately.
 */

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>   /* malloc/free from beerlang's allocator */

#include "uart.h"
#include "memory.h"
#include "symbol.h"
#include "namespace.h"
#include "reader.h"
#include "compiler.h"
#include "vm.h"
#include "value.h"
#include "scheduler.h"
#include "core.h"
#include "task.h"
#include "function.h"
#include "beerlang.h"

#ifdef BEEROS_GFX
#include "gfx_natives.h"
/* Provided by platform/$(BOARD)/hal/gfx_ramfb.c (or equivalent) */
extern void gfx_init_ramfb(void);
#endif

#define INPUT_SIZE  4096
#define ACCUM_SIZE  65536
#define MAX_UNITS   256

/* Retained compiled units (same pattern as main.c — prevents use-after-free
 * when function objects hold raw pointers into compiled bytecode). */
static CompiledCode* compiled_units[MAX_UNITS];
static Value*        constant_arrays[MAX_UNITS];
static int           n_compiled_units = 0;

static void retain_unit(CompiledCode* code, Value* constants) {
    if (n_compiled_units < MAX_UNITS) {
        compiled_units[n_compiled_units]  = code;
        constant_arrays[n_compiled_units] = constants;
        n_compiled_units++;
    } else {
        /* Leak rather than use-after-free — acceptable in Phase 1 */
        (void)code; (void)constants;
    }
}

/* ----------------------------------------------------------------
 * uart_print_int — decimal integer over UART (no printf needed)
 * ---------------------------------------------------------------- */
static void uart_print_int(int n) {
    if (n < 0) { uart_putc('-'); n = -n; }
    if (n == 0) { uart_putc('0'); return; }
    char buf[12]; int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i-- > 0) uart_putc(buf[i]);
}

/* ----------------------------------------------------------------
 * is_form_complete — balanced delimiter check (same logic as main.c)
 * ---------------------------------------------------------------- */
static bool is_form_complete(const char* src) {
    int depth = 0;
    bool in_string = false, has_content = false;
    for (const char* p = src; *p; p++) {
        if (in_string) {
            if (*p == '\\' && *(p + 1)) p++;
            else if (*p == '"') in_string = false;
        } else {
            switch (*p) {
            case '"': in_string = true; has_content = true; break;
            case ';': while (*p && *p != '\n') p++; if (*p) p--; break;
            case '(': case '[': case '{': depth++; has_content = true; break;
            case ')': case ']': case '}': depth--; break;
            default: if ((unsigned char)*p > ' ') has_content = true; break;
            }
        }
    }
    return has_content && (depth <= 0);
}

/* ----------------------------------------------------------------
 * uart_readline — read one line from UART with echo and backspace.
 * Runs scheduler ticks while waiting. Returns number of chars read.
 * ---------------------------------------------------------------- */
static int uart_readline(char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c;
        if (uart_getc_nonblock(&c)) {
            if (c == '\r') {
                /* CR: echo CRLF and end line */
                uart_puts("\r\n");
                buf[i++] = '\n';
                break;
            }
            if (c == 127 || c == '\b') {
                /* Backspace */
                if (i > 0) { i--; uart_puts("\b \b"); }
                continue;
            }
            if ((unsigned char)c < 0x20) continue;  /* ignore other control chars */
            uart_putc(c);   /* echo */
            buf[i++] = c;
        } else {
            /* No char available — yield to scheduler */
            if (global_scheduler) {
                scheduler_run_one_tick(global_scheduler);
            }
        }
    }
    buf[i] = '\0';
    return i;
}

/* ----------------------------------------------------------------
 * run_repl — the interactive REPL loop
 * ---------------------------------------------------------------- */
static void run_repl(void) {
    char input[INPUT_SIZE];
    char accum[ACCUM_SIZE];
    size_t accum_len = 0;
    accum[0] = '\0';

    uart_puts("beeros 0.1 / beerlang ");
    uart_print_int(BEERLANG_VERSION_MAJOR); uart_putc('.');
    uart_print_int(BEERLANG_VERSION_MINOR); uart_putc('.');
    uart_print_int(BEERLANG_VERSION_PATCH);
    uart_puts("\r\nType (exit) to quit\r\n\r\n");

    int line_number = 1;

    while (true) {
        /* Print prompt */
        Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
        if (accum_len == 0) {
            uart_puts(cur_ns ? cur_ns->name : "beer");
            uart_putc(':');
            uart_print_int(line_number);
            uart_puts("> ");
        } else {
            uart_puts("...   ");
        }

        int n = uart_readline(input, INPUT_SIZE);
        if (n == 0) continue;

        /* Exit check */
        if (accum_len == 0) {
            /* Trim */
            char trimmed[INPUT_SIZE];
            strncpy(trimmed, input, INPUT_SIZE - 1);
            trimmed[INPUT_SIZE - 1] = '\0';
            size_t tlen = strlen(trimmed);
            while (tlen > 0 && ((unsigned char)trimmed[tlen-1] <= ' ')) trimmed[--tlen] = '\0';
            if (strcmp(trimmed, "(exit)") == 0 || strcmp(trimmed, "exit") == 0) break;
            if (tlen == 0) continue;
        }

        /* Accumulate */
        size_t input_len = (size_t)n;
        if (accum_len + input_len >= ACCUM_SIZE) {
            uart_puts("Error: input too long\r\n");
            accum_len = 0; accum[0] = '\0';
            continue;
        }
        memcpy(accum + accum_len, input, input_len);
        accum_len += input_len;
        accum[accum_len] = '\0';

        if (!is_form_complete(accum)) continue;

        /* Parse */
        Reader* reader = reader_new(accum, "<repl>");
        if (!reader) {
            uart_puts("Error: out of memory\r\n");
            accum_len = 0; accum[0] = '\0';
            continue;
        }
        Value all_forms = reader_read_all(reader);
        if (reader_has_error(reader)) {
            uart_puts("Read error: ");
            uart_puts(reader_error_msg(reader));
            uart_puts("\r\n");
            reader_free(reader);
            object_release(all_forms);
            accum_len = 0; accum[0] = '\0';
            line_number++;
            continue;
        }
        reader_free(reader);

        /* Compile and run each form */
        size_t n_forms = vector_length(all_forms);
        for (size_t fi = 0; fi < n_forms; fi++) {
            Value form = vector_get(all_forms, fi);

            Compiler* compiler = compiler_new("<repl>");
            CompiledCode* code = compile(compiler, form);

            if (compiler_has_error(compiler)) {
                uart_puts("Compile error: ");
                uart_puts(compiler_error_msg(compiler));
                uart_puts("\r\n");
                compiled_code_free(code);
                compiler_free(compiler);
                continue;
            }
            compiler_free(compiler);

            int n_constants = (int)vector_length(code->constants);
            Value* constants = malloc((size_t)n_constants * sizeof(Value));
            for (int i = 0; i < n_constants; i++) {
                constants[i] = vector_get(code->constants, i);
            }
            for (int i = 0; i < n_constants; i++) {
                if (is_function(constants[i])) {
                    function_set_code(constants[i],
                                      code->bytecode, (int)code->code_size,
                                      constants, n_constants);
                    object_make_immortal(constants[i]);
                }
            }

            Value task_val = task_new_from_code(
                code->bytecode, (int)code->code_size,
                constants, n_constants, global_scheduler);
            Task* repl_task = task_get(task_val);
            scheduler_run_task_to_completion(global_scheduler, repl_task);

            if (repl_task->vm->error) {
                uart_puts("Error: ");
                uart_puts(repl_task->vm->error_msg);
                uart_puts("\r\n");
            } else if (!vm_stack_empty(repl_task->vm)) {
                Value result = repl_task->vm->stack[repl_task->vm->stack_pointer - 1];
                if (is_pointer(result)) object_retain(result);
                vm_pop(repl_task->vm);
                uart_puts("=> ");
                value_print_readable(result);
                uart_puts("\r\n");
                if (is_pointer(result)) object_release(result);
            }

            object_release(task_val);
            retain_unit(code, constants);
        }

        object_release(all_forms);
        accum_len = 0; accum[0] = '\0';
        line_number++;
    }

    uart_puts("\r\nGoodbye.\r\n");
}

/* ----------------------------------------------------------------
 * beeros_main — entry point, called from start.S
 * ---------------------------------------------------------------- */
void beeros_main(void) {
    uart_init(115200);
    uart_puts("\r\n[beeros] starting...\r\n");

    memory_init();
    symbol_init();
    namespace_init();   /* tries core.beer; silent no-op without FS */

#ifdef BEEROS_GFX
    uart_puts("[beeros] gfx init...\r\n");
    gfx_init_ramfb();
    gfx_register_natives();
    gfx_load_library();
    uart_puts("[beeros] gfx ready\r\n");
#endif

    uart_puts("[beeros] ready\r\n");

    run_repl();

    /* Halt */
    while (1) { __asm__ volatile("wfi"); }
}
