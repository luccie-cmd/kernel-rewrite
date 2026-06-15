#include <__syscall.h>

__attribute__((noreturn)) void abort() {
    syscall_execute(SYS_SENDSIGN, SIGABORT);
    __builtin_unreachable();
}