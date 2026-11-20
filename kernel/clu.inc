; =============================================================================
; clu.inc - a CLUSTER, and what holds one (SPEC.md 19.12.3)
;
; Included early by kernel.asm, because instance.inc and others name these
; before disk.inc is reached, and by disk.inc itself for the builds that
; start there (kern_dos). Once only.
; =============================================================================
%ifndef CLU_INC
%define CLU_INC
; --- FAT32 (SPEC.md 19.12) --------------------------------------------------
; kern_mega alone reads and writes it. A cluster is AX plus [dsk_ch], its HIGH
; word (19.12.3); what holds one for longer is CLURES, a dword here and a word
; on every other build, and the CLU_* macros move the pair without spending a
; register - on the other builds they are the word instruction they replace,
; so kern_big, kern_small and kern_dos assemble to the bytes they were.
%ifdef KERN_MEGA
  %define OS88_FAT32
  %define CLURES resd
%else
  %define CLURES resw
%endif
%macro CLU_LD 1                 ; AX:[dsk_ch] = the cluster held at %1
    mov ax, [%1]
  %ifdef OS88_FAT32
    push word [%1+2]
    pop word [dsk_ch]
  %endif
%endmacro
%macro CLU_ST 1                 ; ...and stored there
    mov [%1], ax
  %ifdef OS88_FAT32
    push word [dsk_ch]
    pop word [%1+2]
  %endif
%endmacro
%macro CLU_HI0 0                ; AX holds a cluster that is a WORD by
  %ifdef OS88_FAT32             ; construction (0, the root, a FAT12/16
    mov word [dsk_ch], 0        ; volume's): its high word is 0
  %endif
%endmacro
%macro CLU_ZST 1                ; a zero cluster at %1 (AX untouched)
    mov word [%1], 0
  %ifdef OS88_FAT32
    mov word [%1+2], 0
  %endif
%endmacro
%macro CLU_TST 1                ; ZF = 1 when the cluster at %1 is 0 (the
    cmp word [%1], 0            ; root): flags only
  %ifdef OS88_FAT32
    jne %%x
    cmp word [%1+2], 0
%%x:
  %endif
%endmacro
%macro CLU_TAX 0-1              ; ZF = 1 when AX:[dsk_ch] is 0: flags only
  %if %0                        ; (CLU_TAX or: the `or` spelling a site had)
    or ax, ax
  %else
    test ax, ax
  %endif
  %ifdef OS88_FAT32
    jnz %%x
    cmp word [dsk_ch], 0
%%x:
  %endif
%endmacro
; ...and the Disk window's folder (files.inc's FS_CWD, FS_CWDH its high word
; on kern_mega) moved the same way: %1 the register, %2 the block's base
%macro FS_CWD_LD 2
    mov %1, [%2+FS_CWD]
  %ifdef OS88_FAT32
    push word [%2+FS_CWDH]
    pop word [dsk_ch]
  %endif
%endmacro
%macro FS_CWD_ST 2
    mov [%2+FS_CWD], %1
  %ifdef OS88_FAT32
    push word [dsk_ch]
    pop word [%2+FS_CWDH]
  %endif
%endmacro
%macro FS_CWD_TST 1             ; ZF = 1: the window at %1 shows a root
    cmp word [%1+FS_CWD], 0
  %ifdef OS88_FAT32
    jne %%x
    cmp word [%1+FS_CWDH], 0
%%x:
  %endif
%endmacro
%macro FS_CWD_CMPH 1            ; after an equal `cmp r, [%1+FS_CWD]`: ZF = 1
  %ifdef OS88_FAT32             ; only when [dsk_ch] matches the high word
    jne %%x                     ; too (AX is banked; flags only)
    push ax
    mov ax, [dsk_ch]
    cmp ax, [%1+FS_CWDH]
    pop ax
%%x:
  %endif
%endmacro
; LST_HI reg - [dsk_ch] = the high word a LISTING entry at reg carries at its
; name's +14 (SPEC.md 19.12.6), beside the low word at +18. Flags kept
%macro LST_HI 1
  %ifdef OS88_FAT32
    push word [%1+14]
    pop word [dsk_ch]
  %endif
%endmacro
%ifdef OS88_FAT32
; OSAPI_FILE_PLACE's verbs, in AL (SPEC.md 19.12.8): the SDK's FPL_* twins
DSK_FPL_HERE    equ 0
DSK_FPL_GOTO    equ 1
DSK_FPL_GOTO_Q  equ 2
DSK_FPL_GOTO_QM equ 3
DSK_FPL_ARG     equ 4
DSK_FPL_STAT    equ 5
%endif
%endif ; CLU_INC
