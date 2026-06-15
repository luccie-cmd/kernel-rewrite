#define _POSIX_C_SOURCE 200809L
#include <common/minmax.h>
#include <drivers/fs/sfs.h>
#include <string.h>

typedef struct SFSDriverData {
    PartitionEntry* partEntry;
    DrvDiskPair*    drvDisk;
    DirectoryBlock* rootDir;
    dynarray(SFSFile*) files;
} SFSDriverData;

// static inline void readLBA(SFSDriverData* data, uint64_t LBA, uint32_t length, void* buffer) {
//     debug("buffer, data->partentry = 0x%lx 0x%lx\n", buffer, data->partEntry);
//     debug("Reading LBA %lu (final: %lu)\n", LBA, LBA + data->partEntry->startLBA);
//     data->drvDisk->mscDriver->read(data->drvDisk->mscDriver, buffer,
//                                    LBA + data->partEntry->startLBA, length);
// }

static void readCompleteFunction(MSCDriverRequest* request) {
    request->done = true;
    if (request->proc) {
        request->proc->state = PROCESSSTATE_READY;
    }
}

static inline void readLBA(SFSDriverData* data, uint64_t LBA, uint32_t length, void* buffer) {
    debug("buffer, data->partentry = 0x%lx 0x%lx\n", buffer, data->partEntry);
    debug("Reading LBA %lu (final: %lu)\n", LBA, LBA + data->partEntry->startLBA);
    MSCDriverRequest* req = malloc(sizeof(MSCDriverRequest));
    *req                  = (MSCDriverRequest){
        .buffer           = buffer,
        .lba              = LBA + data->partEntry->startLBA,
        .length           = length,
        .completeFunction = readCompleteFunction,
        .context          = data,
        .done             = false,
        .read             = true,
    };
    if (!data->drvDisk->mscDriver->issueCommand) {
        warn("Fallback to old methods\n");
        data->drvDisk->mscDriver->read(data->drvDisk->mscDriver, buffer,
                                       LBA + data->partEntry->startLBA, length);
    } else {
        data->drvDisk->mscDriver->issueCommand(data->drvDisk->mscDriver, req);
        __asm__("int $237");
        req->proc->state = PROCESSSTATE_BLOCKED;
        info("HOLY SHIT\n");
    }
    free(req);
}

static inline void writeLBA(SFSDriverData* data, uint64_t LBA, uint32_t length, void* buffer) {
    debug("Writing LBA %lu (final: %lu)\n", LBA, LBA + data->partEntry->startLBA);
    data->drvDisk->mscDriver->write(data->drvDisk->mscDriver, buffer,
                                    LBA + data->partEntry->startLBA, length);
}

static inline bool validateBlock(SFSBlockHeader hdr, SFSBlockTypes expectedType, uint64_t readLba) {
    if (hdr.currentLBA != readLba) {
        warn("hdr.currentLBA != readLba (%lu != %lu)\n", hdr.currentLBA, readLba);
        return false;
    }
    if (hdr.type != expectedType) {
        warn("hdr.type != expectedType (%hu != %hu)\n", hdr.type, expectedType);
        return false;
    }
    return true;
}

static DirectoryBlock* readDirBlock(SFSDriverData* data, uint64_t LBA) {
    DirectoryBlock* dirBlock = malloc(sizeof(DirectoryBlock));
    if (!dirBlock) {
        error("Failed to allocate memory for directory block\n");
    }
    memset(dirBlock, 0, sizeof(DirectoryBlock));
    readLBA(data, LBA, 1, (void*)dirBlock);
    if (!validateBlock(dirBlock->header, SFS_BLOCKTYPE_DIRECTORY, LBA)) {
        error("Previous warning\n");
    }
    return dirBlock;
}

static const char* collectName(SFSDriverData* data, uint64_t lba) {
    uint64_t    currentLba  = lba;
    NameBlock*  name        = malloc(sizeof(NameBlock));
    const char* buffer      = NULL;
    size_t      currentSize = 0;
    while (currentLba) {
        readLBA(data, currentLba, 1, name);
        if (!validateBlock(name->header, SFS_BLOCKTYPE_NAME, currentLba)) {
            error("Invalid name chain\n");
        }
        buffer = realloc((void*)buffer, currentSize + name->length);
        if (!buffer) {
            error("Failed to realloc buffer\n");
        }
        memcpy((char*)buffer + currentSize, name->characters, name->length);
        currentSize += name->length;
        currentLba = name->nextName;
    }
    free(name);
    if (buffer) {
        ((char*)buffer)[currentSize - 1] = '\0';
    }
    return buffer;
}

static DirectoryBlock* findDirBlockByName(SFSDriverData* data, DirectoryBlock* block,
                                          const char* name) {
    uint64_t currentLba = block->header.currentLBA;
    while (currentLba) {
        DirectoryBlock* handleBlock =
            currentLba == block->header.currentLBA ? block : readDirBlock(data, currentLba);
        for (size_t i = 0; i < handleBlock->blocksCount; ++i) {
            uint8_t* tmpBuffer = malloc(512);
            readLBA(data, handleBlock->blocksLBA[i], 1, tmpBuffer);
            if (tmpBuffer[0] != SFS_BLOCKTYPE_DIRECTORY) {
                free(tmpBuffer);
                continue;
            }
            DirectoryBlock* childBlock = (DirectoryBlock*)(tmpBuffer);
            const char*     childName  = collectName(data, childBlock->nameBlock);
            if (!childName) {
                error("Childname was null\n");
            }
            if (strcmp(childName, name) == 0) {

                if (currentLba != block->header.currentLBA) {
                    free(handleBlock);
                }
                free((char*)childName);
                return childBlock;
            }
            free(childBlock);
            free((char*)childName);
        }
        if (handleBlock != block) {
            free(handleBlock);
        }
        currentLba = block->nextDirBlock;
    }
    return NULL;
}

static FileBlock* findFileBlockByName(SFSDriverData* data, DirectoryBlock* block,
                                      const char* name) {
    uint64_t currentLba = block->header.currentLBA;
    while (currentLba) {
        DirectoryBlock* handleBlock =
            currentLba == block->header.currentLBA ? block : readDirBlock(data, currentLba);
        for (size_t i = 0; i < handleBlock->blocksCount; ++i) {
            uint8_t* tmpBuffer = malloc(512);
            readLBA(data, handleBlock->blocksLBA[i], 1, tmpBuffer);
            if (tmpBuffer[0] != SFS_BLOCKTYPE_FILE) {
                free(tmpBuffer);
                continue;
            }
            FileBlock*  childBlock = (FileBlock*)(tmpBuffer);
            const char* childName  = collectName(data, childBlock->nameBlock);
            if (!childName) {
                error("Childname was null\n");
            }
            if (strcmp(childName, name) == 0) {
                if (currentLba != block->header.currentLBA) {
                    free(handleBlock);
                }
                free((char*)childName);
                return childBlock;
            }
            free(childBlock);
            free((char*)childName);
        }
        if (handleBlock != block) {
            free(handleBlock);
        }
        currentLba = block->nextDirBlock;
    }
    return NULL;
}

static uint64_t findFreeBlock(SFSDriverData* data) {
    // TODO: B-Trees
    uint64_t LBA = data->rootDir->header.currentLBA + 1;
    info("Starting from LBA %lu (To %lu)\n", LBA,
         data->partEntry->endLBA - data->partEntry->startLBA);
    uint8_t* buffer = malloc(512);
    while (LBA < data->partEntry->endLBA - data->partEntry->startLBA) {
        readLBA(data, LBA, 1, buffer);
        if (*buffer == 0) {
            *buffer = SFS_BLOCKTYPE_TEMP;
            writeLBA(data, LBA, 1, buffer);
            free(buffer);
            debug("New LBA = %lu\n", LBA);
            return LBA;
        }
        LBA++;
    }
    error("Unable to find usable LBA\n");
}

static uint64_t constructNameBlock(SFSDriverData* data, const char* name) {
    if (name == NULL) {
        error("Invalid name\n");
    }
    uint64_t firstLba   = 0;
    uint64_t nameLength = strlen(name);
    uint64_t offset     = 0;
    while (nameLength) {
        uint64_t lba = findFreeBlock(data);
        if (firstLba == 0) {
            firstLba = lba;
        }
        NameBlock* block = malloc(sizeof(NameBlock));
        if (!block) {
            error("Failed to allocate name block\n");
        }
        memset(block, 0, sizeof(NameBlock));
        uint16_t take = min(nameLength, sizeof(block->characters));
        nameLength -= take;
        block->header.currentLBA = lba;
        block->header.type       = SFS_BLOCKTYPE_NAME;
        block->length            = take;
        memcpy(block->characters, name + offset, take);
        writeLBA(data, lba, 1, block);
        free(block);
    }
    return firstLba;
}

static void appendBlock(SFSDriverData* data, DirectoryBlock* block, uint64_t childLba) {
    if (block->blocksCount < sizeof(block->blocksLBA) / sizeof(block->blocksLBA[0])) {
        block->blocksLBA[block->blocksCount++] = childLba;
        writeLBA(data, block->header.currentLBA, 1, block);
    } else {
        todo(true, "Expand block and write child there\n");
    }
}

static DirectoryBlock* createDirBlock(SFSDriverData* data, uint64_t LBA, const char* name) {
    DirectoryBlock* block = malloc(sizeof(DirectoryBlock));
    if (!block) {
        error("Failed to allocate directory block\n");
    }
    memset(block, 0, sizeof(DirectoryBlock));
    block->header.currentLBA = LBA;
    block->header.type       = SFS_BLOCKTYPE_DIRECTORY;
    block->nameBlock         = constructNameBlock(data, name);
    return block;
}

static uint64_t constructDataBlock(SFSDriverData* data) {
    uint64_t   lba   = findFreeBlock(data);
    DataBlock* block = malloc(sizeof(DataBlock));
    if (!block) {
        error("Failed to allocate data block\n");
    }
    memset(block, 0, sizeof(DataBlock));
    block->header.currentLBA = lba;
    block->header.type       = SFS_BLOCKTYPE_DATA;
    block->blockCount        = 0;
    block->lastBlockSize     = 0;
    block->startLBA          = 0;
    writeLBA(data, lba, 1, block);
    if (!validateBlock(block->header, SFS_BLOCKTYPE_DATA, lba)) {
        error("Failed to validate data block\n");
    }
    debug("Data LBA at %lu\n", lba);
    return lba;
}

static FileBlock* createFileBlock(SFSDriverData* data, uint64_t LBA, const char* name) {
    FileBlock* block = malloc(sizeof(FileBlock));
    if (!block) {
        error("Failed to allocate file block\n");
    }
    memset(block, 0, sizeof(FileBlock));
    block->header.currentLBA = LBA;
    block->header.type       = SFS_BLOCKTYPE_FILE;
    block->nameBlock         = constructNameBlock(data, name);
    block->dataBlock         = constructDataBlock(data);
    return block;
}

static void Init(FSDriver* this, PartitionEntry* partEntry, DrvDiskPair* drvDisk) {
    LOCK(this->lock);
    debug("Init\n");
    SFSDriverData* drvData = malloc(sizeof(SFSDriverData));
    if (!drvData) {
        error("Failed to allocate memory for driver data\n");
    }
    memset(drvData, 0, sizeof(SFSDriverData));
    // drvData->partEntry = malloc(sizeof(PartitionEntry));
    // if (!drvData->partEntry) {
    //     error("Failed to allocate memory for partition entry\n");
    // }
    // memcpy(drvData->partEntry, partEntry, sizeof(PartitionEntry));
    // free(partEntry);
    drvData->partEntry = partEntry;
    info("%lx (Ranging from %lu-%lu)\n", partEntry, partEntry->startLBA, partEntry->endLBA);
    drvData->drvDisk            = drvDisk;
    SuperBlockBlock* superBlock = malloc(sizeof(SuperBlockBlock));
    readLBA(drvData, 0, 1, (void*)superBlock);
    if (!validateBlock(superBlock->header, SFS_BLOCKTYPE_SUPERBLOCK, 0)) {
        error("Invalid superblock\n");
    }
    debug("superblock LBA = %lu\n", superBlock->rootDirLBA);
    DirectoryBlock* dirBlock = readDirBlock(drvData, superBlock->rootDirLBA);
    free(superBlock);
    drvData->rootDir = dirBlock;
    this->drvData    = drvData;
    UNLOCK(this->lock);
}

static SFSFile* findAvaliableFile(SFSDriverData* data) {
    for (size_t i = 0; i < dyn_size(data->files); ++i) {
        if (data->files[i]->opened == false) {
            return data->files[i];
        }
    }
    SFSFile* newFile = malloc(sizeof(SFSFile));
    newFile->index   = dyn_size(data->files);
    dyn_push(data->files, newFile);
    return newFile;
}

static void Deinit(FSDriver* this) {
    LOCK(this->lock);
    debug("deinit called\n");
    SFSDriverData* data = (SFSDriverData*)this->drvData;
    free(data->rootDir);
    free(data);
    // UNLOCK(this->lock);
}

static uint64_t getFileLengthFromData(SFSDriverData* data, uint64_t lba) {
    if (lba == 0) {
        return 0;
    }
    debug("Getting length from data LBA %lu\n", lba);
    uint64_t   size  = 0;
    DataBlock* block = malloc(sizeof(DataBlock));
    while (lba) {
        readLBA(data, lba, 1, block);
        if (!validateBlock(block->header, SFS_BLOCKTYPE_DATA, lba)) {
            error("Invalid data block!\n");
        }
        size += block->blockCount * 512 + block->lastBlockSize;
        lba = block->nextData;
    }
    free(block);
    return size;
}

static uint64_t Open(FSDriver* this, const char* path) {
    LOCK(this->lock);
    SFSDriverData*  data     = (SFSDriverData*)this->drvData;
    DirectoryBlock* current  = data->rootDir;
    char*           save     = NULL;
    char*           p        = (char*)path;
    char*           token    = strtok_r(p, "/", &save);
    char*           fileName = NULL;
    while (token) {
        char* next = strtok_r(NULL, "/", &save);
        if (!next) {
            fileName = token;
            break;
        }
        DirectoryBlock* newBlock = findDirBlockByName(data, current, token);
        if (!newBlock) {
            warn("Directory `%s` doesn't exist\n", token);
            if (current != data->rootDir) {
                free(current);
            }
            UNLOCK(this->lock);
            return (uint64_t)-1;
        }
        if (current != data->rootDir) {
            free(current);
        }
        current = newBlock;
        token   = next;
    }
    FileBlock* block = findFileBlockByName(data, current, fileName);
    if (block == NULL) {
        if (current != data->rootDir) {
            free(current);
        }
        warn("File `%s` doesn't exist\n", fileName);
        UNLOCK(this->lock);
        return (uint64_t)-1;
    }
    if (current != data->rootDir) {
        free(current);
    }
    SFSFile* file  = findAvaliableFile(data);
    file->opened   = true;
    file->position = 0;
    file->lba      = block->header.currentLBA;
    debug("Curr LBA %lu, data LBA %lu\n", block->header.currentLBA, block->dataBlock);
    file->length = getFileLengthFromData(data, block->dataBlock);
    UNLOCK(this->lock);
    return file->index;
}

static void Create(FSDriver* this, const char* path, uint32_t permissions) {
    LOCK(this->lock);
    info("Input path `%s`\n", path);
    bool   endsWithSlash = false;
    size_t len           = strlen(path);
    if (len > 0 && path[len - 1] == '/') {
        endsWithSlash = true;
    }
    SFSDriverData*  data      = (SFSDriverData*)this->drvData;
    DirectoryBlock* current   = data->rootDir;
    char*           save      = NULL;
    char*           p         = (char*)path;
    char*           token     = strtok_r(p, "/", &save);
    char*           fileName  = NULL;
    char*           firstNext = NULL;
    while (token) {
        char* next = strtok_r(NULL, "/", &save);
        if (!next) {
            fileName = token;
            token    = NULL;
            break;
        }
        DirectoryBlock* newBlock = findDirBlockByName(data, current, token);
        if (!newBlock) {
            firstNext = next;
            break;
        }
        if (current != data->rootDir) free(current);
        current = newBlock;
        token   = next;
    }
    while (token) {
        char* next = strtok_r(NULL, "/", &save);
        if (!firstNext) {
            fileName = token;
            break;
        }
        uint64_t lba = findFreeBlock(data);
        debug("Creating `%s` at %lu\n", token, lba);
        DirectoryBlock* block = createDirBlock(data, lba, token);
        appendBlock(data, current, block->header.currentLBA);
        writeLBA(data, lba, 1, block);
        if (current != data->rootDir) {
            free(current);
        }
        current   = block;
        token     = firstNext;
        firstNext = next;
    }
    debug("`%s` `%s` `%s` `%s` `%s`\n", token, fileName, firstNext, save, p);
    if (endsWithSlash) {
        debug("Successfully created directory\n");
        return;
    }
    uint64_t lba = findFreeBlock(data);
    debug("Creating `%s` at %lu (file)\n", fileName, lba);
    FileBlock* block   = createFileBlock(data, lba, fileName);
    block->permissions = permissions;
    appendBlock(data, current, block->header.currentLBA);
    writeLBA(data, block->header.currentLBA, 1, block);
    UNLOCK(this->lock);
}

static uint64_t GetLength(FSDriver* this, uint64_t idx) {
    LOCK(this->lock);
    SFSDriverData* data   = (SFSDriverData*)this->drvData;
    uint64_t       length = data->files[idx]->length;
    UNLOCK(this->lock);
    return length;
}

static uint64_t GetOffset(FSDriver* this, uint64_t idx) {
    LOCK(this->lock);
    SFSDriverData* data     = (SFSDriverData*)this->drvData;
    uint64_t       position = data->files[idx]->position;
    UNLOCK(this->lock);
    return position;
}

static void Seek(FSDriver* this, uint64_t idx, uint64_t seekOffset) {
    LOCK(this->lock);
    SFSDriverData* data        = (SFSDriverData*)this->drvData;
    data->files[idx]->position = seekOffset;
    UNLOCK(this->lock);
}

FSDriver* SFSGetDriver(PartitionEntry* entry, DrvDiskPair* drvDisk) {
    FSDriver* drv = malloc(sizeof(FSDriver));
    if (!drv) {
        error("Failed to allocate memory for SFS driver\n");
    }
    memset(drv, 0, sizeof(FSDriver));
    drv->lock   = false;
    drv->init   = Init;
    drv->deinit = Deinit;
    drv->open   = Open;
    drv->create = Create;
    // drv->close     = Close;
    // drv->read      = Read;
    // drv->write     = Write;
    drv->seek      = Seek;
    drv->getOffset = GetOffset;
    drv->getLength = GetLength;
    drv->init(drv, entry, drvDisk);
    return drv;
}