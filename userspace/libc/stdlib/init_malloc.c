#include <__syscall.h>
#include <libc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
static uint64_t PADDING_PATTERN = 0xBEBEBEBEBEBEBEBEULL;

__MallocRegion* __mallocHead;

void __malloc_init() {
    __mallocHead = (__MallocRegion*)syscall_execute(SYS_GETMEM, NULL, MALLOC_INIT_SIZE,
                                                    PROT_READ | PROT_WRITE);
    if (!__mallocHead) {
        abort();
    }
    memset(__mallocHead->prePadding, PADDING_PATTERN & 0xFF, sizeof(__mallocHead->prePadding));
    memset(__mallocHead->postPadding, PADDING_PATTERN & 0xFF, sizeof(__mallocHead->postPadding));
    __mallocHead->free      = true;
    __mallocHead->allocSize = MALLOC_INIT_SIZE - sizeof(__MallocRegion);
    __mallocHead->size      = MALLOC_INIT_SIZE - sizeof(__MallocRegion);
    __mallocHead->freedSize = 0;
    __mallocHead->next      = NULL;
    __mallocHead->prev      = NULL;
}

void __malloc_deinit() {
    if (!syscall_execute(SYS_FREEMEM, __mallocHead)) {
        abort();
    }
}