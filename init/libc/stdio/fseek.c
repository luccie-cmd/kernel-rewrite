#include <__syscall.h>
#include <stdio.h>

int fseek(FILE* __stream, long __off, int __whence) {
    fflush(__stream);
    return (int)syscall_execute(SYS_SEEK, __stream->handle, __off, __whence);
}