.intel_syntax noprefix
.global _start
.extern main
.extern __libc_init_main
.section .text
_start:
    xor rbp, rbp
    and rsp, 0xfffffffffffffff0
    lea rdi, [rip + main]
    mov rsi, qword ptr [rsp]
    lea rdx, [rsp + 8]
    call [rip + __libc_init_main@GOTPCREL]
    hlt