#include <errno.h>
#include <libc.h>

int __libcGetErrnoValue() {
    return tlsBlock->errnoVal;
}