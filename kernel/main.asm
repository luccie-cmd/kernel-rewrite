global initX64
extern puts
extern abort

section .rodata
mxcsrVal:
    dd 0x1f80

section .text
initX64:
    push rbx
    push rcx
    push rdx

    mov eax, 7
    cpuid
    test ebx, 1 << 0
    jz .afterFSGSBaseEnable
    mov rax, cr4
    or rax, (1 << 16)
    mov cr4, rax
.afterFSGSBaseEnable:

    ; CPUID.1
    mov eax, 1
    cpuid

    ; Check SSE (bit 25) and SSE2 (bit 26)
    test edx, 1 << 25
    jz .no_sse
    test edx, 1 << 26
    jz .no_sse2

    ; Enable SSE: CR0.EM=0, CR0.MP=1
    mov rax, cr0
    and rax, ~(1 << 2)
    or  rax, (1 << 1)
    mov cr0, rax

    ; Enable OSFXSR, OSXMMEXCPT, XSAVE in CR4
    mov rax, cr4
    or  rax, (3 << 9)
    mov cr4, rax

    ; Load MXCSR
    ldmxcsr [rel mxcsrVal]

    ; Enable MCE
    mov rax, cr4
    or  rax, (1 << 6) | (1 << 11)
    mov cr4, rax

    ; Enable SMAP/SMEP
    ; mov rax, cr4
    ; ; or  rax, (1 << 20) | (1 << 21) | (1 << 22)
    ; mov cr4, rax

    ; Check SSE3 (bit 0), SSSE3 (bit 9), SSE4.1 (bit 19), SSE4.2 (bit 20)
    test ecx, 1 << 0
    jz .no_sse3
    test ecx, 1 << 9
    jz .no_ssse3
    test ecx, 1 << 19
    jz .no_sse41
    test ecx, 1 << 20
    jz .no_sse42

    ; Check XSAVE (bit 26) and AVX (bit 28)
    test ecx, 1 << 26
    jz .no_xsave
    test ecx, 1 << 28
    jz .no_avx

    mov rax, cr4
    or  rax, (1 << 18)
    mov cr4, rax

    ; Enable x87 (bit 0), SSE (bit 1), AVX (bit 2) in XCR0
    xor ecx, ecx
    xgetbv
    or eax, 0b111
    xsetbv

    ; Optional: check AVX2 support (CPUID.7)
    mov eax, 7
    xor ecx, ecx
    cpuid
    test ebx, 1 << 5
    jz .no_avx2

    ; Success
    jmp .done

.no_sse:
    mov rdi, msg_no_sse
    call puts
    call abort
.no_sse2:
    mov rdi, msg_no_sse2
    call puts
    call abort
.no_sse3:
    mov rdi, msg_no_sse3
    call puts
    jmp .done
    ; call abort
.no_ssse3:
    mov rdi, msg_no_ssse3
    call puts
    jmp .done
    ; call abort
.no_sse41:
    mov rdi, msg_no_sse41
    call puts
    jmp .done
    ; call abort
.no_sse42:
    mov rdi, msg_no_sse42
    call puts
    jmp .done
    ; call abort
.no_xsave:
    mov rdi, msg_no_xsave
    call puts
    jmp .done
    ; call abort
.no_avx:
    mov rdi, msg_no_avx
    call puts
    jmp .done
    ; call abort
.no_avx2:
    mov rdi, msg_no_avx2
    call puts
    ; not fatal

.done:
    pop rdx
    pop rcx
    pop rbx
    ret

section .rodata
msg_no_sse:     db "ERROR: SSE not supported", 0
msg_no_sse2:    db "ERROR: SSE2 not supported", 0
msg_no_sse3:    db "INFO: SSE3 not supported", 0
msg_no_ssse3:   db "INFO: SSSE3 not supported", 0
msg_no_sse41:   db "INFO: SSE4.1 not supported", 0
msg_no_sse42:   db "INFO: SSE4.2 not supported", 0
msg_no_xsave:   db "INFO: XSAVE not supported, AVX unavailable", 0
msg_no_avx:     db "INFO: AVX not supported", 0
msg_no_avx2:    db "INFO: AVX2 not supported", 0
