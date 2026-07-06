#if !defined(__KERNEL_MMU_VMM_VMM_H__)
#define __KERNEL_MMU_VMM_VMM_H__
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAP_PROTECTION_NOEXEC (1 << 0)
#define MAP_PROTECTION_RW (1 << 1)
#define MAP_PROTECTION_KERNEL (1 << 2)

#define MAP_PRESENT (1 << 0)
#define MAP_GLOBAL (1 << 1)
#define MAP_UC (1 << 2)
#define MAP_WT (1 << 3)

#define ONDEMAND_MAP_ADDRESS 0xDEADC0DE

typedef struct __attribute__((packed)) PML4 {
    uint64_t present : 1;    // Is the page present?
    uint64_t rw : 1;         // Can read/write to/from the page?
    uint64_t user : 1;       // Can user access this page?
    uint64_t pwt : 1;        // Page write through
    uint64_t pcd : 1;        // Page cache disable
    uint64_t accesed : 1;    // Is page accessed?
    uint64_t ignored : 1;    // Ignored field.
    uint64_t mbz : 2;        // Must be zero.
    uint64_t ats0 : 3;       // Available to software.
    uint64_t pdpe_ptr : 40;  // Physical page number to the PDP tables.
    uint64_t ats1 : 11;      // Available to the software.
    uint64_t no_execute : 1; // Disable execution of code on this page.
} PML4;
static_assert(sizeof(PML4) == 8, "Structure PML4 isn't 8 PTE bytes big.");

typedef struct __attribute__((packed)) PDPE {
    uint64_t present : 1;    // Is the page present?
    uint64_t rw : 1;         // Can read/write to/from the page?
    uint64_t user : 1;       // Can user access this page?
    uint64_t pwt : 1;        // Page write through
    uint64_t pcd : 1;        // Page cache disable
    uint64_t accesed : 1;    // Is page accessed?
    uint64_t ignored : 1;    // Ignored field.
    uint64_t mbz : 1;        // Must be zero.
    uint64_t ignored2 : 1;   // Ignored field.
    uint64_t ats0 : 3;       // Available to software.
    uint64_t pde_ptr : 40;   // Physical page number to the PD tables.
    uint64_t ats1 : 11;      // Available to the software.
    uint64_t no_execute : 1; // Disable execution of code on this page.
} PDPE;
static_assert(sizeof(PDPE) == 8, "Structure PDP isn't 8 bytes big.");

typedef struct __attribute__((packed)) PDE {
    uint64_t present : 1;    // Is the page present?
    uint64_t rw : 1;         // Can read/write to/from the page?
    uint64_t user : 1;       // Can user access this page?
    uint64_t pwt : 1;        // Page write through
    uint64_t pcd : 1;        // Page cache disable
    uint64_t accesed : 1;    // Is page accessed?
    uint64_t ignored : 1;    // Ignored field.
    uint64_t mbz : 1;        // Must be zero.
    uint64_t ignored2 : 1;   // Ignored field.
    uint64_t ats0 : 3;       // Available to software.
    uint64_t pte_ptr : 40;   // Physical page number to the PT tables.
    uint64_t ats1 : 11;      // Available to the software.
    uint64_t no_execute : 1; // Disable execution of code on this page.
} PDE;
static_assert(sizeof(PDE) == 8, "Structure PD isn't 8 bytes big.");

typedef struct __attribute__((packed)) PTE {
    uint64_t present : 1;    // Is the page present?
    uint64_t rw : 1;         // Can read/write to/from the page?
    uint64_t user : 1;       // Can user access this page?
    uint64_t pwt : 1;        // Page write through
    uint64_t pcd : 1;        // Page cache disable
    uint64_t accesed : 1;    // Is page accessed?
    uint64_t dirty : 1;      // Was the field written to?
    uint64_t pat : 1;        // Page attribute table.
    uint64_t global : 1;     // Is page global (unvalidated)
    uint64_t ats0 : 3;       // Available to software.
    uint64_t papn_ppn : 40;  // Physical page number to the physical address.
    uint64_t ats1 : 7;       // Available to the software.
    uint64_t pkeys : 4;      // Protection keys
    uint64_t no_execute : 1; // Disable execution of code on this page.
} PTE;
static_assert(sizeof(PTE) == 8, "Structure PT isn't 8 bytes big.");

void* getHHDM();
void* makeVirtualAddr(void* addr);
void  vmmUnmapPage(PML4* pml4, size_t virtualAddr);
void  vmmMapPage(PML4* pml4, size_t physicalAddr, size_t virtualAddr, int protFlags, int mapFlags);
PML4* vmmGetPML4(uint64_t pid);
void  vmmClearPML4(PML4* pml4);
uint64_t getPhysicalAddr(PML4* pml4, uint64_t addr, bool ignorePresent);
void     vmmInitialize();
bool     vmmIsInitialized();

#endif // __KERNEL_MMU_VMM_VMM_H__
