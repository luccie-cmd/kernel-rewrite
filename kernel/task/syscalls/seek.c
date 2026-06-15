#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

uint64_t syscallSeek(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state      = PROCESSSTATE_BLOCKED;
    uint64_t  fd     = regs->arg0;
    int64_t   offset = *(int64_t*)(&regs->arg1);
    uint64_t  whence = regs->arg2;
    ProcFile* file   = proc->FDs[fd];
    if (file->type == FILETYPE_TTY) {
        return (uint64_t)-1;
    }
    int64_t newPos = file->pos;
    switch (whence) {
    case SEEK_SET: {
        newPos = offset;
    } break;
    case SEEK_CUR: {
        newPos += offset;
    } break;
    case SEEK_END: {
        newPos = vfsGetLength(file->vfsHandle) + offset;
    } break;
    default: {
        return (uint64_t)-1;
    } break;
    }
    if (newPos < 0) {
        return (uint64_t)-1;
    }
    file->pos = (uint64_t)newPos;
    vfsSeek(file->vfsHandle, file->pos);
    return newPos;
}