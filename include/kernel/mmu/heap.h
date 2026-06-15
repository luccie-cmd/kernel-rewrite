#if !defined(__KERNEL_MMU_HEAP_H__)
#define __KERNEL_MMU_HEAP_H__
#include <common/spinlock.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GIGABYTE (1024 * 1024 * 1024ULL)
#define MEGABYTE (1024 * 1024ULL)
#define KILOBYTE (1024ULL)

typedef struct HeapNode {
    size_t           size;
    size_t           allocSize;
    size_t           freedSize;
    struct HeapNode* next;
    struct HeapNode* prev;
    uint8_t          free;
} HeapNode;

extern HeapNode* heapHead;
extern Spinlock  heapHeadSpinlock;

void heapInitialize();
bool heapIsInitialized();

#endif // __KERNEL_MMU_HEAP_H__
