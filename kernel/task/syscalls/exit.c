#include <common/dbg/dbg.h>
#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

uint64_t syscallExit(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state         = PROCESSSTATE_BLOCKED;
    Thread* thread      = getCurrentThread();
    bool    procCleared = cleanThread(proc->pid, thread->tid, regs->arg0);
    debug("Cleared proc %s\n", procCleared ? "fully" : "current thread only");
    return 0;
}