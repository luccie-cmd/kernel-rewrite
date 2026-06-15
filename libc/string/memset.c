#include <stdint.h>
#include <string.h>

void* memset(void* __s, int __c, size_t __n) {
    uint8_t  cch   = (uint8_t)__c;
    uint8_t* cdest = (uint8_t*)__s;
    for (size_t i = 0; i < __n; ++i) {
        cdest[i] = cch;
    }
    return __s;
}