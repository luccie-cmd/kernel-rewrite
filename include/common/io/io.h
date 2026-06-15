#if !defined(__COMMON_IO_IO_H__)
#define __COMMON_IO_IO_H__
#include <stdint.h>
#define ALIGNUP(data, align) (((data) + (align) - 1) & ~((align) - 1))
#define ALIGNDOWN(data, align) ((data) & ~((align) - 1))
#define iscanonical(addr) ((((uint64_t)(addr) >> 48) == 0) || (((uint64_t)(addr) >> 48) == 0xFFFF))
#define LAPIC_BASE_MSR 0x1B
#define STAR_MSR 0xC0000081
#define LSTAR_MSR 0xC0000082
#define SYSCALL_MASK_MSR 0xC0000084

void     outb(uint16_t port, uint8_t data);
uint16_t inw(uint16_t port);
uint64_t rdmsr(uint32_t msr);
void     wrmsr(uint32_t msr, uint64_t val);
uint64_t rdcr2();
uint64_t rdcr3();
void     invalpg(void* addr);

#endif // __COMMON_IO_IO_H__
