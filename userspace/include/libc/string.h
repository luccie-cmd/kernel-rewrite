#if !defined(__LIBC_STRING_H__)
#define __LIBC_STRING_H__
#include <stddef.h>

void*       memset(void* __s, int __c, size_t __n);
void*       memcpy(void* __dest, const void* __src, size_t __n);
int         memcmp(const void* s1, const void* s2, size_t n);
char*       strchr(const char* __s, int __c);
char*       strtok(char* str, const char* delim);
char*       strtok_r(char* string, const char* limiter, char** context);
size_t      strspn(const char* str1, const char* str2);
size_t      strcspn(const char* str1, const char* str2);
size_t      strlen(const char* __s);
const char* strerr(int err);

#endif // __LIBC_STRING_H__
