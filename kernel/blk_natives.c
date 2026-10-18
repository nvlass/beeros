/* blk_natives.c — beer.blk.native: the C virtio-blk driver's primitives
 *
 * Deliberately thin: validate arguments, call the driver, and return nil
 * or an error keyword. Turning those into exceptions, building device
 * maps and keeping the registry all live in lib/blk.beer, so that policy
 * stays editable from the REPL.
 *
 *   (probe!)                      → (re)initialise all devices; count
 *   (count)                       → number of virtio-blk devices
 *   (sectors id) (read-only? id) (can-flush? id)
 *   (read!  id lba n buf off)     → nil | :io-error :timeout :out-of-range ...
 *   (write! id lba n buf off)     → same; buf is a ByteBuffer
 *   (flush! id)                   → same
 */

#include <stdint.h>

#include "blk_natives.h"
#include "blob_loader.h"
#include "virtio_blk.h"
#include "bytebuffer.h"
#include "memory.h"
#include "namespace.h"
#include "native.h"
#include "symbol.h"
#include "value.h"
#include "vm.h"

#include "blk_beer_blob.h"

static void reg(Namespace* ns, const char* name, NativeFn fn) {
    Value f = native_function_new(-1, fn, name);
    namespace_define(ns, symbol_intern(name), f);
    object_release(f);
}

static Value status_value(vblk_status s) {
    switch (s) {
        case VBLK_OK:              return VALUE_NIL;
        case VBLK_ERR_IO:          return keyword_intern("io-error");
        case VBLK_ERR_UNSUPPORTED: return keyword_intern("unsupported");
        case VBLK_ERR_TIMEOUT:     return keyword_intern("timeout");
        case VBLK_ERR_RANGE:       return keyword_intern("out-of-range");
        case VBLK_ERR_READ_ONLY:   return keyword_intern("read-only");
        case VBLK_ERR_NO_DEVICE:   return keyword_intern("no-device");
    }
    return keyword_intern("io-error");
}

static bool fixnum_arg(Value v, int64_t min, int64_t* out) {
    if (!is_fixnum(v) || untag_fixnum(v) < min) return false;
    *out = untag_fixnum(v);
    return true;
}

static Value native_probe(VM* vm, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
    return make_fixnum(virtio_blk_probe());
}

static Value native_count(VM* vm, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
    return make_fixnum(virtio_blk_count());
}

static Value native_sectors(VM* vm, int argc, Value* argv) {
    (void)vm;
    int64_t id;
    if (argc != 1 || !fixnum_arg(argv[0], 0, &id)) return VALUE_NIL;
    return make_fixnum((int64_t)virtio_blk_sectors((int)id));
}

static Value native_read_only(VM* vm, int argc, Value* argv) {
    (void)vm;
    int64_t id;
    if (argc != 1 || !fixnum_arg(argv[0], 0, &id)) return VALUE_NIL;
    return virtio_blk_read_only((int)id) ? VALUE_TRUE : VALUE_FALSE;
}

static Value native_can_flush(VM* vm, int argc, Value* argv) {
    (void)vm;
    int64_t id;
    if (argc != 1 || !fixnum_arg(argv[0], 0, &id)) return VALUE_NIL;
    return virtio_blk_can_flush((int)id) ? VALUE_TRUE : VALUE_FALSE;
}

/* (read!/write! id lba n buf off) — buf must hold n sectors from off */
static Value rw(int argc, Value* argv, bool write) {
    int64_t id, lba, n, off;
    if (argc != 5 || !fixnum_arg(argv[0], 0, &id) || !fixnum_arg(argv[1], 0, &lba)
        || !fixnum_arg(argv[2], 1, &n) || !is_bytebuffer(argv[3])
        || !fixnum_arg(argv[4], 0, &off))
        return keyword_intern("bad-args");
    uint64_t bytes = (uint64_t)n * VIRTIO_BLK_SECTOR;
    if ((uint64_t)off + bytes > bytebuffer_capacity(argv[3]))
        return keyword_intern("buffer-too-small");
    uint8_t* buf = bytebuffer_data(argv[3]) + off;
    return status_value(virtio_blk_rw((int)id, write, (uint64_t)lba, (uint32_t)n, buf));
}

static Value native_read(VM* vm, int argc, Value* argv)  { (void)vm; return rw(argc, argv, false); }
static Value native_write(VM* vm, int argc, Value* argv) { (void)vm; return rw(argc, argv, true); }

static Value native_flush(VM* vm, int argc, Value* argv) {
    (void)vm;
    int64_t id;
    if (argc != 1 || !fixnum_arg(argv[0], 0, &id)) return keyword_intern("bad-args");
    return status_value(virtio_blk_flush((int)id));
}

/* Doesn't probe: the C driver only touches the hardware if lib/virtio_blk.beer
 * picks it at boot (or (beer.blk/use-native-driver!) is called later). */
void blk_register_natives(void) {
    Namespace* ns = namespace_registry_get_or_create(global_namespace_registry,
                                                     "beer.blk.native");
    reg(ns, "probe!",     native_probe);
    reg(ns, "count",      native_count);
    reg(ns, "sectors",    native_sectors);
    reg(ns, "read-only?", native_read_only);
    reg(ns, "can-flush?", native_can_flush);
    reg(ns, "read!",      native_read);
    reg(ns, "write!",     native_write);
    reg(ns, "flush!",     native_flush);
}

void blk_load_library(void) {
    beer_load_lib(lib_blk_beer, lib_blk_beer_len, "lib/blk.beer", "beer.blk");
}
