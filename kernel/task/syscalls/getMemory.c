#include <kernel/task/syscall.h>
#include <kernel/task/task.h>

static TaskMapping* findHighestMapping(Process* proc) {
    TaskMapping* highest = proc->memoryMapping;
    TaskMapping* head    = proc->memoryMapping;
    while (head) {
        if (highest->virtualStart < head->virtualStart) {
            highest = head;
        }
        head = head->next;
    }
    return highest;
}

uint64_t getMemory(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state          = PROCESSSTATE_BLOCKED;
    TaskMapping* highest = findHighestMapping(proc);
    uint64_t     size    = regs->arg0;
    uint64_t newAddr = ALIGNUP(highest->virtualStart + highest->memLength + PAGE_SIZE, PAGE_SIZE);
    debug("New address for memory is 0x%lx\n", newAddr);
    TaskMapping* memMapping = malloc(sizeof(TaskMapping));
    if (!memMapping) {
        error("Failed to allocate memory for new memory mapping\n");
    }
    memMapping->alignment    = PAGE_SIZE;
    memMapping->handle       = (uint64_t)-1;
    memMapping->fileLength   = 0;
    memMapping->fileOffset   = 0;
    memMapping->memLength    = size;
    memMapping->permissions  = MAP_PROTECTION_NOEXEC | MAP_PROTECTION_RW;
    memMapping->virtualStart = newAddr;
    for (size_t i = 0; i < size; i += PAGE_SIZE) {
        vmmMapPage(proc->pml4, ONDEMAND_MAP_ADDRESS, newAddr + i, memMapping->permissions, 0);
    }
    memMapping->next    = proc->memoryMapping;
    proc->memoryMapping = memMapping;
    return newAddr;
}