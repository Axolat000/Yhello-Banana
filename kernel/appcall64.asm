; kernel/appcall64.asm
;
; int  app_enter(int (*fn)(void*), void* arg, void* stack_top, uintptr_t* saved_sp);
; void app_leave(uintptr_t saved_sp, int code);
;
; app_enter() saves the callee-saved registers on the caller's stack,
; stores that stack pointer through saved_sp, and calls fn(arg) on the
; app's own stack. If fn returns, app_enter returns its result. An app
; that calls exit() lands in app_leave(), which goes straight back to the
; saved stack - app_enter() then returns `code` - wherever on its own
; stack the app was.

bits 64
global app_enter
global app_leave

section .text
app_enter:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rcx], rsp
    mov r12, rcx            ; callee-saved: survives the call
    mov rsp, rdx            ; the app's stack (16-byte aligned)
    mov rax, rdi
    mov rdi, rsi
    call rax
    push rax                ; its code returned: the kernel goes on on the boot core
    call smp_app_gate
    pop rax
    mov rsp, [r12]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

app_leave:
    mov rsp, rdi
    mov eax, esi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; void smp_app_gate(void) - kernel/app.c: every system call an app makes
; (and the return from its code into the kernel) passes through here first.
; On the boot core it does nothing; on another core (kernel/task.c) it
; moves the task back to the boot core, where the kernel runs. Every
; register is kept: the call's arguments are still in them.
global smp_app_gate
extern task_ap_go_home

smp_app_gate:
    pushfq
    cli
    push rax
    str ax
    cmp ax, 0x10                    ; no TSS (0) or the boot core's (0x10)
    jbe .here
    push rdi
    push rsi
    push rdx
    push rcx
    push r8
    push r9
    push r10
    push r11
    call task_ap_go_home            ; (the stack is 16-byte aligned here)
    pop r11
    pop r10
    pop r9
    pop r8
    pop rcx
    pop rdx
    pop rsi
    pop rdi
.here:
    pop rax
    popfq
    ret
; int  kjmp_set(kjmp_buf_t* b);       0 now, `val` again after kjmp_jump(b, val)
; void kjmp_jump(kjmp_buf_t* b, int val);
; (kernel/task.h: a task's way back after a CPU exception in its code)
global kjmp_set
global kjmp_jump

kjmp_set:
    mov [rdi], rbx
    mov [rdi + 8], rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15
    lea rdx, [rsp + 8]      ; the stack once kjmp_set has returned
    mov [rdi + 48], rdx
    mov rdx, [rsp]          ; where it returns to
    mov [rdi + 56], rdx
    xor eax, eax
    ret

kjmp_jump:
    mov eax, esi
    mov rbx, [rdi]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]
    mov rsp, [rdi + 48]
    jmp [rdi + 56]

