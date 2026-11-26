; SPEC.md 31.10.5: shared, non-resident PC1 refresh-rate implementation.
; Expand in CTRL.DRV and the temporary boot overlay; DS = KERNEL_SEG.
%macro PC1_VIDEO 1
%1 %+ _probe:
    cmp byte [cpu_tier], CPU_8086 ; never load a real ROM segment on mega
    jne %%no
    push ax
    push dx
    push es
    mov ax, 0xF000
    mov es, ax
    mov ax, [es:0xFFFD]
    cmp ax, 0xFE44
    je %%ports
    cmp ax, 0xFE49
    je %%ports
    cmp ax, 0xFE4A
    jne %%bad
%%ports:
    mov dx, 0xFFFB
    in al, dx
    cmp al, 0xC0
    jne %%bad
    mov dx, 0xFFF8
    in al, dx
    cmp al, 0xD0
    jne %%bad
    clc
    jmp short %%out
%%bad:
    stc
%%out:
    pop es
    pop dx
    pop ax
    ret
%%no:
    stc
    ret

; AL = 50/60. CF clear applied, set refused; registers preserved.
%1 %+ _apply:
    cmp al, 50
    je %%valid
    cmp al, 60
    jne %%noapply
%%valid:
    call %1 %+ _probe
    jc %%noapply
    push ax
    pushf
    cli                         ; index/data must remain one transaction
    cmp al, 60
    mov ah, 0x89
    jne %%write
    mov ah, 0x81
%%write:
    mov al, 0x65
    out 0xDD, al
    mov al, ah
    out 0xDE, al
    popf
    pop ax
    clc
    ret
%%noapply:
    stc
    ret
%endmacro
