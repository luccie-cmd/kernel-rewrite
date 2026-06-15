#include <common/dbg/dbg.h>
#include <common/io/io.h>
#include <kernel/mmu/heap.h>
#include <stdlib.h>
#include <string.h>

void* realloc(void* oldPtr, size_t size) {
    if (!heapIsInitialized()) {
        heapInitialize();
    }
    if (oldPtr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(oldPtr);
        return NULL;
    }
    LOCK(heapHeadSpinlock);
    HeapNode* oldNode  = (HeapNode*)((uintptr_t)oldPtr - sizeof(HeapNode));
    size_t    useSize  = oldNode->allocSize > size ? oldNode->allocSize : size;
    size_t    smallest = oldNode->allocSize < size ? oldNode->allocSize : size;
    UNLOCK(heapHeadSpinlock);
    void* newPtr = malloc(useSize);
    if (!newPtr) {
        error("Failed to allocate %lu bytes\n", useSize);
    }
    memcpy(newPtr, oldPtr, smallest);
    free(oldPtr);
    return newPtr;
}