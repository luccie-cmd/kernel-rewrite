#include <libc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE*                  stdout;
FILE*                  stdin;
thread_local TLSBlock* tlsBlock;

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
    stdin = malloc(sizeof(FILE));
    if (stdin == NULL) {
        abort();
    }
    stdin->handle     = 0;
    stdin->bufferIdx  = 0;
    stdin->bufferMode = FILE_BUFFER_MODE_FULL;
    memset(stdin->buffer, 0, sizeof(stdin->buffer));
    __initCurrentTlsBlock();
    atexit(__closeAllFiles);
}

void __initCurrentTlsBlock() {
    TLSBlock* tmpTlsBlock = aligned_alloc(16, sizeof(TLSBlock));
    if (!tmpTlsBlock) {
        printf("Failed to allocate TLS block");
        abort();
    }
    tmpTlsBlock->self        = tmpTlsBlock;
    tmpTlsBlock->stackCanary = 69420;
    tmpTlsBlock->errnoVal    = 0;
    __tls_set_addr(tmpTlsBlock);
    tlsBlock = tmpTlsBlock;
}

void __libc_deinit() {
    free(stdout);
    free(stdin);
    __malloc_deinit();
}