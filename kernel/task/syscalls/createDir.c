#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

uint64_t syscallCreateDir(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    const char* path = copyStringFromUser(proc, regs->arg0);
    vfsCreate(path, __UINT32_MAX__);
    proc->state = PROCESSSTATE_BLOCKED;
    free((char*)path);
    return 0;
}