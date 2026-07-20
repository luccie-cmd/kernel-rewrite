#if !defined(__LIBC_STDDEF_H__)
#define __LIBC_STDDEF_H__

#define offsetof(st, m) \
    __builtin_offsetof(st, m)

#define NULL ((void*)0)

typedef unsigned long size_t;
typedef long ptrdiff_t;

#endif // __LIBC_STDDEF_H__
