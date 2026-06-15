#include <common/dbg/dbg.h>
#include <kernel/mmu/mmu.h>
#include <kernel/objects/elf.h>
#include <kernel/task/task.h>
#include <kernel/vfs/vfs.h>

static bool verifyEhdr(Elf64_Ehdr* ehdr) {
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        debug("Invalid ELF magic\n");
        return false;
    }
    return true;
}

static const char* makeStringFromStrTab(uint8_t* strtab, uint64_t size, uint64_t index) {
    dynarray(char) buffer = NULL;
    for (size_t i = index; i < size; ++i) {
        dyn_push(buffer, strtab[i]);
        if (strtab[i] == 0) {
            break;
        }
    }
    char* retBuffer = malloc(dyn_size(buffer));
    memcpy(retBuffer, buffer, dyn_size(buffer));
    return retBuffer;
}

ElfFile* loadElfObject(uint64_t fileHandle, size_t PHDRAddend, const char* depPrefix) {
    Elf64_Ehdr* hdr = malloc(sizeof(Elf64_Ehdr));
    vfsRead(fileHandle, hdr, sizeof(Elf64_Ehdr));
    if (!verifyEhdr(hdr)) {
        debug("Invalid header\n");
        free(hdr);
        return NULL;
    }
    if (hdr->e_type == ET_DYN && PHDRAddend == 0) {
        free(hdr);
        vfsSeek(fileHandle, 0);
        return loadElfObject(fileHandle, 0x1000, depPrefix);
    }
    ElfFile* file = malloc(sizeof(ElfFile));
    if (!file) {
        error("Failed to allocate memory for new ELF object\n");
    }
    memset(file, 0, sizeof(ElfFile));
    file->entryPoint = hdr->e_entry + PHDRAddend;
    file->baseAddr   = PHDRAddend;
    debug("PHDR addend = 0x%lx\n", file->baseAddr);
    Elf64_Off phOffset = hdr->e_phoff;
    vfsSeek(fileHandle, phOffset);
    Elf64_Half  phentsize = hdr->e_phentsize;
    Elf64_Half  phnum     = hdr->e_phnum;
    Elf64_Phdr* dynPhdr   = NULL;
    free(hdr);
    for (Elf64_Half i = 0; i < phnum; i++) {
        Elf64_Phdr* phdr = malloc(sizeof(Elf64_Phdr));
        vfsRead(fileHandle, phdr, phentsize);
        if (phdr->p_type != PT_LOAD && phdr->p_type != PT_DYNAMIC) {
            free(phdr);
            continue;
        }
        if (phdr->p_type == PT_DYNAMIC) {
            if (dynPhdr != NULL) {
                warn("PT_DYNAMIC defined twice\n");
                free(phdr);
                return NULL;
            }
            dynPhdr = phdr;
            phdr    = NULL;
        } else if (phdr->p_type == PT_LOAD) {
            TaskMapping* mapping = malloc(sizeof(TaskMapping));
            if (!mapping) {
                error("Failed to allocate memory for new task mapping\n");
            }
            mapping->fileLength  = phdr->p_filesz;
            mapping->fileOffset  = phdr->p_offset;
            mapping->memLength   = phdr->p_memsz;
            mapping->permissions = 0;
            if ((phdr->p_flags & PF_X) == 0) {
                mapping->permissions |= MAP_PROTECTION_NOEXEC;
            }
            if ((phdr->p_flags & PF_W) != 0) {
                mapping->permissions |= MAP_PROTECTION_RW;
            }
            mapping->virtualStart = phdr->p_vaddr + PHDRAddend;
            mapping->alignment    = phdr->p_align;
            mapping->handle       = fileHandle;
            dyn_push(file->mappings, mapping);
            free(phdr);
        }
    }
    if (!dynPhdr) {
        debug("No dynamic program header found\n");
        return file;
    }
    vfsSeek(fileHandle, dynPhdr->p_offset);
    size_t entryCount                     = dynPhdr->p_filesz / sizeof(Elf64_Dyn);
    dynarray(Elf64_Xword) dtNeededEntries = NULL;
    Elf64_Addr  strtabVirtual             = 0;
    Elf64_Xword strtabSize                = 0;
    Elf64_Addr  symtabVirtual             = 0;
    Elf64_Addr  relaVirtual               = 0;
    Elf64_Xword relaSize                  = 0;
    Elf64_Addr  pltGotVirtual             = 0;
    Elf64_Addr  jmpVirtual                = 0;
    uint64_t    jmpSize                   = 0;
    Elf64_Addr  hashVirtual               = 0;
    for (size_t j = 0; j < entryCount; ++j) {
        Elf64_Dyn* dyn = malloc(sizeof(Elf64_Dyn));
        vfsRead(fileHandle, dyn, sizeof(Elf64_Dyn));
        if (dyn->d_tag == DT_NULL) {
            free(dyn);
            break;
        }
        if (dyn->d_tag >= DT_LOOS) {
            free(dyn);
            continue;
        }
        switch (dyn->d_tag) {
        case DT_NEEDED: {
            dyn_push(dtNeededEntries, dyn->d_un.d_val);
        } break;
        case DT_FINI:
        case DT_FINI_ARRAY:
        case DT_FINI_ARRAYSZ:
        case DT_INIT:
        case DT_INIT_ARRAY:
        case DT_INIT_ARRAYSZ: {
            todo(false, "Add support init and fini Type: 0x%x, Value: 0x%lx\n", dyn->d_tag,
                 dyn->d_un.d_ptr);
            free(dyn);
            dyn_free(dtNeededEntries);
            return NULL;
        } break;
        case DT_STRTAB: {
            strtabVirtual = dyn->d_un.d_ptr + PHDRAddend;
        } break;
        case DT_STRSZ: {
            strtabSize = dyn->d_un.d_val;
        } break;
        case DT_SYMTAB: {
            symtabVirtual = dyn->d_un.d_ptr + PHDRAddend;
        } break;
        case DT_SYMENT: {
            if (dyn->d_un.d_val != 0x18) {
                warn("Symbol entry size isn't 24 bytes long!!!\n");
                free(dyn);
                dyn_free(dtNeededEntries);
                return NULL;
            }
        } break;
        case DT_RELA: {
            relaVirtual = dyn->d_un.d_ptr + PHDRAddend;
        } break;
        case DT_RELASZ: {
            relaSize = dyn->d_un.d_val;
        } break;
        case DT_RELAENT: {
            if (dyn->d_un.d_val != 0x18) {
                warn("Rela entry size isn't 24 bytes long!!!\n");
                free(dyn);
                dyn_free(dtNeededEntries);
                return NULL;
            }
        } break;
        case DT_HASH: {
            hashVirtual = dyn->d_un.d_ptr + PHDRAddend;
        } break;
        case DT_PLTRELSZ: {
            jmpSize = dyn->d_un.d_val;
        } break;
        case DT_PLTGOT: {
            pltGotVirtual = dyn->d_un.d_ptr + PHDRAddend;
        } break;
        case DT_JMPREL: {
            jmpVirtual = dyn->d_un.d_ptr + PHDRAddend;
        } break;
        case DT_PLTREL: {
            if (dyn->d_un.d_val != DT_RELA) {
                warn("DT_PLTREL referincing REL instead of RELA\n");
                free(dyn);
                dyn_free(dtNeededEntries);
                return NULL;
            }
        } break;
        case DT_DEBUG: {
        } break;
        default: {
            warn("Invalid or unhandled ELF DYN tag %lu\n", dyn->d_tag);
        } break;
        }
        free(dyn);
    }
    if (hashVirtual == 0) {
        warn("No hash virtual address specified!!!\n");
        dyn_free(dtNeededEntries);
        return NULL;
    }
    if (symtabVirtual == 0) {
        warn("No symtab virtual address specified!!!\n");
        dyn_free(dtNeededEntries);
        return NULL;
    }
    if (strtabVirtual == 0) {
        warn("No strtab virtual address specified!!!\n");
        dyn_free(dtNeededEntries);
        return NULL;
    }
    if (strtabSize == 0) {
        warn("No strtab size found!!!\n");
        dyn_free(dtNeededEntries);
        return NULL;
    }
    uint64_t strtabFileOffset = 0;
    for (size_t i = 0; i < dyn_size(file->mappings); ++i) {
        TaskMapping* phdr = file->mappings[i];
        if (strtabVirtual >= phdr->virtualStart &&
            strtabVirtual < phdr->virtualStart + phdr->fileLength) {
            size_t offsetInSegment = strtabVirtual - phdr->virtualStart;
            strtabFileOffset       = phdr->fileOffset + offsetInSegment;
            break;
        }
    }
    if (strtabFileOffset == 0) {
        warn("Failed to aquire strtabFileOffset\n");
        dyn_free(dtNeededEntries);
        return NULL;
    }
    uint8_t* strtab = malloc(strtabSize);
    vfsSeek(fileHandle, strtabFileOffset);
    vfsRead(fileHandle, strtab, strtabSize);
    while (dyn_size(dtNeededEntries) != 0) {
        Elf64_Xword offset = dtNeededEntries[dyn_size(dtNeededEntries) - 1];
        dyn_pop(dtNeededEntries);
        if (offset >= strtabSize) {
            warn("DT_NEEDED attempted access (0x%llx) from out of range string\n", offset);
            return NULL;
        }
        const char* libPath  = makeStringFromStrTab(strtab, strtabSize, offset);
        char*       fullPath = malloc(strlen(libPath) + strlen(depPrefix) + 1);
        if (!fullPath) {
            error("Failed to allocate memory for full library path\n");
        }
        memset(fullPath, 0, strlen(libPath) + strlen(depPrefix) + 1);
        memcpy(fullPath, depPrefix, strlen(depPrefix));
        memcpy(fullPath + strlen(depPrefix), libPath, strlen(libPath));
        debug("Fullpath became `%s` from `%s` + `%s`\n", fullPath, depPrefix, libPath);
        uint64_t libHandle = vfsOpen(fullPath, OPEN_FLAG_READ);
        if (libHandle == (uint64_t)-1) {
            warn("Needed library path `%s` doesn't exist\n", fullPath);
            return NULL;
        }
        TaskMapping* lastMapping = file->mappings[dyn_size(file->mappings) - 1];
        uint64_t     addressOffset =
            ALIGNUP(lastMapping->virtualStart + lastMapping->memLength, PAGE_SIZE);
        ElfFile* libObj = loadElfObject(libHandle, addressOffset, depPrefix);
        dyn_push(file->deps, libObj);
    }
    dyn_free(dtNeededEntries);
    if (relaVirtual == 0 || relaSize == 0) {
        warn("WARNING No rela found!\n");
    }
    file->symtabVirtual = symtabVirtual;
    file->hashVirtual   = hashVirtual;
    file->pltGotVirtual = pltGotVirtual;
    file->jmpVirtual    = jmpVirtual;
    file->jmpSize       = jmpSize;
    file->relaVirtual   = relaVirtual;
    file->relaSize      = relaSize;
    file->strtab        = strtab;
    file->strsize       = strtabSize;
    return file;
}