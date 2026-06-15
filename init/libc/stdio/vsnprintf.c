#include <extra/stb_sprintf.h>
#include <stdio.h>

int vsnprintf(char* buffer, size_t bufsz, const char* format, va_list vlist) {
    return stbsp_vsnprintf(buffer, bufsz, format, vlist);
}