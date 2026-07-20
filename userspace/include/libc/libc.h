#if !defined(__LIBC_LIBC_H__)
#define __LIBC_LIBC_H__
#include <stdio.h>
#include <stddef.h>

#ifdef __clang__
#define ATTR_NO_STACK_PROTECTOR
#else
#define ATTR_NO_STACK_PROTECTOR __attribute__((optimize("no-stack-protector")))
#endif

static_assert(sizeof(int) == 4, "Sizeof an int must be exactly equal to 4 bytes\n");

typedef struct TLSBlock {
    struct TLSBlock* self;
    void*            dtv;
    void*            threadPointer;
    void*            reserved0;
    void*            reserved1;
    uint64_t         stackCanary;
    int              errnoVal;
} TLSBlock;

typedef struct __MallocRegion {
    uint64_t               prePadding[5];
    size_t                 size;
    size_t                 freedSize;
    size_t                 allocSize;
    struct __MallocRegion* prev;
    struct __MallocRegion* next;
    uint8_t                free;
    uint8_t                padding[7];
    uint64_t               postPadding[5];
} __MallocRegion;

#define ALIGNED_MAGIC 0x414C49474E4544ULL

typedef struct __AlignedHeader {
    uint64_t magic;
    void *raw;
} __AlignedHeader;


static_assert(offsetof(TLSBlock, stackCanary) == 0x28, "TLS layout mismatch");

#define ALIGNUP(data, align) (((data) + (align) - 1) & ~((align) - 1))
#define ALIGNDOWN(data, align) ((data) & ~((align) - 1))

void __libc_init();
void __tls_set_addr(void* addr);
void __malloc_init();
void __initCurrentTlsBlock();
#define MALLOC_INIT_SIZE 32767

void __libc_deinit();
void __malloc_deinit();
void __closeAllFiles();

void __run_atexit(void);

extern FILE*                  __openFiles[FOPEN_MAX];
extern int8_t                 __openedFilesCount;
extern thread_local TLSBlock* tlsBlock;

#endif // __LIBC_LIBC_H__
