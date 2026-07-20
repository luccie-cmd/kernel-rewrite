#include <common/dbg/dbg.h>
#include <common/io/io.h>
#include <common/io/regs.h>
#include <common/minmax.h>
#include <kernel/task/syscall.h>
#include <kernel/task/task.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define MAXSTRLEN 4096

static syscallHandlerEntry handlers[SYS_MAX];

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

const char* copyStringFromUser(Process* proc, uint64_t base) {
    char* data = malloc(MAXSTRLEN + 1);
    if (!data) {
        warn("Failed to allocate %lu bytes\n", MAXSTRLEN);
        goto fault;
    }
    uint64_t copied   = 0;
    uint64_t tempAddr = getScratchPageVA();
    while (copied < MAXSTRLEN) {
        uint64_t pageBase   = (base + copied) & ~(PAGE_SIZE - 1);
        uint64_t pageOffset = (base + copied) & (PAGE_SIZE - 1);
        uint64_t chunk      = PAGE_SIZE - pageOffset;
        if (chunk > MAXSTRLEN - copied) chunk = MAXSTRLEN - copied;
        uint64_t physicalAddr = getPhysicalAddr(proc->pml4, pageBase, false);
        if (physicalAddr == 0 || physicalAddr == ONDEMAND_MAP_ADDRESS) {
            UNLOCK(proc->lock);
            mapProcessAddr(proc, pageBase, true);
            LOCK(proc->lock);
            physicalAddr = getPhysicalAddr(proc->pml4, pageBase, false);
            if (physicalAddr == 0) {
                warn("Failed to get physicall address for page base 0x%lx\n", pageBase);
                goto fault;
            }
        }
        vmmMapPage(vmmGetPML4(0), physicalAddr, tempAddr,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC, MAP_PRESENT);
        void* src = (void*)(tempAddr + pageOffset);
        void* nul = memchr(src, 0, chunk);
        if (nul) {
            size_t len = (uintptr_t)nul - (uintptr_t)src;
            memcpy(data + copied, src, len);
            data[copied + len] = 0;
            vmmUnmapPage(vmmGetPML4(0), tempAddr);
            return data;
        }
        memcpy(data + copied, src, chunk);
        copied += chunk;
        vmmUnmapPage(vmmGetPML4(0), tempAddr);
    }

fault:
    free(data);
    sendSignal(proc, SIGKILL);
    return NULL;
}

// Helper to check if a range is valid and within user space boundaries
static bool validateUserRange(Process* proc, uint64_t base, uint64_t len) {
    if (len == 0) return true;
    if (!iscanonical(base)) return false;
    if (base >= USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE) return true;
    uint64_t end;
    if (__builtin_add_overflow(base, len, &end)) return false;
    uint64_t curr = base;
    while (curr < end) {
        TaskMapping* mapping = findMappingInProcess(proc, curr);
        if (!mapping) return false;
        uint64_t mappingEnd = mapping->virtualStart + mapping->memLength;
        if (end <= mappingEnd) {
            return true;
        }
        curr = mappingEnd;
    }
    return false;
}

const uint8_t* copyFromUser(Process* proc, uint64_t userSrc, uint64_t len) {
    if (len == 0) return NULL;
    if (!validateUserRange(proc, userSrc, len)) {
        warn("Invalid user read range: 0x%lx (%lu bytes)\n", userSrc, len);
        sendSignal(proc, SIGKILL);
        return NULL;
    }
    uint8_t* dst = (uint8_t*)malloc(len);
    if (!dst) {
        error("Failed to allocate bytes to copy from user process\n");
    }
    uint8_t* originalDst = dst;
    uint64_t src         = userSrc;
    uint64_t remaining   = len;
    uint64_t tempAddr    = getScratchPageVA();
    while (remaining > 0) {
        uint64_t pageBase   = src & ~(PAGE_SIZE - 1);
        uint64_t pageOffset = src & (PAGE_SIZE - 1);
        uint64_t copySize   = PAGE_SIZE - pageOffset;
        if (copySize > remaining) copySize = remaining;
        uint64_t physicalAddr = getPhysicalAddr(proc->pml4, pageBase, false);
        if (physicalAddr == 0 || physicalAddr == ONDEMAND_MAP_ADDRESS) {
            UNLOCK(proc->lock);
            mapProcessAddr(proc, pageBase, true);
            LOCK(proc->lock);
            physicalAddr = getPhysicalAddr(proc->pml4, pageBase, false);
            if (physicalAddr == 0) {
                free(originalDst);
                sendSignal(proc, SIGKILL);
                return NULL;
            }
        }
        vmmMapPage(vmmGetPML4(0), physicalAddr, tempAddr,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_NOEXEC, MAP_PRESENT);
        memcpy(dst, (void*)(tempAddr + pageOffset), copySize);
        vmmUnmapPage(vmmGetPML4(0), tempAddr);
        src += copySize;
        dst += copySize;
        remaining -= copySize;
    }
    return originalDst;
}

bool copyToUser(Process* proc, uint64_t userDst, const void* kernelSrc, uint64_t len) {
    if (len == 0) return true;
    if (!validateUserRange(proc, userDst, len)) {
        warn("Invalid user write range: 0x%lx\n", userDst);
        sendSignal(proc, SIGKILL);
        return false;
    }
    info("Copying to user address 0x%lx - 0x%lx\n", userDst, userDst + len);
    const uint8_t* src       = (const uint8_t*)kernelSrc;
    uint64_t       dst       = userDst;
    uint64_t       remaining = len;
    uint64_t       tempAddr  = getScratchPageVA();
    while (remaining > 0) {
        uint64_t pageBase   = dst & ~(PAGE_SIZE - 1);
        uint64_t pageOffset = dst & (PAGE_SIZE - 1);
        uint64_t copySize   = PAGE_SIZE - pageOffset;
        if (copySize > remaining) copySize = remaining;
        uint64_t physicalAddr = getPhysicalAddr(proc->pml4, pageBase, false);
        if (physicalAddr == 0 || physicalAddr == ONDEMAND_MAP_ADDRESS) {
            mapProcessAddr(proc, pageBase, true);
            physicalAddr = getPhysicalAddr(proc->pml4, pageBase, false);
            if (physicalAddr == 0) {
                sendSignal(proc, SIGKILL);
                return false;
            }
        }
        vmmMapPage(vmmGetPML4(0), physicalAddr, tempAddr,
                   MAP_PROTECTION_KERNEL | MAP_PROTECTION_RW | MAP_PROTECTION_NOEXEC, MAP_PRESENT);
        memcpy((void*)(tempAddr + pageOffset), src, copySize);
        vmmUnmapPage(vmmGetPML4(0), tempAddr);
        dst += copySize;
        src += copySize;
        remaining -= copySize;
    }
    return true;
}

void syscallInitHandlers() {
    handlers[SYS_EXIT]         = syscallExit;
    handlers[SYS_READ]         = syscallRead;
    handlers[SYS_WRITE]        = syscallWrite;
    handlers[SYS_SEEK]         = syscallSeek;
    handlers[SYS_GETINFO]      = syscallGetInfo;
    handlers[SYS_OPEN]         = syscallOpen;
    handlers[SYS_CLOSE]        = syscallClose;
    handlers[SYS_GETMEM]       = getMemory;
    handlers[SYS_FREEMEM]      = freeMemory;
    handlers[SYS_MOUNT]        = syscallMount;
    handlers[SYS_UMOUNT]       = syscallUnmount;
    handlers[SYS_GETPARTITION] = syscallGetPartition;
    handlers[SYS_PIVOT]        = syscallPivot;
    handlers[SYS_CREATEDIR]    = syscallCreateDir;
    handlers[SYS_EXEC]         = syscallExec;
}

extern Spinlock printRegsLock;

static void printRfl(uint64_t rflags) {
    if (rflags & 0x00000001) printf("CF ");
    if (rflags & 0x00000004) printf("PF ");
    if (rflags & 0x00000010) printf("AF ");
    if (rflags & 0x00000040) printf("ZF ");
    if (rflags & 0x00000080) printf("SF ");
    if (rflags & 0x00000100) printf("TF ");
    if (rflags & 0x00000200) printf("IF ");
    if (rflags & 0x00000400) printf("DF ");
    if (rflags & 0x00000800) printf("OF ");
    if (rflags & 0x00010000) printf("RF ");
    if (rflags & 0x00020000) printf("VM ");
    if (rflags & 0x00040000) printf("AC ");
    if (rflags & 0x00080000) printf("VIF ");
    if (rflags & 0x00100000) printf("VIP ");
    if (rflags & 0x00200000) printf("ID ");
    if (rflags & 0x80000000) printf("AI ");
    printf("\n");
}

static void printRegs(IsrRegisters* regs) {
    printf("\tv=0x%016.16llx e=0b%016.16llb\n", regs->interrupt_number, regs->error_code);
    printf("RAX=0x%016.16llx RBX=0x%016.16llx RCX=0x%016.16llx RDX=0x%016.16llx\n", regs->rax,
           regs->rbx, regs->rcx, regs->rdx);
    printf("RSI=0x%016.16llx RDI=0x%016.16llx RBP=0x%016.16llx RSP=0x%016.16llx\n", regs->rsi,
           regs->rdi, regs->rbp, regs->orig_rsp);
    printf("R8 =0x%016.16llx R9 =0x%016.16llx R10=0x%016.16llx R11=0x%016.16llx\n", regs->r8,
           regs->r9, regs->r10, regs->r11);
    printf("R12=0x%016.16llx R13=0x%016.16llx R14=0x%016.16llx R15=0x%016.16llx\n", regs->r12,
           regs->r13, regs->r14, regs->r15);
    printf("RIP=0x%016.16llx RFL=", regs->rip);
    printRfl(regs->rflags);
    printf("CS =0x%02.2llx\n", regs->cs);
    printf("ES =0x%02.2llx\n", regs->es);
    printf("DS =0x%02.2llx\n", regs->ds);
    printf("FS =0x%02.2llx\n", regs->fs);
    printf("GS =0x%02.2llx\n", regs->gs);
    printf("SS =0x%02.2llx\n", regs->ss);
    printf("CR3=0x%016.16llx\n", regs->cr3);
}

void syscallHandler(SyscallRegs* sysRegs) {
    Thread* currentThread = getCurrentThread();
    LOCK(currentThread->owner->lock);
    currentThread->status              = THREADSTATUS_BLOCKED;
    currentThread->registers->cs       = 0x23;
    currentThread->registers->ss       = 0x1B;
    currentThread->registers->rbx      = sysRegs->rbx;
    currentThread->registers->rdx      = sysRegs->arg2;
    currentThread->registers->rsi      = sysRegs->arg1;
    currentThread->registers->rdi      = sysRegs->arg0;
    currentThread->registers->rbp      = sysRegs->rbp;
    currentThread->registers->orig_rsp = getUserRSP();
    currentThread->registers->r8       = sysRegs->arg4;
    currentThread->registers->r9       = sysRegs->arg5;
    currentThread->registers->r10      = sysRegs->arg3;
    currentThread->registers->r12      = sysRegs->r12;
    currentThread->registers->r13      = sysRegs->r13;
    currentThread->registers->r14      = sysRegs->r14;
    currentThread->registers->r15      = sysRegs->r15;
    currentThread->registers->rip      = sysRegs->rcx;
    currentThread->registers->rflags   = sysRegs->r11;
    currentThread->registers->rax      = sysRegs->num;
    UNLOCK(currentThread->owner->lock);
    LOCK(printRegsLock);
    printf("SYSCALL:\n");
    printRegs(currentThread->registers);
    if (sysRegs->num < SYS_MAX && handlers[sysRegs->num]) {
        debug("Dispatching syscall %lu\n", sysRegs->num);
        UNLOCK(printRegsLock);
        uint64_t ret = handlers[sysRegs->num](sysRegs);
        if (sysRegs->num != SYS_EXIT) {
            currentThread->registers->rax = ret;
            Process* proc                 = getCurrentProc();
            proc->state                   = PROCESSSTATE_READY;
            currentThread->status         = THREADSTATUS_READY;
            UNLOCK(proc->lock);
        }
        nextProc();
    }
    todo(true, "Syscall %lu\n", sysRegs->num);
}