#include <string.h>

char* strtok(char* str, const char* delim) {
    static char* nextToken;
    if (!str && !(str = nextToken)) return NULL;
    str += strspn(str, delim);
    if (!*str) return nextToken = NULL;
    nextToken = str + strcspn(str, delim);
    if (*nextToken)
        *nextToken++ = 0;
    else
        nextToken = 0;
    return str;
}