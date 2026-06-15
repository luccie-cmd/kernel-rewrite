#include <__syscall.h>
#include <stdio.h>

size_t fread(void* __ptr, size_t __size, size_t __n, FILE* __s) {
    size_t totalSize = __size * __n;
    if (totalSize == 0) {
        return 0;
    }
    fflush(__s);
    syscall_execute(SYS_READ, __s->handle, __ptr, totalSize);
    return __n;
}