#if !defined(__KERNEL_OBJECTS_ELF_H__)
#define __KERNEL_OBJECTS_ELF_H__
#include <elf.h>
#include <common/dynarray.h>
// #include <kernel/task/task.h>

typedef struct TaskMapping TaskMapping;

typedef struct ElfFile {
    dynarray(struct ElfFile*) deps;
    dynarray(TaskMapping*) mappings;
    dynarray(Elf64_Rela*) relaEntries;
    uint64_t    startAddr;
    uint64_t    entryPoint;
    Elf64_Addr  pltGotVirtual;
    uint64_t    jmpSize;
    uint64_t    jmpVirtual;
    Elf64_Addr  symtabVirtual;
    Elf64_Addr  hashVirtual;
    Elf64_Addr  relaVirtual;
    Elf64_Xword relaSize;
    uint64_t    strsize;
    Elf64_Addr  baseAddr;
    uint8_t*    strtab;
} ElfFile;

ElfFile* loadElfObject(uint64_t fileHandle, size_t PHDRAddend, const char* depPrefix);

#endif // __KERNEL_OBJECTS_ELF_H__
