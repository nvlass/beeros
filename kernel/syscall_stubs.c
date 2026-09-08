/* syscall_stubs.c — picolibc OS layer for bare-metal beeros
 *
 * picolibc uses a callback-based FILE struct (struct __file with put/get
 * function pointers) rather than POSIX file descriptors for its basic I/O.
 * We define stdout/stderr/stdin here backed by the UART callbacks.
 *
 * POSIX fd operations (open/read/write/close) are still stubbed for
 * beerlang code that tries to do file I/O — they just return ENOSYS.
 * The REPL and printf/scanf work entirely through the FILE callbacks.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include "uart.h"

/* ── picolibc FILE callbacks ─────────────────────────────────────────── */

static int _uart_put(char c, FILE *f) {
    (void)f;
    if (c == '\n') uart_putc('\r');
    uart_putc(c);
    return 0;
}

static int _uart_get(FILE *f) {
    (void)f;
    return (unsigned char)uart_getc();
}

static int _uart_flush(FILE *f) { (void)f; return 0; }

/* picolibc's FDEV_SETUP_STREAM initialises struct __file directly */
static FILE _stdout_file = FDEV_SETUP_STREAM(_uart_put, NULL,      _uart_flush, _FDEV_SETUP_WRITE);
static FILE _stderr_file = FDEV_SETUP_STREAM(_uart_put, NULL,      _uart_flush, _FDEV_SETUP_WRITE);
static FILE _stdin_file  = FDEV_SETUP_STREAM(NULL,      _uart_get, NULL,        _FDEV_SETUP_READ);

FILE * const stdout = &_stdout_file;
FILE * const stderr = &_stderr_file;
FILE * const stdin  = &_stdin_file;

/* ── heap: picolibc's fallback sbrk uses __heap_start / __heap_end ───── */
/* These linker symbols are defined in each board's .ld file.             */
/* Our own _sbrk delegates to picolibc's __heap_start/__heap_end model.  */

extern char __heap_start[];
extern char __heap_end[];

void *_sbrk(ptrdiff_t incr) {
    static char *brk = NULL;
    if (!brk) brk = __heap_start;
    char *prev = brk;
    if (brk + incr > __heap_end) {
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

int _kill(int pid, int sig) { (void)pid;(void)sig; errno=EINVAL; return -1; }
int _getpid(void)           { return 1; }

/* ── POSIX fd stubs (required by picolibc's fopen/fdopen internals) ──── */

int   open(const char *p, int f, ...)      { (void)p;(void)f; errno=ENOSYS; return -1; }
int   close(int fd)                        { (void)fd; return 0; }
int   _close(int fd)                       { (void)fd; return 0; }

ssize_t read(int fd, void *buf, size_t n) {
    if (fd == 0) {
        char *b = buf; size_t i;
        for (i=0;i<n;i++) { b[i]=uart_getc(); if(b[i]=='\n'){i++;break;} }
        return (ssize_t)i;
    }
    errno=EBADF; return -1;
}

ssize_t write(int fd, const void *buf, size_t n) {
    if (fd==1||fd==2) {
        const char *b=buf;
        for (size_t i=0;i<n;i++) { if(b[i]=='\n') uart_putc('\r'); uart_putc(b[i]); }
        return (ssize_t)n;
    }
    errno=EBADF; return -1;
}

off_t lseek(int fd, off_t off, int w)      { (void)fd;(void)off;(void)w; errno=ESPIPE; return -1; }
off_t _lseek(int fd, off_t off, int w)     { return lseek(fd,off,w); }

int _write(int fd, const char *buf, int n) { return (int)write(fd, buf, (size_t)n); }
int _read(int fd, char *buf, int n)        { return (int)read(fd, buf, (size_t)n); }

int _fstat(int fd, struct stat *st) {
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd) { return (fd>=0 && fd<=2) ? 1 : 0; }
int _open(const char *p, int f, int m)                      { (void)p;(void)f;(void)m; errno=ENOSYS; return -1; }
int execve(const char *p, char *const v[], char *const e[]) { (void)p;(void)v;(void)e; errno=ENOSYS; return -1; }

/* ── time ────────────────────────────────────────────────────────────── */

extern uint64_t timer_read_us(void);

int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) {
        uint64_t us = timer_read_us();
        tv->tv_sec  = (time_t)(us / 1000000ULL);
        tv->tv_usec = (suseconds_t)(us % 1000000ULL);
    }
    return 0;
}

int clock_gettime(clockid_t clk, struct timespec *ts) {
    (void)clk;
    uint64_t us = timer_read_us();
    ts->tv_sec  = (time_t)(us / 1000000ULL);
    ts->tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    uint64_t us = (uint64_t)req->tv_sec * 1000000ULL + (uint64_t)req->tv_nsec / 1000ULL;
    uint64_t end = timer_read_us() + us;
    while (timer_read_us() < end) __asm__ volatile("nop");
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}

int usleep(useconds_t us) {
    uint64_t end = timer_read_us() + us;
    while (timer_read_us() < end) __asm__ volatile("nop");
    return 0;
}

/* ── file-system stubs (no VFS yet) ─────────────────────────────────── */

int fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; errno = ENOSYS; return -1; }
int stat(const char *path, struct stat *st) { (void)path; (void)st; errno = ENOSYS; return -1; }

DIR *opendir(const char *name)       { (void)name; errno = ENOSYS; return NULL; }
struct dirent *readdir(DIR *d)       { (void)d; errno = ENOSYS; return NULL; }
int closedir(DIR *d)                 { (void)d; errno = ENOSYS; return -1; }

/* ── net/shell registration stubs (excluded from build) ─────────────── */

void core_register_tcp(void)   {}
void core_register_udp(void)   {}
void core_register_shell(void) {}
