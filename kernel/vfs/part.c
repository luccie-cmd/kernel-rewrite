#include <common/dbg/dbg.h>
#include <drivers/drivers.h>
#include <kernel/vfs/vfs.h>

static Spinlock lock;
dynarray(dynarray(PartitionEntry*)) vfsPartArray;
dynarray(MSCDriver*) vfsBlockDevices;

static size_t sizeofPartArray() {
    size_t result = 0;
    for (size_t i = 0; i < dyn_size(vfsPartArray); ++i) {
        for (size_t j = 0; j < dyn_size(vfsPartArray[i]); ++j) {
            result++;
        }
    }
    return result;
}

static uint8_t* parseGUID(uint8_t* GUID) {
    uint8_t* newGUID = malloc(16);
    if (!newGUID) {
        error("Failed to allocate memory for new GUID\n");
    }
    memcpy(newGUID, GUID, 16);
    newGUID[0]  = GUID[3];
    newGUID[1]  = GUID[2];
    newGUID[2]  = GUID[1];
    newGUID[3]  = GUID[0];
    newGUID[4]  = GUID[5];
    newGUID[5]  = GUID[4];
    newGUID[6]  = GUID[7];
    newGUID[7]  = GUID[6];
    newGUID[8]  = GUID[8];
    newGUID[9]  = GUID[9];
    newGUID[10] = GUID[10];
    newGUID[11] = GUID[11];
    newGUID[12] = GUID[12];
    newGUID[13] = GUID[13];
    newGUID[14] = GUID[14];
    newGUID[15] = GUID[15];
    return newGUID;
}

static void readPartitionTable(MSCDriver* drv) {
    PartitionTableHeader* PTH = malloc(sizeof(PartitionTableHeader));
    drv->read(drv, PTH, 1, 1);
    if (memcmp(PTH->signature, "EFI PART", 8) != 0) {
        warn("Partition header corrupted got a signature of `%8s`\n", PTH->signature);
        free(PTH);
        return;
    }
    uint8_t* partBuffer = malloc(15872);
    drv->read(drv, partBuffer, PTH->firstPartitionEntry, 31);
    dynarray(PartitionEntry*) entries = NULL;
    for (uint32_t i = 0; i < PTH->partitionCount; i++) {
        PartitionEntry* entry = (PartitionEntry*)(partBuffer + (i * sizeof(PartitionEntry)));
        if (entry->startLBA == 0 && entry->endLBA == 0) {
            break;
        }
        uint8_t* newUGUID = parseGUID(entry->UGUID);
        memcpy(entry->UGUID, newUGUID, sizeof(entry->UGUID));
        free(newUGUID);
        PartitionEntry* acEntry = malloc(sizeof(PartitionEntry));
        if (!acEntry) {
            dyn_free(entries);
            error("Failed to allocate partition entry\n");
        }
        memcpy(acEntry, entry, sizeof(PartitionEntry));
        dyn_push(entries, acEntry);
        debug("Loaded new partition: %lu-%lu (%x%x%x%x)\n", acEntry->startLBA, acEntry->endLBA,
              ((uint32_t*)acEntry->UGUID)[0], ((uint32_t*)acEntry->UGUID)[1],
              ((uint32_t*)acEntry->UGUID)[2], ((uint32_t*)acEntry->UGUID)[3]);
        info("Partition GUID = %08.8x%08.8x%08.8x%08.8x\n", ((uint32_t*)acEntry->GUID)[0],
             ((uint32_t*)acEntry->GUID)[1], ((uint32_t*)acEntry->GUID)[2],
             ((uint32_t*)acEntry->GUID)[3]);
    }
    if (dyn_size(entries) == 0) {
        warn("Unable to find any partitions on buffer\n");
    }
    dyn_push(vfsPartArray, entries);
    free(partBuffer);
    free(PTH);
}

size_t vfsGetPartCount() {
    size_t result = 0;
    if (vfsPartArray != NULL) {
        result = sizeofPartArray();
    } else {
        LOCK(lock);
        vfsBlockDevices = getBlockDevices();
        for (size_t i = 0; i < dyn_size(vfsBlockDevices); ++i) {
            readPartitionTable(vfsBlockDevices[i]);
        }
        UNLOCK(lock);
        result = sizeofPartArray();
    }
    return result;
}

PartitionEntry* vfsGetPartInfo(size_t idx, uint8_t* outDiskId, uint8_t* outPartId) {
    if (idx > sizeofPartArray()) {
        warn("Invalid idx\n");
        return NULL;
    }
    uint8_t diskId = 0, partId = 0;
    for (size_t i = 0; i < idx; ++i) {
        if (dyn_size(vfsPartArray[diskId]) == partId) {
            diskId += 1;
            partId = 0;
        } else {
            partId += 1;
        }
    }
    *outDiskId = diskId;
    *outPartId = partId;
    return vfsPartArray[diskId][partId];
}