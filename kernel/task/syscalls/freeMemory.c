#include <common/dbg/dbg.h>
#include <kernel/task/syscall.h>

static inline bool isInRange(uint64_t start, uint64_t end, uint64_t address) {
    return (address >= start && address < end);
}
static TaskMapping* findMappingInProcess(Process* proc, uint64_t virtualAddress) {
    TaskMapping* head = proc->memoryMapping;
    while (head) {
        if (isInRange(head->virtualStart, head->virtualStart + head->memLength, virtualAddress)) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

uint64_t freeMemory(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state          = PROCESSSTATE_BLOCKED;
    uint64_t     addr    = regs->arg0;
    TaskMapping* mapping = findMappingInProcess(proc, addr);
    if (!mapping) {
        warn("Unable to find mapping for free\n");
        return -1;
    }
    TaskMapping** cur = &proc->memoryMapping;
    while (*cur) {
        if (*cur == mapping) {
            *cur          = mapping->next;
            mapping->next = NULL;
            break;
        }
        cur = &(*cur)->next;
    }
    return 0;
}