#include <__syscall.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE*   __openFiles[FOPEN_MAX];
uint8_t __openedFilesCount = 0;

static uint8_t __findOpenFileSlot() {
    uint8_t idx = 0;
    while (__openFiles[idx] != NULL) {
        if (idx >= FOPEN_MAX) {
            return (uint8_t)-1;
        }
        idx++;
    }
    return idx;
}

FILE* fopen(const char* __filename, const char* __modes) {
    bool hasRead, hasWrite, hasAppend, hasPlus;
    hasRead   = strchr(__modes, 'r') != NULL;
    hasWrite  = strchr(__modes, 'w') != NULL;
    hasAppend = strchr(__modes, 'a') != NULL;
    if (hasRead + hasWrite + hasAppend >= 2) {
        return NULL;
    }
    hasPlus         = strchr(__modes, '+') != NULL;
    uint32_t oFlags = 0;
    if (hasRead && !hasPlus) {
        oFlags |= O_RDONLY;
    }
    if (hasWrite && !hasPlus) {
        oFlags |= O_WRONLY;
    }
    if (hasAppend && !hasPlus) {
        oFlags |= O_WRONLY;
    }
    if (hasPlus) {
        oFlags |= O_RDWR;
    }
    if (hasAppend) {
        oFlags |= O_APPEND;
    }
    if (hasWrite) {
        oFlags |= O_TRUNC;
    }
    if (hasWrite || hasAppend) {
        oFlags |= O_CREAT;
    }
    uint64_t handle = syscall_execute(SYS_OPEN, __filename, oFlags);
    if (handle != 0) {
        return NULL;
    }
    FILE* stream = malloc(sizeof(FILE));
    if (stream == NULL) {
        syscall_execute(SYS_CLOSE, handle);
        return NULL;
    }
    stream->handle     = handle;
    stream->bufferIdx  = 0;
    stream->bufferMode = FILE_BUFFER_MODE_DIRECT;
    uint8_t openSlot   = __findOpenFileSlot();
    if (openSlot == (uint8_t)-1) {
        syscall_execute(SYS_CLOSE, stream->handle);
        free(stream);
        return NULL;
    }
    stream->openSlot      = openSlot;
    __openFiles[openSlot] = stream;
    __openedFilesCount++;
    return stream;
}

void __closeAllFiles() {
    while (__openedFilesCount > 0) {
        __openedFilesCount--;
        if (__openFiles[__openedFilesCount]) {
            fclose(__openFiles[__openedFilesCount]);
            __openFiles[__openedFilesCount] = NULL;
        }
    }
}