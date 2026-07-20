#include <__syscall.h>
#include <errno.h>
#include <stdio.h>

long ftell(FILE* __stream) {
    getinfo_structure infoStruct;
    if (syscall_execute(SYS_GETINFO, __stream->handle, &infoStruct) != 0) {
        return (long)errno;
    }
    return infoStruct.currentOffset;
}