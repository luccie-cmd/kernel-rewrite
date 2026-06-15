#include <libc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE* stdout;

void __libc_init() {
    __malloc_init();
    stdout = malloc(sizeof(FILE));
    if (stdout == NULL) {
        abort();
    }
    stdout->handle     = 1;
    stdout->bufferIdx  = 0;
    stdout->bufferMode = FILE_BUFFER_MODE_DIRECT;
    memset(stdout->buffer, 0, sizeof(stdout->buffer));
    atexit(__closeAllFiles);
}

void __libc_deinit() {
    free(stdout);
    __malloc_deinit();
}