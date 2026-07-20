#include <__syscall.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int fflush(FILE* __f) {
    if (__f->bufferIdx > 0) {
        if (!syscall_execute(SYS_WRITE, __f->handle, __f->buffer, __f->bufferIdx)) {
            return errno;
        }
        __f->bufferIdx = 0;
    }
    memset(__f->buffer, 0, sizeof(__f->buffer));
    return 0;
}