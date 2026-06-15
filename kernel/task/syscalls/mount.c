#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

uint64_t syscallMount(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state      = PROCESSSTATE_BLOCKED;
    uint8_t     mode = regs->arg0;
    const char* path = copyStringFromUser(proc, regs->arg1);
    switch (mode) {
    case MOUNT_DISKPART: {
        uint8_t disk = regs->arg2;
        uint8_t part = regs->arg3;
        debug("Mode: %hhu path %s disk %hhu part %hhu\n", mode, path, disk, part);
        vfsMountDiskPart(path, disk, part);
    } break;
    default: {
        error("TODO: Mount `%s` as %hhu\n", path, mode);
    }
    }
    free((char*)path);
    return 0;
}