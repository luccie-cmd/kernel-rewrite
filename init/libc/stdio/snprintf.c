#include <stdio.h>

int snprintf(char* __s, size_t __maxlen, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(__s, __maxlen, fmt, args);
    va_end(args);
    return result;
}