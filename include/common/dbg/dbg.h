#if !defined(__COMMON_DBG_DBG_H__)
#define __COMMON_DBG_DBG_H__
#include <common/io/io.h>
#include <stdio.h>
#include <stdlib.h>

#define error(fmt, ...)                                                                            \
    do {                                                                                           \
        printf("ERROR:%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);                           \
        abort();                                                                                   \
    } while (0)
#define warn(fmt, ...)                                                                             \
    do {                                                                                           \
        printf("WARNING:%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);                         \
    } while (0)
#define info(fmt, ...)                                                                             \
    do {                                                                                           \
        printf("INFO:%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);                            \
    } while (0)
#ifdef DEBUG
#define debug(fmt, ...)                                                                            \
    do {                                                                                           \
        printf("DEBUG:%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);                           \
    } while (0)
#define todo(abrt, fmt, ...)                                                                       \
    do {                                                                                           \
        printf("TODO:%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);                            \
        if (abrt) {                                                                                \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
#else
#define debug(fmt, ...)                                                                            \
    do {                                                                                           \
    } while (0)
#define todo(abrt, fmt, ...)                                                                             \
    do {                                                                                           \
    } while (0)
#endif

#endif // __COMMON_DBG_DBG_H__
