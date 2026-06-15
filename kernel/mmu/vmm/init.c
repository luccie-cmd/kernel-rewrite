#include <Limine/limine.h>
#include <common/dbg/dbg.h>
#include <common/spinlock.h>
#include <kernel/mmu/mmu.h>
#include <sys/stat.h>

struct limine_hhdm_request hhdmRequest = {
    .id       = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

static Spinlock lock;
static bool     initialized = false;

extern void* HhdmOffset;

void vmmInitialize() {
    LOCK(lock);
    if (initialized) {
        warn("Attempted to init VMM twice\n");
        UNLOCK(lock);
        return;
    }
    if (hhdmRequest.response == NULL) {
        error("Failed to get HHDM\n");
    }
    HhdmOffset  = (void*)hhdmRequest.response->offset;
    initialized = true;
    UNLOCK(lock);
}

bool vmmIsInitialized() {
    return initialized;
}