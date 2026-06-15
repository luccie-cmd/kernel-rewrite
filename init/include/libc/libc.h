#if !defined(__LIBC_LIBC_H__)
#define __LIBC_LIBC_H__
#include <stdio.h>

#ifdef __clang__
#define ATTR_NO_STACK_PROTECTOR
#else
#define ATTR_NO_STACK_PROTECTOR __attribute__((optimize("no-stack-protector")))
#endif


#define ALIGNUP(data, align) (((data) + (align) - 1) & ~((align) - 1))
#define ALIGNDOWN(data, align) ((data) & ~((align) - 1))

void __libc_init();
void __malloc_init();
#define MALLOC_INIT_SIZE 32767

void __libc_deinit();
void __malloc_deinit();
void __closeAllFiles();

void __run_atexit(void);

extern FILE*  __openFiles[FOPEN_MAX];
extern int8_t __openedFilesCount;

#endif // __LIBC_LIBC_H__
