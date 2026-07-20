#include <common/dbg/dbg.h>
#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

uint64_t syscallClose(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state = PROCESSSTATE_BLOCKED;
    uint64_t fd = regs->arg0;
    if (!proc->FDs[fd]) {
        debug("FD %lu is not valid\n", fd);
        return (uint64_t)-1;
    }
    ProcFile* file = proc->FDs[fd];
    file->close(file);
    return 0;
}