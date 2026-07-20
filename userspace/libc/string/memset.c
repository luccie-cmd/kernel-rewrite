#include <stdint.h>
#include <string.h>

void* memset(void* dest, int ch, size_t count) {
    uint8_t  cch   = (uint8_t)ch;
    uint8_t* cdest = (uint8_t*)dest;
    for (size_t i = 0; i < count; ++i) {
        cdest[i] = cch;
    }
    return dest;
}