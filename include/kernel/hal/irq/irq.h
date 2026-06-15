#if !defined(__KERNEL_HAL_IRQ_IRQ_H__)
#define __KERNEL_HAL_IRQ_IRQ_H__
#include <stdint.h>

uint32_t getAPICID();
void initLAPIC();
void sendIPI(uint8_t vector);

#endif // __KERNEL_HAL_IRQ_IRQ_H__
