/* beer_libs.c — embedded pure-beerlang libraries (no natives of their own).
 * Add a library here and to BEER_LIBS in the Makefile. */

#include <stddef.h>

#include "beer_libs.h"
#include "blob_loader.h"

#include "virtio_beer_blob.h"
#include "virtio_blk_beer_blob.h"
#include "ext2_beer_blob.h"
#include "fs_beer_blob.h"
#include "ustar_beer_blob.h"
#include "pkg_beer_blob.h"
#include "input_beer_blob.h"
#include "virtio_input_beer_blob.h"
#include "tty_beer_blob.h"
#include "lineedit_beer_blob.h"

#define LOAD(sym, path, ns) beer_load_lib(sym, sym##_len, path, ns)

void beer_load_libraries(void) {
    LOAD(lib_virtio_beer,       "lib/virtio.beer",       "beer.virtio");
    LOAD(lib_virtio_blk_beer,   "lib/virtio_blk.beer",   "beer.virtio-blk");   /* attaches disks */
    LOAD(lib_ext2_beer,         "lib/ext2.beer",         "beer.ext2");
    LOAD(lib_fs_beer,           "lib/fs.beer",           "beer.fs");           /* mounts / */
    LOAD(lib_ustar_beer,        "lib/ustar.beer",        "beer.ustar");
    LOAD(lib_pkg_beer,          "lib/pkg.beer",          "beer.pkg");          /* /pkg tars onto *load-path* */
    LOAD(lib_input_beer,        "lib/input.beer",        "beer.input");
    LOAD(lib_virtio_input_beer, "lib/virtio_input.beer", "beer.virtio-input"); /* starts input tasks */
    LOAD(lib_tty_beer,          "lib/tty.beer",          "beer.tty");
    LOAD(lib_lineedit_beer,     "lib/lineedit.beer",     "beer.lineedit");     /* the REPL reads through it */
}

/* /etc/init.beer, last at boot (after gfx too) so it can use everything */
void beer_run_init(void) {
    static const unsigned char run_init[] = "(beer.fs/run-init!)";
    beer_load_blob(run_init, sizeof run_init - 1, "<boot>", NULL);
}
