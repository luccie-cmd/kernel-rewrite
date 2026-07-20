#include <__syscall.h>
#include <dynarray.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static dynarray(partition_entry*) partEntries;

static const char* readFile(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        printf("ERROR: Path `%s` couldn't be opened\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    const char* data = malloc(size);
    if (!data) {
        printf("ERROR: Failed to allocate memory for file\n");
        fclose(file);
        return NULL;
    }
    if (fread((void*)data, 1, size, file) != size) {
        printf("ERROR: Failed to read %lu bytes\n", size);
        fclose(file);
        free((void*)data);
        return NULL;
    }
    *(char*)(data + size) = '\0';
    fclose(file);
    return data;
}

dynarray(partition_entry*) getPartEntries() {
    size_t           count   = 0;
    partition_entry* entries = NULL;
    syscall_execute(SYS_GETPARTITION, NULL, &count);
    if (count == 0) {
        printf("Zero entries found\n");
        return NULL;
    }
    entries = malloc(sizeof(partition_entry) * count);
    if (!entries) {
        printf("Entries is NULL\n");
        exit(1);
    }
    syscall_execute(SYS_GETPARTITION, entries, &count);
    dynarray(partition_entry*) ret = NULL;
    for (size_t i = 0; i < count; ++i) {
        partition_entry* entry = malloc(sizeof(partition_entry));
        if (!entry) {
            printf("ERROR: Failed to allocate memory for partition entry\n");
            for (size_t j = 0; j < dyn_size(ret); ++j) free(ret[j]);
            dyn_free(ret);
            exit(1);
        }
        entry->diskId = entries[i].diskId;
        entry->partId = entries[i].partId;
        memcpy(entry->PARTUUID, entries[i].PARTUUID, sizeof(entry->PARTUUID));
        dyn_push(ret, entry);
    }
    return ret;
}

static partition_entry* getPartEntry(uint32_t targetPartUUID[4]) {
    printf("Searching %08.8x%08.8x%08.8x%08.8x\n", (targetPartUUID)[0], (targetPartUUID)[1],
           (targetPartUUID)[2], (targetPartUUID)[3]);
    for (size_t i = 0; i < dyn_size(partEntries); ++i) {
        partition_entry* entry = partEntries[i];
        printf("Candidate = %08.8x%08.8x%08.8x%08.8x\n", ((uint32_t*)entry->PARTUUID)[0],
               ((uint32_t*)entry->PARTUUID)[1], ((uint32_t*)entry->PARTUUID)[2],
               ((uint32_t*)entry->PARTUUID)[3]);
        if (memcmp(entry->PARTUUID, targetPartUUID, sizeof(entry->PARTUUID)) == 0) {
            return entry;
        }
    }
    return NULL;
}

static inline uint8_t hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static uint8_t* stringToNumber(const char* str) {
    dynarray(uint8_t) num = NULL;
    if (strlen(str) % 2 != 0) {
        dyn_push(num, hexval(*str));
        str++;
    }
    while (*str) {
        dyn_push(num, (hexval(*str) << 4) | hexval(*(str + 1)));
        str += 2;
    }
    return num;
}

static void mountPartitions(const char* data) {
    char* lineSave = NULL;
    char* line     = strtok_r((char*)data, "\n", &lineSave);
    while (line) {
        char* path     = NULL;
        char* partUUID = strtok_r(line, " ", &path);
        if (!path) {
            printf("Path is NULL\n");
            exit(1);
        }
        dynarray(uint8_t) numPartID = stringToNumber(partUUID);
        if (!numPartID) {
            printf("Invalid partition ID found\n");
            exit(1);
        }
        if (dyn_size(numPartID) != 16) {
            printf("numerical partition ID length does not equal 16\n");
            exit(1);
        }
        uint32_t* newGUID = malloc(16);
        if (!newGUID) {
            printf("ERROR: Unable to allocate memory for new GUID\n");
            exit(1);
        }
        memcpy(newGUID, numPartID, 16);
        partition_entry* entry = getPartEntry(newGUID);
        free(newGUID);
        dyn_free(numPartID);
        if (!entry) {
            printf("ERROR: Unable to find partition for `%s`\n", path);
            exit(1);
        }
        char* mountPath = NULL;
        if (memcmp(path, "/", 1) == 0 && strlen(path) == 1) {
            path      = "/tmproot";
            mountPath = malloc(strlen(path) + 9);
            snprintf(mountPath, strlen(path) + 9, "/tmpboot%s", path);
        } else {
            mountPath = malloc(strlen(path) + 17);
            snprintf(mountPath, strlen(path) + 17, "/tmpboot/tmproot%s", path);
        }
        printf("Mounting `%s`\n", mountPath);
        syscall_execute(SYS_MOUNT, MOUNT_DISKPART, mountPath, entry->diskId, entry->partId);
        line = strtok_r(NULL, "\n", &lineSave);
    }
}

int main() {
    partEntries           = getPartEntries();
    const char* mountFile = readFile("/tmpboot/mount");
    if (!mountFile) {
        printf("ERROR: Failed to read mount file\n");
        return -2;
    }
    mountPartitions(mountFile);
    free((void*)mountFile);
    syscall_execute(SYS_PIVOT, "/tmpboot/tmproot", "/");
    char* argv[] = {
        "/bin/init",
        NULL,
    };
    printf("0x%lx 0x%lx\n", argv[0], argv[1]);
    syscall_execute(SYS_UMOUNT, "/tmpboot");
    syscall_execute(SYS_EXEC, "/bin/init", argv, sizeof(argv) / sizeof(argv[0]) - 1);
    printf("FAILED TO SWITCH TO /bin/init\n");
    while (1) {
        __asm__("nop");
    }
    __builtin_unreachable();
}
