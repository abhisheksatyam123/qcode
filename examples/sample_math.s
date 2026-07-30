# Assembly example: Compute sum of numbers from 1 to N and factorial
.globl compute_sum
.type compute_sum, @function
compute_sum:
    # Input: rdi = n
    movq $0, %rax       # sum = 0
    movq $1, %rcx       # i = 1
.L_loop:
    cmpq %rdi, %rcx     # compare i with n
    jg .L_done          # if i > n goto .L_done
    addq %rcx, %rax     # sum += i
    incq %rcx           # i++
    jmp .L_loop
.L_done:
    ret

.globl main
.type main, @function
main:
    movq $10, %rdi      # arg = 10
    call compute_sum    # returns result in rax
    ret
