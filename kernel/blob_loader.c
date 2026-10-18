/* blob_loader.c — load embedded .beer libraries (core, mem, gfx, ...) at boot */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "blob_loader.h"
#include "core.h"
#include "hashmap.h"
#include "memory.h"
#include "namespace.h"
#include "symbol.h"
#include "vm.h"
#include "uart.h"

bool beer_load_blob(const unsigned char* data, unsigned int len,
                    const char* name, const char* home_ns) {
    char* src = malloc(len + 1);
    VM* vm = vm_new(16);                 /* only collects errors */
    if (!src || !vm) {
        uart_puts("[beeros] out of memory loading ");
        uart_puts(name);
        uart_puts("\r\n");
        free(src);
        if (vm) vm_free(vm);
        return false;
    }
    memcpy(src, data, len);
    src[len] = '\0';

    Value r = home_ns ? core_load_source_ns(vm, src, name, home_ns)
                      : core_load_source(vm, src, name);
    bool ok = !vm->error;
    if (!ok) {
        uart_puts("[beeros] ");
        uart_puts(vm->error_msg);
        uart_puts("\r\n");
    }
    if (is_pointer(r)) object_release(r);
    vm_free(vm);
    free(src);
    return ok;
}

bool beer_load_lib(const unsigned char* data, unsigned int len,
                   const char* name, const char* ns) {
    bool ok = beer_load_blob(data, len, name, ns);
    Namespace* core = namespace_registry_get_core(global_namespace_registry);
    Var* var = core ? namespace_lookup(core, symbol_intern("*loaded-libs*")) : NULL;
    Value libs = var ? var_get_value(var) : VALUE_NIL;
    if (ok && is_pointer(libs)) {
        Value m = hashmap_assoc(libs, keyword_intern(ns), VALUE_TRUE);
        var_set_value(var, m);
        object_release(m);
    }
    return ok;
}
