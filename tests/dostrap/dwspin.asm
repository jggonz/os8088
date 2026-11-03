; =============================================================================
; os8088 - tests/dostrap/dwspin.asm - DWSPIN.COM, how fast a windowed program
; RUNS (SPEC.md 96.54.3, docs/plans/DOS-WINDOWED-PLAN.md wave 2)
;
; Counts as fast as it can into the dword at PSP:0103h and never yields, so
; the count per guest second IS the share of the CPU it was given - in the
; bracket (the whole machine) and in a window (pre-empted, its world exchanged
; on every tick) alike. A key ends it: every 65,536 counts it asks INT 16h
; AH=01h, which is the only call it makes, and `q` exits.
; It runs under a real IBM DOS 3.30 unchanged.
; =============================================================================
cpu 8086
org 0x100

    jmp short start
    nop
count: dd 0                         ; PSP:0103h
start:
.spin:
    add word [count], 1
    jnc .spin
    inc word [count + 2]
    mov ah, 0x01
    int 0x16
    jz .spin
    xor ah, ah
    int 0x16
    cmp al, 'q'
    jne .spin
    mov ah, 0x4C
    int 0x21
