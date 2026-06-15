#include <../Limine/limine.h>
#include <common/dbg/dbg.h>
#include <common/io/io.h>
#include <common/spinlock.h>
#include <kernel/acpi/acpi.h>
#include <kernel/acpi/tables.h>
#include <kernel/mmu/mmu.h>
#include <stddef.h>
#include <string.h>

struct limine_rsdp_request __attribute__((section(".limine_requests"))) rsdpRequest = {
    .id       = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};
static XSDT*    xsdt       = NULL;
static size_t   numEntries = 0;
static bool     initialized;
static Spinlock initLock;

void acpiInitialize() {
    LOCK(initLock);
    if (initialized) {
        warn("Attempted to init ACPI twice\n");
        UNLOCK(initLock);
        return;
    }
    void* rsdpAddr = rsdpRequest.response->address;
    XSDP* xsdp     = (XSDP*)(rsdpAddr);
    xsdt           = (XSDT*)(makeVirtualAddr((void*)xsdp->XsdtAddress));
    if (xsdt == NULL) {
        error("Failed to acquire the XSDT\n");
    }
    numEntries  = (xsdt->h.Length - sizeof(xsdt->h)) / sizeof(xsdt->Entries[0]);
    initialized = true;
    FADT* fadt  = (FADT*)(acpiGetTable((char*)"FACP"));
    if (!fadt) {
        error("No FADT found\n");
    }
    if (fadt->SMI_CommandPort == 0) {
        warn("ACPI mode already enabled. SMI command port == 0.\n");
        UNLOCK(initLock);
        return;
    }
    if ((fadt->AcpiEnable == fadt->AcpiDisable) == 0) {
        warn("ACPI mode already enabled. fadt->AcpiEnable == fadt->AcpiDisable == 0\n");
        UNLOCK(initLock);
        return;
    }
    if (fadt->PM1aControlBlock & 1) {
        warn("ACPI mode already enabled. (fadt->PM1aControlBlock & 1) == 1\n");
        UNLOCK(initLock);
        return;
    }
    outb(fadt->SMI_CommandPort, fadt->AcpiEnable);
    while ((inw(fadt->PM1aControlBlock) & 1) == 0) {
        __asm__ volatile("nop");
    }
    info("Enabled ACPI mode\n");
    UNLOCK(initLock);
}

bool acpiIsInitialized() {
    return initialized;
}

void* acpiGetTable(const char* table) {
    if (!acpiIsInitialized()) {
        acpiInitialize();
    }
    for (size_t i = 0; i < numEntries; ++i) {
        ACPISDTHeader* sdtHeader = (ACPISDTHeader*)(makeVirtualAddr((void*)xsdt->Entries[i]));
        if (memcmp(table, sdtHeader->Signature, 4) == 0) {
            return (void*)(sdtHeader);
        }
    }
    info("Table %.4s couldn't be found\n", table);
    return NULL;
}