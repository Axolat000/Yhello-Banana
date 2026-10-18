; S3 firmware enters at the real-mode FACS waking vector, with no usable
; stack. Restore the saved execution environment before returning to C.
%define C_CR0 0
%define C_CR3 8
%define C_CR4 16
%define C_EFER 24
%define C_SP 32
%define C_IP 40
%define C_GDT 48
%define C_IDT 64
%define C_FS 80
%define C_GS 88
%define C_KGS 96
%define C_PAT 104
%define C_APIC 112
%define C_SEG 120
%define C_FPU 144
%ifidn __OUTPUT_FORMAT__,elf64
 %define X64 1
 %define CTRLREG rax
 %define CTXREG rbx
 bits 64
 default abs
%else
 %define X64 0
 %define CTRLREG eax
 %define CTXREG ebx
 bits 32
%endif
global s3_arch_suspend,s3_wake_start,s3_wake_end,s3_wake_segment,s3_wake_fixups
extern acpi_s3_enter_hw,s3_context
section .text
s3_arch_suspend:
%if X64
    pushfq
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov rbx,[rel s3_context]
    mov [rbx+C_SP],rsp
    mov rax,.resumed
    mov [CTXREG+C_IP],rax
%else
    pushfd
    push ebx
    push ebp
    push esi
    push edi
    mov ebx,[s3_context]
    mov [ebx+C_SP],esp
    mov dword [CTXREG+C_IP],.resumed
%endif
    mov CTRLREG,cr0
    mov [CTXREG+C_CR0],eax
    mov CTRLREG,cr3
    mov [CTXREG+C_CR3],eax
    mov CTRLREG,cr4
    mov [CTXREG+C_CR4],eax
    sgdt [CTXREG+C_GDT]
    sidt [CTXREG+C_IDT]
    mov ax,cs
    mov [CTXREG+C_SEG],ax
    mov ax,ss
    mov [CTXREG+C_SEG+2],ax
    mov ax,ds
    mov [CTXREG+C_SEG+4],ax
    mov ax,es
    mov [CTXREG+C_SEG+6],ax
    mov ax,fs
    mov [CTXREG+C_SEG+8],ax
    mov ax,gs
    mov [CTXREG+C_SEG+10],ax
%if X64
    mov ecx,0xc0000080
    rdmsr
    and eax,~(1<<10)             ; LMA is read-only
    mov [CTXREG+C_EFER],eax
    mov ecx,0xc0000100
    rdmsr
    mov [CTXREG+C_FS],eax
    mov [CTXREG+C_FS+4],edx
    inc ecx
    rdmsr
    mov [CTXREG+C_GS],eax
    mov [CTXREG+C_GS+4],edx
    inc ecx
    rdmsr
    mov [CTXREG+C_KGS],eax
    mov [CTXREG+C_KGS+4],edx
%endif
    mov ecx,0x277
    rdmsr
    mov [CTXREG+C_PAT],eax
    mov [CTXREG+C_PAT+4],edx
    mov ecx,0x1b
    rdmsr
    mov [CTXREG+C_APIC],eax
    mov [CTXREG+C_APIC+4],edx
    fxsave [CTXREG+C_FPU]
    wbinvd
%if X64
    call acpi_s3_enter_hw
%else
    sub esp,8
    call acpi_s3_enter_hw
    add esp,8
%endif
    mov eax,-1                  ; hardware did not enter S3
    jmp .restore
.resumed:
    fxrstor [CTXREG+C_FPU]
    xor eax,eax
.restore:
%if X64
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    popfq
%else
    pop edi
    pop esi
    pop ebp
    pop ebx
    popfd
%endif
    ret

section .rodata
align 16
bits 16
s3_wake_start:
    db 0xea
    dw s3_real-s3_wake_start
s3_wake_segment:
    dw 0                         ; patched with the page's real-mode segment
s3_real:
    cli
    cld
    in al,0x92
    and al,0xfe
    or al,2
    out 0x92,al                  ; firmware may leave A20 disabled
    mov ax,cs
    mov ds,ax
    mov es,ax
    movzx ebx,ax
    shl ebx,4
    add ebx,0x800
    lgdt [s3_gdt32_ptr-s3_wake_start]
    mov eax,cr0
    or eax,1
    mov cr0,eax
    jmp dword far [s3_pm_ptr-s3_wake_start]
bits 32
%define CTXREG ebx
s3_pm:
    mov ax,16
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov eax,[CTXREG+C_CR4]
    mov cr4,eax
    mov eax,[CTXREG+C_CR3]
    mov cr3,eax
%if X64
    mov ecx,0xc0000080
    mov eax,[CTXREG+C_EFER]
    xor edx,edx
    wrmsr
%endif
    mov eax,[CTXREG+C_CR0]
    mov cr0,eax
%if X64
    lgdt [ebx+s3_gdt64_ptr-s3_wake_start-0x800]
    jmp far [ebx+s3_lm_ptr-s3_wake_start-0x800]
bits 64
%define CTXREG rbx
s3_lm:
%endif
    mov ecx,0x277
    mov eax,[CTXREG+C_PAT]
    mov edx,[CTXREG+C_PAT+4]
    wrmsr
    mov ecx,0x1b
    mov eax,[CTXREG+C_APIC]
    mov edx,[CTXREG+C_APIC+4]
    wrmsr
    lgdt [CTXREG+C_GDT]
    lidt [CTXREG+C_IDT]
    mov ax,[CTXREG+C_SEG+2]
    mov ss,ax
    mov ax,[CTXREG+C_SEG+4]
    mov ds,ax
    mov ax,[CTXREG+C_SEG+6]
    mov es,ax
%if X64
    mov ax,[CTXREG+C_SEG+8]
    mov fs,ax
    mov ax,[CTXREG+C_SEG+10]
    mov gs,ax
    mov ecx,0xc0000100
    mov eax,[CTXREG+C_FS]
    mov edx,[CTXREG+C_FS+4]
    wrmsr
    inc ecx
    mov eax,[CTXREG+C_GS]
    mov edx,[CTXREG+C_GS+4]
    wrmsr
    inc ecx
    mov eax,[CTXREG+C_KGS]
    mov edx,[CTXREG+C_KGS+4]
    wrmsr
    mov rsp,[CTXREG+C_SP]
    movzx eax,word [CTXREG+C_SEG]
    push rax
    push qword [CTXREG+C_IP]
    retfq
%else
    mov ax,[CTXREG+C_SEG+8]
    mov fs,ax
    mov ax,[CTXREG+C_SEG+10]
    mov gs,ax
    mov esp,[CTXREG+C_SP]
    movzx eax,word [CTXREG+C_SEG]
    push eax
    push dword [CTXREG+C_IP]
    retf
%endif
align 8
s3_gdt32:
    dq 0,0x00cf9a000000ffff,0x00cf92000000ffff
s3_gdt32_ptr:
    dw 23
    dd s3_gdt32-s3_wake_start
s3_pm_ptr:
    dd s3_pm-s3_wake_start
    dw 8
%if X64
align 8
s3_gdt64:
    dq 0,0x00af9a000000ffff,0x00cf92000000ffff
s3_gdt64_ptr:
    dw 23
    dq s3_gdt64-s3_wake_start
s3_lm_ptr:
    dd s3_lm-s3_wake_start
    dw 8
%endif
times 0x800-($-s3_wake_start) db 0
times C_FPU+512 db 0
s3_wake_end:
align 4
s3_wake_fixups:
    dd s3_gdt32_ptr+2-s3_wake_start,s3_pm_ptr-s3_wake_start
%if X64
    dd s3_gdt64_ptr+2-s3_wake_start,s3_lm_ptr-s3_wake_start
%endif
    dd 0xffffffff
