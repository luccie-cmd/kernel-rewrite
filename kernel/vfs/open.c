#include <common/dbg/dbg.h>
#include <kernel/vfs/vfs.h>

dynarray(VFSFile*) vfsFiles;

static VFSFile* findAvaliableFile() {
    for (size_t i = 0; i < dyn_size(vfsFiles); ++i) {
        VFSFile* candidate = vfsFiles[i];
        if (!candidate->used) {
            return candidate;
        }
    }
    debug("Ran out of VFS files\n");
    VFSFile* newFile = malloc(sizeof(VFSFile));
    if (!newFile) {
        error("Failed to allocate memory for new VFS file\n");
    }
    newFile->used      = false;
    newFile->vfsHandle = dyn_size(vfsFiles);
    dyn_push(vfsFiles, newFile);
    return newFile;
}

static inline MountPoint* findMountpoint(const char* mountpoint, const char** outMountPath) {
    MountPoint* current = rootMount;
    const char* p       = mountpoint;
    if (current) {
        p += strlen(current->name);
    }
    while (current) {
        if (*p == '/') {
            p += 1;
        }
        bool found = false;
        for (size_t i = 0; i < dyn_size(current->kids); ++i) {
            if (!current->kids[i]) {
                continue;
            }
            const char* childName = current->kids[i]->name;
            size_t      childLen  = strlen(childName);
            if (memcmp(childName, p, childLen) == 0) {
                if (p[childLen] == '/' || p[childLen] == '\0') {
                    p += childLen;
                    current = current->kids[i];
                    found   = true;
                    break;
                }
            }
        }
        if (!found) {
            break;
        }
    }
    *outMountPath = (*p == '/') ? (p + 1) : p;
    return current;
}

void vfsCreate(const char* path, uint32_t permissions) {
    LOCK(vfsFilesSpinlock);
    const char* fsPath = NULL;
    MountPoint* mp     = findMountpoint(path, &fsPath);
    if (!mp) {
        error("mp became NULL\n");
    }
    size_t fsPathLen = strlen(fsPath);
    if (fsPathLen > 0 && fsPath[fsPathLen - 1] != '/') {
        const char* oldPath = fsPath;
        char*       newPath = malloc(fsPathLen + 2);
        if (!newPath) {
            error("Failed to allocate enough memory for new path\n");
        }
        memcpy(newPath, oldPath, fsPathLen);
        newPath[fsPathLen]     = '/';
        newPath[fsPathLen + 1] = '\0';
        fsPath                 = newPath;
    }
    char* tmpPath = strdup(fsPath);
    mp->fsDriver->create(mp->fsDriver, tmpPath, permissions);
    free(tmpPath);
    UNLOCK(vfsFilesSpinlock);
}

uint64_t vfsOpen(const char* path, uint32_t flags) {
    LOCK(vfsFilesSpinlock);
    const char* fsPath = NULL;
    MountPoint* mp     = findMountpoint(path, &fsPath);
    if (!mp) {
        error("mp became NULL\n");
    }
    VFSFile* file               = findAvaliableFile();
    file->used                  = true;
    file->mount                 = mp;
    file->pathWithoutMountPoint = fsPath;
    file->flags                 = flags;
    debug("Opening file\n");
    char* tmpPath  = strdup(fsPath);
    file->fsHandle = mp->fsDriver->open(mp->fsDriver, tmpPath);
    if (file->fsHandle == (uint64_t)-1) {
        if (flags & OPEN_FLAG_CREATE) {
            free(tmpPath);
            tmpPath = strdup(fsPath);
            mp->fsDriver->create(mp->fsDriver, tmpPath, __UINT32_MAX__);
            free(tmpPath);
            tmpPath        = strdup(fsPath);
            file->fsHandle = mp->fsDriver->open(mp->fsDriver, tmpPath);
            free(tmpPath);
            if (file->fsHandle == (uint64_t)-1) {
                file->used = false;
                UNLOCK(vfsFilesSpinlock);
                return -1;
            }
        } else {
            file->used = false;
            free(tmpPath);
            UNLOCK(vfsFilesSpinlock);
            return -1;
        }
    }
    mp->fsDriver->references += 1;
    UNLOCK(vfsFilesSpinlock);
    if (flags & OPEN_FLAG_TRUNCATE) {
        error("TODO: Truncate\n");
    }
    if (flags & OPEN_FLAG_APPEND) {
        vfsSeek(file->vfsHandle, vfsGetLength(file->vfsHandle));
    } else {
        vfsSeek(file->vfsHandle, 0);
    }
    return file->vfsHandle;
}

static bool dyn_contains(dynarray(FSDriver*) drivers, FSDriver* driver) {
    for (size_t i = 0; i < dyn_size(drivers); ++i) {
        if (drivers[i] == driver) {
            return true;
        }
    }
    return false;
}

void vfsClose(uint64_t fileIndex) {
    LOCK(vfsFilesSpinlock);
    if (fileIndex > dyn_size(vfsFiles)) {
        error("Out of range file index\n");
    }
    VFSFile* file = vfsFiles[fileIndex];
    file->mount->fsDriver->close(file->mount->fsDriver, file->fsHandle);
    file->mount->fsDriver->references -= 1;
    if (dyn_contains(toCloseFSDrivers, file->mount->fsDriver)) {
        file->mount->fsDriver->references -= 1;
        if (file->mount->fsDriver->references == 0) {
            file->mount->fsDriver->deinit(file->mount->fsDriver);
            free(file->mount->fsDriver);
        }
    }
    file->used = false;
    UNLOCK(vfsFilesSpinlock);
}