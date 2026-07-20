#include <__syscall.h>
#include <stdlib.h>

void __stack_chk_fail() {
    syscall_execute(SYS_WRITE, 1, "* Stack smashing detected *\n", 28);
    abort();
}