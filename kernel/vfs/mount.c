#include <common/dbg/dbg.h>
#include <drivers/fs.h>
#include <kernel/vfs/gpt.h>
#include <kernel/vfs/vfs.h>

MountPoint* rootMount;
dynarray(struct FSDriver*) toCloseFSDrivers;

static void printOffset(size_t offset) {
    for (size_t i = 0; i < offset; ++i) {
        putchar(' ');
    }
}

static void printMountpoint(size_t offset, MountPoint* mp) {
    printOffset(offset);
    printf("`%s`\n", mp->name);
    for (size_t i = 0; i < dyn_size(mp->kids); ++i) {
        if (mp->kids[i]) {
            printMountpoint(offset + 4, mp->kids[i]);
        }
    }
}

static inline MountPoint* createNewPoint(const char* name, MountPoint* parent) {
    MountPoint* newPoint = malloc(sizeof(MountPoint));
    if (!newPoint) {
        error("Failed to allocate new mount point\n");
    }
    newPoint->kids     = NULL;
    newPoint->name     = name;
    newPoint->fsDriver = NULL;
    newPoint->parent   = parent;
    return newPoint;
}

static inline MountPoint* getParentMount(const char* mountpoint, const char** outMountPath) {
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

static inline MountPoint* getMount(const char* mountPoint) {
    const char* outMp   = NULL;
    MountPoint* current = getParentMount(mountPoint, &outMp);
    if (current == NULL || (outMp != NULL && strlen(outMp) > 0)) {
        error("Unable to find mount point `%s` `%s`\n", mountPoint, outMp);
    }
    debug("%lx %lx %lx\n", current, current->parent, rootMount);
    return current;
}

// static uint8_t* parseGUID(uint8_t* GUID) {
//     uint8_t* newGUID = malloc(16);
//     if (!newGUID) {
//         error("Failed to allocate memory for new GUID\n");
//     }
//     newGUID[0]  = GUID[3];
//     newGUID[1]  = GUID[2];
//     newGUID[2]  = GUID[1];
//     newGUID[3]  = GUID[0];
//     newGUID[4]  = GUID[5];
//     newGUID[5]  = GUID[4];
//     newGUID[6]  = GUID[7];
//     newGUID[7]  = GUID[6];
//     newGUID[8]  = GUID[8];
//     newGUID[9]  = GUID[9];
//     newGUID[10] = GUID[10];
//     newGUID[11] = GUID[11];
//     newGUID[12] = GUID[12];
//     newGUID[13] = GUID[13];
//     newGUID[14] = GUID[14];
//     newGUID[15] = GUID[15];
//     return newGUID;
// }

typedef struct TempReader {
    union {
        struct {
            void*  bytes;
            size_t size;
        } bytes;
        // TODO: MSC devices
    };
    bool isBytes;
} TempReader;

static TempReader* constructTempBytesReader(void* bytes, size_t size) {
    TempReader* reader = malloc(sizeof(TempReader));
    if (!reader) {
        error("Failed to allocate memory for reader\n");
    }
    reader->bytes.bytes = bytes;
    reader->bytes.size  = size;
    reader->isBytes     = true;
    return reader;
}

static void freeTempReader(TempReader* reader) {
    free(reader);
}

static bool tempReaderRead(TempReader* reader, void** out, size_t size) {
    if (reader->isBytes) {
        if (size > reader->bytes.size) {
            warn("Too little bytes, returning NULL\n");
            *out = NULL;
            return false;
        } else {
            *out = malloc(size);
            if (*out == NULL) {
                error("Failed to allocate memory for out\n");
            }
            memcpy(*out, reader->bytes.bytes, size);
            reader->bytes.bytes = (void*)((uintptr_t)reader->bytes.bytes + size);
            reader->bytes.size -= size;
        }
    } else {
        error("Invalid temporary reader\n");
    }
    return true;
}

static dynarray(PartitionEntry*) getPartEntriesBytes(void* bytes, size_t size) {
    bytes = (void*)((uintptr_t)bytes + 512);
    size -= 512;
    TempReader*           tempReader = constructTempBytesReader(bytes, size);
    PartitionTableHeader* PTH        = NULL;
    tempReaderRead(tempReader, (void**)&PTH, 512);
    if (!PTH) {
        error("Failed to allocate memory for PTH\n");
    }
    if (memcmp(PTH->signature, "EFI PART", 8) != 0) {
        error("Partition header corrupted got a signature of `%8s`\n", PTH->signature);
    }
    uint8_t* partBuffer = NULL;
    tempReaderRead(tempReader, (void**)&partBuffer, 15872);
    if (!partBuffer) {
        error("Failed to allocate memory for partBuffer\n");
    }
    dynarray(PartitionEntry*) entries = NULL;
    for (uint32_t i = 0; i < PTH->partitionCount; i++) {
        PartitionEntry* entry = (PartitionEntry*)(partBuffer + (i * sizeof(PartitionEntry)));
        if (entry->startLBA == 0 && entry->endLBA == 0) {
            break;
        }
        // uint8_t* newGUID = parseGUID(entry->GUID);
        // memcpy(entry->GUID, newGUID, sizeof(entry->GUID));
        // free(newGUID);
        PartitionEntry* acEntry = malloc(sizeof(PartitionEntry));
        if (!acEntry) {
            for (size_t j = 0; j < dyn_size(entries); ++j) {
                free(entries[j]);
            }
            dyn_free(entries);
            error("Failed to allocate memory for actual entry\n");
        }
        memcpy(acEntry, entry, sizeof(PartitionEntry));
        info("Partition GUID = %08.8x%08.8x%08.8x%08.8x\n", ((uint32_t*)acEntry->GUID)[0],
             ((uint32_t*)acEntry->GUID)[1], ((uint32_t*)acEntry->GUID)[2],
             ((uint32_t*)acEntry->GUID)[3]);
        dyn_push(entries, acEntry);
    }
    if (dyn_size(entries) == 0) {
        for (size_t i = 0; i < dyn_size(entries); ++i) {
            free(entries[i]);
        }
        dyn_free(entries);
        error("Unable to find any partitions on buffer\n");
    }
    free(PTH);
    free(partBuffer);
    freeTempReader(tempReader);
    return entries;
}

bool vfsMountFilePart(const char* mountPoint, void* bytes, size_t size, uint8_t partIdx) {
    if (!vfsIsInitialized()) {
        vfsInitialize();
    }
    LOCK(rootMountSpinlock);
    dynarray(PartitionEntry*) partitionEntries = getPartEntriesBytes(bytes, size);
    if (dyn_size(partitionEntries) < partIdx) {
        for (size_t i = 0; i < dyn_size(partitionEntries); ++i) {
            free(partitionEntries[i]);
        }
        dyn_free(partitionEntries);
        warn("Partition index out of range\n");
        return false;
    }
    PartitionEntry* partEntry = partitionEntries[partIdx];
    partitionEntries[partIdx] = NULL;
    for (size_t i = 0; i < dyn_size(partitionEntries); ++i) {
        if (i == partIdx) {
            continue;
        }
        free(partitionEntries[i]);
    }
    dyn_free(partitionEntries);
    partEntry->attr      = 0;
    MSCDriver*   disk    = loadRAMMSC(bytes, size);
    DrvDiskPair* drvDisk = malloc(sizeof(DrvDiskPair));
    if (drvDisk == NULL) {
        error("Failed to allocate memory for driver-disk pair\n");
    }
    drvDisk->mscDriver           = disk;
    drvDisk->index               = 0;
    FSDriver*   fileSystemdriver = loadFSDriver(partEntry, drvDisk);
    const char* outMountPath     = NULL;
    MountPoint* parentMount = rootMount == NULL ? NULL : getParentMount(mountPoint, &outMountPath);
    const char* mpName      = outMountPath ? outMountPath : mountPoint;
    char*       newMP       = malloc(strlen(mpName));
    if (!newMP) {
        error("Failed to allocate memory for new mount point\n");
    }
    memset(newMP, 0, strlen(mpName));
    memcpy((void*)newMP, mpName, strlen(mpName));
    MountPoint* mp = createNewPoint(newMP, parentMount);
    mp->fsDriver   = fileSystemdriver;
    if (!parentMount) {
        rootMount = mp;
    } else {
        // TODO: Search for an open slot and try to place it there first
        dyn_push(parentMount->kids, mp);
    }
    UNLOCK(rootMountSpinlock);
    return true;
}

extern dynarray(MSCDriver*) vfsBlockDevices;
extern dynarray(dynarray(PartitionEntry*)) vfsPartArray;

bool vfsMountDiskPart(const char* mountPoint, uint8_t diskIdx, uint8_t partIdx) {
    if (!vfsIsInitialized()) {
        vfsInitialize();
    }
    LOCK(rootMountSpinlock);
    if (dyn_size(vfsBlockDevices) - 1 < diskIdx) {
        warn("Out of range disk index (Max %lu got %hhu)\n", dyn_size(vfsBlockDevices) - 1,
             diskIdx);
        UNLOCK(rootMountSpinlock);
        return false;
    }
    if (dyn_size(vfsPartArray) - 1 < diskIdx) {
        warn("Out of range disk index (Max %lu got %hhu)\n", dyn_size(vfsPartArray) - 1, diskIdx);
        UNLOCK(rootMountSpinlock);
        return false;
    }
    MSCDriver* disk                     = vfsBlockDevices[diskIdx];
    dynarray(PartitionEntry*) partArray = vfsPartArray[diskIdx];
    if (dyn_size(partArray) - 1 < partIdx) {
        warn("Out of range partition index\n");
        UNLOCK(rootMountSpinlock);
        return false;
    }
    PartitionEntry* entry = partArray[partIdx];
    info("Mounting partition GUID = %08.8x%08.8x%08.8x%08.8x\n", ((uint32_t*)entry->GUID)[0],
         ((uint32_t*)entry->GUID)[1], ((uint32_t*)entry->GUID)[2], ((uint32_t*)entry->GUID)[3]);
    DrvDiskPair* drvDisk = malloc(sizeof(DrvDiskPair));
    if (drvDisk == NULL) {
        error("Failed to allocate memory for driver-disk pair\n");
    }
    drvDisk->mscDriver           = disk;
    drvDisk->index               = 0;
    FSDriver*   fileSystemdriver = loadFSDriver(entry, drvDisk);
    const char* outMountPath     = NULL;
    MountPoint* parentMount = rootMount == NULL ? NULL : getParentMount(mountPoint, &outMountPath);
    const char* mpName      = outMountPath ? outMountPath : mountPoint;
    char*       newMP       = malloc(strlen(mpName));
    if (!newMP) {
        error("Failed to allocate memory for new mount point\n");
    }
    memset(newMP, 0, sizeof(mpName));
    memcpy((void*)newMP, mpName, strlen(mpName));
    MountPoint* mp = createNewPoint(newMP, parentMount);
    mp->fsDriver   = fileSystemdriver;
    if (!parentMount) {
        rootMount = mp;
    } else {
        // TODO: Search for an open slot and try to place it there first
        bool placed = false;
        for (size_t i = 0; i < dyn_size(parentMount->kids); ++i) {
            if (!parentMount->kids[i]) {
                parentMount->kids[i] = mp;
                placed               = true;
                break;
            }
        }
        if (!placed) {
            dyn_push(parentMount->kids, mp);
        }
    }
    UNLOCK(rootMountSpinlock);
    return true;
}

bool vfsUnmount(const char* mountPoint) {
    if (!vfsIsInitialized()) {
        vfsInitialize();
        return false;
    }
    LOCK(rootMountSpinlock);
    LOCK(vfsFilesSpinlock);
    MountPoint* mp = getMount(mountPoint);
    for (size_t i = 0; i < dyn_size(mp->parent->kids); ++i) {
        if (mp->parent->kids[i] == mp) {
            mp->parent->kids[i] = NULL;
            break;
        }
    }
    // mp->fsDriver->sync(mp->fsDriver);
    if (mp->fsDriver->references == 0) {
        mp->fsDriver->deinit(mp->fsDriver);
        free(mp->fsDriver);
    } else {
        warn("Auto lazy umount enabled for `%s` (references = %lu)\n", mp->name,
             mp->fsDriver->references);
        dyn_push(toCloseFSDrivers, mp->fsDriver);
    }
    free((void*)mp->name);
    bool hasKids = false;
    for (size_t i = 0; i < dyn_size(mp->kids); ++i) {
        if (mp->kids[i]) {
            hasKids = true;
        }
    }
    if (hasKids) {
        error("Unable to unmount directory that has kids\n");
    }
    dyn_free(mp->kids);
    free(mp);
    UNLOCK(vfsFilesSpinlock);
    UNLOCK(rootMountSpinlock);
    return true;
}