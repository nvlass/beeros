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

#define LOAD(sym, path) beer_load_blob(sym, sym##_len, path, NULL)

void beer_load_libraries(void) {
    LOAD(lib_virtio_beer,       "lib/virtio.beer");
    LOAD(lib_virtio_blk_beer,   "lib/virtio_blk.beer");   /* attaches disks */
    LOAD(lib_ext2_beer,         "lib/ext2.beer");
    LOAD(lib_fs_beer,           "lib/fs.beer");           /* mounts / */
    LOAD(lib_ustar_beer,        "lib/ustar.beer");
    LOAD(lib_pkg_beer,          "lib/pkg.beer");          /* /pkg tars onto *load-path* */
    LOAD(lib_input_beer,        "lib/input.beer");
    LOAD(lib_virtio_input_beer, "lib/virtio_input.beer"); /* starts input tasks */
    LOAD(lib_tty_beer,          "lib/tty.beer");
    LOAD(lib_lineedit_beer,     "lib/lineedit.beer");     /* the REPL reads through it */

    /* Last, so it can use everything above */
    static const unsigned char run_init[] = "(beer.fs/run-init!)";
    beer_load_blob(run_init, sizeof run_init - 1, "<boot>", NULL);
}
