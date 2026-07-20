#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int vfprintf(FILE* stream, const char* fmt, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    if (needed <= 0) {
        return needed;
    }
    needed += 1;
    char* str = (char*)malloc((size_t)needed);
    if (!str) {
        puts("Failed to allocate memory for string");
        abort();
    }
    vsnprintf(str, (size_t)needed, fmt, args);
    size_t written = fwrite((const void*)str, 1, (size_t)needed - 1, stream);
    free((void*)str);
    return (int)written;
}