/* timer.c — ARM Generic Timer for qemu-system-aarch64 -M virt
 *
 * CNTPCT_EL0: physical counter (accessible from EL1).
 * CNTFRQ_EL0: counter frequency in Hz (set by firmware / QEMU).
 * QEMU -M virt typically programs 62,500,000 Hz.
 */

#include "timer.h"

uint64_t timer_read_us(void) {
    uint64_t cnt, freq;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    /* cnt / (freq / 1e6) — avoid float; freq always >= 1 MHz on this target */
    return cnt / (freq / 1000000ULL);
}
