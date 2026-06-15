#include <common/dbg/dbg.h>
#include <stdlib.h>
#include <string.h>

char* strdup(const char* str1) {
    char* result = malloc(strlen(str1) + 1);
    if (!result) {
        error("Failed to allocate enough bytes for strdup\n");
    }
    memset(result, 0, strlen(str1) + 1);
    memcpy(result, str1, strlen(str1));
    return result;
}