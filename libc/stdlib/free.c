#include <common/dbg/dbg.h>
#include <kernel/mmu/heap.h>
#include <stdlib.h>

#ifndef __SANITIZE_ADDRESS__

static void walkStack(void** rbp) {
    const uint32_t MAX_DEPTH = 4096;
    uint32_t       depth     = 0;
    while (rbp && depth++ < MAX_DEPTH) {
        void* saved_rbp = rbp[0];
        void* ret_addr  = rbp[1];
        if (!ret_addr) break;
        printf("%p\n", ret_addr);
        if (saved_rbp <= (void*)rbp) break;
        rbp = (void**)saved_rbp;
    }
}

void free(void* addr) {
    LOCK(heapHeadSpinlock);
    if (!addr) {
        uint64_t rbp = 0;
        __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
        walkStack((void**)rbp);
        warn("Passed NULL to free\n");
        goto end;
    }
    // debug("Freeing pointer %lx\n", addr);
    HeapNode* node = (HeapNode*)((uint64_t)addr - sizeof(HeapNode));
    if (node->free) {
        uint64_t rbp = 0;
        __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
        walkStack((void**)rbp);
        warn("Freed node passed to free\n");
        goto end;
    }
    if (node->allocSize != node->size) {
        error("Corrupted block detected node->allocSize != node->size\n");
    }
    node->free     = true;
    HeapNode* head = heapHead;
    while (head && head->next) {
        if (head->free && head->next->free) {
            head->size += head->next->size;
            HeapNode* next = head->next;
            head->next     = next->next;
            if (next->next) {
                next->next->prev = head;
            }
            continue;
        }
        head = head->next;
    }
end:
    UNLOCK(heapHeadSpinlock);
}

#endif // __SANITIZE_ADDRESS__