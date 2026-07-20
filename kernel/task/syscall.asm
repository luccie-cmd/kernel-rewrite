extern tempValue
extern syscallHandler
section .trampoline.data
syscallSpinlock: db 0
section .trampoline.text
global syscallEntry
global switchProc
syscallEntry:
    swapgs
    mov gs:0x20, rsp
    mov rsp, gs:0x18
    push rax
    mov rax, gs:0x10
    mov cr3, rax
    mov rsp, gs:0x0
    mov eax, 0b111
    xsave [rsp]
    mov rsp, gs:0x18
    sub rsp, 8

    swapgs
    mov eax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs, ax
    swapgs

    push r15
    mov rax, gs:0x0
    rdfsbase r15
    mov [rax + 1040], r15
    swapgs
    mov eax, 0x10
    mov fs, ax
    swapgs
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rdx
    push rcx
    push rbx

    lea rdi, [rsp]
    sub rsp, 8
    jmp syscallHandler
    
switchProc:
    swapgs
    mov r11, gs:0x0
    swapgs
    mov eax, 0b111
    xrstor [r11]
    stac
    mfence
    mov cr3, rsi
    jmp .flush
.flush:
    mov r15, rdx

    mov ecx, 0xC0000080
	rdmsr
    or eax, 1
	wrmsr

    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    wrfsbase r15

    mov rbx, [rdi + 152]
    mov rcx, [rdi + 128]
    mov rdx, [rdi + 120]
    mov rbp, [rdi + 112]
    mov rsi, [rdi + 104]
    mov r8,  [rdi + 88]
    mov r9,  [rdi + 80]
    mov r10, [rdi + 72]
    mov r11, [rdi + 64]
    mov r12, [rdi + 56]
    mov r13, [rdi + 48]
    mov r14, [rdi + 40]
    mov r15, [rdi + 32]

    swapgs
    mov rsp, gs:0x18
    swapgs

    mov rax, [rdi + 224]
    push rax ; ss
    mov rax, [rdi + 216]
    push rax ; rsp
    mov rax, [rdi + 208]
    and rax, ~0x40000
    push rax ; rfl
    mov rax, [rdi + 200]
    push rax ; cs
    mov rax, [rdi + 192]
    push rax, ; rip

    mov rax, [rdi + 168]
    mov rdi, [rdi + 96]
    clac

    iretq
