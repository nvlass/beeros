/* syscall_stubs.c — newlib/picolibc syscall layer for bare-metal beeros
 *
 * These are the ~10 low-level hooks that picolibc calls into the "OS".
 * All I/O is routed to the UART; everything else returns an appropriate stub.
 *
 * To add filesystem support later, replace the file-descriptor functions
 * with real LittleFS calls — nothing else in the system needs changing.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include "uart.h"

/* ── I/O ─────────────────────────────────────────────────────────────── */

int _write(int fd, const char *buf, int count) {
    if (fd == 1 || fd == 2) {
        for (int i = 0; i < count; i++) {
            if (buf[i] == '\n') uart_putc('\r');
            uart_putc(buf[i]);
        }
        return count;
    }
    errno = EBADF;
    return -1;
}

int _read(int fd, char *buf, int count) {
    if (fd == 0) {
        for (int i = 0; i < count; i++) {
            buf[i] = uart_getc();
            if (buf[i] == '\n') return i + 1;
        }
        return count;
    }
    errno = EBADF;
    return -1;
}

/* ── heap ────────────────────────────────────────────────────────────── */

/* picolibc's malloc calls _sbrk to grow the heap.
 * We give it a 16 MB static region between BSS end and the stack. */
extern char _end[];           /* defined by the linker script (end of BSS) */
extern char _stack_top[];     /* defined by the linker script */
#define HEAP_LIMIT ((uintptr_t)_stack_top - 0x40000) /* leave 256 KB for stack */

void *_sbrk(ptrdiff_t incr) {
    static char *brk = NULL;
    if (!brk) brk = _end;
    char *prev = brk;
    if ((uintptr_t)(brk + incr) > HEAP_LIMIT) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += incr;
    return prev;
}

/* ── process ─────────────────────────────────────────────────────────── */

void _exit(int status) {
    (void)status;
    uart_puts("\r\n[beeros] halted\r\n");
    for (;;) __asm__ volatile("wfi");
}

int _kill(int pid, int sig) {
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void) { return 1; }

/* ── file descriptor stubs ───────────────────────────────────────────── */

int _close(int fd)                           { (void)fd; return 0; }
int _isatty(int fd)                          { return (fd >= 0 && fd <= 2) ? 1 : 0; }
off_t _lseek(int fd, off_t off, int whence) { (void)fd;(void)off;(void)whence; errno=ESPIPE; return -1; }

int _fstat(int fd, struct stat *st) {
    (void)fd;
    st->st_mode = S_IFCHR;   /* character device — enables line-buffered stdio */
    return 0;
}

int _open(const char *path, int flags, int mode) {
    (void)path;(void)flags;(void)mode;
    errno = ENOSYS;
    return -1;
}

/* ── time ────────────────────────────────────────────────────────────── */

/* picolibc's clock_gettime ultimately calls _gettimeofday.
 * timer_read_us() is the platform HAL microsecond counter. */
extern uint64_t timer_read_us(void);

struct timeval { long tv_sec; long tv_usec; };
struct timezone { int tz_minuteswest; int tz_dsttime; };

int _gettimeofday(struct timeval *tv, struct timezone *tz) {
    (void)tz;
    if (tv) {
        uint64_t us = timer_read_us();
        tv->tv_sec  = (long)(us / 1000000ULL);
        tv->tv_usec = (long)(us % 1000000ULL);
    }
    return 0;
}
