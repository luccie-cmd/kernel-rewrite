#include <__syscall.h>
#include <libc.h>
#include <stdio.h>
#include <stdlib.h>

int fclose(FILE* __f) {
    if (fflush(__f) == EOF) {
        return EOF;
    }
    syscall_execute(SYS_CLOSE, __f->handle);
    __openFiles[__f->openSlot] = NULL;
    __openedFilesCount--;
    free(__f);
    return 0;
}