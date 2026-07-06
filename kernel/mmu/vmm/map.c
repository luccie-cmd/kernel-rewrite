#include <common/dbg/dbg.h>
#include <common/io/io.h>
#include <common/spinlock.h>
#include <kernel/mmu/mmu.h>
#include <stdlib.h>
#include <string.h>

PML4**          pml4List   = NULL;
uint64_t*       pml4Lookup = NULL;
size_t          maxPml4s   = 0;
static Spinlock lock;

PML4* vmmGetPML4(uint64_t pid) {
    if (!vmmIsInitialized()) {
        vmmInitialize();
    }
    if (pid == 0) {
        return (PML4*)(rdcr3() + (uint64_t)getHHDM());
    }
    LOCK(lock);
    pid -= 1;
    for (size_t i = 0; i < maxPml4s; ++i) {
        if (pml4Lookup[i] == pid) {
            UNLOCK(lock);
            return pml4List[i];
        }
    }
    pml4List           = realloc(pml4List, sizeof(pml4List[0]) * (maxPml4s + 1));
    pml4Lookup         = realloc(pml4Lookup, sizeof(pml4Lookup[0]) * (maxPml4s + 1));
    uint64_t pml4      = (uint64_t)pmmAllocate();
    pml4List[maxPml4s] = (PML4*)(pml4 + (uint64_t)getHHDM());
    memset(pml4List[maxPml4s], 0, PAGE_SIZE);
    pml4Lookup[maxPml4s] = pid;
    maxPml4s++;
    UNLOCK(lock);
    return (PML4*)(pml4 + (uint64_t)getHHDM());
}

typedef struct __attribute__((packed)) vmm_address {
    uint64_t offset : 12;
    uint64_t pte : 9;
    uint64_t pde : 9;
    uint64_t pdpe : 9;
    uint64_t pml4e : 9;
    uint64_t padding : 16;
} vmm_address;

static vmm_address getVMMfromVA(uint64_t vaddr) {
    vmm_address result;
    memcpy(&result, &vaddr, sizeof(result));
    // result.padding = (vaddr >> 48) & 0xFFFF;
    // result.pml4e   = (vaddr >> 39) & 0x1FF;
    // result.pdpe    = (vaddr >> 30) & 0x1FF;
    // result.pde     = (vaddr >> 21) & 0x1FF;
    // result.pte     = (vaddr >> 12) & 0x1FF;
    // result.offset  = vaddr & 0xFFF;
    return result;
}

void vmmUnmapPage(PML4* pml4, size_t virtualAddr) {
    LOCK(lock);
    virtualAddr &= ~(0xFFF);
    debug("Unmapping virtual 0x%lx in PML4 0x%lp\n", virtualAddr, pml4);
    vmm_address vma = getVMMfromVA(virtualAddr);
    if (pml4[vma.pml4e].pdpe_ptr == 0 || pml4[vma.pml4e].present == 0) {
        warn("Attempted to unmap page that has an unmapped pml4\n");
        UNLOCK(lock);
        return;
    }
    PDPE* pdpe = (PDPE*)(makeVirtualAddr((void*)(uint64_t)(pml4[vma.pml4e].pdpe_ptr << 12)));
    if (pdpe[vma.pdpe].pde_ptr == 0 || pdpe[vma.pdpe].present == 0) {
        warn("Attempted to unmap page that has an unmapped pdpe\n");
        UNLOCK(lock);
        return;
    }
    PDE* pde = (PDE*)(makeVirtualAddr((void*)(uint64_t)(pdpe[vma.pdpe].pde_ptr << 12)));
    if (pde[vma.pde].pte_ptr == 0 || pde[vma.pde].present == 0) {
        warn("Attempted to unmap page that has an unmapped pde\n");
        UNLOCK(lock);
        return;
    }
    PTE* pte = (PTE*)(makeVirtualAddr((void*)(uint64_t)(pde[vma.pde].pte_ptr << 12)));
    if (pte[vma.pte].papn_ppn == 0 || pte[vma.pte].present == 0) {
        warn("Attempted to unmap page that has an unmapped pte\n");
        UNLOCK(lock);
        return;
    }
    pte[vma.pte].present = 0;
    invalpg((void*)virtualAddr);
    UNLOCK(lock);
}

void vmmMapPage(PML4* pml4, size_t physicalAddr, size_t virtualAddr, int protFlags, int mapFlags) {
    if (!vmmIsInitialized()) {
        vmmInitialize();
    }
    LOCK(lock);
    virtualAddr &= ~(0xFFF);
    physicalAddr &= ~(0xFFF);
    vmm_address vma        = getVMMfromVA(virtualAddr);
    bool        readWrite  = (protFlags & MAP_PROTECTION_RW) != 0;
    bool        execute    = (protFlags & MAP_PROTECTION_NOEXEC) == 0;
    bool        kernelPage = (protFlags & MAP_PROTECTION_KERNEL) != 0;
    if (readWrite && execute) {
        error("Cannot map physical 0x%lx to virtual 0x%lx because of W^X\n", physicalAddr,
              virtualAddr);
    }
    bool presentMap  = (mapFlags & MAP_PRESENT) != 0;
    bool globalMap   = (mapFlags & MAP_GLOBAL) != 0;
    bool uncachable  = (mapFlags & MAP_UC) != 0;
    bool writeTrough = (mapFlags & MAP_WT) != 0;
    debug("Mapping physical 0x%lx to virtual 0x%lx in PML4 0x%lp (Present = %b RW = %b)\n",
          physicalAddr, virtualAddr, pml4, presentMap, readWrite);
    if (pml4[vma.pml4e].pdpe_ptr == 0) {
        uint64_t page = (uint64_t)pmmAllocate();
        memset(makeVirtualAddr((void*)page), 0, PAGE_SIZE);
        pml4[vma.pml4e].pdpe_ptr = page >> 12;
    }
    pml4[vma.pml4e].present = 1;
    PDPE* pdpe = (PDPE*)(makeVirtualAddr((void*)(uint64_t)(pml4[vma.pml4e].pdpe_ptr << 12)));
    if (pdpe[vma.pdpe].pde_ptr == 0) {
        uint64_t page = (uint64_t)pmmAllocate();
        memset(makeVirtualAddr((void*)page), 0, PAGE_SIZE);
        pdpe[vma.pdpe].pde_ptr = page >> 12;
    }
    pdpe[vma.pdpe].present = 1;
    PDE* pde = (PDE*)(makeVirtualAddr((void*)(uint64_t)(pdpe[vma.pdpe].pde_ptr << 12)));
    if (pde[vma.pde].pte_ptr == 0) {
        uint64_t page = (uint64_t)pmmAllocate();
        memset(makeVirtualAddr((void*)page), 0, PAGE_SIZE);
        pde[vma.pde].pte_ptr = page >> 12;
    }
    pde[vma.pde].present = 1;
    PTE* pte             = (PTE*)(makeVirtualAddr((void*)(uint64_t)(pde[vma.pde].pte_ptr << 12)));

    pml4[vma.pml4e].rw         = 1;
    pml4[vma.pml4e].user       = 1;
    pml4[vma.pml4e].no_execute = 0;
    pml4[vma.pml4e].pwt        = 0;
    pml4[vma.pml4e].pcd        = 0;
    pml4[vma.pml4e].accesed    = 0;
    pml4[vma.pml4e].ignored    = 0;
    pml4[vma.pml4e].mbz        = 0;
    pml4[vma.pml4e].ats0       = 0;
    pml4[vma.pml4e].ats1       = 0;
    // debug("pml4[vma.pml4e].present = %b\n", pml4[vma.pml4e].present);

    pdpe[vma.pdpe].rw         = 1;
    pdpe[vma.pdpe].user       = 1;
    pdpe[vma.pdpe].no_execute = 0;
    pdpe[vma.pdpe].pwt        = 0;
    pdpe[vma.pdpe].pcd        = 0;
    pdpe[vma.pdpe].accesed    = 0;
    pdpe[vma.pdpe].ignored    = 0;
    pdpe[vma.pdpe].mbz        = 0;
    pdpe[vma.pdpe].ats0       = 0;
    pdpe[vma.pdpe].ats1       = 0;
    // debug("pdpe[vma.pdpe].present = %b\n", pdpe[vma.pdpe].present);

    pde[vma.pde].rw         = 1;
    pde[vma.pde].user       = 1;
    pde[vma.pde].no_execute = 0;
    pde[vma.pde].pwt        = 0;
    pde[vma.pde].pcd        = 0;
    pde[vma.pde].accesed    = 0;
    pde[vma.pde].ignored    = 0;
    pde[vma.pde].mbz        = 0;
    pde[vma.pde].ats0       = 0;
    pde[vma.pde].ats1       = 0;
    // debug("pde[vma.pde].present = %b\n", pde[vma.pde].present);

    pte[vma.pte].present    = presentMap;
    pte[vma.pte].no_execute = !execute;
    pte[vma.pte].rw         = readWrite;
    pte[vma.pte].user       = !kernelPage;
    pte[vma.pte].global     = globalMap;
    pte[vma.pte].papn_ppn   = physicalAddr >> 12;
    pte[vma.pte].pwt        = writeTrough;
    pte[vma.pte].pcd        = uncachable;
    pte[vma.pte].accesed    = 0;
    pte[vma.pte].dirty      = 0;
    pte[vma.pte].pat        = 0;
    pte[vma.pte].ats0       = 0;
    pte[vma.pte].ats1       = 0;
    pte[vma.pte].pkeys      = 0;
    invalpg((void*)virtualAddr);
    UNLOCK(lock);
}

uint64_t getPhysicalAddr(PML4* pml4, uint64_t addr, bool ignorePresent) {
    LOCK(lock);
    addr            = ALIGNDOWN(addr, PAGE_SIZE);
    vmm_address vma = getVMMfromVA(addr);
    if (pml4[vma.pml4e].pdpe_ptr == 0 || (pml4[vma.pml4e].present == 0 && !ignorePresent)) {
        UNLOCK(lock);
        return 0;
    }
    PDPE* pdpe =
        (PDPE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pml4[vma.pml4e].pdpe_ptr << 12)));
    if (pdpe[vma.pdpe].pde_ptr == 0 || (pdpe[vma.pdpe].present == 0 && !ignorePresent)) {
        UNLOCK(lock);
        return 0;
    }
    PDE* pde = (PDE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pdpe[vma.pdpe].pde_ptr << 12)));
    if (pde[vma.pde].pte_ptr == 0 || (pde[vma.pde].present == 0 && !ignorePresent)) {
        UNLOCK(lock);
        return 0;
    }
    PTE* pte = (PTE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pde[vma.pde].pte_ptr << 12)));
    if (pte[vma.pte].papn_ppn == 0 || (pte[vma.pte].present == 0 && !ignorePresent)) {
        UNLOCK(lock);
        return 0;
    }
    uint64_t retAddr = pte[vma.pte].papn_ppn << 12;
    UNLOCK(lock);
    return retAddr;
}

static void clearPTEEntry(PML4* pml4, uint64_t pml4e, uint64_t pdpe, uint64_t pde, uint64_t pte) {
    vmm_address vma = {
        .padding = pml4e >= 256 ? 0xFFFF : 0,
        .pml4e   = pml4e,
        .pdpe    = pdpe,
        .pde     = pde,
        .pte     = pte,
        .offset  = 0,
    };
    uint64_t addr = 0;
    memcpy(&addr, &vma, sizeof(addr));
    vmmUnmapPage(pml4, addr);
}

static void clearPTE(PML4* pml4, uint64_t pml4e, uint64_t pdpee, uint64_t pdee) {
    PDPE* pdpe = (PDPE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pml4[pml4e].pdpe_ptr << 12)));
    PDE*  pde  = (PDE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pdpe[pdpee].pde_ptr << 12)));
    PTE*  pte  = (PTE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pde[pdee].pte_ptr << 12)));
    for (uint64_t ptee = 0; ptee < 512; ++ptee) {
        if (pte[ptee].present != 0 && pte[ptee].papn_ppn != 0) {
            clearPTEEntry(pml4, pml4e, pdpee, pdee, ptee);
        }
    }
}

static void clearPDE(PML4* pml4, uint64_t pml4e, uint64_t pdpee) {
    PDPE* pdpe = (PDPE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pml4[pml4e].pdpe_ptr << 12)));
    PDE*  pde  = (PDE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pdpe[pdpee].pde_ptr << 12)));
    for (uint64_t pdee = 0; pdee < 512; ++pdee) {
        if (pde[pdee].present != 0) {
            clearPTE(pml4, pml4e, pdpee, pdee);
        }
    }
}

static void clearPDPE(PML4* pml4, uint64_t pml4e) {
    PDPE* pdpe = (PDPE*)((uint64_t)makeVirtualAddr((void*)(uint64_t)(pml4[pml4e].pdpe_ptr << 12)));
    for (uint64_t pdpee = 0; pdpee < 512; ++pdpee) {
        if (pdpe[pdpee].present != 0) {
            clearPDE(pml4, pml4e, pdpee);
        }
    }
}

void vmmClearPML4(PML4* pml4) {
    for (uint64_t pml4e = 0; pml4e < 512; ++pml4e) {
        if (pml4[pml4e].present != 0) {
            clearPDPE(pml4, pml4e);
        }
    }
}