#include <common/dbg/dbg.h>
#include <kernel/mmu/heap.h>
#include <kernel/mmu/pmm/pmm.h>
#include <kernel/mmu/vmm/vmm.h>
#define PMM_SIZE MEGABYTE
#define VMM_MAX 2 * MEGABYTE

HeapNode*       heapHead;
Spinlock        heapHeadSpinlock;
static bool     initialized = false;
static uint64_t pmmSize     = PMM_SIZE;

void heapInitialize() {
    LOCK(heapHeadSpinlock);
    if (initialized) {
        warn("Attempted to init VMM twice\n");
        UNLOCK(heapHeadSpinlock);
        return;
    }
    uint64_t base       = (uintptr_t)pmmAllocateSize(pmmSize) + (uintptr_t)getHHDM();
    heapHead            = (HeapNode*)base;
    heapHead->free      = true;
    heapHead->freedSize = 0;
    heapHead->allocSize = pmmSize - sizeof(HeapNode);
    heapHead->size      = pmmSize - sizeof(HeapNode);
    heapHead->next      = NULL;
    heapHead->prev      = NULL;
    initialized         = true;
    UNLOCK(heapHeadSpinlock);
}

bool heapIsInitialized() {
    return initialized;
}