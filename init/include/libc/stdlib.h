#if !defined(__LIBC_STDLIB_H__)
#define __LIBC_STDLIB_H__
#include <stddef.h>
#include <stdint.h>

typedef struct __MallocRegion {
    uint64_t               prePadding[5];
    size_t                 size;
    size_t                 freedSize;
    size_t                 allocSize;
    struct __MallocRegion* prev;
    struct __MallocRegion* next;
    uint8_t                free;
    uint8_t                padding[7];
    uint64_t               postPadding[5];
} __MallocRegion;

__attribute__((noreturn)) void exit(int status);
__attribute__((noreturn)) void abort();
void*                          malloc(size_t n);
void*                          realloc(void* ptr, size_t new_size);
void                           free(void* addr);
int                            atexit(void (*func)(void));

#endif // __LIBC_STDLIB_H__
