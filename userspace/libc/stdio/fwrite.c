#include <__syscall.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

size_t fwrite(const void* __ptr, size_t __size, size_t __n, FILE* __s) {
    size_t totalSize = __size * __n;
    if (totalSize == 0) {
        return 0;
    }
    while (totalSize >= 8) {
        if (__s->bufferIdx >= sizeof(__s->buffer)) {
            if (!syscall_execute(SYS_WRITE, __s->handle, __s->buffer, __s->bufferIdx)) {
                return (size_t)errno;
            }
            __s->bufferIdx = 0;
            memset(__s->buffer, 0, sizeof(__s->buffer));
        }
        memcpy(__s->buffer + __s->bufferIdx, __ptr, 8);
        totalSize -= 8;
        __ptr = (void*)((uint8_t*)__ptr + 8);
        __s->bufferIdx += 8;
    }
    while (totalSize) {
        if (__s->bufferIdx >= sizeof(__s->buffer)) {
            if (!syscall_execute(SYS_WRITE, __s->handle, __s->buffer, __s->bufferIdx)) {
                return (size_t)errno;
            }
            __s->bufferIdx = 0;
            memset(__s->buffer, 0, sizeof(__s->buffer));
        }
        memcpy(__s->buffer + __s->bufferIdx, __ptr, 1);
        totalSize -= 1;
        __ptr = (void*)((uint8_t*)__ptr + 1);
        __s->bufferIdx += 1;
    }
    if (__s->bufferMode == FILE_BUFFER_MODE_DIRECT) {
        if (!syscall_execute(SYS_WRITE, __s->handle, __s->buffer, __s->bufferIdx)) {
            return (size_t)errno;
        }
        __s->bufferIdx = 0;
        memset(__s->buffer, 0, sizeof(__s->buffer));
    } else if (__s->bufferMode == FILE_BUFFER_MODE_FULL) {
        if (__s->bufferIdx >= sizeof(__s->buffer)) {
            if (!syscall_execute(SYS_WRITE, __s->handle, __s->buffer, __s->bufferIdx)) {
                return (size_t)errno;
            }
            __s->bufferIdx = 0;
            memset(__s->buffer, 0, sizeof(__s->buffer));
        }
    }
    return __n;
}