#include <common/dbg/dbg.h>
#include <common/io/io.h>
#include <stddef.h>
#include <string.h>

void* memcpy(void* dst, const void* src, size_t num) {
    char*       u8Dst = (char*)dst;
    const char* u8Src = (const char*)src;
    for (size_t i = 0; i < num; i++) {
        u8Dst[i] = u8Src[i];
    }
    return dst;
}