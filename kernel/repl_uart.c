/* repl_uart.c — bare-metal Beerlang REPL over UART
 *
 * Replaces beerlang/src/repl/main.c for the beeros target.
 * Entry point: beeros_main(), called from platform/<board>/boot/start.S
 * after the stack and BSS are set up.
 *
 * Boot: core.beer and the beeros libraries come from embedded blobs
 * (beer_load_lib). Interrupts: Ctrl-\, a serial BREAK or (while a form
 * runs) Ctrl-C stop the form; see kernel/console.c.
 */

#include <stddef.h>
#include <stdio.h>
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
#include "bstring.h"

#include "mem_natives.h"
#include "blk_natives.h"
#include "blob_loader.h"
#include "beer_libs.h"
#include "console.h"
#include "core_beer_blob.h"

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
 * Runs scheduler ticks while waiting. Returns number of chars read, or
 * -1 if an interrupt key dropped the line.
 * ---------------------------------------------------------------- */
static int uart_readline(char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c;
        if (vm_interrupt_requested) {
            vm_interrupt_requested = 0;
            uart_puts("\r\n");
            return -1;
        }
        if (console_getc_nonblock(&c)) {
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
 * lineedit_read — line input through beer.lineedit (lib/lineedit.beer)
 *
 * The call form is compiled once and re-run for every input; the prompt
 * goes in through the var beer.lineedit/prompt. Returns the length read,
 * or -1 to use uart_readline: the editor isn't loaded, is disabled, or
 * just failed (then it is disabled so a broken edit can't lock the REPL).
 * ---------------------------------------------------------------- */
static CompiledCode* lineedit_code;
static Value*        lineedit_consts;
static int           lineedit_nconsts;

static Namespace* lineedit_ns(void) {
    return namespace_registry_get(global_namespace_registry, "beer.lineedit");
}

static bool lineedit_enabled(void) {
    Namespace* ns = lineedit_ns();
    if (!ns) return false;
    Var* v = namespace_lookup(ns, symbol_intern("enabled?"));
    if (!v) return false;
    Value e = var_get_value(v);
    return !is_nil(e) && !is_false(e);
}

static bool lineedit_compile(void) {
    if (lineedit_code) return true;
    Reader* r = reader_new("(beer.lineedit/read-form beer.lineedit/prompt)", "<lineedit>");
    if (!r) return false;
    Value form = reader_read(r);
    bool bad = reader_has_error(r);
    reader_free(r);
    if (bad) return false;
    Compiler* c = compiler_new("<lineedit>");
    CompiledCode* code = compile(c, form);
    bad = compiler_has_error(c);
    compiler_free(c);
    if (is_pointer(form)) object_release(form);
    if (bad) { compiled_code_free(code); return false; }
    lineedit_nconsts = (int)vector_length(code->constants);
    lineedit_consts = malloc((size_t)(lineedit_nconsts ? lineedit_nconsts : 1) * sizeof(Value));
    for (int i = 0; i < lineedit_nconsts; i++) {
        lineedit_consts[i] = vector_get(code->constants, i);
        if (is_pointer(lineedit_consts[i])) object_make_immortal(lineedit_consts[i]);
    }
    lineedit_code = code;
    return true;
}

static void lineedit_disable(const char* why) {
    uart_puts("\r\n[beeros] line editor failed: ");
    uart_puts(why);
    uart_puts("\r\n[beeros] using the built-in reader; (beer.lineedit/enable!) to retry\r\n");
    Namespace* ns = lineedit_ns();
    if (ns) namespace_define(ns, symbol_intern("enabled?"), VALUE_FALSE);
}

static int lineedit_read(const char* prompt, char* buf, int max) {
    if (!lineedit_enabled()) return -1;
    if (!lineedit_compile()) { lineedit_disable("cannot compile the call"); return -1; }

    Value p = string_from_cstr(prompt);
    namespace_define(lineedit_ns(), symbol_intern("prompt"), p);
    object_release(p);

    Value tv = task_new_from_code(lineedit_code->bytecode, (int)lineedit_code->code_size,
                                  lineedit_consts, lineedit_nconsts, global_scheduler);
    Task* t = task_get(tv);
    vm_interrupt_requested = 0;
    scheduler_run_task_to_completion(global_scheduler, t);

    int n = -1;
    if (t->vm->error && vm_is_interrupt(t->vm->thrown_exception)) {
        uart_puts("\r\n");          /* an interrupt key drops the line */
        n = 0;
    } else if (t->vm->error) {
        lineedit_disable(t->vm->error_msg);
    } else if (!is_string(t->result)) {
        lineedit_disable("read-form did not return a string");
    } else {
        size_t len = string_byte_length(t->result);
        if (len >= (size_t)max) {
            uart_puts("Error: input too long\r\n");
            len = 0;
        }
        memcpy(buf, string_cstr(t->result), len);
        buf[len] = '\0';
        n = (int)len;
    }
    object_release(tv);
    return n;
}

/* ----------------------------------------------------------------
 * recover — after an interrupted form: console back to the serial tty,
 * terminal attributes reset (lib/tty.beer, beer.tty/recover!)
 * ---------------------------------------------------------------- */
static void recover(void) {
    uart_puts("\r\n[beeros] interrupted\r\n");
    VM* vm = vm_new(16);
    core_load_source(vm, "(beer.tty/recover!)", "<recover>");
    if (vm->error) {
        uart_puts("[beeros] recover failed: ");
        uart_puts(vm->error_msg);
        uart_puts("\r\n");
    }
    vm_free(vm);
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
        /* Prompt: the line editor draws it itself */
        Namespace* cur_ns = namespace_registry_current(global_namespace_registry);
        char prompt[128];
        if (accum_len == 0) {
            snprintf(prompt, sizeof prompt, "%s:%d> ", cur_ns ? cur_ns->name : "beer", line_number);
        } else {
            strcpy(prompt, "...   ");
        }

        /* The editor returns whole forms, so only use it at the start of one */
        /* Ctrl-C is an editing key while reading, an interrupt otherwise */
        console_set_reading(true);
        int n = accum_len == 0 ? lineedit_read(prompt, input, INPUT_SIZE) : -1;
        bool whole_form = n > 0;   /* the editor already checked it (and knows \( etc.) */
        if (n < 0) {
            uart_puts(prompt);
            n = uart_readline(input, INPUT_SIZE);
            if (n < 0) { accum_len = 0; accum[0] = '\0'; n = 0; }
        }
        console_set_reading(false);
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

        if (!whole_form && !is_form_complete(accum)) continue;

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
                    /* Free symbols resolve in the defining ns, not the caller's */
                    Namespace* cur = namespace_registry_current(global_namespace_registry);
                    if (cur) function_set_ns_name(constants[i], cur->name);
                    object_make_immortal(constants[i]);
                }
            }

            Value task_val = task_new_from_code(
                code->bytecode, (int)code->code_size,
                constants, n_constants, global_scheduler);
            Task* repl_task = task_get(task_val);
            vm_interrupt_requested = 0;   /* keys pressed before the form don't stop it */
            scheduler_run_task_to_completion(global_scheduler, repl_task);

            bool interrupted = repl_task->vm->error &&
                               vm_is_interrupt(repl_task->vm->thrown_exception);
            if (interrupted) {
                object_release(task_val);
                retain_unit(code, constants);
                recover();
                break;                    /* the rest of the input goes too */
            } else if (repl_task->vm->error) {
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
    namespace_init_bare();   /* core.beer comes from the blob below, not a file */
    /* defn/ns/let/when/loop... are core.beer macros; mem.beer and gfx.beer need them */
    beer_load_lib(core_beer, core_beer_len, "lib/core.beer", "beer.core");
    uart_puts("[beeros] core macros loaded (embedded lib/core.beer)\r\n");

    mem_register_natives();
    mem_load_library();
    console_register_natives();
    console_init();          /* UART receive by interrupt: Ctrl-\ works from here on */

    blk_register_natives();
    blk_load_library();
    beer_load_libraries();

#ifdef BEEROS_GFX
    uart_puts("[beeros] gfx init...\r\n");
    gfx_init_ramfb();
    gfx_register_natives();
    gfx_load_library();
    uart_puts("[beeros] gfx ready\r\n");
#endif

    beer_run_init();

    uart_puts("[beeros] ready\r\n");

    run_repl();

    /* Halt */
    while (1) { __asm__ volatile("wfi"); }
}
