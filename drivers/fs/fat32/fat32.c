#include <common/dbg/dbg.h>
#include <common/minmax.h>
#include <ctype.h>
#include <drivers/fs/fat32.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
static inline int fatToupper(int c) {
    return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}
#define SECTOR_SIZE 512

static uint32_t readFat(FAT32DriverData* drvData, size_t lbaIdx, uint32_t offset);
static uint32_t readBytes(FAT32DriverData* drvData, FATFile* file, uint32_t bytesCount,
                          void* dataOut);
static bool     readEntry(FAT32DriverData* drvData, FATFile* file, FATDirEntry* dirEntry);

static uint32_t clusterToLBA(FAT32DriverData* drvData, uint32_t cluster) {
    return (drvData->dataSectionLBA + (cluster - 2) * drvData->bootSector->SectorsPerCluster);
}

static dynarray(uint16_t) appendLFN(FATDirEntry* entry) {
    const FATLFNEntry* lfn = (const FATLFNEntry*)(entry);
    if (lfn->Attributes != 0x0F) {
        debug("Not an LFN entry\n");
        return NULL;
    }
    uint16_t namePart[13] = {0};
    memcpy(&namePart[0], lfn->Name1, 5 * sizeof(uint16_t));
    memcpy(&namePart[5], lfn->Name2, 6 * sizeof(uint16_t));
    memcpy(&namePart[11], lfn->Name3, 2 * sizeof(uint16_t));
    uint8_t idx = 0;
    while (namePart[idx]) {
        idx++;
    }
    dynarray(uint16_t) tempRet = NULL;
    for (size_t i = 0; i < idx; ++i) {
        dyn_push(tempRet, namePart[i]);
    }
    return tempRet;
}

static dynarray(uint8_t) decodeLFN(const dynarray(uint16_t) buffer) {
    dynarray(uint8_t) result = NULL;
    for (size_t i = 0; i < dyn_size(buffer); ++i) {
        uint16_t c = buffer[i];
        if (c == 0xFFFF || c == 0x0000) break;
        if (c <= 0x7F) {
            dyn_push(result, (uint8_t)c);
        } else {
            dyn_push(result, (uint8_t)'?');
        }
    }
    dyn_push(result, 0);
    return result;
}

static void getShortName(char* name, char shortName[12]) {
    memset(shortName, ' ', 11);
    shortName[11]   = '\0';
    const char* ext = strchr(name, '.');
    if (ext == NULL) ext = name + 11;

    for (int i = 0; i < 8 && name[i] && (name + i) < ext; i++)
        shortName[i] = fatToupper((unsigned char)name[i]);

    if (ext != name + 11 && *ext == '.') {
        for (int i = 0; i < 3 && ext[i + 1]; i++)
            shortName[i + 8] = fatToupper((unsigned char)ext[i + 1]);
    }
}

static bool findFile(FAT32DriverData* drvData, FATFile* file, char* name, FATDirEntry* outEntry) {
    char         shortName[12];
    FATDirEntry* entry = malloc(sizeof(FATDirEntry));
    if (!entry) {
        error("Failed to allocate memory for directory entry\n");
        return false;
    }
    memset(entry, 0, sizeof(FATDirEntry));
    getShortName(name, shortName);
    dynarray(uint16_t) lfnBuffer = NULL;
    bool lfnActive               = false;

    while (readEntry(drvData, file, entry)) {
        if (entry->Name[0] == 0x00) {
            break;
        }
        if (entry->Name[0] == 0xE5) {
            dyn_free(lfnBuffer);
            lfnBuffer = NULL;
            lfnActive = false;
            continue;
        }
        if (entry->Attributes == (uint8_t)FAT_ATTR_LFN) {
            lfnActive                    = true;
            dynarray(uint16_t) tempArray = appendLFN(entry);
            for (size_t i = 0; i < dyn_size(tempArray); ++i) {
                dyn_push(lfnBuffer, tempArray[i]);
            }
            dyn_free(tempArray);
            continue;
        }
        if (lfnActive) {
            dynarray(uint8_t) lfnName = decodeLFN(lfnBuffer);
            if (strlen((const char*)lfnName) == strlen(name) &&
                strcmp((const char*)lfnName, name) == 0) {
                memcpy(outEntry, entry, sizeof(*outEntry));
                dyn_free(lfnName);
                dyn_free(lfnBuffer);
                free(entry);
                return true;
            }
            dyn_free(lfnName);
            dyn_free(lfnBuffer);
            lfnBuffer = NULL;
            lfnActive = false;
        }
        if (memcmp(shortName, entry->Name, 11) == 0) {
            memcpy(outEntry, entry, sizeof(*outEntry));
            if (lfnBuffer) {
                dyn_free(lfnBuffer);
                lfnBuffer = NULL;
            }
            free(entry);
            return true;
        }
    }
    if (lfnBuffer) dyn_free(lfnBuffer);
    free(entry);
    return false;
}

static FATFile* openEntry(FAT32DriverData* drvData, FATDirEntry* entry) {
    uint64_t handle = (uint64_t)-1;
    for (uint64_t i = 1; i < dyn_size(drvData->files); ++i) {
        if (drvData->files[i] == NULL) {
            error("File at index %lu deallocated before module release\n", i);
        }
        if (drvData->files[i]->opened == false) {
            handle = i;
            break;
        }
    }
    if (handle == (uint64_t)-1) {
        debug("Ran out of fat file datas, adding new one\n");
        FATFileData* fd = malloc(sizeof(FATFileData));
        if (fd == NULL) {
            error("Failed to allocate memory for new fat file data\n");
        }
        fd->opened = false;
        dyn_push(drvData->files, fd);
        for (uint64_t i = 1; i < dyn_size(drvData->files); ++i) {
            if (drvData->files[i] == NULL) {
                error("File at index %llu deallocated before module release\n", i);
            }
            if (drvData->files[i]->opened == false) {
                handle = i;
                break;
            }
        }
        if (handle == (uint64_t)-1) {
            error("No available file handles\n");
        }
    }
    FATFileData* fd            = drvData->files[handle];
    fd->Public.handle          = handle;
    fd->Public.isDirectory     = (entry->Attributes & (uint8_t)FAT_ATTR_DIRECTORY) != 0;
    fd->Public.size            = entry->Size;
    fd->Public.position        = 0;
    fd->opened                 = true;
    fd->firstCluster           = (entry->FirstClusterHigh << 16) | entry->FirstClusterLow;
    fd->currentCluster         = fd->firstCluster;
    fd->currentSectorInCluster = 0;
    drvData->drvDisk->mscDriver->read(
        drvData->drvDisk->mscDriver, fd->buffer,
        clusterToLBA(drvData, fd->firstCluster) + drvData->partEntry->startLBA, 1);
    return &fd->Public;
}

static bool readEntry(FAT32DriverData* drvData, FATFile* file, FATDirEntry* dirEntry) {
    return readBytes(drvData, file, sizeof(FATDirEntry), (void*)dirEntry) == sizeof(FATDirEntry);
}

static uint32_t nextCluster(FAT32DriverData* drvData, uint32_t currentCluster) {
    uint32_t fatByteOffset  = currentCluster * 4;
    uint32_t fatSectorIndex = fatByteOffset / SECTOR_SIZE;
    uint32_t sectorOffset   = fatByteOffset % SECTOR_SIZE;
    uint32_t nextCluster    = readFat(drvData, fatSectorIndex, sectorOffset);
    nextCluster &= 0x0FFFFFFF;
    return nextCluster;
}

static uint32_t readBytes(FAT32DriverData* drvData, FATFile* file, uint32_t bytesCount,
                          void* dataOut) {
    if (file->handle != FAT32_ROOT_DIRECTORY_HANDLE && file->handle >= dyn_size(drvData->files)) {
        error("Invalid file handle in readBytes\n");
    }
    FATFileData* fd = file->handle == FAT32_ROOT_DIRECTORY_HANDLE ? drvData->rootDirData
                                                                  : drvData->files[file->handle];
    uint8_t*     u8DataOut = (uint8_t*)dataOut;
    if (u8DataOut == NULL) {
        error("Invalid readBytes buffer passed in\n");
    }
    uint32_t oldBytesCount = bytesCount;
    if (!fd->Public.isDirectory || (fd->Public.isDirectory && fd->Public.size != 0)) {
        if (fd->Public.position >= fd->Public.size) return 0;
        bytesCount = min(bytesCount, (uint32_t)(fd->Public.size - fd->Public.position));
    }
    if (bytesCount != oldBytesCount) {
        warn("Attempted to read more bytes then the file has left, overriding from "
             "%u to %u\n",
             oldBytesCount, bytesCount);
    }
    while (bytesCount > 0) {
        uint32_t leftInBuffer = SECTOR_SIZE - (fd->Public.position % SECTOR_SIZE);
        uint32_t take         = min(bytesCount, leftInBuffer);
        memcpy(u8DataOut, fd->buffer + (fd->Public.position % SECTOR_SIZE), take);
        u8DataOut += take;
        fd->Public.position += take;
        bytesCount -= take;
        if (leftInBuffer == take) {
            if (fd->Public.handle == FAT32_ROOT_DIRECTORY_HANDLE) {
                fd->currentCluster++;
                drvData->drvDisk->mscDriver->read(
                    drvData->drvDisk->mscDriver, fd->buffer,
                    (clusterToLBA(drvData, fd->currentCluster) + fd->currentSectorInCluster) +
                        drvData->partEntry->startLBA,
                    1);
            } else {
                if (++fd->currentSectorInCluster >= drvData->bootSector->SectorsPerCluster) {
                    fd->currentSectorInCluster = 0;
                    fd->currentCluster         = nextCluster(drvData, fd->currentCluster);
                }
                if (fd->currentCluster >= 0xFFFFFFF8) {
                    fd->Public.size = fd->Public.position;
                    debug("End of cluster chain at cluster=%lu\n", fd->currentCluster);
                    break;
                }
                drvData->drvDisk->mscDriver->read(
                    drvData->drvDisk->mscDriver, fd->buffer,
                    (clusterToLBA(drvData, fd->currentCluster) + fd->currentSectorInCluster) +
                        drvData->partEntry->startLBA,
                    1);
            }
        }
    }
    return (uint32_t)(u8DataOut - (uint8_t*)dataOut);
}

static uint32_t readFat(FAT32DriverData* drvData, size_t lbaIdx, uint32_t offset) {
    size_t   fatLba = drvData->bootSector->ReservedSectors + lbaIdx + drvData->partEntry->startLBA;
    uint8_t* temp   = malloc(SECTOR_SIZE);
    if (!temp) error("Out of memory reading FAT\n");
    // debug("readFat drvData = 0x%lx, drvData->partEntry = 0x%lx, lbaIdx = %lu, offset = %lu\n",
    //       drvData, drvData->partEntry, lbaIdx, offset);
    drvData->drvDisk->mscDriver->read(drvData->drvDisk->mscDriver, temp, fatLba, 1);
    uint32_t ret = *(uint32_t*)(temp + offset);
    free(temp);
    return ret;
}

static bool FAT32read(FSDriver* this, uint64_t fd, void* buffer, size_t length) {
    LOCK(this->lock);
    FAT32DriverData* drvData = (FAT32DriverData*)this->drvData;
    if (fd >= dyn_size(drvData->files) && fd != FAT32_ROOT_DIRECTORY_HANDLE) {
        warn("Out of range FD\n");
        UNLOCK(this->lock);
        return false;
    }
    FATFile  file       = fd == FAT32_ROOT_DIRECTORY_HANDLE ? drvData->rootDirData->Public
                                                            : drvData->files[fd]->Public;
    uint32_t readLength = min(length, (size_t)file.size);
    if (readLength != length) {
        debug("Overwritten input length of %u to max length of %u\n", length, readLength);
    }
    readBytes(drvData, &file, readLength, buffer);
    UNLOCK(this->lock);
    return true;
}

static bool FAT32write(FSDriver* this, uint64_t fd, const void* buffer, size_t length) {
    (void)this;
    (void)fd;
    (void)buffer;
    (void)length;
    error("Writing to FAT32 partitions isn't supported\n");
}

static void FAT32create(FSDriver* this, const char* path, uint32_t permissions) {
    (void)this;
    (void)path;
    (void)permissions;
    error("Creating FAT32 files/dirs isn't supported\n");
}

static void FAT32close(FSDriver* this, uint64_t fd) {
    LOCK(this->lock);
    FAT32DriverData* drvData = (FAT32DriverData*)this->drvData;
    if (fd >= dyn_size(drvData->files) && fd != FAT32_ROOT_DIRECTORY_HANDLE) {
        warn("Out of range FD\n");
        UNLOCK(this->lock);
        return;
    }
    if (fd == FAT32_ROOT_DIRECTORY_HANDLE) {
        drvData->rootDirData->Public.position        = 0;
        drvData->rootDirData->currentCluster         = drvData->rootDirData->firstCluster;
        drvData->rootDirData->currentSectorInCluster = 0;
    } else {
        drvData->files[fd]->opened = false;
    }
    UNLOCK(this->lock);
}

static uint64_t FAT32open(FSDriver* this, const char* path) {
    LOCK(this->lock);
    const char*      pathCopy = path;
    FAT32DriverData* drvData  = (FAT32DriverData*)this->drvData;
    if (*path == '/') {
        path += 1;
    }
    char name[256];
    memset(name, 0, sizeof(name));
    FATFile* current = &drvData->rootDirData->Public;
    debug("Trying to find %s\n", path);
    while (*path) {
        bool        isLast = false;
        const char* delim  = strchr(path, '/');
        if (delim != NULL) {
            size_t n = (size_t)(delim - path);
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, path, n);
            name[n] = '\0';
            path    = delim + 1;
        } else {
            size_t len = strlen(path);
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, path, len);
            name[len] = '\0';
            path += len;
            isLast = true;
        }
        debug("Finding `%s` (delim `%s`)\n", name, delim);
        FATDirEntry entry;
        if (findFile(drvData, current, name, &entry)) {
            if (!isLast && (entry.Attributes & FAT_ATTR_DIRECTORY) == 0) {
                debug("`%s` is not a directory\n", name);
                UNLOCK(this->lock);
                this->close(this, current->handle);
                return (uint64_t)-1;
            }
            UNLOCK(this->lock);
            debug("Found `%s`\n", name);
            this->close(this, current->handle);
            LOCK(this->lock);
            current = openEntry(drvData, &entry);
        } else {
            debug("`%s` does not exist\n", name);
            UNLOCK(this->lock);
            this->close(this, current->handle);
            return (uint64_t)-1;
        }
    }
    current->name = strdup(name);
    UNLOCK(this->lock);
    debug("`%s` resolved to handle %lu (File size: %lu)\n", pathCopy, current->handle,
          this->getLength(this, current->handle));
    return current->handle;
}

static size_t FAT32GetOffset(FSDriver* this, uint64_t fd) {
    LOCK(this->lock);
    FAT32DriverData* drvData = (FAT32DriverData*)this->drvData;
    if (fd >= dyn_size(drvData->files) && fd != FAT32_ROOT_DIRECTORY_HANDLE) {
        warn("Out of range FD\n");
        UNLOCK(this->lock);
        return false;
    }
    FATFile  file   = fd == FAT32_ROOT_DIRECTORY_HANDLE ? drvData->rootDirData->Public
                                                        : drvData->files[fd]->Public;
    uint64_t offset = file.position;
    UNLOCK(this->lock);
    return offset;
}

static size_t FAT32GetLength(FSDriver* this, uint64_t fd) {
    LOCK(this->lock);
    FAT32DriverData* drvData = (FAT32DriverData*)this->drvData;
    if (fd >= dyn_size(drvData->files) && fd != FAT32_ROOT_DIRECTORY_HANDLE) {
        warn("Out of range FD\n");
        UNLOCK(this->lock);
        return false;
    }
    FATFile  file = fd == FAT32_ROOT_DIRECTORY_HANDLE ? drvData->rootDirData->Public
                                                      : drvData->files[fd]->Public;
    uint64_t size = file.size;
    UNLOCK(this->lock);
    return size;
}

static void FAT32init(FSDriver* this, PartitionEntry* entry, DrvDiskPair* drvDisk) {
    LOCK(this->lock);
    FAT32DriverData* drvData = malloc(sizeof(FAT32DriverData));
    if (!drvData) {
        error("Failed to allocate memory for driver data\n");
    }
    drvData->bootSector = malloc(sizeof(FATBootSector));
    if (!drvData->bootSector) {
        error("Failed to allocate memory for boot sector\n");
    }
    drvDisk->mscDriver->read(drvDisk->mscDriver, drvData->bootSector, entry->startLBA, 1);
    drvData->dataSectionLBA =
        (drvData->bootSector->ReservedSectors +
         drvData->bootSector->EBR32.SectorsPerFat * drvData->bootSector->FatCount);
    uint32_t rootLBA = clusterToLBA(drvData, drvData->bootSector->EBR32.RootDirectoryCluster == 0
                                                 ? 2
                                                 : drvData->bootSector->EBR32.RootDirectoryCluster);
    drvData->rootDirData = malloc(sizeof(FATFileData));
    if (!drvData->rootDirData) {
        error("Failed to allocate memory for root directory\n");
    }
    drvData->rootDirData->Public.handle      = FAT32_ROOT_DIRECTORY_HANDLE;
    drvData->rootDirData->Public.isDirectory = true;
    drvData->rootDirData->Public.position    = 0;
    drvData->rootDirData->Public.size    = sizeof(FATDirEntry) * drvData->bootSector->DirEntryCount;
    drvData->rootDirData->opened         = true;
    drvData->rootDirData->firstCluster   = rootLBA;
    drvData->rootDirData->currentCluster = rootLBA;
    drvData->rootDirData->currentSectorInCluster = 0;
    drvData->drvDisk                             = drvDisk;
    drvData->drvDisk->mscDriver->read(drvData->drvDisk->mscDriver, drvData->rootDirData->buffer,
                                      rootLBA + entry->startLBA, 1);
    drvData->files     = NULL;
    drvData->partEntry = entry;
    dyn_push(drvData->files, NULL);
    this->drvData = drvData;
    UNLOCK(this->lock);
}

static void FAT32deinit(FSDriver* this) {
    LOCK(this->lock);
    FAT32DriverData* drvData   = (FAT32DriverData*)this->drvData;
    bool             canDeinit = true;
    for (size_t i = 0; i < dyn_size(drvData->files); ++i) {
        if (drvData->files[i]) {
            if (drvData->files[i]->opened) {
                info("Open file `%s` exists\n", drvData->files[i]->Public.name);
                canDeinit = false;
            } else {
                free(drvData->files[i]);
            }
        }
    }
    if (!canDeinit) {
        error("Failed to deinit FAT, open file still exists\n");
    }
    dyn_free(drvData->files);
    free(drvData->rootDirData);
    free(drvData->bootSector);
    free(drvData);
    UNLOCK(this->lock);
}

static void FAT32seek(FSDriver* this, uint64_t fd, size_t offset) {
    LOCK(this->lock);
    FAT32DriverData* drvData = (FAT32DriverData*)this->drvData;
    if (fd >= dyn_size(drvData->files) && fd != FAT32_ROOT_DIRECTORY_HANDLE) {
        warn("Out of range FD\n");
        UNLOCK(this->lock);
        return;
    }
    // debug("FAT32Seek this = 0x%lx, fd = %lu, offset = %lu\n", this, fd, offset);
    FATFileData* fileData =
        (fd == FAT32_ROOT_DIRECTORY_HANDLE) ? drvData->rootDirData : drvData->files[fd];
    if (offset > fileData->Public.size) {
        debug("Seek offset %lu exceeds file size %lu\n", offset, fileData->Public.size);
        offset = fileData->Public.size;
    }
    uint32_t bytesPerCluster    = drvData->bootSector->SectorsPerCluster * SECTOR_SIZE;
    uint32_t newClusterOffset   = offset / bytesPerCluster;
    uint32_t newSectorInCluster = (offset % bytesPerCluster) / SECTOR_SIZE;
    if (newClusterOffset != (fileData->Public.position / bytesPerCluster)) {
        fileData->currentCluster = fileData->firstCluster;
        for (uint32_t i = 0; i < newClusterOffset; i++) {
            fileData->currentCluster = nextCluster(drvData, fileData->currentCluster);
            if (fileData->currentCluster >= 0xFFFFFFF8) {
                warn("Invalid cluster chain during seek\n");
                break;
            }
        }
    }
    fileData->currentSectorInCluster = newSectorInCluster;
    drvData->drvDisk->mscDriver->read(
        drvData->drvDisk->mscDriver, fileData->buffer,
        (clusterToLBA(drvData, fileData->currentCluster) + fileData->currentSectorInCluster) +
            drvData->partEntry->startLBA,
        1);
    fileData->Public.position = offset;
    UNLOCK(this->lock);
}

FSDriver* FAT32GetDriver(PartitionEntry* entry, DrvDiskPair* drvDisk) {
    FSDriver* driver = malloc(sizeof(FSDriver));
    if (!driver) {
        error("Failed to allocate memory for FAT driver\n");
    }
    driver->lock      = false;
    driver->init      = FAT32init;
    driver->deinit    = FAT32deinit;
    driver->create    = FAT32create;
    driver->open      = FAT32open;
    driver->close     = FAT32close;
    driver->read      = FAT32read;
    driver->write     = FAT32write;
    driver->seek      = FAT32seek;
    driver->getOffset = FAT32GetOffset;
    driver->getLength = FAT32GetLength;
    driver->init(driver, entry, drvDisk);
    return driver;
}
