#include <__syscall.h>
#include <libc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void*       __shadow_start;
void*       __shadow_end;
void*       __kasan_shadow_offset;
extern void asan_init();

void ATTR_NO_STACK_PROTECTOR __libc_init_main(int (*main)(int, char**, char**)) {
#ifdef __ADDRESS_SANITIZER__
    __shadow_start        = (void*)syscall_execute(SYS_GETMEM, MALLOC_INIT_SIZE / 8);
    __shadow_end          = (uint8_t*)__shadow_start + (MALLOC_INIT_SIZE / 8);
    __kasan_shadow_offset = __shadow_start;
    asan_init();
#endif
    __libc_init();
    int returnCode = main(1, (char*[]){"main"}, (void*)0);
    exit(returnCode);
    __builtin_unreachable();
}