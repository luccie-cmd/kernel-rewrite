#include <common/dbg/dbg.h>
#include <common/io/io.h>
#include <kernel/mmu/heap.h>
#include <stdlib.h>
#include <string.h>
#define ALIGN (2 * sizeof(void*))

#ifndef __SANITIZE_ADDRESS__

void* malloc(size_t allocSize) {
    if (!heapIsInitialized()) {
        heapInitialize();
    }
    LOCK(heapHeadSpinlock);
    HeapNode* current     = heapHead;
    size_t    alignedSize = ALIGNUP(allocSize, ALIGN);
    void*     ret         = NULL;
    while (current && ret == NULL) {
        if (current->free && current->size >= alignedSize) {
            if (current->size > alignedSize + sizeof(HeapNode)) {
                HeapNode* newNode = (HeapNode*)((uint64_t)current + alignedSize + sizeof(HeapNode));
                newNode->freedSize = 0;
                newNode->allocSize = current->size - alignedSize - sizeof(HeapNode);
                newNode->size      = current->size - alignedSize - sizeof(HeapNode);
                newNode->free      = true;
                newNode->prev      = current;
                newNode->next      = current->next;
                current->next      = newNode;
                if (newNode->next) {
                    newNode->next->prev = newNode;
                }
            }
            current->allocSize = alignedSize;
            current->size      = alignedSize;
            current->freedSize = 0;
            current->free      = false;
            ret                = (void*)((uint64_t)current + sizeof(HeapNode));
        }
        current = current->next;
    }
    UNLOCK(heapHeadSpinlock);
    return ret;
}

#endif // __SANITIZE_ADDRESS__