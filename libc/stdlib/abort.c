#include <common/spinlock.h>
#include <kernel/hal/irq/irq.h>
#include <kernel/vfs/vfs.h>
#include <stdio.h>

static Spinlock abortLock;

static void printOffset(size_t offset) {
    for (size_t i = 0; i < offset; ++i) {
        putchar(' ');
    }
}

static void printMountpoint(size_t offset, MountPoint* mp) {
    printOffset(offset);
    printf("`%s`\n", mp->name);
    for (size_t i = 0; i < dyn_size(mp->kids); ++i) {
        if (mp->kids[i]) {
            printMountpoint(offset + 4, mp->kids[i]);
        }
    }
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

void __attribute__((noreturn)) abort() {
    LOCK(abortLock);
    puts("KERNEL PANIC");
    printMountpoint(0, rootMount);
    puts("Sending abort signal to other cores");
    sendIPI(0xeb);
    puts("Sent IPI");
    uint64_t rbp = 0;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    walkStack((void**)rbp);
    // TODO: Do we want to print info from the drivers and vfs?
    // TODO: Stacktrace
    __asm__ volatile("cli" ::: "memory", "cc");
    while (1) {
        __asm__("hlt");
    }
}
