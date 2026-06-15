#if !defined(__KERNEL_TASK_TASK_H__)
#define __KERNEL_TASK_TASK_H__
#include <common/io/regs.h>
#include <common/spinlock.h>
#include <kernel/mmu/mmu.h>
#include <kernel/objects/elf.h>
#include <stdbool.h>
#include <stdint.h>
#define MAX_SIGNALS 3
#define MAX_FDS 16
#define INIT_PID 1
#define USER_STACK_TOP 0x00007FFFFFFFF000ULL
#define USER_STACK_PAGES 16

typedef struct TaskMapping {
    uint64_t            virtualStart;
    uint64_t            fileOffset;
    uint64_t            memLength;
    uint64_t            fileLength;
    uint64_t            alignment;
    uint64_t            handle;
    uint8_t             permissions;
    struct TaskMapping* next;
} TaskMapping;

typedef enum ThreadStatus {
    THREADSTATUS_READY,
    THREADSTATUS_RUNNING,
    THREADSTATUS_BLOCKED,
    THREADSTATUS_ZOMBIE,
    THREADSTATUS_DEAD,
} ThreadStatus;

typedef struct __attribute__((aligned(64))) Thread {
    uint8_t         fpuState[1024];
    uint8_t         exitCode;
    uint16_t        tid;
    IsrRegisters*   registers;
    uint64_t        fsBase;
    uint64_t        rsp;
    ThreadStatus    status;
    struct Thread*  next;
    struct Process* owner;
    bool            isKernel;
} Thread;

typedef enum ProcessState {
    PROCESSSTATE_READY,
    PROCESSSTATE_ZOMBIE,
    PROCESSSTATE_BLOCKED,
    PROCESSSTATE_RUNNING,
    PROCESSSTATE_WAITING,
} ProcessState;

typedef enum FileType {
    FILETYPE_NONE,
    FILETYPE_REGULAR,
    FILETYPE_TTY,
} FileType;

typedef struct ProcFile {
    FileType type;
    size_t   pos;
    uint8_t  flags;
    uint8_t  refcount;
    uint64_t vfsHandle;
    size_t (*read)(struct ProcFile* f, void* buf, size_t count);
    size_t (*write)(struct ProcFile* f, const void* buf, size_t count);
    void (*close)(struct ProcFile* f);
} ProcFile;

typedef void (*signalHandler)(size_t);
typedef struct Process {
    uint64_t        pid;
    uint64_t        waitingFor;
    ProcessState    state;
    uint8_t         waitStatus;
    uint8_t         exitCode;
    bool            hasStarted;
    PML4*           pml4;
    struct Process *next, *prev;
    dynarray(struct Process*) children;
    struct Process* parent;
    TaskMapping*    memoryMapping;
    Thread*         threads;
    Elf64_Addr      baseAddr;
    signalHandler   signals[MAX_SIGNALS];
    ElfFile*        elfObj;
    dynarray(Elf64_Rela*) relas;
    ProcFile* FDs[MAX_FDS];
    Spinlock  lock;
} Process;

typedef struct __attribute__((packed)) GSbase {
    Thread*  currentThread; // 0x00
    Process* currentProc;   // 0x08
    uint64_t kernelCR3;     // 0x10
    uint64_t stackTop;      // 0x18
    uint64_t userRSP;       // 0x20
    uint64_t scratchPageVA; // 0x28
} GSbase;

void     taskInitialize();
bool     taskIsInitialized();
uint64_t getNewPID();
void     makeNewProcess(uint64_t pid, ElfFile* obj);
void     attachThread(uint64_t pid, uint64_t entryPoint);
void     mapProcessAddr(Process* proc, uint64_t virtualAddress, bool shouldReturn);
void     nextProc();
void     cleanProc(uint64_t pid, uint8_t exitCode);
bool     cleanThread(uint64_t pid, uint64_t tid, uint8_t exitCode);
Process* getCurrentProc();
Thread*  getCurrentThread();
Process* getParentProc();
uint64_t getUserRSP();
uint64_t getScratchPageVA();
void     unblockProcess(Process* proc);
void     blockProcess(Process* proc);
void     sendSignal(Process* proc, size_t signal);
void     loadGSbase();

#endif // __KERNEL_TASK_TASK_H__
