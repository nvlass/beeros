/* blob_loader.c — load embedded .beer libraries (core, mem, gfx) at boot */

#include <stdlib.h>
#include <string.h>

#include "blob_loader.h"
#include "namespace.h"
#include "reader.h"
#include "compiler.h"
#include "function.h"
#include "memory.h"
#include "scheduler.h"
#include "symbol.h"
#include "hashmap.h"
#include "task.h"
#include "vector.h"
#include "vm.h"
#include "uart.h"

static void report(const char* name, const char* kind, const char* msg) {
    uart_puts("[");
    uart_puts(name);
    uart_puts("] ");
    uart_puts(kind);
    uart_puts(" error: ");
    uart_puts(msg);
    uart_puts("\r\n");
}

static void run_form(Value form, const char* name) {
    Compiler* compiler = compiler_new(name);
    CompiledCode* code = compile(compiler, form);
    if (compiler_has_error(compiler)) {
        report(name, "compile", compiler_error_msg(compiler));
        compiled_code_free(code);
        compiler_free(compiler);
        return;
    }
    compiler_free(compiler);

    int nc = (int)vector_length(code->constants);
    Value* consts = malloc((size_t)nc * sizeof(Value));
    if (!consts) { compiled_code_free(code); return; }
    for (int i = 0; i < nc; i++) consts[i] = vector_get(code->constants, i);

    /* Read per form, not once per file: a file's (ns ...) switches it */
    Namespace* cur = namespace_registry_current(global_namespace_registry);
    for (int i = 0; i < nc; i++) {
        if (is_function(consts[i])) {
            function_set_code(consts[i], code->bytecode, (int)code->code_size,
                              consts, nc);
            /* Free symbols resolve in the defining ns, not the caller's */
            if (cur) function_set_ns_name(consts[i], cur->name);
            object_make_immortal(consts[i]);
        }
    }

    Value tv = task_new_from_code(code->bytecode, (int)code->code_size,
                                  consts, nc, global_scheduler);
    Task* t = task_get(tv);
    scheduler_run_task_to_completion(global_scheduler, t);
    if (t->vm->error) report(name, "runtime", t->vm->error_msg);
    object_release(tv);
    /* code and consts are never freed: functions defined here point into them */
}

/* Record ns in *loaded-libs* (beer.core), as require does after loading a
 * file, so (require 'beer.input) finds embedded namespaces already loaded */
static void mark_loaded(const char* ns_name) {
    Namespace* core = namespace_registry_get_core(global_namespace_registry);
    if (!core || !ns_name) return;
    Value sym = symbol_intern("*loaded-libs*");
    Var* var = namespace_lookup(core, sym);
    Value libs = var ? var_get_value(var) : VALUE_NIL;
    if (is_nil(libs)) {
        libs = hashmap_create_default();
        var = namespace_define(core, sym, libs);
        object_release(libs);
    }
    hashmap_set(libs, keyword_intern(ns_name), VALUE_TRUE);
}

void beer_load_blob(const unsigned char* data, unsigned int len,
                    const char* name, const char* home_ns) {
    Namespace* saved = namespace_registry_current(global_namespace_registry);
    if (home_ns) {
        Namespace* ns = namespace_registry_get_or_create(global_namespace_registry, home_ns);
        if (ns) namespace_registry_set_current(global_namespace_registry, ns);
    }

    char* src = malloc(len + 1);
    if (!src) goto restore;
    memcpy(src, data, len);
    src[len] = '\0';

    /* The reader wraps src without copying and reads it lazily, so src
     * must outlive reader_read_all, not just reader_new */
    Reader* reader = reader_new(src, name);
    if (!reader) { free(src); goto restore; }
    Value forms = reader_read_all(reader);
    bool failed = reader_has_error(reader);
    if (failed) report(name, "read", reader_error_msg(reader));
    reader_free(reader);
    free(src);

    if (!failed) {
        size_t n = vector_length(forms);
        for (size_t i = 0; i < n; i++) {
            run_form(vector_get(forms, i), name);
            /* A file's (ns ...) makes its namespace current */
            Namespace* cur = namespace_registry_current(global_namespace_registry);
            if (cur && cur != saved) mark_loaded(cur->name);
        }
    }
    object_release(forms);

restore:
    if (!saved) return;
    namespace_registry_set_current(global_namespace_registry, saved);
    /* A file's (ns ...) also set *ns* in beer.core; keep it in step, as in-ns would */
    Namespace* core = namespace_registry_get_core(global_namespace_registry);
    if (core) namespace_define(core, symbol_intern("*ns*"), symbol_intern(saved->name));
}
