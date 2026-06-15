#if !defined(__LIBC_SYSCALL_H__)
#define __LIBC_SYSCALL_H__
#include <stddef.h>
#include <stdint.h>

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

#define SIGABORT 0
#define SIGKILL 1
#define SIGCHILD 2

#define O_RDONLY (1 << 0)
#define O_WRONLY (1 << 1)
#define O_RDWR (1 << 2)
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
    uint32_t PARTUUID[4];
    uint8_t  diskId;
    uint8_t  partId;
} partition_entry;

uint64_t syscall_execute(uint64_t code, ...);

#endif // __LIBC_SYSCALL_H__
