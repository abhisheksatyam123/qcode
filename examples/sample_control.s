; Intel syntax example: Fibonacci calculation with stack local variables
global fibonacci
fibonacci:
    push rbp
    mov rbp, rsp
    mov [rbp-8], rdi    ; n
    cmp qword [rbp-8], 1
    jle .L_base
    
    ; fib(n-1)
    mov rax, [rbp-8]
    sub rax, 1
    mov rdi, rax
    call fibonacci
    mov [rbp-16], rax   ; store fib(n-1)

    ; fib(n-2)
    mov rax, [rbp-8]
    sub rax, 2
    mov rdi, rax
    call fibonacci
    
    add rax, [rbp-16]   ; fib(n-1) + fib(n-2)
    jmp .L_ret

.L_base:
    mov rax, [rbp-8]

.L_ret:
    mov rsp, rbp
    pop rbp
    ret

global main
main:
    mov rdi, 7
    call fibonacci
    ret
