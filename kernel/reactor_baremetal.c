/* reactor_baremetal.c — bare-metal stub for beeros
 *
 * No epoll/kqueue. On bare metal, I/O readiness is polled directly
 * (e.g. uart_getc_nonblock) or driven by interrupts via the board HAL.
 * reactor_poll() returns immediately with zero events; the REPL loop
 * and scheduler tick handle progress.
 */

#include "reactor.h"

struct Reactor { int dummy; };

static struct Reactor _reactor_singleton;

Reactor* reactor_new(void) {
    return &_reactor_singleton;
}

void reactor_free(Reactor* r) {
    (void)r;
}

int reactor_set(Reactor* r, int fd, bool read, bool write) {
    (void)r; (void)fd; (void)read; (void)write;
    return 0;
}

int reactor_poll(Reactor* r, ReactorEvent* out, int max_events, int timeout_ms) {
    (void)r; (void)out; (void)max_events; (void)timeout_ms;
    return 0;
}
