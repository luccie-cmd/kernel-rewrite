#if !defined(__KERNEL_MMU_PMM_PMM_H__)
#define __KERNEL_MMU_PMM_PMM_H__
#include <stdbool.h>
#include <stdint.h>
#define PAGE_SIZE 4096

typedef struct PMMNode {
    uint64_t size;
    struct PMMNode* next;
} PMMNode;

void* pmmAllocateSize(uint64_t bytes);
void* pmmAllocateVirtual(uint32_t pages);
void* pmmAllocate();
void  pmmFree(void* addr, uint64_t bytes);
void  pmmInitialize();
bool  pmmIsInitialized();

#endif // __KERNEL_MMU_PMM_PMM_H__
