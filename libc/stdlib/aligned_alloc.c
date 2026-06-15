#include <common/io/io.h>
#include <kernel/mmu/heap.h>
#include <stdlib.h>

void* aligned_alloc(size_t alignment, size_t size) {
    size_t total = size + alignment + sizeof(HeapNode);
    void*  raw   = malloc(total);
    // memset(raw, 0, total);
    if (!raw) return NULL;
    uintptr_t base    = (uintptr_t)raw + sizeof(HeapNode);
    uintptr_t aligned = (base + alignment - 1) & ~(alignment - 1);
    HeapNode* new_hdr = (HeapNode*)(aligned - sizeof(HeapNode));
    HeapNode* old_hdr = (HeapNode*)((uintptr_t)raw - sizeof(HeapNode));
    *new_hdr          = *old_hdr;
    return (void*)aligned;
}