#include <common/dbg/dbg.h>
#include <kernel/vfs/vfs.h>

void vfsSeek(uint64_t fileIndex, size_t seekOffset) {
    LOCK(vfsFilesSpinlock);
    VFSFile* file = vfsFiles[fileIndex];
    if (!file->mount->fsDriver || !file->mount->fsDriver->seek) {
        error("Failed to find file->mount->fsDriver->seek\n");
    }
    file->mount->fsDriver->seek(file->mount->fsDriver, file->fsHandle, seekOffset);
    UNLOCK(vfsFilesSpinlock);
}

size_t vfsGetOffset(uint64_t fileIndex) {
    LOCK(vfsFilesSpinlock);
    VFSFile* file = vfsFiles[fileIndex];
    if (!file->mount->fsDriver || !file->mount->fsDriver->getOffset) {
        error("Failed to find file->mount->fsDriver->getOffset\n");
    }
    size_t offset = file->mount->fsDriver->getOffset(file->mount->fsDriver, file->fsHandle);
    UNLOCK(vfsFilesSpinlock);
    return offset;
}

size_t vfsGetLength(uint64_t fileIndex) {
    LOCK(vfsFilesSpinlock);
    VFSFile* file = vfsFiles[fileIndex];
    if (!file->mount->fsDriver || !file->mount->fsDriver->getLength) {
        error("Failed to find file->mount->fsDriver->getLength\n");
    }
    size_t length = file->mount->fsDriver->getLength(file->mount->fsDriver, file->fsHandle);
    UNLOCK(vfsFilesSpinlock);
    return length;
}