#include <common/dbg/dbg.h>
#include <common/io/io.h>
#include <common/spinlock.h>
#include <kernel/mmu/mmu.h>
#include <stddef.h>

static Spinlock lock    = false;
PMMNode*        pmmHead = NULL;

void* __attribute__((no_sanitize_address)) pmmAllocateSize(uint64_t bytes) {
    if (!pmmIsInitialized()) {
        pmmInitialize();
    }
    if (bytes == 0) {
        error("PMM requested 0 byte allocation\n");
    }
    LOCK(lock);
    PMMNode* current = pmmHead;
    PMMNode* prev    = NULL;
    void*    ret     = NULL;
    uint64_t size    = ALIGNUP(bytes, PAGE_SIZE);
    while (current) {
        if (current->size >= size) {
            ret = current;
            if (current->size > size) {
                PMMNode* newNode = (PMMNode*)((uintptr_t)current + size);
                newNode->size    = current->size - size;
                newNode->next    = current->next;
                if (prev != NULL) {
                    prev->next = newNode;
                } else {
                    pmmHead = newNode;
                }
            } else {
                if (prev != NULL) {
                    prev->next = current->next;
                } else {
                    pmmHead = current->next;
                }
            }
            break;
        }
        prev    = current;
        current = current->next;
    }
    if (ret == NULL) {
        error("Failed to get a physical region for size 0x%lx\n", bytes);
    }
    UNLOCK(lock);
    debug("Pmm alloc 0x%lx\n", (uint64_t)((uintptr_t)ret - (uintptr_t)getHHDM()));
    return (void*)((uintptr_t)ret - (uintptr_t)getHHDM());
}

void* pmmAllocateVirtual(uint32_t pages) {
    return pmmAllocateSize(pages * PAGE_SIZE);
}

void* pmmAllocate() {
    return pmmAllocateVirtual(1);
}

static inline void mergeBlocks() {
    PMMNode* current = pmmHead;
    while (current && current->next) {
        uintptr_t currentEnd = (uintptr_t)current + current->size;
        uintptr_t nextStart  = (uintptr_t)current->next;
        if (currentEnd == nextStart) {
            current->size += current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

void __attribute__((no_sanitize_address)) pmmFree(void* addr, uint64_t size) {
    if (!pmmIsInitialized()) {
        pmmInitialize();
        return;
    }
    LOCK(lock);
    size = ALIGNUP(size, PAGE_SIZE);
    debug("PMM free 0x%lx-0x%lx\n", addr, (uint64_t)addr + size);
    PMMNode* newNode = (PMMNode*)((uint64_t)addr + (uint64_t)getHHDM());
    newNode->size    = size;
    PMMNode* current = pmmHead;
    PMMNode* prev    = NULL;
    while (current && current < newNode) {
        prev    = current;
        current = current->next;
    }
    newNode->next = current;
    if (prev) {
        prev->next = newNode;
    } else {
        pmmHead = newNode;
    }
    mergeBlocks();
    UNLOCK(lock);
}