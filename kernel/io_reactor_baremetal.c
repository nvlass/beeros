/* io_reactor_baremetal.c — bare-metal stub for beeros
 *
 * On bare metal there is no background pthread and no epoll/kqueue.
 * The scheduler runs cooperatively on a single core; I/O readiness is
 * handled by polling in the UART REPL loop and interrupt callbacks
 * (when a board HAL registers them). No beeros native ever parks a task
 * on an fd (tcp.c/udp.c are excluded from the build, and UART I/O never
 * blocks), so every task-waiting function here is unreachable in
 * practice — they exist to satisfy the linker and stay correct if that
 * ever changes.
 *
 * Pattern mirrors wasm/io_reactor_stub.c from the beerlang tree.
 */

#include "io_reactor.h"
#include <unistd.h>   /* usleep — busy-waits on timer_read_us(), see syscall_stubs.c */

struct IOReactor { int dummy; };

static struct IOReactor _reactor_singleton;

IOReactor* io_reactor_new(void) {
    return &_reactor_singleton;
}

void io_reactor_free(IOReactor* r) {
    (void)r;
}

void io_reactor_wait(IOReactor* r, int fd, bool write, Task* task) {
    (void)r; (void)fd; (void)write; (void)task;
}

void io_reactor_cancel_fd(IOReactor* r, Scheduler* sched, int fd) {
    (void)r; (void)sched; (void)fd;
}

void io_reactor_forget(IOReactor* r, Task* task) {
    (void)r; (void)task;
}

int io_reactor_waiting(IOReactor* r) {
    (void)r;
    return 0;
}

int io_reactor_poll(IOReactor* r, Scheduler* sched, int timeout_ms,
                     int watch_fd, bool* watch_ready) {
    (void)r; (void)sched; (void)watch_fd;
    if (watch_ready) *watch_ready = false;
    /* Never actually reached with timeout_ms < 0 by the scheduler's own
     * logic (it only waits forever when something is watching an fd or a
     * timer is pending, neither of which beeros ever has) — but never
     * block indefinitely here regardless, since nothing could wake us. */
    if (timeout_ms > 0) usleep((useconds_t)timeout_ms * 1000);
    return 0;
}
