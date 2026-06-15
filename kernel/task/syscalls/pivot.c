#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

static inline MountPoint* getMountpointByName(const char* point) {
    MountPoint* current = rootMount;
    if (current) {
        point += strlen(current->name);
    }
    while (*point && current) {
        if (*point == '/') {
            point += 1;
        }
        bool found = false;
        for (size_t i = 0; i < dyn_size(current->kids); ++i) {
            const char* childName = current->kids[i]->name;
            size_t      childLen  = strlen(childName);
            if (memcmp(childName, point, childLen) == 0) {
                if (point[childLen] == '/' || point[childLen] == '\0') {
                    point += childLen;
                    current = current->kids[i];
                    found   = true;
                    break;
                }
            }
        }
        if (!found) {
            break;
        }
    }
    return current;
}

uint64_t syscallPivot(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state                   = PROCESSSTATE_BLOCKED;
    const char* newRoot           = copyStringFromUser(proc, regs->arg0);
    const char* asRoot            = copyStringFromUser(proc, regs->arg1);
    MountPoint* newRootMountPoint = getMountpointByName(newRoot);
    if (!newRootMountPoint) {
        return (uint64_t)-1;
    }
    for (size_t i = 0; i < dyn_size(newRootMountPoint->parent->kids); ++i) {
        if (newRootMountPoint->parent->kids[i] == newRootMountPoint) {
            newRootMountPoint->parent->kids[i] = NULL;
            break;
        }
    }
    // for (size_t i = 0; i < dyn_size(rootMount->kids); ++i) {
    //     if (rootMount->kids[i]) {
    //         rootMount->kids[i]->parent = newRootMountPoint;
    //     }
    // }
    // newRootMountPoint->parent = NULL;
    const char* tempName = rootMount->name;
    rootMount->name      = malloc(strlen(tempName));
    memcpy((void*)rootMount->name, tempName + 1, strlen(tempName));
    free((char*)tempName);
    // dyn_push(newRootMountPoint->kids, rootMount);
    // rootMount       = newRootMountPoint;
    dyn_push(newRootMountPoint->kids, rootMount);
    rootMount->parent         = newRootMountPoint;
    newRootMountPoint->parent = NULL;
    rootMount                 = newRootMountPoint;
    rootMount->name           = strdup(asRoot);
    free((char*)asRoot);
    free((char*)newRoot);
    return 0;
}