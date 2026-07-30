/* io_reactor_baremetal.c — bare-metal stub for beeros
 *
 * On bare metal there is no background pthread and no epoll/kqueue.
 * The scheduler runs cooperatively on a single core; I/O readiness is
 * handled by polling in the UART REPL loop and interrupt callbacks
 * (when a board HAL registers them). This file satisfies the linker.
 *
 * Pattern mirrors wasm/io_reactor_stub.c from the beerlang tree.
 */

#include "io_reactor.h"

struct IOReactor { int dummy; };

static struct IOReactor _reactor_singleton;

IOReactor* io_reactor_new(void) {
    return &_reactor_singleton;
}

void io_reactor_free(IOReactor* r) {
    (void)r;
}

void io_reactor_register(IOReactor* r, int fd, bool read, bool write, Task* task) {
    (void)r; (void)fd; (void)read; (void)write; (void)task;
}

void io_reactor_unregister(IOReactor* r, int fd) {
    (void)r; (void)fd;
}

int io_reactor_drain(IOReactor* r, Task** tasks_out, int max) {
    (void)r; (void)tasks_out; (void)max;
    return 0;
}
