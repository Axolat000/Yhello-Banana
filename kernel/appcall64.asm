; kernel/appcall64.asm
;
; int  app_enter(int (*fn)(void*), void* arg, void* stack_top, uintptr_t* saved_sp, uintptr_t cr3);
; void app_leave(uintptr_t saved_sp, int code);
;
; app_enter() saves the callee-saved registers on the caller's stack,
; stores that stack pointer through saved_sp, and calls fn(arg) on the
; app's own stack. If fn returns, app_enter returns its result. An app
; that calls exit() lands in app_leave(), which goes straight back to the
; saved stack - app_enter() then returns `code` - wherever on its own
; stack the app was.
;
; cr3: the app's page tables (kernel/appmem.h), loaded once on its stack,
; or 0; the kernel's come back when its code returns.

bits 64
global app_enter
global app_leave
extern g_kernel_cr3

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
    test r8, r8
    jz .tables
    mov cr3, r8             ; the app's page tables (its stack is writable there)
.tables:
    mov rax, rdi
    mov rdi, rsi
    call rax
    mov rdx, [rel g_kernel_cr3]   ; its code returned: the kernel's tables first
    test rdx, rdx
    jz .kernel
    mov rcx, cr3
    cmp rcx, rdx
    je .kernel
    mov cr3, rdx
.kernel:
    push rax                ; the kernel goes on on the boot core
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

; The system calls: every entry of an app's table (kernel/app.c) is a stub
; "mov r11, function; jmp app_gate". app_gate switches to the kernel's page
; tables - before anything else, smp_app_gate's trip back to the boot core
; included - calls the function with the app's arguments, and comes back
; on the tables the app had (they are the same wherever it continues).
; The return address stays where the app put it, so the arguments that
; came on the stack (a 7th, 8th... - up to 12 in all) are copied below.
global app_gate

app_gate:
    push rbx
    push r12
    mov rbx, cr3                    ; the app's tables (callee-saved: survives the call)
    mov r12, r11                    ; the function
    mov r10, [rel g_kernel_cr3]
    test r10, r10
    jz .kernel
    cmp rbx, r10
    je .kernel
    mov cr3, r10
.kernel:
    call smp_app_gate               ; (keeps every register)
    sub rsp, 56                     ; 16-byte aligned for the call again
    mov r10, [rsp + 80]             ; above: r12, rbx, the return address, then the arguments
    mov [rsp], r10
    mov r10, [rsp + 88]
    mov [rsp + 8], r10
    mov r10, [rsp + 96]
    mov [rsp + 16], r10
    mov r10, [rsp + 104]
    mov [rsp + 24], r10
    mov r10, [rsp + 112]
    mov [rsp + 32], r10
    mov r10, [rsp + 120]
    mov [rsp + 40], r10
    call r12
    add rsp, 56
    mov r10, cr3
    cmp r10, rbx
    je .same
    mov cr3, rbx
.same:
    pop r12
    pop rbx
    ret
