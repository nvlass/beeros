/* posix_stubs.c — POSIX stubs for bare-metal beeros
 *
 * stdout/stderr writes are routed to the UART so that all beerlang
 * internal output (value_print_readable, error messages) appears on
 * the serial console without any code changes in the core.
 *
 * Functions are replaced one-by-one as real HAL components are added
 * (fopen/fread/fwrite replaced when LittleFS is wired in).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "uart.h"

/* ---- FILE type (opaque to callers) ---- */

struct _FILE { int fd; };   /* fd: 0=stdin 1=stdout 2=stderr */
typedef struct _FILE FILE;

static FILE _stdin  = {0};
static FILE _stdout = {1};
static FILE _stderr = {2};

FILE* stdin  = &_stdin;
FILE* stdout = &_stdout;
FILE* stderr = &_stderr;

static void _write_uart(const char* buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\n') uart_putc('\r');
        uart_putc(buf[i]);
    }
}

/* ---- Environment ---- */

char* getenv(const char* name) {
    (void)name;
    return NULL;
}

/* ---- File I/O ---- */

FILE* fopen(const char* path, const char* mode) {
    (void)path; (void)mode;
    return NULL;
}

int fclose(FILE* f) {
    (void)f;
    return 0;
}

size_t fread(void* buf, size_t size, size_t n, FILE* f) {
    (void)buf; (void)size; (void)n; (void)f;
    return 0;
}

size_t fwrite(const void* buf, size_t size, size_t nmemb, FILE* f) {
    if (f && (f->fd == 1 || f->fd == 2)) {
        _write_uart((const char*)buf, size * nmemb);
    }
    return nmemb;
}

char* fgets(char* s, int n, FILE* f) {
    (void)s; (void)n; (void)f;
    return NULL;
}

int fflush(FILE* f) {
    (void)f;
    return 0;
}

int fputc(int c, FILE* f) {
    if (f && (f->fd == 1 || f->fd == 2)) {
        if (c == '\n') uart_putc('\r');
        uart_putc((char)c);
    }
    return c;
}

int fputs(const char* s, FILE* f) {
    if (f && (f->fd == 1 || f->fd == 2)) {
        while (*s) { fputc(*s++, f); }
    }
    return 0;
}

int fgetc(FILE* f) {
    (void)f;
    return -1;
}

int feof(FILE* f)   { (void)f; return 1; }
int ferror(FILE* f) { (void)f; return 0; }

long ftell(FILE* f)                        { (void)f; return -1; }
int  fseek(FILE* f, long off, int w)       { (void)f;(void)off;(void)w; return -1; }
void rewind(FILE* f)                       { (void)f; }

/* ---- Minimal printf/fprintf (write to UART; no full format support) ---- */

static void _put_str(const char* s)   { while (*s) { if (*s=='\n') uart_putc('\r'); uart_putc(*s++); } }
static void _put_int(long n)          { if(n<0){uart_putc('-');n=-n;} char b[20];int i=0;if(!n){uart_putc('0');return;}while(n){b[i++]='0'+(n%10);n/=10;}while(i--)uart_putc(b[i]); }
static void _put_uint_hex(unsigned long n) { const char* h="0123456789abcdef"; char b[16]; int i=0; if(!n){uart_putc('0');return;} while(n){b[i++]=h[n&0xf];n>>=4;} while(i--)uart_putc(b[i]); }

static int _vfprintf(FILE* f, const char* fmt, va_list ap) {
    int count = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (f && (f->fd==1||f->fd==2)) fputc(*fmt, f);
            count++;
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 'd': case 'i': _put_int(va_arg(ap, int)); break;
        case 'u':           _put_uint_hex(va_arg(ap, unsigned)); break;
        case 'x':           _put_uint_hex(va_arg(ap, unsigned)); break;
        case 's': { const char* s = va_arg(ap, const char*); if(s) _put_str(s); break; }
        case 'c':           fputc(va_arg(ap, int), f); break;
        case 'p':           _put_str("0x"); _put_uint_hex((unsigned long)(uintptr_t)va_arg(ap, void*)); break;
        case '%':           fputc('%', f); break;
        default:            fputc('%', f); fputc(*fmt, f); break;
        }
    }
    return count;
}

int fprintf(FILE* f, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = _vfprintf(f, fmt, ap); va_end(ap); return r;
}

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = _vfprintf(stdout, fmt, ap); va_end(ap); return r;
}

int vfprintf(FILE* f, const char* fmt, va_list ap) {
    return _vfprintf(f, fmt, ap);
}

/* ---- Process / signal ---- */

void exit(int code) {
    (void)code;
    uart_puts("\r\n[beeros] halted\r\n");
    while (1) { __asm__ volatile("wfi"); }
}

void abort(void) {
    uart_puts("\r\n[beeros] abort!\r\n");
    while (1) { __asm__ volatile("wfi"); }
}

/* ---- Filesystem (stubbed until LittleFS) ---- */

int stat(const char* path, void* buf)         { (void)path;(void)buf; return -1; }
int mkdir(const char* path, unsigned int mode) { (void)path;(void)mode; return -1; }
