#include <kernel/task/syscall.h>
#include <kernel/task/task.h>

uint64_t syscallWrite(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state   = PROCESSSTATE_BLOCKED;
    size_t length = regs->arg2;
    if (length == 0) {
        return 0;
    }
    size_t   fd         = regs->arg0;
    uint64_t userBuffer = regs->arg1;
    debug("Write base 0x%lx to fd %lu\n", userBuffer, fd);
    const uint8_t* data = copyFromUser(proc, userBuffer, length);
    proc->FDs[fd]->write(proc->FDs[fd], data, length);
    free((void*)data);
    return length;
}