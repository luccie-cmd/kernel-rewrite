#include <common/io/io.h>
#include <common/spinlock.h>
#include <stdio.h>

extern Spinlock E9LineLock;

int putchar(int c) {
    outb(0xE9, c);
    return c;
}

int putc(int c, FILE* stream) {
    (void)stream;
    return putchar(c);
}

int puts(const char* str) {
    LOCK(E9LineLock);
    int i = 0;
    while (*str) {
        putchar(*str);
        str++;
        i++;
    }
    putchar('\n');
    UNLOCK(E9LineLock);
    return i;
}