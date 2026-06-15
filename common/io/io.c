#include <common/io/io.h>

void outb(uint16_t port, uint8_t byte) {
    __asm__ volatile("outb %0, %1" : : "a"(byte), "Nd"(port) : "memory");
}

uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

uint64_t rdmsr(uint32_t msr) {
    uint32_t eax, edx;
    __asm__ volatile("rdmsr" : "=a"(eax), "=d"(edx) : "c"(msr) : "memory");
    return ((uint64_t)edx << 32) | eax;
}

void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t eax = val & 0xFFFFFFFF, edx = val >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(eax), "d"(edx) : "memory");
}

void invalpg(void* addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

uint64_t rdcr3() {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

uint64_t rdcr2() {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}