#include <__syscall.h>
#include <libc.h>
#include <stdlib.h>

__attribute__((noreturn)) void exit(int status) {
    printf("Exiting with status & 0xFF = %hhu\n", (uint8_t)(status & 0xFF));
    __run_atexit();
    __libc_deinit();
    syscall_execute(SYS_EXIT, status & 0xFF);
    __builtin_unreachable();
}