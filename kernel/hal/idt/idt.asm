bits 64
section .text
global loadIDTASM
loadIDTASM:
    mov [rel IDT.limit], rsi
    mov [rel IDT.base], rdi
    lidt [rel IDT]
    sti
    ret

section .trampoline.data
global IDT
IDT:
    .limit: dw 0
    .base: dq 0