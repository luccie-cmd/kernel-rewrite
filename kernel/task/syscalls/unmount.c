#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

uint64_t syscallUnmount(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state      = PROCESSSTATE_BLOCKED;
    const char* path = copyStringFromUser(proc, regs->arg0);
    vfsUnmount(path);
    free((char*)path);
    return 0;
}