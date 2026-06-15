#if !defined(__KERNEL_TASK_SYSCALL_H__)
#define __KERNEL_TASK_SYSCALL_H__
#include <common/io/regs.h>
#include <kernel/task/task.h>

#define SYS_EXIT 0
#define SYS_READ 1
#define SYS_WRITE 2
#define SYS_SEEK 3
#define SYS_GETINFO 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_GETMEM 7
#define SYS_FREEMEM 8
#define SYS_MOUNT 9
#define SYS_UMOUNT 10
#define SYS_GETPARTITION 11
#define SYS_SENDSIGN 12
#define SYS_PIVOT 13
#define SYS_CREATEDIR 14
#define SYS_EXEC 15
#define SYS_MAX 16

#define SIGABORT 0
#define SIGKILL 1
#define SIGCHILD 2

#define O_RDONLY (1 << 0)
#define O_WRONLY (1 << 1)
#define O_RDWR (1 << 2)
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#define O_APPEND (1 << 3)
#define O_TRUNC (1 << 4)
#define O_CREAT (1 << 5)

#define MOUNT_DISKPART (1 << 0)

typedef struct __attribute__((packed)) getinfo_structure {
    size_t   size;
    size_t   currentOffset;
    uint32_t permissions; // RWX
    uint32_t flags;       // O_*
    uint8_t  type;
} getinfo_structure;

typedef struct __attribute__((packed)) partition_entry {
    uint8_t PARTUUID[16];
    uint8_t diskId;
    uint8_t partId;
} partition_entry;

typedef uint64_t (*syscallHandlerEntry)(SyscallRegs* regs);
uint64_t getMemory(SyscallRegs* regs);
uint64_t freeMemory(SyscallRegs* regs);
uint64_t syscallExit(SyscallRegs* regs);
uint64_t syscallRead(SyscallRegs* regs);
uint64_t syscallWrite(SyscallRegs* regs);
uint64_t syscallSeek(SyscallRegs* regs);
uint64_t syscallClose(SyscallRegs* regs);
uint64_t syscallOpen(SyscallRegs* regs);
uint64_t syscallGetInfo(SyscallRegs* regs);
uint64_t syscallGetPartition(SyscallRegs* regs);
uint64_t syscallMount(SyscallRegs* regs);
uint64_t syscallUnmount(SyscallRegs* regs);
uint64_t syscallPivot(SyscallRegs* regs);
uint64_t syscallCreateDir(SyscallRegs* regs);
uint64_t syscallExec(SyscallRegs* regs);

void           syscallInitHandlers();
void           syscallEntry(SyscallRegs* regs);
const char*    copyStringFromUser(Process* proc, uint64_t base);
const uint8_t* copyFromUser(Process* proc, uint64_t base, uint64_t len);
bool           copyToUser(Process* proc, uint64_t userBase, const void* kernelBuffer, uint64_t len);

#endif // __KERNEL_TASK_SYSCALL_H__
