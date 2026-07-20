#include <common/dbg/dbg.h>
#include <kernel/task/syscall.h>
#include <kernel/task/task.h>

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

static bool validateUserRange(Process* proc, uint64_t base, uint64_t len) {
    if (len == 0) return true;
    if (!iscanonical(base)) return false;
    if (base >= USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE) return true;
    uint64_t end;
    if (__builtin_add_overflow(base, len, &end)) return false;
    uint64_t curr = base;
    while (curr < end) {
        TaskMapping* mapping = findMappingInProcess(proc, curr);
        debug("Mapping 0x%lx from addr 0x%lx\n", mapping, curr);
        if (!mapping) return false;
        uint64_t mappingEnd = mapping->virtualStart + mapping->memLength;
        if (end <= mappingEnd) {
            return true;
        }
        curr = mappingEnd;
    }
    return false;
}

uint64_t getMemory(SyscallRegs* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state = PROCESSSTATE_BLOCKED;
    void* addr  = (void*)regs->arg0;
    if (addr == NULL) {
        TaskMapping* mapping = findHighestMapping(proc);
        addr                 = (void*)(mapping->virtualStart + mapping->memLength + PAGE_SIZE);
    }
    uint64_t size        = regs->arg1;
    uint8_t  protections = regs->arg2;
    uint64_t newAddr     = ALIGNUP((uint64_t)addr, PAGE_SIZE);
    size                 = ALIGNUP(size, PAGE_SIZE);
    debug("newaddr = 0x%lx, newsize = %lu\n", newAddr, size);
    if (validateUserRange(proc, newAddr, size)) {
        warn("Address 0x%lx already mapped\n", newAddr);
        return 0;
    }
    debug("New address for memory is 0x%lx\n", newAddr);
    TaskMapping* memMapping = malloc(sizeof(TaskMapping));
    if (!memMapping) {
        error("Failed to allocate memory for new memory mapping\n");
    }
    memMapping->alignment  = PAGE_SIZE;
    memMapping->handle     = (uint64_t)-1;
    memMapping->fileLength = 0;
    memMapping->fileOffset = 0;
    memMapping->memLength  = size;
    uint8_t permissions    = MAP_PROTECTION_NOEXEC;
    if (protections & PROT_EXEC) {
        permissions = 0;
    }
    if (protections & PROT_WRITE) {
        permissions |= MAP_PROTECTION_RW;
    }
    if (!(protections & PROT_READ)) {
        warn("Nonsensical protections, read not set\n");
        return 0;
    }
    memMapping->permissions  = permissions;
    memMapping->virtualStart = newAddr;
    for (size_t i = 0; i < size; i += PAGE_SIZE) {
        vmmMapPage(proc->pml4, ONDEMAND_MAP_ADDRESS, newAddr + i, memMapping->permissions, 0);
    }
    memMapping->next    = proc->memoryMapping;
    proc->memoryMapping = memMapping;
    return newAddr;
}