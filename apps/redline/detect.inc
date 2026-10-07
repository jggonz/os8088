; REDLINE inventory. New opcodes live only behind the OS's boot CPU tier.
; Local probes never install exception handlers or change CPU configuration.
rl_detect:
    pushf
    call OSAPI_CPU_INFO
    mov [rl_tier], ax
    mov word [rl_cpuname], rl_cpu286
    cmp al, CPU_286
    je .done
    cmp al, CPU_386
    je .modern
    pushf
    push es
    cld
    mov dx, 4                       ; 80188/80186
    mov ax, 0xFF
    mov cl, 33
    shr ax, cl
    or ax, ax
    jnz .queue
    mov dx, 0                       ; 8088/8086 unless restart test proves NEC
    mov bx, 4                       ; bounded retries if no IRQ0 occurred
    mov ax, 0x40
    mov es, ax
.nec:
    mov di, [es:0x6C]
    xor si, si
    mov cx, 0xFFFF
    ; Entry is from a normal UI callback with IF=1. Preserve caller FLAGS;
    ; deliberately ordered prefixes expose the Intel restart defect.
    db 0xF3, 0x26, 0xAC             ; rep es lodsb, reads conventional RAM only
    or cx, cx
    jnz .queue
    cmp di, [es:0x6C]
    jne .isnec
    dec bx
    jnz .nec
    mov word [rl_cpuname], rl_cpuoldunknown
    jmp .earlydone
.isnec:
    mov dx, 2
.queue:
    push cs
    pop es
    ; Restore the instruction bytes BEFORE each run, including reruns.
    mov byte [.qinc], 0x42        ; inc dx
    mov word [.qinc+1], 0x9090
    mov di, .qend
    mov al, 0x90                    ; overwrite inc with NOP, same 1-byte shape
    mov cx, 3
    pushf
    cli
    std
    rep stosb
    cld
    nop
    nop
    nop
.qinc:
    inc dx
    nop
.qend:
    nop
    popf
    mov byte [.qinc], 0x42
    mov word [.qinc+1], 0x9090
    mov [rl_early], dl
    mov bx, dx
    shl bx, 1
    mov ax, [rl_oldnames+bx]
    mov [rl_cpuname], ax
.earlydone:
    pop es
    popf
.done:
    popf
    ret
.modern:
    call rl_detect386
    popf
    ret

; cpu 386 is restricted to this block, reached only after CPU_386.
cpu 386
rl_detect386:
    pushfd
    pushfd
    pop eax
    mov edx, eax
    xor eax, 0x40000                ; AC: 386 versus 486+
    push eax
    popfd
    pushfd
    pop eax
    xor eax, edx
    push edx
    popfd
    mov word [rl_cpuname], rl_cpu386
    test eax, 0x40000
    jz .fingerprint                 ; also catches 386-class Cyrix SLC/DLC
    mov word [rl_cpuname], rl_cpu486
    pushfd
    pop eax
    mov edx, eax
    xor eax, 0x200000               ; ID: CPUID available
    push eax
    popfd
    pushfd
    pop eax
    xor eax, edx
    push edx
    popfd
    test eax, 0x200000
    jz .fingerprint
cpu 586
    xor eax, eax
    cpuid
    mov [rl_cpumax], eax
    mov [rl_vendor], ebx
    mov [rl_vendor+4], edx
    mov [rl_vendor+8], ecx
    mov byte [rl_vendor+12], 0
    mov word [rl_cpuname], rl_vendor
    cmp dword [rl_vendor], 'Cyri'
    jne .tm
    mov word [rl_cpuname], rl_cpucyrixcpuid
.tm:
    cmp dword [rl_vendor], 'Tran'
    je .istransmeta
    cmp dword [rl_vendor+7], 'TMx8'
    jne .vendorok
.istransmeta:
    mov word [rl_cpuname], rl_cputransmeta
.vendorok:
    mov byte [rl_cpuid], 1
    cmp dword [rl_cpumax], 1
    jb .brand
    mov eax, 1
    cpuid
    mov byte [rl_leaf1], 1
    mov [rl_signature], eax
    mov [rl_features], edx
    mov ecx, eax
    shr eax, 8
    and eax, 15
    mov [rl_family], ax
    cmp ax, 15
    jne .model
    mov eax, ecx
    shr eax, 20
    and eax, 255
    add [rl_family], ax
.model:
    mov eax, ecx
    shr eax, 4
    and eax, 15
    mov [rl_model], ax
    cmp word [rl_family], 6
    je .extmodel
    cmp word [rl_family], 15
    jb .step
.extmodel:
    mov eax, ecx
    shr eax, 12
    and eax, 0xF0
    add [rl_model], ax
.step:
    and cx, 15
    mov [rl_stepping], cx
.brand:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000004
    jb .nominal
    mov di, rl_brand
    mov si, 0x8002
.brandloop:
    movzx eax, si
    add eax, 0x80000000 - 0x8000
    cpuid
    mov [di], eax
    mov [di+4], ebx
    mov [di+8], ecx
    mov [di+12], edx
    add di, 16
    inc si
    cmp si, 0x8005
    jb .brandloop
.nominal:
    cmp dword [rl_cpumax], 0x16
    jb .out
    mov eax, 0x16
    xor ecx, ecx
    cpuid
    and eax, 0xFFFF
    mov [rl_nominal], ax
.out:
    popfd
    ret
.fingerprint:
    ; Historic Cyrix/TI DIV fingerprint, including CPUID-disabled models.
    ; Undefined flags are a fingerprint, not proof of a specific Cyrix part.
    xor ax, ax
    sahf
    mov ax, 5
    mov bl, 2
    div bl
    lahf
    cmp ah, 2
    jne .out
    mov byte [rl_cyrix], 1
    mov word [rl_cpuname], rl_cpucyrix
    jmp .out

rl_tsc_mhz:
    ; CPUID feature bit 4 is the ONLY permission to execute RDTSC.
    test dword [rl_features], 0x10
    jz .out
    push es
    mov ax, 0x40
    mov es, ax
    mov bx, [es:0x6C]
    mov ecx, 0x10000000              ; bound missing firmware timer service
.sync:
    cmp bx, [es:0x6C]
    jne .start
    dec ecx
    jnz .sync
    jmp .unavailable
.start:
    mov bx, [es:0x6C]
    rdtsc
    mov [rl_tscstart], eax
    mov [rl_tscstart+4], edx
    mov ecx, 0x10000000
.wait:
    mov ax, [es:0x6C]
    sub ax, bx
    cmp ax, 4                      ; four whole 65536-count PIT periods
    jae .sample
    dec ecx
    jnz .wait
    jmp .unavailable
.sample:
    movzx eax, ax
    shl eax, 16
    mov [rl_clockspan], eax
    rdtsc
    pop es
    sub eax, [rl_tscstart]
    sbb edx, [rl_tscstart+4]
    ; cycles / PITcounts * 1.193182 = MHz. First divide by counts,
    ; carrying 0.01MHz precision by multiplying the quotient/remainder.
    mov ecx, [rl_clockspan]
    cmp edx, ecx                    ; reject a quotient overflow
    jae .out
    div ecx
    mov ebx, edx
    mov esi, eax
    mov eax, 11932                  ; MHz x10000 scale, rounded PIT crystal
    mul esi
    mov edi, eax
    mov eax, ebx
    mov esi, 11932
    mul esi
    div ecx
    add eax, edi
    xor edx, edx
    mov ecx, 100
    div ecx
    mov [rl_tscmhz], eax            ; MHz x100
.out:
    ret
.unavailable:
    pop es
    ret

rl_e820:
    push es
    push ds
    pop es
    xor ebx, ebx
    mov si, 128                     ; bound broken firmware continuation
.next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 20
    mov di, rl_mapentry
    push ds
    push es
    push si
    int 0x15
    pop si
    pop es
    pop ds
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .done
    cmp dword [rl_mapentry+16], 1
    jne .continue
    mov eax, [rl_mapentry+8]
    mov edx, [rl_mapentry+12]
    add [rl_ramtotal], eax
    adc [rl_ramtotal+4], edx
.continue:
    test ebx, ebx
    jz .complete
    dec si
    jnz .next
.done:
    mov dword [rl_ramtotal], 0
    mov dword [rl_ramtotal+4], 0
    jmp .out
.complete:
    mov eax, [rl_ramtotal]
    mov edx, [rl_ramtotal+4]
    cmp edx, 1024
    jae .done                      ; >4TiB outside this report's KB range
    shrd eax, edx, 10
    mov [rl_ramkb], eax
    mov byte [rl_mapok], 1
.out:
    pop es
    ret
cpu 8086

rl_inventory:
    call rl_detect
    int 0x12
    mov [rl_convkb], ax
    int 0x11
    mov [rl_equipment], ax
    cmp byte [rl_tier], CPU_286
    jb .noext
    mov ah, 0x88
    push ds
    push es
    int 0x15
    pop es
    pop ds
    jc .noext
    mov [rl_extkb], ax
    mov byte [rl_extok], 1
.noext:
    cmp byte [rl_tier], CPU_386
    jne .os
    call rl_e820
    call rl_tsc_mhz
.os:
    call OSAPI_MEM_AVAIL
    mov [rl_free], bx
    mov [rl_largest], ax
    call OSAPI_XMEM_CAPS
    mov [rl_xfree], ax
    mov bx, [rl_win]
    call OSAPI_WM_DISPLAY
    mov [rl_video], dx
    mov [rl_vw], ax
    mov [rl_vh], bx
    ret

rl_oldnames: dw rl_cpu88, rl_cpu86, rl_cpuv20, rl_cpuv30, rl_cpu188, rl_cpu186
rl_cpu88: db '8088-compatible', 0
rl_cpu86: db '8086-compatible', 0
rl_cpuv20: db 'NEC V20', 0
rl_cpuv30: db 'NEC V30', 0
rl_cpu188: db '80188-compatible', 0
rl_cpu186: db '80186-compatible', 0
rl_cpu286: db '80286 class', 0
rl_cpu386: db '80386 class', 0
rl_cpu486: db '80486+ (no CPUID)', 0
rl_cpucyrixcpuid: db 'Cyrix (CPUID)', 0
rl_cputransmeta: db 'Transmeta (CPUID)', 0
rl_cpucyrix: db 'Cyrix/TI DIV signature', 0
rl_cpuoldunknown: db '8086/NEC (IRQ unproven)', 0
