#if !defined(__COMMON_DYNARRAY_H__)
#define __COMMON_DYNARRAY_H__

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define dynarray(T) T*

typedef struct {
    size_t size;
    size_t capacity;
} DynArrayHeader;

#define _dyn_hdr(arr) ((DynArrayHeader*)((char*)(arr) - sizeof(DynArrayHeader)))
#define dyn_size(arr) ((arr) ? _dyn_hdr(arr)->size : 0)
#define dyn_capacity(arr) ((arr) ? _dyn_hdr(arr)->capacity : 0)

/* Ensure array has room for at least n more elements */
static inline void* _dyn_grow(void* arr, size_t elem_size) {
    size_t new_capacity = dyn_capacity(arr) ? dyn_capacity(arr) * 2 : 2;
    size_t new_size     = sizeof(DynArrayHeader) + new_capacity * elem_size;

    if (arr) {
        DynArrayHeader* old_hdr = _dyn_hdr(arr);
        old_hdr                 = realloc(old_hdr, new_size);
        if (!old_hdr) {
            printf("Failed to allocate memory for old_hdr\n");
            abort();
        }
        old_hdr->capacity = new_capacity;
        return (char*)old_hdr + sizeof(DynArrayHeader);
    } else {
        DynArrayHeader* hdr = malloc(new_size);
        if (!hdr) {
            printf("Failed to allocate memory for hdr\n");
            abort();
        }
        hdr->size     = 0;
        hdr->capacity = new_capacity;
        return (char*)hdr + sizeof(DynArrayHeader);
    }
}

// #ifdef __clang__
#define PUSH_DIAG(x)
// #else
// #define PUSH_DIAG(x) _Pragma(#x)
// #endif

#define dyn_push(arr, val)                                                                         \
    do {                                                                                           \
        PUSH_DIAG(GCC diagnostic push);                                                            \
        PUSH_DIAG(GCC diagnostic ignored "-Wanalyzer-malloc-leak");                                \
        if (!arr || dyn_size(arr) == dyn_capacity(arr)) {                                          \
            arr = _dyn_grow(arr, sizeof(*(arr)));                                                  \
        }                                                                                          \
        arr[dyn_size(arr)] = (val);                                                                \
        _dyn_hdr(arr)->size++;                                                                     \
        PUSH_DIAG(GCC diagnostic pop);                                                             \
    } while (0)

#define dyn_pop(arr)                                                                               \
    do {                                                                                           \
        if (dyn_size(arr) > 0) {                                                                   \
            _dyn_hdr(arr)->size--;                                                                 \
        }                                                                                          \
    } while (0)

#define dyn_free(arr)                                                                              \
    do {                                                                                           \
        if (arr) free(_dyn_hdr(arr));                                                              \
        arr = NULL;                                                                                \
    } while (0)

#endif // __COMMON_DYNARRAY_H__
