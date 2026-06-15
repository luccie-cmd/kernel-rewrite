global syscall_execute
section .text
syscall_execute:
;     cmp rdi, 1
;     jne .syscall
;     int3
;     jmp $
; .syscall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9
    mov r9, [rsp + 0x08]
    syscall
    ret