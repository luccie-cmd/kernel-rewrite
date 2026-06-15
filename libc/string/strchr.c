#include <string.h>
#undef strchr

char* strchr(const char* str, int chr) {
    while (*str) {
        if (*str == chr) return (char*)str;
        ++str;
    }
    return NULL;
}