#include <__syscall.h>
#include <stdio.h>

long ftell(FILE* __stream) {
    getinfo_structure infoStruct;
    if (syscall_execute(SYS_GETINFO, __stream->handle, &infoStruct) != 0) {
        return (long)-1;
    }
    return infoStruct.currentOffset;
}