#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

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
    debug("entry point = 0x%lx\n", file->entryPoint);
    debug("current base addr = 0x%lx new base addr = 0x%lx\n", proc->baseAddr, file->baseAddr);
    todo(true, "Replace current with `%s`\n", path);
out:
    free((char*)path);
    for (size_t j = 0; j < dyn_size(argv) - 1; ++j) {
        free((char*)argv[j]);
    }
    dyn_free(argv);
    return returnCode;
}