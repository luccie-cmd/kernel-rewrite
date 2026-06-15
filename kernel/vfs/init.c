#include <common/dbg/dbg.h>
#include <common/spinlock.h>
#include <kernel/vfs/vfs.h>

static bool initialized = false;
Spinlock    rootMountSpinlock;
Spinlock    vfsFilesSpinlock;

void vfsInitialize() {
    LOCK(rootMountSpinlock);
    LOCK(vfsFilesSpinlock);
    if (initialized) {
        warn("Attempted to init VFS twice\n");
        UNLOCK(rootMountSpinlock);
        return;
    }
    rootMount   = NULL;
    vfsFiles    = NULL;
    initialized = true;
    UNLOCK(vfsFilesSpinlock);
    UNLOCK(rootMountSpinlock);
}

bool vfsIsInitialized() {
    return initialized;
}