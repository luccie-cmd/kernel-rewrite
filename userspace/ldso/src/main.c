#include <__syscall.h>
#include <types.h>

size_t strlen(const char* s) {
    size_t i = 0;
    while (*s) {
        s++;
        i++;
    }
    return i;
}

void __attribute__((noreturn)) ldsoMain(int argc, char** argv, void* rsp) {
    (void)rsp;
    for (int i = 0; i < argc; ++i) {
        syscall_execute(SYS_WRITE, 1, argv[i], strlen(argv[i]));
    }
    // AT_BASE
    // AT_PHDR
    // AT_PHENT
    // AT_PHNUM
    // AT_ENTRY
    while (1) {}
}