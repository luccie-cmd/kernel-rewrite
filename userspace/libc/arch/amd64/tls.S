.intel_syntax noprefix
.global __tls_get_addr
.global __tls_set_addr
.section .text
__tls_get_addr:
    rdfsbase rax
    ret

__tls_set_addr:
    wrfsbase rdi
    ret