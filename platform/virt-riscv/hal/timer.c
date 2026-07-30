/* timer.c — rdtime shim for qemu-system-riscv64 -M virt
 *
 * The virt machine's timebase runs at 10 MHz (QEMU default).
 * rdtime is an unprivileged CSR read — no M-mode setup required.
 */

#include "timer.h"

uint64_t timer_read_us(void) {
    uint64_t t;
    __asm__ volatile("rdtime %0" : "=r"(t));
    return t / 10;  /* 10 MHz → microseconds */
}
