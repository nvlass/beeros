/* malloc.c — size-class allocator, replacing picolibc's
 *
 * picolibc's malloc keeps one free list sorted by address, so free() walks
 * it: O(free blocks). beerlang allocates and frees small objects all the
 * time, and that walk made an assoc on an 80-element vector cost ~0.4 ms.
 *
 * Here every block has a 16-byte header naming its size class; a freed
 * block goes on its class's list and malloc takes from it: O(1), no
 * splitting or coalescing. Classes: multiples of 16 up to 2 KiB, then
 * powers of two. Memory comes from _sbrk and is never returned. Defining
 * malloc/free/realloc/calloc here keeps picolibc's from being linked.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void* _sbrk(ptrdiff_t incr);

#define ALIGN        16
#define SMALL_MAX    2048                    /* small classes: 16, 32, ... 2048 */
#define SMALL_COUNT  (SMALL_MAX / ALIGN)
#define LARGE_COUNT  20                      /* 4 KiB << 0 .. 19: up to 2 GiB */
#define CLASSES      (SMALL_COUNT + LARGE_COUNT)

typedef struct {
    uint64_t cls;        /* size class */
    uint64_t offset;     /* aligned_alloc: bytes back to the block's header */
} Header;

static void* free_lists[CLASSES];

static size_t class_of(size_t n) {
    if (n <= SMALL_MAX) return n == 0 ? 0 : (n - 1) / ALIGN;
    size_t c = SMALL_COUNT, size = 2 * SMALL_MAX;
    while (size < n && c < CLASSES - 1) { size <<= 1; c++; }
    return size < n ? CLASSES : c;
}

static size_t class_size(size_t c) {
    return c < SMALL_COUNT ? (c + 1) * ALIGN : (size_t)(2 * SMALL_MAX) << (c - SMALL_COUNT);
}

static Header* header_of(void* p) {
    Header* h = (Header*)p - 1;
    return h->offset ? (Header*)((char*)h - h->offset) : h;
}

void* malloc(size_t n) {
    size_t c = class_of(n);
    if (c >= CLASSES) { errno = ENOMEM; return NULL; }
    Header* h = free_lists[c];
    if (h) {
        free_lists[c] = *(void**)(h + 1);
    } else {
        h = _sbrk((ptrdiff_t)(sizeof(Header) + class_size(c)));
        if (h == (void*)-1) { errno = ENOMEM; return NULL; }
        h->cls = c;
    }
    h->offset = 0;
    return h + 1;
}

void free(void* p) {
    if (!p) return;
    Header* h = header_of(p);
    h->offset = 0;
    *(void**)(h + 1) = free_lists[h->cls];
    free_lists[h->cls] = h;
}

static size_t usable(void* p) {
    Header* h = header_of(p);
    return class_size(h->cls) - (size_t)((char*)p - (char*)(h + 1));
}

size_t malloc_usable_size(void* p) { return p ? usable(p) : 0; }

void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    size_t have = usable(p);
    if (n <= have) return p;
    void* q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, have);
    free(p);
    return q;
}

void* calloc(size_t count, size_t size) {
    size_t n;
    if (__builtin_mul_overflow(count, size, &n)) { errno = ENOMEM; return NULL; }
    void* p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void* aligned_alloc(size_t align, size_t n) {
    if (align <= ALIGN) return malloc(n);
    char* base = malloc(n + align);
    if (!base) return NULL;
    char* p = (char*)(((uintptr_t)base + align - 1) & ~(uintptr_t)(align - 1));
    if (p != base) {
        Header* h = (Header*)p - 1;          /* inside the block: align > 16 */
        h->offset = (uint64_t)((char*)h - ((char*)base - sizeof(Header)));
    }
    return p;
}

void* memalign(size_t align, size_t n) { return aligned_alloc(align, n); }

int posix_memalign(void** out, size_t align, size_t n) {
    void* p = aligned_alloc(align, n);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}
