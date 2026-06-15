#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_MASK ((1UL << KASAN_SHADOW_SCALE_SHIFT) - 1)
#define KASAN_POISON 0xFF

static uintptr_t kasan_shadow_offset;

/* These are defined in the linker script */
extern char __shadow_start[];
extern char __shadow_end[];
extern char __kasan_shadow_offset[]; /* Symbol to hold the offset value */

/* Translate real -> shadow */
static inline uint8_t* __attribute__((no_sanitize_address)) kasan_mem_to_shadow(uintptr_t addr) {
    /* Handle kernel space addresses (assuming x86_64 kernel space starts at 0xffff800000000000)
     */
    if (addr >= 0xffff800000000000) {
        addr -= 0xffff800000000000;
    }
    return (uint8_t*)((addr >> KASAN_SHADOW_SCALE_SHIFT) + kasan_shadow_offset);
}

void __attribute__((no_sanitize_address)) kasan_poison(void* addr, size_t size) {
    uintptr_t start         = (uintptr_t)addr;
    uintptr_t end           = start + size;
    uintptr_t aligned_start = start & ~7UL; /* Align to 8-byte boundary */

    /* Poison each 8-byte block in the range */
    for (uintptr_t block = aligned_start; block < end; block += 8) {
        uint8_t* shadow = kasan_mem_to_shadow(block);

        if (block < start) {
            /* Partial block at the beginning - only poison the offset part */
            size_t offset = start - block;
            if (offset > 0 && offset < 8) {
                *shadow = offset; /* First 'offset' bytes are poisoned */
            }
        } else if (block + 8 > end) {
            /* Partial block at the end */
            size_t valid_bytes = end - block;
            if (valid_bytes > 0 && valid_bytes < 8) {
                *shadow = valid_bytes; /* Only 'valid_bytes' are safe */
            }
        } else {
            /* Full block - all 8 bytes poisoned */
            *shadow = KASAN_POISON;
        }
    }
}

void __attribute__((no_sanitize_address)) kasan_unpoison(void* addr, size_t size) {
    uintptr_t start         = (uintptr_t)addr;
    uintptr_t end           = start + size;
    uintptr_t aligned_start = start & ~7UL;

    for (uintptr_t block = aligned_start; block < end; block += 8) {
        uint8_t* shadow = kasan_mem_to_shadow(block);

        if (block < start) {
            /* Beginning partial block */
            size_t offset = start - block;
            if (offset > 0 && offset < 8) {
                *shadow = offset; /* First 'offset' bytes remain poisoned */
            }
        } else if (block + 8 > end) {
            /* End partial block */
            size_t valid_bytes = end - block;
            if (valid_bytes > 0 && valid_bytes < 8) {
                *shadow = valid_bytes; /* Only 'valid_bytes' are unpoisoned */
            }
        } else {
            /* Full block - completely unpoisoned */
            *shadow = 0;
        }
    }
}

void __attribute__((no_sanitize_address)) asan_init(void) {
    /* Get the shadow offset from the linker symbol */
    kasan_shadow_offset = (uintptr_t)&__kasan_shadow_offset;

    /* Clear the entire shadow region */
    uint8_t* shadow_start = (uint8_t*)__shadow_start;
    uint8_t* shadow_end   = (uint8_t*)__shadow_end;

    for (uint8_t* p = shadow_start; p < shadow_end; p++) {
        *p = 0;
    }

    printf("ASAN initialized: shadow region [%p - %p), offset 0x%lx\n", shadow_start, shadow_end,
           kasan_shadow_offset);
}

static void __attribute__((no_sanitize_address)) kasan_report(uintptr_t addr, size_t size,
                                                              bool store) {
    uint8_t* shadow     = kasan_mem_to_shadow(addr);
    uint8_t  shadow_val = *shadow;
    size_t   offset     = addr & 7;

    printf("\n===================================\n");
    printf("KASAN: invalid %s of size %zu at address %p\n", store ? "write" : "read", size,
           (void*)addr);
    printf("Shadow byte at %p is 0x%02x (offset %zu)\n", shadow, shadow_val, offset);

    if (shadow_val > 0 && shadow_val < 8) {
        printf("First %d byte(s) are poisoned in this 8-byte block\n", shadow_val);
    }

    printf("===================================\n");
    exit(1);
}

void __attribute__((no_sanitize_address)) __asan_handle_no_return() {
    /* Handle functions that don't return (like longjmp) */
}

static bool __attribute__((no_sanitize_address)) kasan_check(uintptr_t addr, size_t size) {
    /* Skip checking for certain ranges (e.g., kernel text, modules) */
    if (addr >= 0xffffffff80000000) {
        return true; /* Skip kernel text */
    }

    uintptr_t end     = addr + size;
    uintptr_t current = addr;

    while (current < end) {
        uintptr_t shadow_addr = (current >> KASAN_SHADOW_SCALE_SHIFT) + kasan_shadow_offset;

        /* Check if shadow address is within the shadow region */
        if (shadow_addr < (uintptr_t)__shadow_start || shadow_addr >= (uintptr_t)__shadow_end) {
            return true; /* Skip if outside shadow region */
        }

        uint8_t shadow = *(uint8_t*)shadow_addr;

        if (shadow != 0) {
            size_t offset = current & 7;

            if (shadow == KASAN_POISON) {
                /* Entire 8-byte block is poisoned */
                return false;
            }

            /* Partial poisoning (first 'shadow' bytes are poisoned) */
            if (offset < shadow) {
                /* Accessing poisoned bytes */
                return false;
            }
        }

        current = (current & ~7UL) + 8;
    }

    return true;
}

/* Define ASAN instrumentation callbacks */
#define DEFINE_ASAN_LOAD(size)                                                                     \
    void __attribute__((no_sanitize_address)) __asan_load##size##_noabort(uintptr_t addr) {        \
        if (!kasan_check(addr, size)) kasan_report(addr, size, false);                             \
    }

#define DEFINE_ASAN_STORE(size)                                                                    \
    void __attribute__((no_sanitize_address)) __asan_store##size##_noabort(uintptr_t addr) {       \
        if (!kasan_check(addr, size)) kasan_report(addr, size, true);                              \
    }

DEFINE_ASAN_LOAD(1)
DEFINE_ASAN_LOAD(2)
DEFINE_ASAN_LOAD(4)
DEFINE_ASAN_LOAD(8)
DEFINE_ASAN_LOAD(16)

DEFINE_ASAN_STORE(1)
DEFINE_ASAN_STORE(2)
DEFINE_ASAN_STORE(4)
DEFINE_ASAN_STORE(8)
DEFINE_ASAN_STORE(16)

void __attribute__((no_sanitize_address)) __asan_loadN_noabort(uintptr_t addr, size_t size) {
    if (!kasan_check(addr, size)) kasan_report(addr, size, false);
}

void __attribute__((no_sanitize_address)) __asan_storeN_noabort(uintptr_t addr, size_t size) {
    if (!kasan_check(addr, size)) kasan_report(addr, size, true);
}