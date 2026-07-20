global _start
extern ldsoMain
section .text
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    lea rdx, [rsp]
    jmp ldsoMain