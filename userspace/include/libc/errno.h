#if !defined(__LIBC_ERRNO_H__)
#define __LIBC_ERRNO_H__

int __libcGetErrnoValue();

#define EGENERAL 1

#define errno (__libcGetErrnoValue())

#endif // __LIBC_ERRNO_H__
