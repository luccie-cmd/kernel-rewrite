#include <common/dbg/dbg.h>
#include <common/minmax.h>
#include <common/spinlock.h>
#include <kernel/hal/gdt/gdt.h>
#include <kernel/hal/irq/irq.h>
#include <kernel/task/syscall.h>
#include <kernel/task/task.h>
#include <kernel/vfs/vfs.h>
#include <stdlib.h>

static Spinlock spinlock;
static Spinlock gsBasesSpinlock;
static bool     initialized;
static Process* globalParent;
static dynarray(GSbase*) gsBases;
static const size_t stackSize = USER_STACK_PAGES * PAGE_SIZE;

void taskInitialize() {
    LOCK(spinlock);
    if (initialized) {
        warn("Attempted to init Tasking twice\n");
        UNLOCK(spinlock);
        return;
    }
    for (size_t i = 0; i < 16; ++i) {
        GSbase* gsBase = (GSbase*)((uint64_t)pmmAllocate() + (uint64_t)getHHDM());
        memset(gsBase, 0, PAGE_SIZE);
        gsBase->kernelCR3     = (uint64_t)vmmGetPML4(0) - (uint64_t)getHHDM();
        gsBase->scratchPageVA = ((uint64_t)pmmAllocate());
        dyn_push(gsBases, gsBase);
    }
    syscallInitHandlers();
    initialized = true;
    UNLOCK(spinlock);
}

bool taskIsInitialized() {
    return initialized;
}

static inline bool isInRange(uint64_t start, uint64_t end, uint64_t address) {
    return (address >= start && address < end);
}
static TaskMapping* findMappingInProcess(Process* proc, uint64_t virtualAddress) {
    TaskMapping* head = proc->memoryMapping;
    while (head) {
        if (isInRange(head->virtualStart, head->virtualStart + head->memLength, virtualAddress)) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

void taskAddAddrsToProc(Process* proc, ElfFile* obj) {
    // for (size_t j = 0; j < dyn_size(obj->deps); ++j) {
    //     ElfFile* loadObj = obj->deps[j];
    //     taskAddAddrsToProc(proc, loadObj);
    // }
    for (size_t k = 0; k < dyn_size(obj->mappings); ++k) {
        TaskMapping* memMapping = obj->mappings[k];
        size_t       offset     = memMapping->virtualStart % PAGE_SIZE;
        size_t       totalLen   = ALIGNUP(memMapping->memLength + offset, PAGE_SIZE);
        uintptr_t    mapBase    = ALIGNDOWN(memMapping->virtualStart, PAGE_SIZE);
        if (memMapping->virtualStart % PAGE_SIZE == 0) {
            for (size_t i = 0; i < totalLen; i += PAGE_SIZE) {
                vmmMapPage(vmmGetPML4(proc->pid), ONDEMAND_MAP_ADDRESS, mapBase + i,
                           memMapping->permissions, 0);
            }
        } else {
            TaskMapping* procMapping = findMappingInProcess(
                proc, ALIGNDOWN(memMapping->virtualStart, memMapping->alignment));
            if (procMapping == NULL) {
                for (size_t i = 0; i < totalLen; i += PAGE_SIZE) {
                    vmmMapPage(vmmGetPML4(proc->pid), ONDEMAND_MAP_ADDRESS, mapBase + i,
                               memMapping->permissions, 0);
                }
            } else {
                todo(true, "Further memory permisssion sharing\n");
            }
        }
        debug("Added new mapping to process %llu (VADDR = 0x%llx Mapped Length = 0x%llx)\n",
              proc->pid, memMapping->virtualStart, memMapping->memLength);
        memMapping->next    = proc->memoryMapping;
        proc->memoryMapping = memMapping;
    }
    // if (obj->relaVirtual) {
    //     TaskMapping* mapping = findMappingInProcess(proc, obj->relaVirtual);
    //     if (mapping == NULL) {
    //         error("Failed to find process memory mapping for rela virtual 0x%lx\n",
    //               obj->relaVirtual);
    //     }
    //     for (size_t i = 0; i < obj->relaSize; i += sizeof(Elf64_Rela)) {
    //         Elf64_Rela* rela = malloc(sizeof(Elf64_Rela));
    //         vfsSeek(mapping->handle, (obj->relaVirtual - obj->baseAddr) + mapping->fileOffset +
    //         i); vfsRead(mapping->handle, rela, sizeof(Elf64_Rela)); rela->r_offset +=
    //         obj->baseAddr; if (rela->r_info == 0) break; dyn_push(obj->relaEntries, rela);
    //         dyn_push(proc->relas, rela);
    //     }
    // }
    // if (obj->jmpVirtual) {
    //     TaskMapping* mapping = findMappingInProcess(proc, obj->jmpVirtual);
    //     if (mapping == NULL) {
    //         error("Failed to find process memory mapping for jmp virtual 0x%lx\n",
    //         obj->jmpVirtual);
    //     }
    //     for (size_t i = 0; i < obj->jmpSize; i += sizeof(Elf64_Rela)) {
    //         Elf64_Rela* rela = malloc(sizeof(Elf64_Rela));
    //         vfsSeek(mapping->handle, (obj->jmpVirtual - obj->baseAddr) + mapping->fileOffset +
    //         i); vfsRead(mapping->handle, rela, sizeof(Elf64_Rela)); rela->r_offset +=
    //         obj->baseAddr; if (rela->r_info == 0) break; dyn_push(obj->relaEntries, rela);
    //         dyn_push(proc->relas, rela);
    //     }
    // }
}

static Process* findProcByPID(uint64_t pid) {
    Process* head = globalParent;
    while (head->pid != pid) {
        head = head->next;
        if (head == globalParent) {
            warn("Circular dependency exceeded, process %llu not found\n", pid);
            return NULL;
        }
    }
    return head;
}

void attachThread(uint64_t pid, uint64_t entryPoint) {
    if (!taskIsInitialized()) {
        taskInitialize();
    }
    LOCK(spinlock);
    Process* proc = findProcByPID(pid);
    UNLOCK(spinlock);
    uint64_t stackPhys = (uint64_t)pmmAllocateSize(stackSize + (PAGE_SIZE - 256));
    uint64_t stackVirt = USER_STACK_TOP - stackSize;
    for (size_t i = 0; i < stackSize + (PAGE_SIZE - 256); i += PAGE_SIZE) {
        vmmMapPage(proc->pml4, stackPhys + i, stackVirt + i,
                   MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW, MAP_PRESENT);
        // vmmMapPage(vmmGetPML4(0), stackPhys + i, stackVirt + i,
        //            MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW | MAP_PROTECTION_KERNEL,
        //            MAP_PRESENT);
        debug("Stack addr 0x%lx mapped\n", stackVirt + i);
    }
    Thread* thread = aligned_alloc(64, sizeof(Thread));
    thread->tid    = 0;
    memset(thread->fpuState, 0, sizeof(thread->fpuState));
    thread->registers =
        (IsrRegisters*)((uint64_t)pmmAllocateSize(sizeof(IsrRegisters)) + (uint64_t)getHHDM());
    vmmMapPage(proc->pml4, (uint64_t)thread->registers - (uint64_t)getHHDM(),
               (uint64_t)thread->registers, MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW, MAP_PRESENT);
    memset(thread->registers, 0, sizeof(IsrRegisters));
    thread->registers->orig_rsp = stackVirt + stackSize + (PAGE_SIZE - 256);
    thread->registers->orig_rsp &= ~0xF;
    thread->registers->orig_rsp -= 8;
    thread->registers->rbp    = thread->registers->orig_rsp;
    thread->registers->rip    = entryPoint;
    thread->registers->rflags = 0x202;
    thread->registers->cs     = 0x23;
    thread->registers->ss     = 0x1B;
    thread->rsp               = stackPhys;
    thread->status            = THREADSTATUS_READY;
    thread->owner             = proc;
    thread->fsBase            = (uint64_t)pmmAllocate() + (uint64_t)getHHDM();
    vmmMapPage(proc->pml4, (uint64_t)thread->fsBase - (uint64_t)getHHDM(), (uint64_t)thread->fsBase,
               MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW, MAP_PRESENT);
    memset((void*)thread->fsBase, 0, PAGE_SIZE);
    LOCK(proc->lock);
    if (proc->threads == NULL) {
        thread->next  = thread;
        proc->threads = thread;
    } else {
        Thread* current = proc->threads;
        while (current->next != proc->threads) {
            current = current->next;
        }
        thread->next  = proc->threads;
        current->next = thread;
    }
    UNLOCK(proc->lock);
}

void makeNewProcess(uint64_t pid, ElfFile* obj) {
    if (!taskIsInitialized()) {
        taskInitialize();
    }
    Process* proc = malloc(sizeof(Process));
    if (!proc) {
        error("Failed to allocate memory for new process\n");
    }
    memset(proc, 0, sizeof(Process));
    LOCK(proc->lock);
    proc->pid   = pid;
    proc->state = PROCESSSTATE_READY;
    proc->pml4  = vmmGetPML4(pid);
    // proc->signals[SIGKILL] = defaultSignalHandler;
    // if (!obj->relaVirtual) {
    //     debug("No RELA address was passed in\n");
    // }
    debug("0x%lx 0x%lx\n", obj->startAddr, obj->baseAddr);
    proc->baseAddr = obj->baseAddr;
    proc->threads  = NULL;
    taskAddAddrsToProc(proc, obj);
    proc->elfObj = obj;
    if (globalParent == NULL) {
        globalParent = proc;
    }
    LOCK(spinlock);
    proc->parent = getCurrentProc();
    if (proc->parent == NULL) {
        LOCK(gsBasesSpinlock);
        gsBases[getAPICID()]->currentProc = proc;
        UNLOCK(gsBasesSpinlock);
        proc->next = proc;
        proc->prev = proc;
    } else {
        dyn_push(proc->parent->children, proc);
        proc->prev               = proc->parent->prev;
        proc->next               = proc->parent;
        proc->parent->prev->next = proc;
        proc->parent->prev       = proc;
    }
    UNLOCK(spinlock);
    UNLOCK(proc->lock);
    attachThread(pid, obj->entryPoint);
}

void loadGSbase() {
    if (!taskIsInitialized()) {
        taskInitialize();
    }
    GSbase* gsbase = gsBases[getAPICID()];
    wrmsr(0xC0000102, (uint64_t)gsbase);
}

extern __attribute__((noreturn)) void switchProc(IsrRegisters* regs, PML4* pml4, uint64_t fsBase);
extern uint64_t                       __trampoline_text_start;
extern uint64_t                       __trampoline_text_end;
extern uint64_t                       __trampoline_data_start;
extern uint64_t                       __trampoline_data_end;

void nextProc() {
    if (!taskIsInitialized()) {
        taskInitialize();
    }
    LOCK(spinlock);
    Process* candidate = globalParent;
    Thread*  thread    = NULL;
    while (candidate) {
        LOCK(candidate->lock);
        if (candidate->state == PROCESSSTATE_READY || candidate->state == PROCESSSTATE_RUNNING ||
            candidate->state == PROCESSSTATE_WAITING) {
            Thread* head = candidate->threads;
            Thread* cur  = head;
            if (cur) {
                do {
                    if (cur->status == THREADSTATUS_READY) {
                        thread = cur;
                        goto found_thread;
                    }
                    cur = cur->next;
                } while (cur && cur != head);
            }
        }
        UNLOCK(candidate->lock);
        candidate = candidate->next;
    }
found_thread:
    Process* launch = candidate;
    if (!launch) {
        error("No real processes found\n");
    }
    if (!thread) {
        error("No real thread found\n");
    }
    launch->state  = PROCESSSTATE_RUNNING;
    thread->status = THREADSTATUS_RUNNING;
    UNLOCK(spinlock);
    debug("CPU %lu choose TID %lu in PID %lu\n", getAPICID(), thread->tid, launch->pid);
    if (!launch->hasStarted) {
        launch->hasStarted       = true;
        uint64_t trampoline_va   = (uint64_t)&__trampoline_text_start;
        uint64_t trampoline_phys = getPhysicalAddr(vmmGetPML4(0), trampoline_va, false);
        while (trampoline_va < (uint64_t)&__trampoline_text_end) {
            if (trampoline_phys == 0) {
                error("Unable to find trampoline physical address!!!\n");
            }
            vmmMapPage(launch->pml4, trampoline_phys, trampoline_va, MAP_PROTECTION_KERNEL,
                       MAP_PRESENT);
            trampoline_va += PAGE_SIZE;
            trampoline_phys = getPhysicalAddr(vmmGetPML4(0), trampoline_va, false);
        }
        trampoline_va   = (uint64_t)&__trampoline_data_start;
        trampoline_phys = getPhysicalAddr(vmmGetPML4(0), trampoline_va, false);
        while (trampoline_va < (uint64_t)&__trampoline_data_end) {
            if (trampoline_phys == 0) {
                error("Unable to find trampoline physical address!!!\n");
            }
            vmmMapPage(launch->pml4, trampoline_phys, trampoline_va,
                       MAP_PROTECTION_KERNEL | MAP_PROTECTION_RW | MAP_PROTECTION_NOEXEC,
                       MAP_PRESENT);
            trampoline_va += PAGE_SIZE;
            trampoline_phys = getPhysicalAddr(vmmGetPML4(0), trampoline_va, false);
        }
        if (thread->next != thread) {
            error("Thread chain broken on init, thread->next doesn't point to itself\n");
        }
    }
    GSbase* gsbase        = gsBases[getAPICID()];
    gsbase->currentProc   = launch;
    gsbase->currentThread = thread;
    if (!gsbase->stackTop) {
        gsbase->stackTop = (uint64_t)pmmAllocate() + (uint64_t)getHHDM() - 24 + PAGE_SIZE;
    }
    if (getPhysicalAddr(vmmGetPML4(launch->pid), gsbase->stackTop, false) == 0) {
        vmmMapPage(launch->pml4, (uint64_t)gsbase->stackTop - (uint64_t)getHHDM(),
                   (uint64_t)gsbase->stackTop,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW, MAP_PRESENT);
    }
    if (getPhysicalAddr(vmmGetPML4(launch->pid), (uint64_t)gsbase, false) == 0) {
        vmmMapPage(launch->pml4, (uint64_t)gsbase - (uint64_t)getHHDM(), (uint64_t)gsbase,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW, MAP_PRESENT);
    }
    loadGSbase();
    gdtMapStacksToProc(launch->pid, launch->pml4);
    IsrRegisters* regs   = thread->registers;
    PML4*         pml4   = (PML4*)((uint64_t)(launch->pml4) - (uint64_t)getHHDM());
    uint64_t      fsBase = thread->fsBase;
    UNLOCK(launch->lock);
    switchProc(regs, pml4, fsBase);
    __builtin_unreachable();
}

uint64_t getUserRSP() {
    return gsBases[getAPICID()]->userRSP;
}

Process* getParentProc() {
    return globalParent;
}

Process* getCurrentProc() {
    return gsBases[getAPICID()]->currentProc;
}

Thread* getCurrentThread() {
    return gsBases[getAPICID()]->currentThread;
}

uint64_t getScratchPageVA() {
    return gsBases[getAPICID()]->scratchPageVA;
}

static void findJoinMembers(Process* proc) {
    error("TODO: Find join members for PID %lu that are waiting on %lu\n", proc->pid,
          proc->waitingFor);
}

bool cleanThread(uint64_t pid, uint64_t tid, uint8_t exitCode) {
    Thread*  thread       = getCurrentThread();
    Process* proc         = thread->owner;
    bool     isLastThread = thread == thread->next;
    Thread*  prev         = thread;
    while (proc->threads != thread) {
        prev          = proc->threads;
        proc->threads = proc->threads->next;
    }
    if (proc->waitingFor == tid && proc->state == PROCESSSTATE_WAITING) {
        proc->state      = PROCESSSTATE_READY;
        proc->waitStatus = exitCode;
        findJoinMembers(proc);
    }
    if (!isLastThread) {
        prev->next = thread->next;
    }
    pmmFree((void*)((uint64_t)thread->fsBase - (uint64_t)getHHDM()), PAGE_SIZE + (PAGE_SIZE - 256));
    pmmFree((void*)((uint64_t)thread->registers - (uint64_t)getHHDM()), sizeof(IsrRegisters));
    pmmFree((void*)thread->rsp, stackSize);
    debug("%lx %lx\n", thread, prev);
    free(thread);
    if (isLastThread) {
        cleanProc(pid, exitCode);
    } else {
        UNLOCK(proc->lock);
    }
    return isLastThread;
}

void cleanProc(uint64_t pid, uint8_t exitCode) {
    if (pid == INIT_PID) {
        error("Cannot exit init proc\n");
    }
    todo(true, "Clean %lu with %hhu\n", pid, exitCode);
}

// static dynarray(Elf64_Rela*) findRelasInPage(Process* proc, uint64_t virtualAddr) {
//     dynarray(Elf64_Rela*) newRelas = NULL;
//     for (size_t i = 0; i < dyn_size(proc->relas); ++i) {
//         Elf64_Rela* rela = proc->relas[i];
//         if (isInRange(virtualAddr, virtualAddr + PAGE_SIZE, rela->r_offset)) {
//             dyn_push(newRelas, rela);
//         }
//     }
//     return newRelas;
// }

// static uint32_t getSymbolCount(Process* proc, uint64_t symtabVirtual) {
//     TaskMapping* mapping = findMappingInProcess(proc, symtabVirtual);
//     if (!mapping) return 0;
//     uint64_t symtabSizeInMapping = mapping->memLength - (symtabVirtual - mapping->virtualStart);
//     return symtabSizeInMapping / sizeof(Elf64_Sym);
// }

// static bool dyn_contains(dynarray(Elf64_Rela*) arr, Elf64_Rela* rela) {
//     for (size_t i = 0; i < dyn_size(arr); ++i) {
//         if (memcmp(arr[i], rela, sizeof(Elf64_Rela)) == 0) {
//             return true;
//         }
//     }
//     return false;
// }

// static ElfFile* searchFileForRela(ElfFile* file, Elf64_Rela* rela) {
//     if (dyn_contains(file->relaEntries, rela)) {
//         return file;
//     }
//     for (size_t i = 0; i < dyn_size(file->deps); ++i) {
//         ElfFile* dep = file->deps[i];
//         if (searchFileForRela(dep, rela) != NULL) {
//             return dep;
//         }
//     }
//     return NULL;
// }

// static ElfFile* searchProcForRela(Process* proc, Elf64_Rela* rela) {
//     return searchFileForRela(proc->elfObj, rela);
// }

// static Elf64_Sym* _readSym(Process* proc, ElfFile* file, uint32_t idx) {
//     TaskMapping* mapping = findMappingInProcess(proc, file->symtabVirtual);
//     if (!mapping) {
//         error("Failed to get symtabVirtual for file base %lx\n", file->baseAddr);
//     }
//     if (idx == 0 || idx >= getSymbolCount(proc, file->symtabVirtual)) {
//         return NULL;
//     }
//     Elf64_Sym* sym = malloc(sizeof(Elf64_Sym));
//     if (!sym) {
//         error("Failed to allocate memory for symbol\n");
//     }
//     uint64_t symOffsetInFile =
//         (file->symtabVirtual - file->baseAddr) + (uint64_t)idx * sizeof(Elf64_Sym);
//     uint64_t vfsPosition = mapping->fileOffset + symOffsetInFile;
//     vfsSeek(mapping->handle, vfsPosition);
//     vfsRead(mapping->handle, sym, sizeof(Elf64_Sym));
//     return sym;
// }

// static Elf64_Sym* loadSymByIndex(Process* proc, ElfFile* file, uint32_t idx, uint64_t*
// baseAddrOut,
//                                  const char* lookupName, bool recurse) {
//     Elf64_Sym* sym = _readSym(proc, file, idx);
//     if (!sym) {
//         return NULL;
//     }
//     *baseAddrOut = file->baseAddr;
//     if (sym->st_shndx == SHN_UNDEF && !recurse) {
//         bool        found = false;
//         const char* name  = (const char*)file->strtab + sym->st_name;
//         if (lookupName == NULL) {
//             lookupName = name;
//         }
//         for (size_t i = 0; i < dyn_size(file->deps) && !found; ++i) {
//             ElfFile* dep = file->deps[i];
//             for (size_t j = 1; j < getSymbolCount(proc, dep->symtabVirtual) && !found; ++j) {
//                 uint64_t   depBaseAddr = 0;
//                 Elf64_Sym* depSym = loadSymByIndex(proc, dep, j, &depBaseAddr, lookupName, true);
//                 if (!depSym) {
//                     continue;
//                 }
//                 if (depSym->st_shndx != SHN_UNDEF) {
//                     const char* symName = (const char*)dep->strtab + depSym->st_name;
//                     if (strlen(symName) > 0 && strlen(lookupName) > 0 &&
//                         strcmp(lookupName, symName) == 0) {
//                         free(sym);
//                         sym          = depSym;
//                         *baseAddrOut = depBaseAddr;
//                         found        = true;
//                         debug("Found symbol %s in dependency file %lx\n", symName,
//                         dep->baseAddr); break;
//                     }
//                 }
//                 free(depSym);
//             }
//         }
//     }
//     return sym;
// }

// static uint64_t resolveAddr(Process* proc, Elf64_Rela* rela) {
//     uint64_t baseAddr = 0;
//     ElfFile* relaFile = searchProcForRela(proc, rela);
//     if (!relaFile) {
//         error("Failed to find rela file\n");
//     }
//     Elf64_Sym* sym =
//         loadSymByIndex(proc, relaFile, ELF64_R_SYM(rela->r_info), &baseAddr, NULL, false);
//     if (!sym) {
//         error("Failed to find symbol index %lu\n", ELF64_R_SYM(rela->r_info));
//     }
//     uint64_t val = sym->st_value + baseAddr;
//     free(sym);
//     return val;
// }

// static void applyRela(Process* proc, Elf64_Rela* rela, uint64_t bufferAddr) {
//     // debug("Applying at 0x%lx symidx %u type %u addend 0x%lx\n", rela->r_offset,
//     //       ELF64_R_SYM(rela->r_info), ELF64_R_TYPE(rela->r_info), rela->r_addend);
//     debug("0x%lx 0x%lx\n", proc->baseAddr, rela->r_addend);
//     switch (ELF64_R_TYPE(rela->r_info)) {
//     case R_X86_64_RELATIVE: {
//         *(uint64_t*)(bufferAddr + (rela->r_offset & 0xFFF)) = proc->baseAddr + rela->r_addend;
//     } break;
//     case R_X86_64_GLOB_DAT:
//     case R_X86_64_JUMP_SLOT: {
//         uint64_t resolvedAddr                               = resolveAddr(proc, rela);
//         *(uint64_t*)(bufferAddr + (rela->r_offset & 0xFFF)) = resolvedAddr;
//     } break;
//     default: {
//         todo(true, "Handle rela 0x%llx type: %llu sym: %llu addend: 0x%llx\n", rela->r_offset,
//              ELF64_R_TYPE(rela->r_info), ELF64_R_SYM(rela->r_info), rela->r_addend);
//     } break;
//     }
//     debug("Rela at 0x%lx patched to 0x%lx\n", rela->r_offset,
//           *(uint64_t*)(bufferAddr + (rela->r_offset & 0xFFF)));
// }

void mapProcessAddr(Process* proc, uint64_t virtualAddress, bool shouldReturn) {
    LOCK(proc->lock);
    proc->state   = PROCESSSTATE_BLOCKED;
    uint64_t base = ALIGNDOWN(virtualAddress, PAGE_SIZE);
    if (getPhysicalAddr(proc->pml4, base, false) != 0) {
        warn("Attempted double map (0x%lx)\n", getPhysicalAddr(proc->pml4, base, false));
        goto out;
    }
    info("[%u]: Mapping VADDR 0x%lx\n", getAPICID(), base);
    uint64_t phys = (uint64_t)pmmAllocate();
    if (!phys) {
        sendSignal(proc, SIGKILL);
        goto out;
    }
    uint8_t*     kpage            = (uint8_t*)(getScratchPageVA());
    TaskMapping* mapping          = NULL;
    uint8_t      totalPermissions = 0;
    size_t       i                = 0;
    while (i < PAGE_SIZE) {
        uint64_t     vaddr        = base + i;
        TaskMapping* localMapping = findMappingInProcess(proc, vaddr);
        if (!localMapping) {
            i += 1;
            continue;
        }
        mapping             = localMapping;
        uint8_t permissions = mapping->permissions;
        totalPermissions |= permissions;
        vmmMapPage(vmmGetPML4(0), phys, getScratchPageVA(),
                   permissions | MAP_PROTECTION_KERNEL | MAP_PROTECTION_RW | MAP_PROTECTION_NOEXEC,
                   MAP_PRESENT);
        uint64_t pageOffset      = vaddr & (PAGE_SIZE - 1);
        uint64_t addedFileOffset = vaddr - mapping->virtualStart;
        debug("PO = 0x%lx AFO = 0x%lx vaddr = 0x%lx handle = %lu\n", pageOffset, addedFileOffset,
              vaddr, mapping->handle);
        if (mapping->handle != (uint64_t)-1) {
            uint64_t savedOffset = vfsGetOffset(mapping->handle);
            if (addedFileOffset < mapping->fileLength) {
                uint64_t fileRemaining = mapping->fileLength - addedFileOffset;
                uint64_t readSize      = min(fileRemaining, (uint64_t)PAGE_SIZE - i);
                debug("Readsize = 0x%lx\n", readSize);
                vfsSeek(mapping->handle, mapping->fileOffset + addedFileOffset);
                debug("Reading for addr 0x%lx - 0x%lx (current file offset = 0x%lx fileRemaining = "
                      "0x%lx)\n",
                      vaddr + addedFileOffset, vaddr + addedFileOffset + readSize,
                      vfsGetOffset(mapping->handle), fileRemaining);
                // uint64_t vfsReadSize = readSize;
                vfsRead(mapping->handle, (kpage + pageOffset), readSize);
                debug("0x%x\n", *(uint32_t*)(kpage + 0x120));
            }
            vfsSeek(mapping->handle, savedOffset);
        }
        vmmUnmapPage(vmmGetPML4(0), getScratchPageVA());
        i += mapping->memLength;
    }
    if (!mapping) {
        warn("Failed to find addr 0x%lx in proc\n", base);
        sendSignal(proc, SIGABORT);
        goto out;
    }
    // dynarray(Elf64_Rela*) relas = findRelasInPage(proc, base);
    // vmmMapPage(vmmGetPML4(0), phys, getScratchPageVA(),
    //            totalPermissions | MAP_PROTECTION_KERNEL | MAP_PROTECTION_RW |
    //            MAP_PROTECTION_NOEXEC, MAP_PRESENT);
    // for (size_t j = 0; j < dyn_size(relas); ++j) {
    //     applyRela(proc, relas[j], (uint64_t)kpage);
    // }
    // vmmUnmapPage(vmmGetPML4(0), getScratchPageVA());
    // dyn_free(relas);
    vmmMapPage(proc->pml4, phys, base, totalPermissions, MAP_PRESENT);
    info("[%u]: Mapping PADDR 0x%lx to VADDR 0x%lx\n", getAPICID(), phys, base);
out:
    proc->state = PROCESSSTATE_RUNNING;
    if (!shouldReturn) {
        getCurrentThread()->status = THREADSTATUS_READY;
    }
    UNLOCK(proc->lock);
    if (!shouldReturn) {
        nextProc();
    }
}

void sendSignal(Process* proc, size_t signal) {
    error("TODO: Send signal %lu to proc %lu\n", signal, proc->pid);
}