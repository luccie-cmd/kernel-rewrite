#include <common/dbg/dbg.h>
#include <kernel/vfs/vfs.h>

void vfsRead(uint64_t fileIndex, void* outData, size_t size) {
    LOCK(vfsFilesSpinlock);
    VFSFile* file = vfsFiles[fileIndex];
    if (!(file->flags & OPEN_FLAG_READ)) {
        warn("Trying to read from non readable file\n");
        return;
    }
    file->mount->fsDriver->read(file->mount->fsDriver, file->fsHandle, outData, size);
    UNLOCK(vfsFilesSpinlock);
}