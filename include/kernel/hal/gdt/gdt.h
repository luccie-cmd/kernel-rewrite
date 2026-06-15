#if !defined(__KERNEL_HAL_GDT_GDT_H__)
#define __KERNEL_HAL_GDT_GDT_H__
#include <kernel/mmu/mmu.h>

typedef struct __attribute__((packed)) TSS {
    uint32_t reserved0;
    uint64_t rsp0; // Kernel stack pointer
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} TSS;
typedef struct __attribute__((packed)) GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  limit_middle : 4;
    uint8_t  flags : 4;
    uint8_t  base_high;
} GDTEntry;
typedef struct __attribute__((packed)) GDTR {
    uint16_t limit;
    uint64_t base;
} GDTR;

void loadGDT();
void gdtMapStacksToProc(uint64_t pid, PML4* pml4);

#endif // __KERNEL_HAL_GDT_GDT_H__
