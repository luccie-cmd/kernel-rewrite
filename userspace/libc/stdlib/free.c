#include <libc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// TODO: Merge regions
void free(void* ptr) {
    __MallocRegion*  block = (__MallocRegion*)((uint64_t)ptr - sizeof(__MallocRegion));
    __AlignedHeader* hdr   = (__AlignedHeader*)((uintptr_t)ptr - sizeof(__AlignedHeader));
    if (hdr->magic == ALIGNED_MAGIC) {
        free(hdr->raw);
    }
    if (block->free) {
        fwrite("Double free detected\n", 21, 1, stdout);
        abort();
    }
    if (block->allocSize != block->size) {
        fwrite("Corrupted block detected (block->allocSize != block->size)\n", 59, 1, stdout);
        abort();
    }
    size_t alignedSize = block->allocSize;
    if (alignedSize > block->allocSize - block->freedSize) {
        fwrite("Invalid free size\n", 18, 1, stdout);
        abort();
    }
    block->freedSize += alignedSize;
    if (block->freedSize == block->allocSize) {
        block->free = true;
    }
}