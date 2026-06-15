#include <common/io/io.h>
#include <kernel/hal/msr.h>
#include <kernel/task/syscall.h>

void initMSRs() {
    uint64_t star_msr = ((uint64_t)(0x13) << 48) | ((uint64_t)0x08 << 32);
    wrmsr(STAR_MSR, star_msr);
    wrmsr(SYSCALL_MASK_MSR, 0x202);
    wrmsr(LSTAR_MSR, (uint64_t)syscallEntry);
}