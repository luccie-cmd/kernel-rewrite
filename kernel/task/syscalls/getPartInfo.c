#include <common/dbg/dbg.h>
#include <common/minmax.h>
#include <kernel/task/syscall.h>
#include <kernel/vfs/vfs.h>

uint64_t syscallGetPartition(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state                       = PROCESSSTATE_BLOCKED;
    dynarray(partition_entry) entries = NULL;
    size_t count                      = min(regs->arg1, vfsGetPartCount());
    if (regs->arg0 == 0) {
        copyToUser(proc, regs->arg1, &count, sizeof(size_t));
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        partition_entry entry;
        PartitionEntry* vfsEntry = vfsGetPartInfo(i, &entry.diskId, &entry.partId);
        memcpy(entry.PARTUUID, vfsEntry->UGUID, sizeof(vfsEntry->UGUID));
        dyn_push(entries, entry);
    }
    debug("Got %lu partitions (arg0 = 0x%lx arg1 = 0x%lx)\n", vfsGetPartCount(), regs->arg0,
          regs->arg1);
    copyToUser(proc, regs->arg0, entries, sizeof(partition_entry) * count);
    copyToUser(proc, regs->arg1, &count, sizeof(size_t));
    dyn_free(entries);
    entries = NULL;
    return 0;
}