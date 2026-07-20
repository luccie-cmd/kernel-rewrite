#include <libc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t        PADDING_PATTERN = 0xBEBEBEBEBEBEBEBEULL;
extern __MallocRegion* __mallocHead;
#define MIN_ALIGNMENT 8

static char mallocPrintBuffer[64];

void* malloc(size_t size) {
    if (stdout) {
        int n =
            snprintf(mallocPrintBuffer, sizeof(mallocPrintBuffer), "Allocating %lu bytes\n", size);
        fwrite(mallocPrintBuffer, sizeof(mallocPrintBuffer[0]), n, stdout);
        memset(mallocPrintBuffer, 0, sizeof(mallocPrintBuffer));
    }
    size                    = ALIGNUP(size, MIN_ALIGNMENT);
    __MallocRegion* current = __mallocHead;
    while (current) {
        if (current->free && current->size >= size) {
            if (current->size > size + sizeof(__MallocRegion)) {
                __MallocRegion* newNode =
                    (__MallocRegion*)((uint64_t)current + sizeof(__MallocRegion) + size);
                newNode->freedSize = 0;
                newNode->allocSize = current->size - size - sizeof(__MallocRegion);
                newNode->size      = current->size - size - sizeof(__MallocRegion);
                newNode->free      = true;
                memset(newNode->prePadding, PADDING_PATTERN & 0xFF, sizeof(newNode->prePadding));
                memset(newNode->postPadding, PADDING_PATTERN & 0xFF, sizeof(newNode->postPadding));

                newNode->prev = current;
                newNode->next = current->next;
                current->next = newNode;
                if (newNode->next) {
                    newNode->next->prev = newNode;
                }
            }
            current->allocSize = size;
            current->size      = size;
            current->free      = false;
            current->freedSize = 0;
            return (void*)((uint64_t)current + sizeof(__MallocRegion));
        }
        current = current->next;
    }
    fwrite("TODO: Grow heap\n", 17, 1, stdout);
    abort();
}