#include <common/spinlock.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char     str[8192];
static Spinlock strLock;
extern Spinlock E9LineLock;

static int print(const char* fmtStr) {
    LOCK(E9LineLock);
    int i = 0;
    while (*fmtStr) {
        putchar(*fmtStr);
        fmtStr++;
        i++;
    }
    UNLOCK(E9LineLock);
    return i;
}

int vprintf(const char* format, va_list arg) {
    LOCK(strLock);
    memset(str, 0, sizeof(str));
    vsnprintf(str, sizeof(str), format, arg);
    int written = print(str);
    UNLOCK(strLock);
    return written;
}