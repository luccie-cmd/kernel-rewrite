#include <kernel/mmu/mmu.h>
#include <stddef.h>

void* HhdmOffset = NULL;

void* getHHDM() {
    if (!vmmIsInitialized()) {
        vmmInitialize();
    }
    return HhdmOffset;
}

void* makeVirtualAddr(void* addr) {
    return (void*)((uintptr_t)addr + (uintptr_t)getHHDM());
}