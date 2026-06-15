#include <Limine/limine.h>
#include <common/dbg/dbg.h>
#include <common/spinlock.h>
#include <kernel/mmu/mmu.h>
#include <stddef.h>

struct limine_memmap_request memmapRequest = {
    .id       = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

static Spinlock    lock;
static atomic_bool initialized = false;

extern PMMNode* pmmHead;

void __attribute__((no_sanitize_address)) pmmInitialize() {
    LOCK(lock);
    if (initialized) {
        warn("Attempted to init PMM twice\n");
        UNLOCK(lock);
        return;
    }
    if (memmapRequest.response == NULL) {
        error("Failed to get memory map\n");
    }
    for (uint64_t i = 0; i < memmapRequest.response->entry_count; ++i) {
        struct limine_memmap_entry* entry = memmapRequest.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= PAGE_SIZE) {
            PMMNode* newNode = (PMMNode*)makeVirtualAddr((void*)entry->base);
            newNode->size    = entry->length;
            newNode->next    = NULL;
            if (pmmHead == NULL) {
                pmmHead = newNode;
            } else {
                newNode->next = pmmHead;
                pmmHead       = newNode;
            }
        }
    }
    initialized = true;
    UNLOCK(lock);
}
bool pmmIsInitialized() {
    return initialized;
}