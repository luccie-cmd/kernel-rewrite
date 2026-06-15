#if !defined(__LIBC_STDIO_H__)
#define __LIBC_STDIO_H__
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define EOF (-1)
#define FOPEN_MAX 16

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct FILE {
    uint64_t handle;
    uint8_t  buffer[512];
    uint16_t bufferIdx;
    uint8_t  bufferMode;
    uint8_t  openSlot;
} FILE;

enum {
    FILE_BUFFER_MODE_FULL,
    FILE_BUFFER_MODE_DIRECT,
};

extern FILE* stdout;

int printf(const char* fmt, ...);
int puts(const char* str);
int vfprintf(FILE* stream, const char* fmt, va_list args);
int vsnprintf(char* __s, size_t __maxlen, const char* __format, va_list __arg);
int snprintf(char* __s, size_t __maxlen, const char* __format, ...);

FILE*  fopen(const char* __filename, const char* __modes);
int    fclose(FILE* __f);
int    fflush(FILE* __stream);
size_t fwrite(const void* __ptr, size_t __size, size_t __n, FILE* __s);
size_t fread(void* __ptr, size_t __size, size_t __n, FILE* __s);
int    fseek(FILE* __stream, long __off, int __whence);
long   ftell(FILE* __stream);

#endif // __LIBC_STDIO_H__
