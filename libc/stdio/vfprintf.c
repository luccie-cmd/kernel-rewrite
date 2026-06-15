#include <common/spinlock.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

FILE* stdout = (FILE*){(FILE*)1};

static char     str[8192];
static Spinlock strLock;
Spinlock        E9LineLock;

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

int vfprintf(FILE* stream, const char* format, va_list arg) {
    (void)stream;
    LOCK(strLock);
    memset(str, 0, sizeof(str));
    vsnprintf(str, sizeof(str), format, arg);
    int written = print(str);
    UNLOCK(strLock);
    return written;
}