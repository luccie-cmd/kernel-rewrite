#if !defined(__LIBC_STDLIB_H__)
#define __LIBC_STDLIB_H__
#include <stddef.h>
#include <stdint.h>

__attribute__((noreturn)) void exit(int status);
__attribute__((noreturn)) void abort();
void*                          malloc(size_t n);
void*                          realloc(void* ptr, size_t new_size);
void                           free(void* addr);
int                            atexit(void (*func)(void));
void*                          aligned_alloc(size_t alignment, size_t size);

#endif // __LIBC_STDLIB_H__
