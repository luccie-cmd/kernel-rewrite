#if !defined(__DRIVERS_FS_SFS_H__)
#define __DRIVERS_FS_SFS_H__
#include "../fs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum SFSBlockTypes : uint8_t {
    SFS_BLOCKTYPE_EMPTY = 0,
    SFS_BLOCKTYPE_SUPERBLOCK,
    SFS_BLOCKTYPE_DIRECTORY,
    SFS_BLOCKTYPE_FILE,
    SFS_BLOCKTYPE_NAME,
    SFS_BLOCKTYPE_DATA,
    SFS_BLOCKTYPE_TEMP,
} SFSBlockTypes;
typedef struct __attribute__((packed)) SFSBlockHeader {
    SFSBlockTypes type;
    uint64_t      currentLBA;
} SFSBlockHeader;
typedef struct __attribute__((packed)) SuperBlockBlock {
    // uint32_t       magic; // "SFSF"
    SFSBlockHeader header;
    uint64_t       rootDirLBA;
    uint8_t        padding[495];
} SuperBlockBlock;
static_assert(sizeof(SuperBlockBlock) == 512, "SuperBlockBlock alignment is messed up");
typedef struct __attribute__((packed)) DirectoryBlock {
    SFSBlockHeader header;
    uint64_t       nextDirBlock;
    uint64_t       nameBlock;
    uint32_t       blocksCount;
    uint64_t       blocksLBA[60];
    uint8_t        padding[3];
} DirectoryBlock;
static_assert(sizeof(DirectoryBlock) == 512, "DirectoryBlock alignment is messed up");
typedef struct __attribute__((packed)) FileBlock {
    SFSBlockHeader header;
    uint64_t       nameBlock;
    uint64_t       dataBlock;
    uint32_t       permissions;
    uint8_t        padding[483];
} FileBlock;
static_assert(sizeof(FileBlock) == 512, "FileBlock alignment is messed up");
typedef struct __attribute__((packed)) NameBlock {
    SFSBlockHeader header;
    uint64_t       nextName;
    uint16_t       length;
    uint8_t        characters[493];
} NameBlock;
static_assert(sizeof(NameBlock) == 512, "NameBlock alignment is messed up");
typedef struct __attribute__((packed)) DataBlock {
    SFSBlockHeader header;
    uint64_t       nextData;
    uint32_t       blockCount;
    uint16_t       lastBlockSize;
    uint64_t       startLBA;
    uint8_t        padding[481];
} DataBlock;
static_assert(sizeof(DataBlock) == 512, "DataBlock alignment is messed up");
typedef struct SFSFile {
    bool     opened;
    uint64_t lba;
    uint64_t position;
    uint64_t length;
    uint64_t index;
} SFSFile;

FSDriver* SFSGetDriver(PartitionEntry* entry, DrvDiskPair* drvDisk);

#endif // __DRIVERS_FS_SFS_H__
