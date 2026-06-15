#if !defined(__COMMON_SPINLOCK_H__)
#define __COMMON_SPINLOCK_H__
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef atomic_bool Spinlock;
#define LOCK(s)                                                                                    \
    do {                                                                                           \
        while (atomic_exchange_explicit(&s, true, memory_order_acquire));                           \
    } while (0)

#define UNLOCK(s)                                                                                  \
    do {                                                                                           \
        atomic_store_explicit(&s, false, memory_order_release);                                     \
    } while (0)

#endif // __COMMON_SPINLOCK_H__
