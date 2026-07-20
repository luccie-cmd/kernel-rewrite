#include <common/dbg/dbg.h>
#include <kernel/task/syscall.h>
#include <kernel/task/task.h>

uint64_t syscallRead(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state   = PROCESSSTATE_BLOCKED;
    size_t length = regs->arg2;
    if (length == 0) {
        return 0;
    }
    size_t   fd         = regs->arg0;
    uint64_t userBuffer = regs->arg1;
    if (!proc->FDs[fd]) {
        debug("FD %lu is not valid\n", fd);
        return (uint64_t)-1;
    }
    debug("Read base 0x%lx to fd %lu\n", userBuffer, fd);
    const uint8_t* data = malloc(length);
    proc->FDs[fd]->read(proc->FDs[fd], (void*)data, length);
    copyToUser(proc, userBuffer, (void*)data, length);
    free((uint8_t*)data);
    return length;
}