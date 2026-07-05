#include <common/dbg/dbg.h>
#include <common/spinlock.h>
#include <kernel/hal/gdt/gdt.h>
#include <kernel/mmu/mmu.h>
#include <stddef.h>

#define GDT_ACCESS_ACCESSED 1 << 0
#define GDT_ACCESS_RW 1 << 1
#define GDT_ACCESS_DIRECTION 1 << 2
#define GDT_ACCESS_EXECUTABLE 1 << 3
#define GDT_ACCESS_REGULAR_SEGMENT 1 << 4
#define GDT_ACCESS_DPL(level) ((level & 0b11) << 5)
#define GDT_ACCESS_PRESENT 1 << 7

#define GDT_FLAGS_LONG 1 << 1
#define GDT_FLAG_SIZE 1 << 2
#define GDT_FLAG_GRANULARITY 1 << 3

#define SEGMENT(level) (level * 8)

#define GDT_ENTRY(access, flags)                                                                   \
    (GDTEntry){                                                                                    \
        0,      /* limit0 */                                                                       \
        0,      /* base0 */                                                                        \
        0,      /* base1 */                                                                        \
        access, /* access */                                                                       \
        0,      /* limit1 */                                                                       \
        flags,  /* flags */                                                                        \
        0       /* base2 */                                                                        \
    }

extern void loadGDTASM(GDTR* gdtr);

static Spinlock initGDTLock;

static void setRSP0(TSS* tss) {
    uint64_t stack1Physical   = (uint64_t)pmmAllocate();
    uint64_t ISTstackPhysical = (uint64_t)pmmAllocate();
    tss->rsp0                 = stack1Physical + (uint64_t)getHHDM() + PAGE_SIZE - 16;
    tss->ist1                 = ISTstackPhysical + (uint64_t)getHHDM() + PAGE_SIZE - 16;
    debug("Set TSS->RSP0 to physical address 0x%llx\n", tss->rsp0);
    debug("Set TSS->IST1 to physical address 0x%llx\n", tss->ist1);
}

void loadGDT() {
    LOCK(initGDTLock);
    GDTEntry* entries =
        (GDTEntry*)((uintptr_t)pmmAllocateSize(7 * sizeof(GDTEntry)) + (uintptr_t)getHHDM());
    TSS*     tss       = (TSS*)((uintptr_t)pmmAllocate() + (uintptr_t)getHHDM());
    uint32_t tss_limit = sizeof(TSS) - 1;
    uint64_t tss_base  = (uint64_t)tss;
    entries[0]         = GDT_ENTRY(0, 0);
    entries[1]         = GDT_ENTRY(0x9A, 0xa); // Kernel 64 bit code
    entries[2]         = GDT_ENTRY(0x93, 0xc); // Kernel 64 bit data
    entries[3]         = GDT_ENTRY(0xF3, 0xc); // User 64 bit data
    entries[4]         = GDT_ENTRY(0xFA, 0xa); // User 64 bit code
    entries[5]         = (GDTEntry){
        .limit_low    = (uint16_t)(tss_limit & 0xFFFF),
        .base_low     = (uint16_t)(tss_base & 0xFFFF),
        .base_middle  = (uint8_t)((tss_base >> 16) & 0xFF),
        .access       = 0x89,
        .limit_middle = (uint8_t)((tss_limit >> 16) & 0xF),
        .flags        = 0,
        .base_high    = (uint8_t)((tss_base >> 24) & 0xFF),
    };
    entries[6] = (GDTEntry){
        .limit_low    = (uint16_t)((tss_base >> 32) & 0xFFFF),
        .base_low     = (uint16_t)((tss_base >> 48) & 0xFFFF),
        .base_middle  = 0,
        .access       = 0,
        .limit_middle = 0,
        .flags        = 0,
        .base_high    = 0,
    };
    GDTR* gdt       = (GDTR*)((uintptr_t)pmmAllocate() + (uintptr_t)getHHDM());
    gdt->base       = (uint64_t)entries;
    gdt->limit      = 7 * sizeof(GDTEntry) - 1;
    tss->iomap_base = sizeof(TSS);
    loadGDTASM(gdt);
    setRSP0(tss);
    UNLOCK(initGDTLock);
}

void gdtMapStacksToProc(uint64_t pid, PML4* pml4) {
    GDTR     gdtr;
    uint16_t tr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    __asm__ volatile("str %0" : "=a"(tr));
    GDTEntry* tssEntryLow  = (GDTEntry*)((uint64_t*)(gdtr.base + tr));
    GDTEntry* tssEntryHigh = (GDTEntry*)((uint64_t*)(gdtr.base + tr + 8));
    uint64_t  tssValue =
        ((uint64_t)tssEntryHigh->base_low << 48) | ((uint64_t)tssEntryHigh->limit_low << 32) |
        ((uint64_t)tssEntryLow->base_high << 24) | ((uint64_t)tssEntryLow->base_middle << 16) |
        ((uint64_t)tssEntryLow->base_low);
    TSS* tempTss = (TSS*)(tssValue);
    if (pid != 0) {
        debug("tss = 0x%lx rsp0 = 0x%lx ist1 = 0x%lx\n", (uint64_t)tempTss, tempTss->rsp0,
              tempTss->ist1);
        if (getPhysicalAddr(pml4, (uint64_t)tempTss, false) == 0) {
            vmmMapPage(pml4, ((uint64_t)tempTss) - (uint64_t)getHHDM(), (uint64_t)tempTss,
                       MAP_PROTECTION_KERNEL | MAP_PROTECTION_RW | MAP_PROTECTION_NOEXEC,
                       MAP_PRESENT);
        }
        if (getPhysicalAddr(pml4, gdtr.base, false) == 0) {
            vmmMapPage(pml4, gdtr.base - (uint64_t)getHHDM(), gdtr.base,
                       MAP_PROTECTION_KERNEL | MAP_PROTECTION_RW | MAP_PROTECTION_NOEXEC,
                       MAP_PRESENT);
        }
        if (getPhysicalAddr(pml4, tempTss->rsp0 - PAGE_SIZE + 16, false) == 0) {
            vmmMapPage(pml4, tempTss->rsp0 - (uint64_t)getHHDM() - PAGE_SIZE + 16,
                       tempTss->rsp0 - PAGE_SIZE + 16,
                       MAP_PROTECTION_KERNEL | MAP_PROTECTION_RW | MAP_PROTECTION_NOEXEC,
                       MAP_PRESENT);
        }
        if (getPhysicalAddr(pml4, tempTss->ist1 - PAGE_SIZE + 16, false) == 0) {
            vmmMapPage(pml4, tempTss->ist1 - (uint64_t)getHHDM() - PAGE_SIZE + 16,
                       tempTss->ist1 - PAGE_SIZE + 16,
                       MAP_PROTECTION_KERNEL | MAP_PROTECTION_RW | MAP_PROTECTION_NOEXEC,
                       MAP_PRESENT);
        }
    }
}