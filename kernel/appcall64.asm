; SPDX-License-Identifier: GPL-3.0-or-later
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

; int app_enter_user(uintptr_t ip, uintptr_t a0, uintptr_t a1, uintptr_t a2,
;                    uintptr_t user_sp, uintptr_t* saved_sp);
;
; Like app_enter, but the app runs in ring 3 (kernel/app.c): the
; callee-saved registers are saved here and the stack pointer stored
; through saved_sp, then iretq starts ip(a0, a1, a2) in user mode on
; user_sp - where the return address the app's code returns to (a system
; call that ends it) is already stored. It comes back here only through
; app_leave(). The task's kernel stack for interrupts and system calls is
; in the TSS already (task_set_user, kernel/task.c).
global app_enter_user
extern g_user_cs
extern g_user_ss

app_enter_user:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [r9], rsp
    push qword [rel g_user_ss]
    push r8                         ; the app's stack
    push qword 0x202                ; interrupts on, nothing else
    push qword [rel g_user_cs]
    push rdi                        ; where it starts
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    xor eax, eax                    ; nothing of the kernel's goes to the app
    xor ebx, ebx
    xor ecx, ecx
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq


; int app_resume_user(void* frame, uintptr_t* saved_sp);
;
; A fork's child goes back to ring 3 from the system call its parent made:
; like app_enter_user (the callee-saved registers kept here, the stack
; pointer stored through saved_sp: app_leave comes back here), then every
; register comes from frame - the system call's (kernel/app.c's sysframe_t:
; PUSH_ALL's order, int_no and err_code, then what iretq takes), on top of
; the child's own kernel stack - and iretq.
global app_resume_user

app_resume_user:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rsi], rsp
    cli                             ; (the frame is on the kernel stack the TSS names: no interrupt in between)
    mov rsp, rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                     ; int_no, err_code
    iretq
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
