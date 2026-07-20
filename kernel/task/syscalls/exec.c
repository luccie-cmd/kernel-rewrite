#include <common/dbg/dbg.h>
#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

static void clearELFObj(ElfFile* obj) {
    for (size_t i = 0; i < dyn_size(obj->mappings); ++i) {
        free(obj->mappings[i]);
    }
    dyn_free(obj->mappings);
    // if (obj->strtab) {
    //     free(obj->strtab);
    // }
    // for (uint64_t i = 0; i < dyn_size(obj->relaEntries); ++i) {
    //     free(obj->relaEntries[i]);
    // }
    // dyn_free(obj->relaEntries);
    // for (uint64_t i = 0; i < dyn_size(obj->deps); ++i) {
    //     clearELFObj(obj->deps[i]);
    // }
    free(obj);
}

static inline void addInitialPointers(Thread* thread, dynarray(char*) argv, dynarray(char*) envp,
                                      dynarray(Elf64_auxv_t) auxv) {
    (void)thread;
    (void)auxv;
    for (uint64_t i = 0; i < dyn_size(argv); ++i) {
        todo(true, "Push `%s`", argv[i]);
    }
    if (envp) {
        todo(true, "Add support for environment variables\n");
    }
}

static const size_t stackSize = USER_STACK_PAGES * PAGE_SIZE;

uint64_t syscallExec(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    const char* path           = copyStringFromUser(proc, regs->arg0);
    dynarray(const char*) argv = NULL;
    uint8_t  maxArgs           = (uint8_t)256;
    uint64_t returnCode        = 0;
    uint8_t  i                 = 0;
    while (true) {
        const uint8_t* strAddr  = copyFromUser(proc, regs->arg1 + i * 8, 8);
        uint64_t       argvAddr = *(uint64_t*)strAddr;
        free((uint8_t*)strAddr);
        debug("argv addr = 0x%lx\n", argvAddr);
        if (argvAddr == 0) {
            dyn_push(argv, NULL);
            break;
        }
        const char* argvI = copyStringFromUser(proc, argvAddr);
        dyn_push(argv, argvI);
        maxArgs--;
        i++;
        if (maxArgs == 0) {
            break;
        }
    }
    if (maxArgs == 0) {
        warn("Exhausted argv\n");
        returnCode = 1;
        goto out;
    }
    for (size_t j = 0; j < dyn_size(argv); ++j) {
        debug("Arg = `%s`\n", argv[j]);
    }
    uint64_t fd = vfsOpen(path, OPEN_FLAG_READ);
    if (fd == (uint64_t)-1) {
        warn("File `%s` doesn't exist\n", path);
        returnCode = 2;
        goto out;
    }
    ElfFile* file = loadElfObject(fd, 0, "/lib/");
    if (!file) {
        warn("File `%s` is not a valid ELF file\n", path);
        returnCode = 3;
        goto out;
    }
    debug("entry point = 0x%lx\n", file->entryPoint);
    debug("current base addr = 0x%lx new base addr = 0x%lx\n", proc->baseAddr, file->baseAddr);
    vmmClearPML4(proc->pml4);
    proc->hasStarted = false;
    // if (!file->relaVirtual) {
    //     debug("No RELA address was passed in\n");
    // }
    proc->baseAddr      = file->baseAddr;
    proc->threads       = NULL;
    proc->memoryMapping = NULL;
    // cleanThread
    Thread* prevThread = NULL;
    Thread* thread     = proc->threads;
    while (thread) {
        pmmFree((void*)((uint64_t)thread->fsBase - (uint64_t)getHHDM()),
                PAGE_SIZE + (PAGE_SIZE - 256));
        pmmFree((void*)((uint64_t)thread->registers - (uint64_t)getHHDM()), sizeof(IsrRegisters));
        pmmFree((void*)thread->rsp, stackSize);
        prevThread = thread;
        thread     = thread->next;
        free(prevThread);
    }
    clearELFObj(proc->elfObj);
    taskAddAddrsToProc(proc, file);
    proc->elfObj = file;
    UNLOCK(proc->lock);
    attachThread(proc->pid, file->entryPoint);
    LOCK(proc->lock);
    if (thread->next == thread) {
        dynarray(Elf64_auxv_t) auxv = NULL;
        uint64_t base               = 0;
        if (file->interpreter) {
            base = file->interpreter->baseAddr;
        }
        dyn_push(auxv, ((Elf64_auxv_t){.a_type = AT_BASE, .a_un.a_val = base}));
        dyn_push(auxv, ((Elf64_auxv_t){.a_type = AT_PHDR, .a_un.a_val = file->baseAddr}));
        // AT_PHENT
        // AT_PHNUM
        // AT_ENTRY
        addInitialPointers(thread, (char**)argv, NULL, auxv);
    }
out:
    free((char*)path);
    for (size_t j = 0; j < dyn_size(argv) - 1; ++j) {
        free((char*)argv[j]);
    }
    dyn_free(argv);
    return returnCode;
}