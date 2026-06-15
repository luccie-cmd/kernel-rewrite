#include <libc.h>
#include <stdlib.h>
#include <string.h>

void* realloc(void* oldPtr, size_t size) {
    if (oldPtr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(oldPtr);
        return NULL;
    }
    __MallocRegion* oldNode  = (__MallocRegion*)((uintptr_t)oldPtr - sizeof(__MallocRegion));
    size_t          useSize  = oldNode->allocSize > size ? oldNode->allocSize : size;
    size_t          smallest = oldNode->allocSize < size ? oldNode->allocSize : size;
    void*           newPtr   = malloc(useSize);
    memcpy(newPtr, oldPtr, smallest);
    free(oldPtr);
    return newPtr;
}