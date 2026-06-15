#include <stdint.h>
#include <string.h>

int strcmp(const char* s1, const char* s2) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (*p1 && *p2) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        ++p1;
        ++p2;
    }
    return 0;
}