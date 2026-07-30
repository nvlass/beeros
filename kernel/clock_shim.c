/* clock_shim.c — clock_gettime shim for bare metal
 *
 * The beerlang scheduler uses clock_gettime(CLOCK_MONOTONIC) for sleep timers.
 * On bare metal, we implement it using the board HAL's timer_read_us().
 */

#include <time.h>
#include "timer.h"

#define CLOCK_MONOTONIC 1

int clock_gettime(int clk_id, struct timespec* ts) {
    (void)clk_id;   /* only CLOCK_MONOTONIC is called; treat all the same */
    uint64_t us = timer_read_us();
    ts->tv_sec  = (time_t)(us / 1000000ULL);
    ts->tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
    return 0;
}
