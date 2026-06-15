#if !defined(__COMMON_IO_REGS_H__)
#define __COMMON_IO_REGS_H__
#include <stdint.h>

typedef struct __attribute__((packed)) IsrRegisters {
    //   0   8  16   24  32   40   48    56   64  72   80  88  96   104  112  120  128
    uint64_t gs, fs, es, ds, r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rdx, rcx,
        //  136 144  152  160  168  176                184        192  200  208     216      224
        _, returnIP, rbx, cr3, rax, interrupt_number, error_code, rip, cs, rflags, orig_rsp, ss;
} IsrRegisters;

typedef struct __attribute__((packed)) SyscallRegs {
    _Alignas(8) uint64_t rbx, rcx, arg2, rbp, arg1, arg0, arg4, arg5, arg3, r11, r12, r13, r14, r15, num;
} SyscallRegs;

#endif // __COMMON_IO_REGS_H__
