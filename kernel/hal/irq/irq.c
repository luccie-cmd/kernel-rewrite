#include <common/dbg/dbg.h>
#include <common/io/io.h>
#include <kernel/hal/irq/irq.h>
#include <kernel/hal/msr.h>

static void __attribute__((no_sanitize_address)) writeAPIC(uint32_t offset, uint32_t value) {
    __asm__("mfence" ::: "memory");
    *(volatile uint32_t* volatile)((rdmsr(LAPIC_BASE_MSR) & 0xFFFFF000) + offset) = value;
    __asm__("mfence" ::: "memory");
}
static uint32_t __attribute__((no_sanitize_address)) readAPIC(uint32_t offset) {
    __asm__("mfence" ::: "memory");
    return *(volatile uint32_t* volatile)((rdmsr(LAPIC_BASE_MSR) & 0xFFFFF000) + offset);
}

static uint64_t getAPICBase() {
    return rdmsr(LAPIC_BASE_MSR);
}

static void setAPICBase(uint64_t base) {
    wrmsr(LAPIC_BASE_MSR, (base & 0xFFFFF000) | (getAPICBase() & 0xFFF) | (1 << 11));
}

uint32_t getAPICID() {
    return (readAPIC(0x20) >> 24) & 0xff;
}

void __attribute__((no_sanitize_address)) initLAPIC() {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    setAPICBase(getAPICBase());
    writeAPIC(0xF0, readAPIC(0xF0) | 0x100);
}

void sendIPI(uint8_t vector) {
    uint32_t val = ((uint32_t)vector & 0xff) | (0 << 8) | (1 << 14) | (1 << 15) | (3 << 18);
    debug("Sending %x\n", val);
    writeAPIC(0x310, 0);
    writeAPIC(0x300, val);
}