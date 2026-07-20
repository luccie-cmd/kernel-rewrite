#include <stdio.h>
#include <string.h>

int puts(const char* str) {
    return (int)fwrite(str, strlen(str), 1, stdout);
}