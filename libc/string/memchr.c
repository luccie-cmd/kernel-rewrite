#include <stdint.h>
#include <string.h>
#undef memchr

void* memchr(const void* ptr, int ch, size_t count) {
    const uint8_t* u8ptr = (const uint8_t*)ptr;
    while (count) {
        if (*u8ptr == ch) return (void*)u8ptr;

        ++u8ptr;
        --count;
    }
    return NULL;
}