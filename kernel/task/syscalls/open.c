#include <common/dbg/dbg.h>
#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

static uint8_t findFreeFD(Process* proc) {
    for (size_t i = 0; i < MAX_FDS; ++i) {
        if (proc->FDs[i] == NULL) {
            return i;
        }
    }
    return (uint8_t)-1;
}
size_t writeHandler(ProcFile* f, const void* buf, size_t count) {
    (void)f;
    (void)buf;
    (void)count;
    todo(true, "Write files\n");
}
size_t readHandler(ProcFile* f, void* buf, size_t count) {
    uint64_t mode = f->flags & O_ACCMODE;
    if (mode == O_WRONLY) {
        debug("Tried reading from a non readable file");
        return (size_t)-1;
    }
    if (count == 0) {
        return 0;
    }
    if (f->type != FILETYPE_REGULAR) {
        todo(false, "Read from non regular text files\n");
        return 0;
    }
    vfsRead(f->vfsHandle, buf, count);
    return count;
}
void closeHandler(ProcFile* f) {
    Process* proc = getCurrentProc();
    uint8_t  fd   = 255;
    for (uint8_t i = 0; i < sizeof(proc->FDs) / sizeof(proc->FDs[0]); ++i) {
        if (proc->FDs[i] == f) {
            fd = i;
            break;
        }
    }
    if (fd == 255) {
        error("Invalid FD passed to close\n");
    }
    f->refcount -= 1;
    if (f->refcount == 0) {
        vfsClose(f->vfsHandle);
        free(f);
        proc->FDs[fd] = NULL;
    }
}

uint64_t syscallOpen(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state    = PROCESSSTATE_BLOCKED;
    size_t   path  = regs->arg0;
    uint64_t flags = regs->arg1;
    if ((flags & O_ACCMODE) == 0) {
        warn("Unable to open files that have no mode set\n");
        return (uint64_t)-1;
    }
    const char* data = copyStringFromUser(proc, path);
    debug("Path: %s\n", data);
    uint32_t vfsFlags = 0;
    if (flags & O_RDONLY) {
        vfsFlags |= OPEN_FLAG_READ;
    } else if (flags & O_WRONLY) {
        vfsFlags |= OPEN_FLAG_WRITE;
    } else {
        vfsFlags |= OPEN_FLAG_READ | OPEN_FLAG_WRITE;
    }
    if (flags & O_APPEND) {
        vfsFlags |= OPEN_FLAG_APPEND;
    }
    if (flags & O_TRUNC) {
        vfsFlags |= OPEN_FLAG_TRUNCATE;
    }
    if (flags & O_CREAT) {
        vfsFlags |= OPEN_FLAG_CREATE;
    }
    uint64_t vfsHandle = vfsOpen(data, vfsFlags);
    if (vfsHandle == (uint64_t)-1) {
        return (uint64_t)-1;
    }
    free((char*)data);
    data           = NULL;
    uint8_t fdSlot = findFreeFD(proc);
    if (fdSlot == (uint8_t)-1) {
        warn("Unable to find free file slot\n");
        vfsClose(vfsHandle);
        return (uint64_t)-1;
    }
    proc->FDs[fdSlot] = malloc(sizeof(ProcFile));
    if (!proc->FDs[fdSlot]) {
        warn("Failed to allocate fdFile\n");
        vfsClose(vfsHandle);
        return (uint64_t)-1;
    }
    proc->FDs[fdSlot]->flags     = flags;
    proc->FDs[fdSlot]->pos       = vfsGetOffset(vfsHandle);
    proc->FDs[fdSlot]->refcount  = 1;
    proc->FDs[fdSlot]->vfsHandle = vfsHandle;
    proc->FDs[fdSlot]->type      = FILETYPE_REGULAR;
    proc->FDs[fdSlot]->write     = writeHandler;
    proc->FDs[fdSlot]->read      = readHandler;
    proc->FDs[fdSlot]->close     = closeHandler;
    return (uint64_t)fdSlot;
}