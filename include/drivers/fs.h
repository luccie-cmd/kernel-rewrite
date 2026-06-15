#if !defined(__DRIVERS_FS_H__)
#define __DRIVERS_FS_H__
#include <kernel/vfs/gpt.h>
#include <kernel/vfs/vfs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPEN_FLAG_READ (1 << 0)
#define OPEN_FLAG_WRITE (1 << 1)
#define OPEN_FLAG_CREATE (1 << 2)
#define OPEN_FLAG_APPEND (1 << 3)
#define OPEN_FLAG_TRUNCATE (1 << 4)

typedef struct DrvDiskPair DrvDiskPair;

typedef struct FSDriver {
    bool (*read)(struct FSDriver* this, uint64_t fd, void* buffer, size_t length);
    bool (*write)(struct FSDriver* this, uint64_t fd, const void* buffer, size_t length);
    void (*seek)(struct FSDriver* this, uint64_t fd, size_t seekOffset);
    size_t (*getOffset)(struct FSDriver* this, uint64_t fd);
    size_t (*getLength)(struct FSDriver* this, uint64_t fd);
    void (*close)(struct FSDriver* this, uint64_t fd);
    uint64_t (*open)(struct FSDriver* this, const char* path);
    void (*create)(struct FSDriver* this, const char* path, uint32_t permissions);
    void (*init)(struct FSDriver* this, PartitionEntry* partEntry, DrvDiskPair* drvDisk);
    void (*deinit)(struct FSDriver* this);
    void*    drvData;
    Spinlock lock;
    size_t   references;
} FSDriver;

FSDriver* loadFSDriver(PartitionEntry* partEntry, DrvDiskPair* drvDisk);

#endif // __DRIVERS_FS_H__
