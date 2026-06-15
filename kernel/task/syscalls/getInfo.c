#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

uint64_t syscallGetInfo(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state            = PROCESSSTATE_BLOCKED;
    uint64_t          fd   = regs->arg0;
    ProcFile*         file = proc->FDs[fd];
    getinfo_structure getInfo;
    getInfo.currentOffset = file->pos;
    getInfo.flags         = file->flags;
    todo(false, "Figure out correct permissions\n");
    getInfo.permissions = 0b111;
    getInfo.type        = file->type;
    getInfo.size        = vfsGetLength(file->vfsHandle);
    copyToUser(proc, regs->arg1, &getInfo, sizeof(getinfo_structure));
    return 0;
}