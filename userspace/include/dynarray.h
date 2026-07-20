#ifndef __COMMON_DYNARRAY_H__
#define __COMMON_DYNARRAY_H__

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define dynarray(T) T*

typedef struct {
    size_t size;
    size_t capacity;
} DynArrayHeader;

#define _dyn_hdr(arr) ((DynArrayHeader*)((char*)(arr) - sizeof(DynArrayHeader)))
#define dyn_size(arr) ((arr) ? _dyn_hdr(arr)->size : 0)
#define dyn_capacity(arr) ((arr) ? _dyn_hdr(arr)->capacity : 0)

static inline void* _dyn_grow(void* arr, size_t elem_size) {
    size_t          old_capacity = arr ? dyn_capacity(arr) : 0;
    size_t          new_capacity = old_capacity ? old_capacity * 2 : 8;
    size_t          total        = sizeof(DynArrayHeader) + new_capacity * elem_size;
    DynArrayHeader* hdr;
    if (arr) {
        hdr = realloc(_dyn_hdr(arr), total);
    } else {
        hdr = malloc(total);
        if (hdr) hdr->size = 0;
    }
    if (!hdr) return NULL;
    hdr->capacity = new_capacity;
    return (char*)hdr + sizeof(DynArrayHeader);
}

#define dyn_push(arr, value)                                                                       \
    do {                                                                                           \
        if (!(arr) || dyn_size(arr) >= dyn_capacity(arr)) {                                        \
            void* _tmp = _dyn_grow((arr), sizeof(*(arr)));                                         \
            if (!_tmp) abort();                                                                    \
            (arr) = _tmp;                                                                          \
        }                                                                                          \
        (arr)[_dyn_hdr(arr)->size++] = (value);                                                    \
    } while (0)

#define dyn_pop(arr)                                                                               \
    do {                                                                                           \
        if ((arr) && _dyn_hdr(arr)->size) _dyn_hdr(arr)->size--;                                   \
    } while (0)

#define dyn_free(arr)                                                                              \
    do {                                                                                           \
        if (arr) {                                                                                 \
            free(_dyn_hdr(arr));                                                                   \
            (arr) = NULL;                                                                          \
        }                                                                                          \
    } while (0)

#endif