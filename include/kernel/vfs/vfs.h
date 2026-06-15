#if !defined(__KERNEL_VFS_VFS_H__)
#define __KERNEL_VFS_VFS_H__
#include <common/dynarray.h>
#include <common/spinlock.h>
#include <drivers/fs.h>
#include <drivers/msc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MountPoint {
    const char* name;
    // TODO: Do we really want mount flags?
    struct MountPoint* parent;
    dynarray(struct MountPoint*) kids;
    struct FSDriver* fsDriver;
} MountPoint;

typedef struct VFSFile {
    uint64_t    vfsHandle;
    uint64_t    fsHandle;
    MountPoint* mount;
    bool        used;
    uint32_t    flags;
    const char* pathWithoutMountPoint;
} VFSFile;

typedef struct DrvDiskPair {
    MSCDriver* mscDriver;
    uint8_t    index;
} DrvDiskPair;

extern Spinlock    rootMountSpinlock;
extern MountPoint* rootMount;
extern Spinlock    vfsFilesSpinlock;
extern dynarray(VFSFile*) vfsFiles;
extern dynarray(struct FSDriver*) toCloseFSDrivers;

size_t          vfsGetPartCount();
PartitionEntry* vfsGetPartInfo(size_t index, uint8_t* outDiskId, uint8_t* outPartId);
bool            vfsMountFilePart(const char* mountPoint, void* bytes, size_t size, uint8_t partIdx);
bool            vfsMountDiskPart(const char* mountPoint, uint8_t diskIdx, uint8_t partIdx);
bool            vfsUnmount(const char* mountPoint);
void            vfsRead(uint64_t fileIndex, void* outData, size_t size);
void            vfsSeek(uint64_t fileIndex, size_t seekOffset);
size_t          vfsGetOffset(uint64_t fileIndex);
uint64_t        vfsGetLength(uint64_t fileIndex);
void            vfsCreate(const char* path, uint32_t permissions);
uint64_t        vfsOpen(const char* path, uint32_t flags);
void            vfsClose(uint64_t fileIndex);
void            vfsInitialize();
bool            vfsIsInitialized();

#endif // __KERNEL_VFS_VFS_H__
