#include <common/dbg/dbg.h>
#include <drivers/fs.h>
#include <drivers/fs/fat32.h>
#include <drivers/fs/sfs.h>
static const uint32_t FAT32_GUID = 0xc12a7328;
static const uint32_t SFS_GUID   = 0xebd0a0a2;
//  6e756f4d

FSDriver* loadFSDriver(PartitionEntry* partEntry, DrvDiskPair* drvDisk) {
    if (memcmp(partEntry->GUID, (void*)(uintptr_t)(&FAT32_GUID), sizeof(uint32_t)) == 0) {
        return FAT32GetDriver(partEntry, drvDisk);
    } else if (memcmp(partEntry->GUID, (void*)(uintptr_t)(&SFS_GUID), sizeof(uint32_t)) == 0) {
        return SFSGetDriver(partEntry, drvDisk);
    }
    error("Invalid partition GUID %08.8x\n", ((uint32_t*)partEntry->GUID)[0]);
}