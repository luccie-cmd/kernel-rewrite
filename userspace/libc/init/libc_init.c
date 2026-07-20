#include <__syscall.h>
#include <libc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void (*__preinit_array_start[])(void);
extern void (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

#if defined(__clang__)
#define UBSAN_NOSAN __attribute__((no_sanitize_address, no_sanitize("undefined")))
#elif defined(__GNUC__)
#define UBSAN_NOSAN __attribute__((no_sanitize_address, no_sanitize_undefined))
#else
#define UBSAN_NOSAN
#endif

void initArrays() {
    void** begin = (void**)__preinit_array_start;
    void** end   = (void**)__preinit_array_end;
    while (begin < end) {
        void* fn           = *begin;
        void (*func)(void) = (void (*)(void))fn;
        func();
        begin += sizeof(void*);
    }
    begin = (void**)__init_array_start;
    end   = (void**)__init_array_end;
    while (begin < end) {
        void* fn           = *begin;
        void (*func)(void) = (void (*)(void))fn;
        func();
        begin += sizeof(void*);
    }
}

void finiArrays() {
    void** begin = (void**)__fini_array_start;
    void** end   = (void**)__fini_array_end;
    while (begin < end) {
        void* fn           = *begin;
        void (*func)(void) = (void (*)(void))fn;
        func();
        begin += sizeof(void*);
    }
}

void ATTR_NO_STACK_PROTECTOR UBSAN_NOSAN __libc_init_main(int (*main)(int, char**, char**)) {
    initArrays();
    atexit(finiArrays);
    __libc_init();
    printf("Pointer to main is 0x%lx\n", main);
    int returnCode = main(1, (char*[]){"main"}, (void*)0);
    exit(returnCode);
    __builtin_unreachable();
}