#if !defined(__DRIVERS_FS_FAT32_H__)
#define __DRIVERS_FS_FAT32_H__
#include "../fs.h"

#include <common/dynarray.h>
#include <stdint.h>
#define FAT32_ROOT_DIRECTORY_HANDLE 0

typedef struct __attribute__((packed)) FATExtendedBootRecord {
    uint8_t  DriveNumber;
    uint8_t  _Reserved;
    uint8_t  Signature;
    uint32_t VolumeId;
    uint8_t  VolumeLabel[11];
    uint8_t  SystemId[8];
} FATExtendedBootRecord;

typedef struct __attribute((packed)) FAT32ExtendedBootRecord {
    uint32_t              SectorsPerFat;
    uint16_t              Flags;
    uint16_t              FatVersion;
    uint32_t              RootDirectoryCluster;
    uint16_t              FSInfoSector;
    uint16_t              BackupBootSector;
    uint8_t               _Reserved[12];
    FATExtendedBootRecord EBR;
} FAT32ExtendedBootRecord;

typedef struct __attribute__((packed)) FATBootSector {
    uint8_t  BootJumpInstruction[3];
    uint8_t  OemIdentifier[8];
    uint16_t BytesPerSector;
    uint8_t  SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t  FatCount;
    uint16_t DirEntryCount;
    uint16_t TotalSectors;
    uint8_t  MediaDescriptorType;
    uint16_t SectorsPerFat;
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t LargeSectorCount;
    union {
        FATExtendedBootRecord   EBR1216;
        FAT32ExtendedBootRecord EBR32;
    };
    uint8_t reserved0[422];
} FATBootSector;

typedef struct __attribute__((packed)) FATDirEntry {
    uint8_t  Name[11];
    uint8_t  Attributes;
    uint8_t  _Reserved;
    uint8_t  CreatedTimeTenths;
    uint16_t CreatedTime;
    uint16_t CreatedDate;
    uint16_t AccessedDate;
    uint16_t FirstClusterHigh;
    uint16_t ModifiedTime;
    uint16_t ModifiedDate;
    uint16_t FirstClusterLow;
    uint32_t Size;
} FATDirEntry;

typedef struct __attribute__((packed)) FATLFNEntry {
    uint8_t  sequence;
    uint16_t Name1[5];
    uint8_t  Attributes;
    uint8_t  Type;
    uint8_t  Checksum;
    uint16_t Name2[6];
    uint16_t FirstCluster;
    uint16_t Name3[2];
} FATLFNEntry;

typedef struct FATFile {
    uint64_t    handle;
    bool        isDirectory;
    uint32_t    position;
    uint32_t    size;
    const char* name;
} FATFile;

typedef struct FATFileData {
    FATFile  Public;
    bool     opened;
    uint32_t firstCluster;
    uint32_t currentCluster;
    uint32_t currentSectorInCluster;
    char buffer[512];
} FATFileData;

typedef enum FATAttributes {
    FAT_ATTR_READ_ONLY = 0x01,
    FAT_ATTR_HIDDEN    = 0x02,
    FAT_ATTR_SYSTEM    = 0x04,
    FAT_ATTR_VOLUME_ID = 0x08,
    FAT_ATTR_DIRECTORY = 0x10,
    FAT_ATTR_ARCHIVE   = 0x20,
    FAT_ATTR_LFN       = FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID
} FATAttributes;

typedef struct FAT32DriverData {
    FATBootSector* bootSector;
    FATFileData*   rootDirData;
    dynarray(FATFileData*) files;
    size_t dataSectionLBA;
    PartitionEntry* partEntry;
    DrvDiskPair* drvDisk;
} FAT32DriverData;

FSDriver* FAT32GetDriver(PartitionEntry* entry, DrvDiskPair* drvDisk);

#endif // __DRIVERS_FS_FAT32_H__
