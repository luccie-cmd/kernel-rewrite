#include <common/dbg/dbg.h>
#include <common/io/regs.h>
#include <common/spinlock.h>
#include <kernel/hal/idt/idt.h>
#include <kernel/hal/irq/irq.h>
#include <kernel/task/task.h>

extern void loadIDTASM(uint64_t base, uint16_t limit);

typedef void (*exceptionHandler)(IsrRegisters*);
static IDTEntry __attribute__((section(".trampoline.data"))) entries[256];
static exceptionHandler                                      exceptionHandlers[32];
static void                                                  PFhandler(IsrRegisters* regs);

void loadIDT() {
    __asm__("cli");
    loadIDTASM((uint64_t)entries, sizeof(entries) - 1);
    idtLoadGates();
    for (uint8_t i = 0; i < 255; ++i) {
        idtEnableGate(i);
    }
    enablePFProtection();
    // enableUDProtection();
    // enableGPProtection();
}

void idtRegisterHandler(uint8_t gate, void* function, uint8_t type) {
    entries[gate] = IDT_ENTRY((uint64_t)function, 0x8, type, 3, 1);
}

void idtEnableGate(uint8_t gate) {
    entries[gate].present = 1;
}

void disablePFProtection() {
    exceptionHandlers[0xe] = NULL;
}

void enablePFProtection() {
    exceptionHandlers[0xe] = PFhandler;
}

// void disableUDProtection() {}
// void enableUDProtection() {}
// void disableGPProtection() {}
// void enableGPProtection() {}

Spinlock printRegsLock;

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

void saveKernelExecStateAndSchedule(IsrRegisters* regs) {
    Process* proc = getCurrentProc();
    LOCK(proc->lock);
    proc->state = PROCESSSTATE_BLOCKED;
    __asm__ volatile("swapgs");
    Thread* currentThread              = getCurrentThread();
    currentThread->registers->rax      = regs->rax;
    currentThread->registers->rbx      = regs->rbx;
    currentThread->registers->rcx      = regs->rcx;
    currentThread->registers->rdx      = regs->rdx;
    currentThread->registers->rdi      = regs->rdi;
    currentThread->registers->rsi      = regs->rsi;
    currentThread->registers->rbp      = regs->rbp;
    currentThread->registers->orig_rsp = regs->orig_rsp;
    currentThread->registers->r8       = regs->r8;
    currentThread->registers->r9       = regs->r9;
    currentThread->registers->r10      = regs->r10;
    currentThread->registers->r11      = regs->r11;
    currentThread->registers->r12      = regs->r12;
    currentThread->registers->r13      = regs->r13;
    currentThread->registers->r14      = regs->r14;
    currentThread->registers->r15      = regs->r15;
    currentThread->registers->rip      = regs->rip;
    currentThread->registers->rflags   = regs->rflags;
    currentThread->registers->cs       = regs->cs;
    currentThread->registers->ss       = regs->ss;
    UNLOCK(proc->lock);
    __asm__ volatile("sti");
    nextProc();
}

void handleInt(IsrRegisters* regs) {
    if (regs->interrupt_number == 0xeb) {
        __asm__ volatile("sti\nswapgs");
        debug("(CPU %lu) Received stop interrupt\n", getAPICID());
        while (true) {
            __asm__ volatile("nop");
        }
    }
    if (regs->interrupt_number == 0xed) {
        saveKernelExecStateAndSchedule(regs);
        __builtin_unreachable();
    }
    if (regs->interrupt_number < 32 && exceptionHandlers[regs->interrupt_number]) {
        exceptionHandlers[regs->interrupt_number](regs);
        return;
    }
    if (regs->interrupt_number == 19) {
        return;
    }
    LOCK(printRegsLock);
    printRegs(regs);
    UNLOCK(printRegsLock);
    error("Add interrupt handler for %lu\n", regs->interrupt_number);
}

static void walkStack(void** rbp) {
    const uint32_t MAX_DEPTH = 4096;
    uint32_t       depth     = 0;
    while (rbp && depth++ < MAX_DEPTH) {
        void* saved_rbp = rbp[0];
        void* ret_addr  = rbp[1];
        if (!ret_addr) break;
        printf("%p\n", ret_addr);
        if (saved_rbp <= (void*)rbp) break;
        rbp = (void**)saved_rbp;
    }
}
typedef struct __attribute__((packed)) PageFaultError {
    uint8_t  PPV : 1;   // 1=PPV   0=NP         0
    uint8_t  write : 1; // 1=Write 0=Read       1
    uint8_t  user : 1;  // 1=CPL3  0=CPL0       0
    uint8_t  rsvw : 1;  // Reserved field write 1
    uint8_t  insF : 1;  // Instruction fetch
    uint8_t  PKV : 1;   // Protection key violation
    uint8_t  SS : 1;    // Shadow stack
    uint8_t  reserved0;
    uint8_t  SGX : 1; // Software guard extension
    uint16_t reserved1;
} PageFaultError;
static void PFhandler(IsrRegisters* regs) {
    PageFaultError err = *(PageFaultError*)(&regs->error_code);
    if (err.user) {
        __asm__ volatile("swapgs");
        LOCK(printRegsLock);
        printf("ISR:\n");
        printRegs(regs);
        UNLOCK(printRegsLock);
        Thread* currentThread              = getCurrentThread();
        currentThread->status              = THREADSTATUS_BLOCKED;
        currentThread->registers->rax      = regs->rax;
        currentThread->registers->rbx      = regs->rbx;
        currentThread->registers->rcx      = regs->rcx;
        currentThread->registers->rdx      = regs->rdx;
        currentThread->registers->rdi      = regs->rdi;
        currentThread->registers->rsi      = regs->rsi;
        currentThread->registers->rbp      = regs->rbp;
        currentThread->registers->orig_rsp = regs->orig_rsp;
        currentThread->registers->r8       = regs->r8;
        currentThread->registers->r9       = regs->r9;
        currentThread->registers->r10      = regs->r10;
        currentThread->registers->r11      = regs->r11;
        currentThread->registers->r12      = regs->r12;
        currentThread->registers->r13      = regs->r13;
        currentThread->registers->r14      = regs->r14;
        currentThread->registers->r15      = regs->r15;
        currentThread->registers->rip      = regs->rip;
        currentThread->registers->rflags   = regs->rflags;
        currentThread->registers->cs       = regs->cs;
        currentThread->registers->ss       = regs->ss;
        __asm__ volatile("sti");
        mapProcessAddr(getCurrentProc(), rdcr2(), true);
        // error("Returned from mapProcAddr\n");
        return;
        // __builtin_unreachable();
    }
    LOCK(printRegsLock);
    printf("ISR (Kernel):\n");
    printRegs(regs);
    info("PPV = %lu\nwrite = %lu\nuser = %lu\nrsvw = %lu\ninsF = %lu\nPKV = %lu\nSS "
         "=%lu\nreserved0 = %lu\nSGX = %lu\nreserved1 = %lu\n",
         err.PPV, err.write, err.user, err.rsvw, err.insF, err.PKV, err.SS, err.reserved0, err.SGX,
         err.reserved1);
    UNLOCK(printRegsLock);
    error("TODO: Handle page fault\n");
}
