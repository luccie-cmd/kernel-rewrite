bits 64
section .text
global loadIDTASM
loadIDTASM:
    mov [rel IDT.limit], si
    mov [rel IDT.base], rdi
    lidt [rel IDT]
    ret

section .trampoline.data
global IDT
IDT:
    .limit: dw 0
    .base: dq 0