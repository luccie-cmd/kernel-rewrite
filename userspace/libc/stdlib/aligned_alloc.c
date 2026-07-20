
#include <libc.h>
#include <stdlib.h>

void* aligned_alloc(size_t alignment, size_t size) {
    if (alignment == 0 || (alignment & (alignment - 1))) {
        return NULL;
    }
    size_t total = size + alignment + sizeof(void*);
    void*  raw   = malloc(total);
    if (!raw) {
        return NULL;
    }
    uintptr_t        start   = (uintptr_t)raw + sizeof(void*);
    uintptr_t        aligned = (start + alignment - 1) & ~(alignment - 1);
    __AlignedHeader* hdr     = (__AlignedHeader*)(aligned - sizeof(__AlignedHeader));
    hdr->magic               = ALIGNED_MAGIC;
    hdr->raw                 = raw;
    return (void*)aligned;
}