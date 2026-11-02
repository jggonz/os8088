; =============================================================================
; os8088 - fsck.inc
;
; CHECK THE DISK, REPAIR IT, DEFRAGMENT IT (SPEC.md 118.14), prefix `fsk_`.
; A CHKDSK and a defragmenter for the FAT12/FAT16 volume the caller stands on,
; driven by System Check one bounded step at a time from its OSAPI_WM_ONWAKE
; slices, so the desktop stays live through a run.
;
; **IT IS KERNEL CODE AND THE PACKAGE LOADS IT.** A package has no sector read
; or write and must not be given one (SPEC.md 118.9, 96.5.2.2), so the engine
; is assembled here, with the kernel, and reaches the FAT through the kernel's
; own dsk_next_clus_x / dskw_setfat / dskw_flush_x and every sector through
; disk_read_x / disk_write_x - so the FAT window, both FAT copies and the
; read-ahead cache stay the kernel's to keep coherent. tools/os88mod.py cuts
; the image out as FSCHECK.DRV exactly as it cuts every module, but it is NO
; mod_tab row: mod_need reads a module from the boot volume only, and the
; 360KB system disk has no room for this one. It ships beside SYSCHECK.O88,
; which reads it into a claim of its own and hands the segment over on every
; call (SPEC.md 118.14.1). osapi_vol_check below checks the image's header
; against this kernel's on every call, so a copy from another build is
; refused rather than executed, and the kernel keeps NOTHING between calls:
; the image and the workspace are the package's, and go when it goes.
;
; RESIDENT: the stub (.text), fsk_cnear (.cold), [dsk_wgen] and its bump in
; disk_write_x (disk.inc). Everything else is the image's.
; =============================================================================

%ifdef OS88_FSCKIMG

; --- the verbs, answers and refusals (SPEC.md 118.14.2) -----------------------
FSK_SIZE    equ 0
FSK_BEGIN   equ 1
FSK_STEP    equ 2
FSK_END     equ 3
FSK_DELSYS  equ 4
FSKD_RETIRED equ 0              ; FSK_DELSYS's two forms (118.8.1, 118.8.2)
FSKD_REFUSED equ 1
FSKM_FIX    equ 1
FSKM_DEFRAG equ 2
FSKS_MORE   equ 0
FSKS_FIND   equ 1
FSKS_DONE   equ 2
FSKE_IMAGE  equ 2
FSKE_NOFAT  equ 3
FSKE_IO     equ 4
FSKE_CHANGED equ 5
FSKE_SMALL  equ 6
FSKE_WPROT  equ 7
FSKE_ARG    equ 8
FSKE_NOENT  equ 9

FSKP_FATCMP equ 1
FSKP_TREE   equ 2
FSKP_SCAN   equ 3
FSKP_LOST   equ 4
FSKP_SYNC   equ 5
FSKP_DEFRAG equ 6
FSKP_DONE   equ 7

; findings (118.14.3)
FSKF_FATDIFF equ 1
FSKF_DOT    equ 2
FSKF_BADENT equ 3
FSKF_DIRBAD equ 4
FSKF_DEEP   equ 5
FSKF_OFFDISK equ 6
FSKF_INFREE equ 7
FSKF_INBAD  equ 8
FSKF_LONG   equ 9
FSKF_SHORT  equ 10
FSKF_XLINK  equ 11
FSKF_LOST   equ 12
FSKF_SAVED  equ 13
FSKF_FREED  equ 14
FSKF_SPACE  equ 15
FSKF_BADCL  equ 16
FSKF_FRAGS  equ 17
FSKF_MOVED  equ 18
FSKF_SKIPDEF equ 19
FSKF_NOTDEF equ 20
FSKF_NOFOLD equ 21
FSKF_FOLDERS equ 22

FSV_NOTE    equ 1               ; System Check's SV_* (SPEC.md 118.1)
FSV_WARN    equ 2
FSV_BROKEN  equ 3

; --- the workspace (SPEC.md 118.14.2): the record, then the engine's -----------
FSW_PHASE   equ 0
FSW_MODE    equ 1
FSW_FAT16   equ 2
FSW_SPC     equ 3
FSW_DONE    equ 4
FSW_TOTAL   equ 6
FSW_NCLUS   equ 8
FSW_FREE    equ 10
FSW_BAD     equ 12
FSW_USED    equ 14
FSW_FILES   equ 16
FSW_DIRS    equ 18
FSW_LOST    equ 20
FSW_LOSTC   equ 22
FSW_XLINK   equ 24
FSW_FRAG    equ 26
FSW_ERRS    equ 28
FSW_FIXED   equ 30
FSW_MOVED   equ 32
FSW_STUCK   equ 34
FSW_FATDF   equ 36
FSW_CBYTES  equ 38
FSW_MOVEDF  equ 40              ; folders the defragmenter moved
FSW_OTHERS  equ 42              ; other programs open (folders stay, 118.14.5)
FSW_FROW    equ 48
FSW_FSEV    equ 49
FSW_FCODE   equ 50
FSW_FFIX    equ 51
FSW_FA      equ 52
FSW_FB      equ 56
FSW_FPATH   equ 64
FSK_PATHN   equ 80

P_VOL       equ 0x100           ; db the volume
P_GEN       equ 0x101           ; db [dsk_wgen] at the end of the last step
P_SWEEP     equ 0x102           ; db the phase's sweep
P_DEPTH     equ 0x103           ; db the walk's level
P_FLAGS     equ 0x104           ; db PF_*
P_DPASS     equ 0x105           ; db defragment pass: 0 = pieces, 1-3 = pack
P_DMOVED    equ 0x106           ; db moved in this pass
P_QN        equ 0x107           ; db findings queued
P_MAXC      equ 0x108           ; dw [dsk_maxclus]
P_NB        equ 0x10A           ; dw one bitmap's bytes
P_SIG       equ 0x10C           ; dw [dsk_sigcur]
P_IDX       equ 0x10E           ; dw a sweep's cursor
P_CNT       equ 0x110           ; dw a sweep's count
P_CHKN      equ 0x112           ; dw the next FILEnnnn.CHK
P_SPC       equ 0x114           ; dw sectors per cluster
P_FATSZ     equ 0x116           ; dw
P_FATLBA    equ 0x118           ; dw
P_NFATS     equ 0x11A           ; dw
P_ROOTLBA   equ 0x11C           ; dw
P_ROOTSECS  equ 0x11E           ; dw
P_PLEN      equ 0x120           ; dw the walk path's length
P_LBA       equ 0x122           ; dd the folder sector in DBUF
M_DLBA      equ 0x126           ; dd the moving file's entry sector
M_DOFF      equ 0x12A           ; dw ...and the entry's offset in it
M_OLD       equ 0x12C           ; dw its first cluster
M_N         equ 0x12E           ; dw its clusters
M_DST       equ 0x130           ; dw where it goes
M_K         equ 0x132           ; dw clusters copied
M_SRC       equ 0x134           ; dw the next source cluster
M_KS        equ 0x136           ; dw sectors copied into it (big clusters)
P_CB        equ 0x138           ; dw bytes per cluster
P_FREED     equ 0x13A           ; dw lost clusters freed
M_KERN      equ 0x13C           ; db 1 = the file moving is KERNEL.SYS
P_FOLDOK    equ 0x13D           ; db 1 = folders may move: nothing else open
M_DIR       equ 0x13E           ; db 1 = what is moving is a FOLDER
P_Q         equ 0x140           ; FSK_QMAX x 12: row sev code fix A(4) B(4)
FSK_QMAX    equ 8
P_QPATH     equ 0x1A0           ; 80: the queued findings' path
P_PATH      equ 0x200           ; 80: the walk's folder, "\DIR\"
P_STK       equ 0x260           ; FSK_MAXDEP levels of LV_SIZE
LV_DIR      equ 0               ; the folder's first cluster, 0 = the root
LV_CLUS     equ 2               ; the cluster being read
LV_SEC      equ 4               ; its sector (the root's index)
LV_ENT      equ 6               ; the next entry in it
LV_PLEN     equ 8               ; the path's length above this folder
LV_LEFT     equ 10              ; clusters the chain walk counted
LV_SIZE     equ 12
FSK_MAXDEP  equ 16              ; 0x260 + 192 = 0x320
P_E         equ 0x320           ; dw the entry being handled, and its level
P_L         equ 0x322           ; dw
P_N         equ 0x324           ; dw its chain: clusters reached,
P_LAST      equ 0x326           ; dw ...the last good one,
P_A         equ 0x328           ; dw ...where it stopped and its place there,
P_B         equ 0x32A           ; dw
P_NEED      equ 0x32C           ; dw ...and what its size needs
P_KC        equ 0x32E           ; dw fsk_kids: the moved folder's cluster,
P_KS        equ 0x330           ; dw ...its sector, its entry
P_KE        equ 0x332           ; dw
FSK_DBUF    equ 0x400           ; one folder sector
FSK_CBUF    equ 0x600           ; the copy buffer
FSK_CBSECS  equ 16              ; ...in sectors
FSK_BM1     equ FSK_CBUF + FSK_CBSECS * 512
                                ; BM1: reached (the check) / in use (defrag)
                                ; BM2 = BM1 + [P_NB]: pointed at by a lost one
PF_SYNC     equ 1               ; the FAT copies differ
PF_TREEDEF  equ 2               ; the walk is the defragmenter's
PF_MOVING   equ 4               ; a move is under way (M_*)
PF_WROTE    equ 8               ; something was written

; =============================================================================
; RESIDENT: the cell's body and the trampoline
; =============================================================================
%ifdef OS88_FSCK
; -----------------------------------------------------------------------------
; osapi_vol_check - OSAPI_VOL_CHECK (0x04F5, SPEC.md 118.14.2). The cell is
; OSAPI_RCSLOT's: DS = KERNEL_SEG here, every other register the caller's.
; in:  AL = verb (AH, CX: its arguments), ES = the image, DX = the workspace
; out: the image's answer (CF, AX); CF=1 AX=FSKE_IMAGE for an image that is
;      not this kernel's. Every register but AX preserved
;
; THE HEADER IS CHECKED ON EVERY CALL, because the kernel holds nothing
; between them: the magic, MOD_VER with MOD_FSCK, this build and this
; layout - mod_hdrt's own words, as mod_check asks them of a module mod_need
; loaded. Then a far call to its first entry.
; -----------------------------------------------------------------------------
section .cold                   ; ALL OF IT `.cold`, so kern_mega's kernel
                                ; segment carries none of it (megaseg.txt)
osapi_vol_check:                ; OSAPI_RCSLOT's body: far-called, DS = KERNEL
    mov bp, inst_vol_enter      ; V: the caller's folder (SPEC.md 19.2.1),
    call KERNEL_SEG:spw_near    ; a `.text` routine, through its one door.
                                ; BP is api_rsc's to restore and an input to
                                ; no rare cell (kernel size pass 17)
    push si
    push di
    push cx
    mov byte [mod_hdrt + MOD_H_ID], MOD_FSCK ; mod_check's own compare: the
    mov si, mod_hdrt            ; magic, MOD_VER with MOD_FSCK, the BUILD and
    xor di, di                  ; the layout STAMP - mod_hdrt's four words,
    mov cx, 4                   ; its id byte stamped per call as mod_check
    cld                         ; stamps it (kernel size pass 17: two
    repe cmpsw                  ; compares and a cmpsw were the same words)
    pop cx
    pop di
    pop si                      ; (pop writes no flag)
    jne .bad
%ifdef KERN_MEGA
    push ax                     ; THE CODE SELECTOR: a claim is a data/code
    push dx                     ; pair in protected mode, and its data half
    mov dx, es                  ; cannot be far-called. Real mode answers ES.
    mov bp, osapi_mem_codesel   ; (OSAPI_MEM_CODESEL's own body, through
    call KERNEL_SEG:spw_near    ; spw_near: `.cold` names no segment of its
    pop dx                      ; own - SPEC.md 2.10.2)
    xchg ax, bp                 ; BP = the selector
    pop ax                      ; (neither writes a flag)
    jc .bad                     ; NO CODE HALF: a claim above 1MB is a data
                                ; selector alone - the package asks OSAPI_SEG
                                ; SEG_SUBC for a pair over it (118.14.8), and
                                ; a far call through data would be a #GP
 %define FSK_CSEG bp
%else
 %define FSK_CSEG es            ; ...where real mode's is ES itself
%endif
    push FSK_CSEG               ; A FAR JUMP into the image, not a call: its
    push word [es:MOD_H_ENT]    ; own retf returns to api_rsc, which this
    retf                        ; body's would have reached next (kernel size
                                ; pass 17: no [fsk_fp]). ONE push line for
                                ; both arms, api_far's way: stkbalance reads
                                ; the source and not the %ifdef
.bad:
    mov ax, FSKE_IMAGE
    stc
    retf

section .cold
; fsk_cnear - the module's way into a NEAR .cold routine: BP = it. It is a
; module's `call COLD_SEG:dskf_*` shim with the target in a register, so the
; engine reaches every disk routine it needs through three bytes instead of a
; far shim apiece. `call`/`retf` touch no flag.
fsk_cnear:
    call bp
    retf

section .bss
fsk_cold:   resd 1              ; fsk_cnear where `.cold` IS, for this call
fsk_sp:     resw 1              ; ...and the stack fsk_die unwinds to
dsk_wgen:   resb DVOL_MAX       ; THE WRITE GENERATION per volume (118.14.6):
                                ; disk_write_x bumps [disk_drive]'s

; KC - a near .cold routine, from the image; ES comes back as it was. THROUGH
; [fsk_cold], never `call COLD_SEG:`: `.cold` can move (the slide over a
; dropped VGA rung, a ROM, the HMA) and mod_cfix re-points a module's far
; calls only when mod_need loads it - which never loads this one. So the
; entry builds the pointer out of [api_coldseg] on every call
%macro KC 1
    push es
    mov bp, %1
    call far [fsk_cold]
    pop es
%endmacro
%endif                          ; OS88_FSCK

; =============================================================================
; FSCHECK.DRV - the image
; =============================================================================
section .modfs
modfs_header:
    dw 0x384f
    db MOD_VER, MOD_FSCK
    dw BUILD_NUM, MOD_STAMP
    dw modfs_end, FSK_NENT
    dw fsk_entry
FSK_NENT    equ 1


; NOTHING HERE IS WRITTEN THROUGH CS: kern_mega runs this image through a
; claim's CODE selector, which no store may use (KERN-MEGA-PLAN 2.4). Its
; per-call words are the kernel's ([fsk_sp], [fsk_cold], reached through DS =
; KERNEL_SEG) and its scratch is the workspace's (P_E onward).
;
; **ES IS THE WORKSPACE, FROM fsk_entry's `mov es, dx` TO ITS RETURN**, and
; nothing loads it again: KC banks it round every kernel routine, and the two
; DS swaps (fsk_qpop, fsk_ph_fatcmp) copy it and leave it alone. So no
; routine here reloads it, and the kernel holds no [fsk_ws] (kernel size pass
; 17: fourteen reloads of a value ES already had). FSK_SIZE is the one verb
; that has no workspace, and it touches none.

; -----------------------------------------------------------------------------
; fsk_entry - the one entry (SPEC.md 118.14.2), far-called by the stub
; in:  AL = verb, AH = FSKM_* (BEGIN), CX = the workspace's KB (BEGIN), DX =
;      the workspace; DS = KERNEL_SEG
; out: CF=0 AX = the answer, CF=1 AX = FSKE_*; every other register kept
; -----------------------------------------------------------------------------
fsk_entry:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [fsk_sp], sp
    mov word [fsk_cold], fsk_cnear
%ifdef KERN_MEGA
    mov word [fsk_cold+2], COLD_SEG ; `.cold`'s identity selector (mega.inc)
%else
    mov bx, [api_coldseg]
    mov [fsk_cold+2], bx
%endif
    cld
    cmp al, FSK_SIZE
    je .size
    mov es, dx
    cmp al, FSK_BEGIN
    je .begin
    cmp al, FSK_STEP
    je .step
    cmp al, FSK_END
    je .end
    cmp al, FSK_DELSYS
    je .delsys
    mov ax, FSKE_ARG
    jmp fsk_die
.delsys:
    call fsk_delsys
    jmp short fsk_ok0
.size:
    call fsk_mounted
    call fsk_need               ; AX = KB
    jmp short fsk_ok
.begin:
    call fsk_begin
    jmp short fsk_ok0
.step:
    call fsk_step               ; AX = FSKS_*
    jmp short fsk_ok
.end:
    call fsk_end
fsk_ok0:
    xor ax, ax
fsk_ok:
    clc
fsk_ret:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; fsk_die - end the call with CF=1 and AX = FSKE_*, from any depth. Whatever
; FAT entries are still unflushed are dropped (dskw_refat): every repair
; flushes as it goes, so what is dropped was never meant to stand alone
fsk_die:  ; STKBALANCE-OK: unwinds every frame at once to fsk_entry's, through [fk_sp]
    mov sp, [fsk_sp]
    push ax
    KC dskw_refat
    pop ax
    stc
    jmp short fsk_ret

; fsk_ioerr - die with the last transfer's verdict
fsk_ioerr:
    mov ax, FSKE_IO
    cmp byte [dsk_ioerr], 0x03
    jne fsk_die
    mov al, FSKE_WPROT
    jmp short fsk_die

; fsk_mounted - the volume is a FAT one, or die FSKE_NOFAT
fsk_mounted:
    cmp byte [dsk_mntok], 1
    jne .no
    cmp byte [dsk_vkind], DVK_FILE
    je .no
    ret
.no:
    mov ax, FSKE_NOFAT
    jmp short fsk_die

; fsk_need - AX = KB the workspace needs, BX = one bitmap's bytes
fsk_need:
    mov bx, [dsk_maxclus]
    mov cl, 3
    shr bx, cl
    add bx, 2
    and bl, 0FEh                ; bits 0..maxclus, rounded to a word
    mov ax, bx
    shl ax, 1
    add ax, FSK_BM1 + 1023
    mov cl, 10
    shr ax, cl
    ret

; fsk_gen - AL = the volume's write generation now
fsk_gen:
    push bx
    mov bl, [disk_drive]
    mov bh, 0
    mov al, [bx+dsk_wgen]
    pop bx
    ret

; =============================================================================
; FSK_DELSYS - remove a driver this kernel has no use for (SPEC.md 118.8.1,
;              118.8.2)
; in:  ES:SI -> its name, ASCIIZ, in the caller's folder (the stub stood us
;      there); AH = FSKD_RETIRED or FSKD_REFUSED; for FSKD_REFUSED DI = a
;      buffer segment of the caller's and CX = its KB, room for the file
;      EXPANDED; DS = KERNEL_SEG
; out: returns, deleted; dies FSKE_ARG for a name it may not delete,
;      FSKE_NOENT for one that is not there, FSKE_SMALL for a buffer the
;      file does not fit, FSKE_IO/WPROT
;
; A package can neither list nor delete a hidden + system file (SPEC.md 19.6),
; and a leftover driver is one. So the kernel deletes it - and THE KERNEL
; decides which, never the caller. Only a *.DRV of one of two kinds, or an old
; COLDHMA.SYS:
;
;   FSKD_RETIRED  a name on fsk_retired, this release's own list of the
;                 drivers and modules earlier ones shipped and this one does
;                 not (apps/syscheck/retired.txt's .DRV lines, held to it by
;                 t_sysman);
;   FSKD_REFUSED  a file THIS KERNEL WOULD REFUSE as both a driver and a
;                 module, read whole through the kernel's own read - which
;                 expands a packed file - and judged here: an 'O8' header
;                 whose version byte is not DRV_VER (drv_check's test) and
;                 whose words are not mod_hdrt's but for the id (mod_check's).
;                 Every image anything loads today carries one of the two,
;                 sub-images such as HTTPPAGE.DRV included; a header that is
;                 not 'O8' at all is left, as nothing this kernel can name.
;                 And COLDHMA.SYS (118.8.3), the one other name it takes: an
;                 HMA image carries no build key, so it goes when it IS one
;                 ('OS88ROM', kind 3) and `.cold` is not running from it.
;
; So no caller can name KERNEL.SYS, SYSTEM.CFG, a driver this kernel loads or
; a module it reads. Read-only, hidden and system are forgiven, a folder and
; a label are not; the rest is dskw_delete's own commit, so the caches and
; the Disk windows follow
; =============================================================================
fsk_delsys:
    call fsk_mounted
    push ax                     ; AH = the form
    push si                     ; the caller's name, for FSKD_REFUSED's read,
    push cx                     ; ...its buffer's KB
    push di                     ; ...and its buffer
    mov di, dskw_n83            ; the name, raw: eight and three, space-padded
    push es                     ; the workspace, which ES is for the whole
    push di                     ; call (no [fsk_ws]: kernel size pass 17)
    push ds
    pop es
    mov ax, 0x2020
    mov cx, 5
    rep stosw
    stosb
    pop di
    pop es
    mov cx, 8
.base:
    mov al, [es:si]
    inc si
    or al, al
    jz .named
    cmp al, '.'
    je .ext
    jcxz .badp                  ; nine before the dot
    mov [di], al
    inc di
    dec cx
    jmp short .base
.ext:
    mov di, dskw_n83 + 8
    mov cx, 3
.e:
    mov al, [es:si]
    inc si
    or al, al
    jz .named
    jcxz .badp
    mov [di], al
    inc di
    dec cx
    jmp short .e
.badp:                          ; four words deep: back to the entry's
    pop di                      ; depth first, so fsk_die is reached at one
    pop cx
    pop si
    pop ax
    jmp short .bad
.named:
    pop di
    pop cx
    pop si
    pop ax
    cmp word [dskw_n83 + 8], 'DR'   ; a *.DRV, whichever the form...
    jne .hma
    cmp byte [dskw_n83 + 10], 'V'
    jne .hma
    cmp ah, FSKD_REFUSED
    je .refused
    or ah, ah
    jnz .bad
    mov bx, fsk_retired         ; ...and it is one this release retired
.list:
    cmp byte [cs:bx], 0
    je .bad
    push ds
    pop es
    mov si, bx
    mov di, dskw_n83
    mov cx, 11
.cmp:
    mov al, [cs:si]
    cmp al, [es:di]
    jne .nx
    inc si
    inc di
    loop .cmp
    jmp .ours
.nx:
    add bx, 11
    jmp short .list
.bad:
.bad2:
    mov ax, FSKE_ARG
    jmp fsk_die
.hma:                           ; ...or the one other name the refused form
    cmp ah, FSKD_REFUSED        ; takes: COLDHMA.SYS (118.8.3)
    jne .bad
    push es                     ; the workspace, back for .refused's name
    push cx
    push si
    push di
    push ds
    pop es
    mov si, fsk_n_hma
    mov di, dskw_n83
    mov cx, 11
.hn:
    mov al, [cs:si]
    cmp al, [es:di]
    jne .hn2
    inc si
    inc di
    loop .hn
    cmp al, al                  ; all eleven: ZF = 1 (the incs left it 0)
.hn2:
    pop di
    pop si
    pop cx
    pop es
    jne .bad                    ; ZF = 0: a mismatch jumped here
.refused:
    ; --- read it whole, as the kernel itself would, and judge the header -
    push di                     ; the caller's buffer
    mov di, cp_sbuf             ; the name, staged where the file API reads
    push es                     ; it (DS:SI): cp_stage's landing ground, one
    push ds                     ; string at a time on the UI task - and the
    pop es                      ; name is the workspace's, which ES held
    pop ds
    mov bx, 13                  ; 8 + '.' + 3 + NUL, already proved
.st:
    lodsb
    stosb
    or al, al
    jz .staged
    dec bx
    jnz .st
.staged:
    push es
    pop ds
    pop es                      ; ES:BX = the buffer, DX:CX = its bytes
    mov bx, cx
    mov dx, cx
    mov cl, 6
    shr dx, cl
    mov cl, 10
    shl bx, cl
    mov cx, bx
    xor bx, bx
    mov si, cp_sbuf
    KC dskw_read_x
    jnc .read
    cmp ax, FERR_NOENT
    je .noent
    cmp ax, FERR_BIG
    jne .io
    mov ax, FSKE_SMALL
    jmp fsk_die
.read:
    cmp byte [dskw_n83 + 8], 'S'    ; COLDHMA.SYS, the only .SYS let in
    jne .rdrv
    ; --- THE HMA IMAGE carries no build key, so two facts instead: it IS
    ; one ('OS88ROM', kind 3 - boot/osrom.asm's rom_id), and `.cold` is not
    ; running from it, so this boot did not adopt it. Whether its .text hash
    ; fits this kernel is the tail's own question (SPEC.md 2.10.7)
    mov si, fsk_romid
    xor di, di
    mov cx, 9
.rm:
    mov al, [cs:si]
    cmp al, [es:di]
    jne .bad2
    inc si
    inc di
    loop .rm
%ifndef KERN_MEGA
    cmp word [api_coldseg], HMA_SEG ; in use this boot: never
    je .bad2
%endif
    jmp short .ours
.rdrv:
    cmp word [es:0], 0x384F     ; 'O8': anything else is not ours to judge
    jne .bad2
    mov al, [es:2]
    cmp al, DRV_VER             ; drv_check would take it
    je .bad2
    cmp al, MOD_VER             ; ...and so would mod_check, unless its
    jne .ours                   ; build or this kernel's layout differs
    mov ax, [es:MOD_H_BUILD]
    cmp ax, [mod_hdrt + MOD_H_BUILD]
    jne .ours
    mov ax, [es:MOD_H_STAMP]
    cmp ax, [mod_hdrt + MOD_H_STAMP]
    je .bad2
.ours:
    KC dws_gate                 ; a pending WRITE_SEQ hold, committed first
    KC dskw_find
    jnc .found
    cmp ax, FERR_NOENT
    jne .io
.noent:
    mov ax, FSKE_NOENT
    jmp fsk_die
.found:
    test byte [dskw_raw+DSK_R_ATTR], DSKW_DIR | 0x08
    jnz .bad2                   ; a folder or the volume label: never
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_oldclus], ax
    KC dskw_rmbody_n83.delent   ; the deleted mark, the commit, the chain
    jnc .out
.io:
    jmp fsk_ioerr
.out:
    ret

; THE RETIRED DRIVERS AND MODULES, raw 8.3 - apps/syscheck/retired.txt's .DRV
; lines, in its order; tests/unit/t_sysman.py fails the build when the two
; disagree. A name stays for ever, as it does there
fsk_n_hma:  db 'COLDHMA SYS'
fsk_romid:  db 'OS88ROM', 0, 3  ; rom_id's magic and ROM_KIND_HMA

fsk_retired:
    db 'DEBUG   DRV'
    db 'CLONE   DRV'
    db 'DESKTOP DRV'
%ifndef KERN_EMU                ; ...which kern_emu still loads (SPEC.md 9.11.7)
    db 'VMMOUSE DRV'
%endif
    db 0

; =============================================================================
; FSK_BEGIN
; =============================================================================
fsk_begin:
    call fsk_mounted
    mov dl, ah                  ; DL = the mode
    cmp dl, FSKM_FIX | FSKM_DEFRAG
    jb .modeok
    mov ax, FSKE_ARG
    jmp fsk_die
.modeok:
    push cx
    call fsk_need               ; AX = KB, BX = bitmap bytes
    pop cx
    cmp cx, ax
    jae .room
    mov ax, FSKE_SMALL
    jmp fsk_die
.room:
    KC dws_gate                 ; a pending WRITE_SEQ hold, committed
    KC dskw_flush_x             ; ...and nothing of the kernel's unflushed
    jnc .clean
    jmp fsk_ioerr
.clean:
    push bx
    xor di, di                  ; zero the record, the state and the maps
    mov cx, bx
    add cx, FSK_BM1 / 2         ; words: BM1 / 2 + 2 * NB / 2
    xor ax, ax
    rep stosw
    pop bx
    mov [es:P_NB], bx
    mov [es:FSW_MODE], dl
    mov al, [disk_drive]
    mov [es:P_VOL], al
    mov ax, [dsk_maxclus]
    mov [es:P_MAXC], ax
    dec ax
    mov [es:FSW_NCLUS], ax
    mov ax, [dsk_sigcur]
    mov [es:P_SIG], ax
    mov al, [dsk_spc]
    mov [es:FSW_SPC], al
    xor ah, ah
    mov [es:P_SPC], ax
    mov cl, 9
    shl ax, cl
    mov [es:P_CB], ax
    mov [es:FSW_CBYTES], ax
    mov al, [dsk_fattype]
    mov [es:FSW_FAT16], al
    mov ax, [dsk_fatlba]
    mov [es:P_FATLBA], ax
    mov ax, [dsk_fatsz]
    mov [es:P_FATSZ], ax
    mov [es:FSW_TOTAL], ax
    mov al, [dsk_nfats]
    xor ah, ah
    mov [es:P_NFATS], ax
    mov ax, [dsk_rootlba]
    mov [es:P_ROOTLBA], ax
    mov ax, [dsk_rootsecs]
    mov [es:P_ROOTSECS], ax
    mov word [es:P_STK+LV_LEFT], 0FFFFh
    mov byte [es:P_PATH], '\'
    mov word [es:P_PLEN], 1
    mov al, FSKP_FATCMP
    cmp word [es:P_NFATS], 2
    jae .ph
    mov al, FSKP_TREE
.ph:
    mov [es:FSW_PHASE], al
    call fsk_gen
    mov [es:P_GEN], al
    ret

; =============================================================================
; FSK_STEP
; =============================================================================
fsk_step:
    ; --- the disk is the one we began on, and nobody has written it -------
    call fsk_mounted
    mov al, [disk_drive]
    cmp al, [es:P_VOL]
    jne .changed
    mov ax, [dsk_maxclus]
    cmp ax, [es:P_MAXC]
    jne .changed
    mov ax, [dsk_sigcur]
    cmp ax, [es:P_SIG]
    jne .changed
    call fsk_gen
    cmp al, [es:P_GEN]
    je .same
.changed:
    mov ax, FSKE_CHANGED
    jmp fsk_die
.same:
    call fsk_qpop               ; a finding already waiting goes first
    jnc .found
    mov bl, [es:FSW_PHASE]
    or bl, bl
    jnz .begun
    mov ax, FSKE_ARG            ; a step before a BEGIN
    jmp fsk_die
.begun:
    cmp bl, FSKP_DONE
    jae .done
    xor bh, bh
    dec bx
    shl bx, 1
    call [cs:fsk_phtab+bx]
    call fsk_gen                ; what WE wrote is not a change
    mov [es:P_GEN], al
    call fsk_qpop
    jnc .found
    cmp byte [es:FSW_PHASE], FSKP_DONE
    jae .done
    mov ax, FSKS_MORE
    ret
.found:
    mov ax, FSKS_FIND
    ret
.done:
    mov ax, FSKS_DONE
    ret

fsk_phtab:  dw fsk_ph_fatcmp, fsk_ph_tree, fsk_ph_scan, fsk_ph_lost
            dw fsk_ph_sync, fsk_ph_defrag

; =============================================================================
; FSK_END - flush, tell the Disk windows, done
; =============================================================================
fsk_end:
    test byte [es:P_FLAGS], PF_WROTE
    jz .out
    mov al, [es:P_VOL]
    cmp al, [disk_drive]        ; only while the volume is still the one
    jne .out                    ; written: a flush belongs to its own disk
    KC dskw_flush_x
    call fsk_markwins
    KC dskw_sync_x
.out:
    ret

; fsk_markwins - every Disk window on this volume owes a re-list (SPEC.md
; 22.8): a repair or a move touched folders all over it, not only the one the
; machine stands in, which is all dskw_sync's fmv_mark can name. After a
; FOLDER moves too, at once: a window's cached listing names its folders by
; cluster (118.14.5)
fsk_markwins:
    mov dl, [es:P_VOL]
    mov si, inst_tab
    mov cx, INST_MAX
.inst:
    cmp byte [si+I_STATE], 1
    jne .next
    cmp byte [si+I_KIND], KIND_FILES
    jne .next
    mov di, [si+I_SPTR]
    cmp dl, [di+FS_DRV]
    jne .next
    mov byte [di+FS_DIRTY], FSD_CACHE
.next:
    add si, I_RECSZ
    loop .inst
    KC fm_owed
    ret

; =============================================================================
; the finding queue (one path for every finding in it)
; =============================================================================
; fsk_fix - BH = 1 when the run is a Fix, else 0, and ZF with it (kernel size
; pass 17: six callers spelled it out)
fsk_fix:
    mov bh, [es:FSW_MODE]
    and bh, FSKM_FIX
    ret

; fsk_q - queue a finding. AL = row, AH = severity, BL = code, BH = 1 fixed,
; DX:CX = A, SI:DI = B. Keeps every register. A full queue drops it (the
; counters are exact either way). fsk_q0 is the same with DX = DI = SI = 0,
; and fsk_q0ds with DX = SI = 0 - nine and seven callers wrote the zeros
; out (kernel size pass 17)
fsk_q0:
    xor di, di
fsk_q0ds:
    xor dx, dx
    xor si, si
fsk_q:
    push ax
    push bx
    cmp ah, FSV_BROKEN
    jne .nb
    or bh, bh
    jnz .nb                     ; a repaired breakage is not left Broken
    inc word [es:FSW_ERRS]
.nb:
    push ax
    mov al, [es:P_QN]
    cmp al, FSK_QMAX
    pop ax
    jae .out
    push di
    push ax
    mov al, [es:P_QN]
    inc byte [es:P_QN]
    mov ah, 12
    mul ah
    add ax, P_Q
    xchg ax, di                 ; DI = the slot
    pop ax
    mov [es:di], ax             ; row, severity
    mov [es:di+2], bx           ; code, fixed
    mov [es:di+4], cx           ; A
    mov [es:di+6], dx
    pop ax                      ; B's low word (the caller's DI)
    mov [es:di+8], ax
    mov [es:di+10], si
    mov di, ax
.out:
    pop bx
    pop ax
    ret

; fsk_qpop - CF=0: the oldest finding is in the record; CF=1 none
fsk_qpop:
    push cx
    push si
    push di
    push ds
    cmp byte [es:P_QN], 0
    je .none
    push es
    pop ds                      ; DS = ES = the workspace for the moves
    mov si, P_Q
    mov di, FSW_FROW
    movsw                       ; row, severity
    movsw                       ; code, fixed
    movsw                       ; A
    movsw
    movsw                       ; B
    movsw
    mov si, P_QPATH
    mov di, FSW_FPATH
    mov cx, FSK_PATHN / 2
    rep movsw
    dec byte [P_QN]
    mov si, P_Q + 12            ; the rest move up
    mov di, P_Q
    mov cx, (FSK_QMAX - 1) * 6
    rep movsw
    clc
    jmp short .out
.none:
    stc
.out:
    pop ds
    pop di
    pop si
    pop cx
    ret

; fsk_qp_vol / fsk_qp_dir / fsk_qp_ent - the queue's path: the volume (""),
; the walk's folder, or the folder and the entry at ES:SI. Keep every register
fsk_qp_vol:
    mov byte [es:P_QPATH], 0
    ret
fsk_qp_dir:
    push cx
    push si
    push di
    mov si, P_PATH
    mov di, P_QPATH
    mov cx, [es:P_PLEN]
    call fsk_ecopy
    mov byte [es:di], 0
    pop di
    pop si
    pop cx
    ret
fsk_qp_ent:
    call fsk_qp_dir
    push di
    push cx
    mov di, P_QPATH
    add di, [es:P_PLEN]
    mov cx, P_QPATH + FSK_PATHN - 1
    call fsk_name               ; the entry's 8.3 name at ES:DI
    mov byte [es:di], 0
    pop cx
    pop di
    ret

; fsk_ecopy - CX bytes ES:SI -> ES:DI, DI advanced
fsk_ecopy:
    jcxz .out
.b:
    mov al, [es:si]
    mov [es:di], al
    inc si
    inc di
    loop .b
.out:
    ret

; fsk_name - an entry's name as 8.3 text. in: ES:SI = the entry, ES:DI = where,
; CX = the last offset it may write before (a NUL after it is the caller's).
; out: DI past it; SI kept; AX clobbered
fsk_name:
    push bx
    push si
    mov bx, 8
.n:
    mov al, [es:si]
    cmp al, ' '
    je .ext
    cmp bx, 8
    jne .put
    cmp al, 05h                 ; the 0E5h that starts a name, escaped
    jne .put
    mov al, 0E5h
.put:
    cmp di, cx
    jae .ext
    mov [es:di], al
    inc di
    inc si
    dec bx
    jnz .n
.ext:
    pop si
    push si
    cmp byte [es:si+8], ' '
    je .out
    cmp di, cx
    jae .out
    mov byte [es:di], '.'
    inc di
    add si, 8
    mov bx, 3
.e:
    mov al, [es:si]
    cmp al, ' '
    je .out
    cmp di, cx
    jae .out
    mov [es:di], al
    inc di
    inc si
    dec bx
    jnz .e
.out:
    pop si
    pop bx
    ret

; =============================================================================
; sectors, clusters, bits
; =============================================================================
; fsk_rootsec - read root sector AX into DBUF (and P_LBA)
fsk_rootsec:
    add ax, [es:P_ROOTLBA]
    xor dx, dx
; fsk_rdlba - the folder sector at DX:AX into DBUF, and P_LBA = DX:AX
fsk_rdlba:
    mov [es:P_LBA], ax
    mov [es:P_LBA+2], dx
; fsk_rd1d - one sector at DX:AX into DBUF
fsk_rd1d:
    mov bx, FSK_DBUF
    jmp short fsk_rd1
; fsk_rd0c / fsk_rd1c - the volume's sector 0 / the one at DX:AX into CBUF
; (kernel size pass 17: eight callers loaded BX, CX and the LBA by hand)
fsk_rd0c:
    xor ax, ax
    xor dx, dx
fsk_rd1c:
    mov bx, FSK_CBUF
fsk_rd1:
    mov cx, 1
; fsk_rd / fsk_wr - CX sectors at DX:AX to/from ES = the workspace : BX.
; Die on an error
fsk_rd:
    mov [dsk_lbahi], dx
    KC disk_read_x
    jc fsk_ioerr
    ret
fsk_wr:
    or byte [es:P_FLAGS], PF_WROTE
    mov [dsk_lbahi], dx
    KC disk_write_x
    jc fsk_ioerr
    ret

; fsk_c2l - DX:AX = the first LBA of cluster AX (in range: the callers ask)
fsk_c2l:
    KC dsk_clus2lba_x
    mov dx, [dsk_c2hi]
    ret

; fsk_fat - AX = the FAT entry of cluster AX (in range). Die on an error
fsk_fat:
    KC dsk_next_clus_x
    jc .io
    ret
.io:
    jmp fsk_ioerr

; fsk_fatdx - DX = the FAT entry of cluster AX, ZF = 1 when it is free; AX
; kept (kernel size pass 17: five callers banked AX by hand)
fsk_fatdx:
    push ax
    call fsk_fat
    xchg ax, dx
    pop ax
    or dx, dx
    ret

; fsk_set - FAT entry AX = DX, in the kernel's window (flushed by fsk_flush)
fsk_set:
    or byte [es:P_FLAGS], PF_WROTE
    KC dskw_setfat
    ret

; fsk_flush - write the FAT's dirty range to both copies, or die
fsk_flush:
    push ax
    KC dskw_flush_x
    jc .bad
    pop ax
    ret
.bad:
    cmp ax, FERR_WPROT
    mov ax, FSKE_WPROT
    je .die
    mov al, FSKE_IO
.die:
    jmp fsk_die

; fsk_isend - CF=1: AX (a FAT value) is an end-of-chain mark
; fsk_isbad - ZF=1: AX is the bad mark. Both keep AX
fsk_isend:
    call fsk_isbad              ; AX against the bad mark, so CF = 0 at or
    cmc                         ; past it: at or past the end mark, one more
    jnz .r                      ; unless AX IS the bad mark (kernel size
    clc                         ; pass 17: one compare for both)
.r:
    ret
fsk_isbad:
    cmp byte [dsk_fattype], 0
    jne .w
    cmp ax, 0FF7h
    ret
.w:
    cmp ax, 0FFF7h
    ret

; fsk_bpos - ES:BX = the byte and DL = the bit of cluster AX in the map at
; SI (an offset in the workspace). Keeps AX, SI
fsk_bpos:
    push cx
    mov bx, ax
    mov cl, 3
    shr bx, cl
    add bx, si
    mov cl, al
    and cl, 7
    mov dl, 1
    shl dl, cl
    pop cx
    ret

; the map bits: fsk_bt (ZF=0 set), fsk_bs (set), fsk_bc (clear), on BM1 or,
; with SI given, any map. Keep every register but flags
fsk_bt1:
    push si
    mov si, FSK_BM1
    call fsk_bt
    pop si
    ret
fsk_bs1:
    push si
    mov si, FSK_BM1
    call fsk_bs
    pop si
    ret
fsk_bc1:
    push si
    mov si, FSK_BM1
    call fsk_bc
    pop si
    ret
fsk_bt:
    push bx
    push dx
    call fsk_bpos
    test [es:bx], dl
    pop dx
    pop bx
    ret
fsk_bs:
    push bx
    push dx
    call fsk_bpos
    or [es:bx], dl
    pop dx
    pop bx
    ret
fsk_bc:
    push bx
    push dx
    call fsk_bpos
    not dl
    and [es:bx], dl
    pop dx
    pop bx
    ret
fsk_bm2:                        ; SI = BM2
    mov si, FSK_BM1
    add si, [es:P_NB]
    ret

; =============================================================================
; fsk_walk - follow a chain (SPEC.md 118.14.3)
; in:  AX = its first cluster (>= 2), CL = 1 mark it in BM1 and stop at a bit
;      already set (the check) / 0 neither (the defragmenter, after a check)
; out: AL = 0 ended properly, FSKF_OFFDISK / _INFREE / _INBAD / _XLINK;
;      CX = the clusters reached, DX = the pieces, BX = the last good one
;      (0 none), SI = the cluster it stopped at, DI = its place in the chain
; =============================================================================
fsk_walk:
    mov si, ax                  ; SI = the cluster under test
    xor di, di                  ; DI = clusters reached
    xor dx, dx                  ; DX = pieces
    xor bx, bx                  ; BX = the last good cluster
    mov ch, cl                  ; CH = mark
.next:
    mov al, FSKF_OFFDISK
    cmp si, 2
    jb .out
    cmp si, [es:P_MAXC]
    ja .out
    or ch, ch
    jz .nomark
    mov ax, si
    call fsk_bt1
    mov al, FSKF_XLINK
    jnz .out
.nomark:
    mov ax, si
    call fsk_fat                ; AX = the link
    or ax, ax
    jnz .used
    mov al, FSKF_INFREE
    jmp short .out
.used:
    call fsk_isbad
    jne .good
    mov al, FSKF_INBAD
    jmp short .out
.good:
    or ch, ch
    jz .nm2
    xchg ax, si
    call fsk_bs1
    xchg ax, si
.nm2:
    or bx, bx                   ; a new piece: the first, or not BX + 1
    jz .piece
    inc bx
    cmp bx, si
    je .same
.piece:
    inc dx
.same:
    mov bx, si
    inc di
    cmp di, [es:P_MAXC]         ; a loop the bits did not catch (no marks)
    ja .loop
    call fsk_isend
    jc .ended
    mov si, ax
    jmp short .next
.loop:
    mov al, FSKF_XLINK
    jmp short .out
.ended:
    mov al, 0
.out:
    mov cx, di
    ret

; fsk_nth - AX = the cluster at place CX (0 = the first) of the chain at AX
fsk_nth:
    jcxz .out
.l:
    call fsk_fat
    loop .l
.out:
    ret

; fsk_unmark - clear BM1 for CX clusters of the chain at AX (a checked one)
fsk_unmark:
    jcxz .out
.l:
    call fsk_bc1
    call fsk_fat
    loop .l
.out:
    ret

; =============================================================================
; FSKP_FATCMP - the two copies, FSK_CBSECS / 2 sectors of each a step
; =============================================================================
fsk_ph_fatcmp:
    mov cx, [es:P_FATSZ]
    mov ax, [es:P_IDX]
    sub cx, ax
    jbe .end
    cmp cx, FSK_CBSECS / 2
    jbe .n
    mov cx, FSK_CBSECS / 2
.n:
    push cx
    add ax, [es:P_FATLBA]
    xor dx, dx
    mov bx, FSK_CBUF
    call fsk_rd                 ; FAT1
    add ax, [es:P_FATSZ]
    mov bx, FSK_CBUF + FSK_CBSECS / 2 * 512
    call fsk_rd                 ; FAT2
    pop cx
    add [es:P_IDX], cx
    mov ax, [es:P_IDX]
    mov [es:FSW_DONE], ax
    push ds
    push es
    pop ds
    mov si, FSK_CBUF
    mov di, FSK_CBUF + FSK_CBSECS / 2 * 512
.sec:
    push cx
    mov cx, 256
    repe cmpsw
    je .eq
    inc word [FSW_FATDF]
    add si, cx                  ; the rest of this sector, both sides
    add si, cx
    add di, cx
    add di, cx
.eq:
    pop cx
    loop .sec
    pop ds
    ret
.end:
    mov ax, [es:FSW_FATDF]
    or ax, ax
    jz .tree
    or byte [es:P_FLAGS], PF_SYNC
    xchg ax, cx
    mov di, [es:P_FATSZ]
    call fsk_qp_vol
    call fsk_fix                ; repaired, in FSKP_SYNC, under Fix
    mov bl, FSKF_FATDIFF
    mov ax, FSV_WARN << 8 | 0
    call fsk_q0ds
.tree:
    mov byte [es:FSW_PHASE], FSKP_TREE
    xor ax, ax
    mov [es:FSW_DONE], ax
    mov [es:P_IDX], ax
    ret

; =============================================================================
; FSKP_TREE - one folder sector a step (the check, or the defragmenter's walk)
; =============================================================================
; fsk_lv - BX = the current level's record
fsk_lv:
    mov al, [es:P_DEPTH]
    mov ah, LV_SIZE
    mul ah
    add ax, P_STK
    xchg ax, bx
    ret

fsk_ph_tree:
    test byte [es:P_FLAGS], PF_MOVING
    jz .walk
    jmp fsk_move
.walk:
    call fsk_lv                 ; BX = the level
    cmp word [es:bx+LV_DIR], 0
    jne .sub
    ; --- the root: a fixed region --------------------------------------
    mov ax, [es:bx+LV_SEC]
    cmp ax, [es:P_ROOTSECS]
    jae .dirdone
    add ax, [es:P_ROOTLBA]
    xor dx, dx
    jmp short .read
.sub:
    mov ax, [es:bx+LV_SEC]
    cmp ax, [es:P_SPC]
    jb .insec
    cmp word [es:bx+LV_LEFT], 1 ; the clusters the walk counted
    jbe .dirdone
    mov ax, [es:bx+LV_CLUS]
    call fsk_fat
    cmp ax, 2
    jb .dirdone
    cmp ax, [es:P_MAXC]
    ja .dirdone
    mov [es:bx+LV_CLUS], ax
    mov word [es:bx+LV_SEC], 0
    dec word [es:bx+LV_LEFT]
.insec:
    mov ax, [es:bx+LV_CLUS]
    call fsk_c2l
    add ax, [es:bx+LV_SEC]
    adc dx, 0
.read:
    push bx
    call fsk_rdlba
    pop bx
    ; --- the entries ---------------------------------------------------
.ent:
    mov si, [es:bx+LV_ENT]
    cmp si, 16
    jae .secdone
    mov cl, 5
    shl si, cl
    add si, FSK_DBUF            ; ES:SI = the entry
    inc word [es:bx+LV_ENT]
    mov al, [es:si]
    or al, al
    jz .dirdone                 ; the folder ends here
    cmp al, 0E5h
    je .ent
    mov al, [es:si+11]
    cmp al, 0Fh
    je .ent                     ; a long-name part
    test al, 08h
    jnz .ent                    ; the volume label
    call fsk_entry1             ; CF=1: this step is over (a push, a move)
    jc .stop
    cmp byte [es:P_QN], 0
    je .ent                     ; ...or a finding to hand back
.stop:
    ret
.secdone:
    inc word [es:bx+LV_SEC]
    mov word [es:bx+LV_ENT], 0
    ret
.dirdone:
    cmp byte [es:P_DEPTH], 0
    je .treedone
    mov ax, [es:bx+LV_PLEN]     ; up a folder: the path loses its name
    mov [es:P_PLEN], ax
    dec byte [es:P_DEPTH]
    ret
.treedone:
    jmp fsk_treedone

; fsk_entry1 - one entry at ES:SI, BX = its level. out CF=1 = stop the step;
; SI and BX come back as they went in
fsk_entry1:
    mov [es:P_E], si
    mov [es:P_L], bx
    test byte [es:P_FLAGS], PF_TREEDEF
    jz .chk
    jmp fsk_defent
.chk:
    ; --- '.' and '..', in a folder's first two entries -------------------
    cmp word [es:bx+LV_DIR], 0
    je .notdot
    mov ax, [es:bx+LV_CLUS]
    cmp ax, [es:bx+LV_DIR]
    jne .notdot
    cmp word [es:bx+LV_SEC], 0
    jne .notdot
    mov ax, [es:bx+LV_ENT]      ; 1 = '.', 2 = '..' (it has moved on)
    cmp ax, 2
    ja .notdot
    cmp byte [es:si], '.'
    jne .notdot
    mov dx, [es:bx+LV_DIR]      ; '.' names its own folder...
    mov cl, ' '
    cmp ax, 1
    je .dotx
    mov cl, '.'
    xor dx, dx                  ; ...'..' the one above (0 = the root)
    cmp byte [es:P_DEPTH], 1
    je .dotx
    mov dx, [es:bx+LV_DIR-LV_SIZE]
.dotx:
    cmp [es:si+1], cl
    jne .notdot
    mov ax, [es:si+26]
    cmp ax, dx
    je fsk_eback
    mov cx, ax                  ; A = what it named, B = what it should
    mov di, dx
    call fsk_qp_dir
    call fsk_fix
    jz .dotq
    mov si, [es:P_E]           ; ...repaired: it names the right one
    mov [es:si+26], di
    call fsk_wdir
    inc word [es:FSW_FIXED]
.dotq:
    mov bl, FSKF_DOT
    mov ax, FSV_WARN << 8 | 1
    call fsk_q0ds
    jmp short fsk_eback
.notdot:
    ; --- an entry that is not one -------------------------------------
    mov al, [es:si]
    cmp al, 20h
    jae .named
    cmp al, 05h
    je .named
    mov cx, [es:bx+LV_ENT]
    dec cx
    call fsk_qp_dir
    mov bx, FSKF_BADENT
    mov ax, FSV_WARN << 8 | 1
    call fsk_q0
    jmp short fsk_eback
.named:
    cmp al, '.'                 ; a stray '.' entry past the first two
    je fsk_eback
    test byte [es:si+11], 10h
    jz .file
    jmp fsk_dirent
.file:
    jmp fsk_filent

; fsk_eback - an entry's handler ends: SI, BX back, CF = 0
fsk_eback:
    mov si, [es:P_E]
    mov bx, [es:P_L]
    clc
    ret

; fsk_wdir - write the folder sector in DBUF back (a repair)
fsk_wdir:
    push ax
    push bx
    push cx
    push dx
    mov ax, [es:P_LBA]
    mov dx, [es:P_LBA+2]
    mov bx, FSK_DBUF
    mov cx, 1
    call fsk_wr
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fsk_chainq - queue a chain's stop: AL = the code (row 3 for a cross-link,
; else 2), [fk_a] = the cluster, [fk_b] = its place, BH = 1 repaired. The
; path is the entry at [fk_e]
fsk_chainq:
    mov bl, al
    mov si, [es:P_E]
    call fsk_qp_ent
    mov cx, [es:P_A]
    xor dx, dx
    mov di, [es:P_B]
    xor si, si
    mov ax, FSV_BROKEN << 8 | 2
    cmp bl, FSKF_XLINK
    jne .q
    inc word [es:FSW_XLINK]
    mov al, 3
    mov bh, 0                   ; a cross-link is never repaired
.q:
    jmp fsk_q

; -----------------------------------------------------------------------------
; fsk_dirent - a folder's entry (the check): walk its chain, then go in
; in:  ES:SI = the entry, BX = the level; out CF=1 = the step is over
; -----------------------------------------------------------------------------
fsk_dirent:
    inc word [es:FSW_DIRS]
    mov ax, [es:si+26]
    cmp ax, 2
    jb .bad
    cmp ax, [es:P_MAXC]
    jbe .walk
.bad:
    xchg ax, cx                 ; A = the cluster it names
    call fsk_qp_ent
    mov bx, FSKF_DIRBAD
    mov ax, FSV_BROKEN << 8 | 1
    call fsk_q0
    jmp fsk_eback
.walk:
    mov cl, 1
    call fsk_walk               ; AL, CX = n, SI, DI
    add [es:FSW_USED], cx
    or al, al
    jz .ok
    mov [es:P_A], si           ; a folder's chain is reported, never cut,
    mov [es:P_B], di           ; and not gone into
    mov bh, 0
    call fsk_chainq
    jmp fsk_eback
.ok:
    mov si, [es:P_E]
    mov bx, [es:P_L]
    jcxz .none                  ; no clusters: nothing to go into
    mov al, [es:P_DEPTH]
    inc ax
    cmp al, FSK_MAXDEP
    jb .deep_ok
    xor ah, ah                  ; too deep: said, and not gone into
    xchg ax, cx
    call fsk_qp_ent
    mov bx, FSKF_DEEP
    mov ax, FSV_NOTE << 8 | 1
    call fsk_q0
.none:
    jmp fsk_eback
.deep_ok:
    ; --- go in: the next step reads its first sector. SI = the entry, BX =
    ; the level, CX = its clusters (fsk_defent joins here too)
    mov ax, [es:si+26]
    inc byte [es:P_DEPTH]
    add bx, LV_SIZE
    mov [es:bx+LV_DIR], ax
    mov [es:bx+LV_CLUS], ax
    mov [es:bx+LV_LEFT], cx
    xor ax, ax
    mov [es:bx+LV_SEC], ax
    mov [es:bx+LV_ENT], ax
    mov ax, [es:P_PLEN]
    mov [es:bx+LV_PLEN], ax
    mov di, P_PATH              ; the path gains "NAME\"
    add di, ax
    mov cx, P_PATH + FSK_PATHN - 2
    call fsk_name
    mov byte [es:di], '\'
    inc di
    sub di, P_PATH
    mov [es:P_PLEN], di
    mov si, [es:P_E]
    mov bx, [es:P_L]
    stc
    ret

; -----------------------------------------------------------------------------
; fsk_filent - a file's entry (the check): its chain against its size, and
; the repairs SPEC.md 118.14.4 makes when it is asked to
; in:  ES:SI = the entry ([fk_e]), BX = the level; out CF = 0
; -----------------------------------------------------------------------------
fsk_filent:
    inc word [es:FSW_FILES]
    mov ax, [es:si+26]
    or ax, ax
    jnz .chain
    mov ax, [es:si+28]          ; no chain: right exactly when the size is 0
    or ax, [es:si+30]
    jz .done
    xor ax, ax
    jmp .short
.chain:
    mov cl, 1
    call fsk_walk               ; AL, CX = n, DX = pieces, BX = last, SI, DI
    add [es:FSW_USED], cx
    cmp dx, 1
    jbe .onep
    inc word [es:FSW_FRAG]
.onep:
    or al, al
    jz .ended
    ; --- the chain stopped early ----------------------------------------
    mov [es:P_N], cx
    mov [es:P_LAST], bx
    mov [es:P_A], si
    mov [es:P_B], di
    call fsk_fix
    push ax
    call fsk_chainq
    pop ax
    cmp al, FSKF_XLINK
    je .done
    or bh, bh
    jz .done
    ; ...repaired: the chain ends at its last good cluster and the size is
    ; cut to what it holds. The entry first, the FAT second (118.14.4)
    inc word [es:FSW_FIXED]
    mov si, [es:P_E]
    mov ax, [es:P_LAST]
    or ax, ax
    jnz .brkend
    mov [es:si+26], ax          ; not one good cluster: no chain, size 0
    mov [es:si+28], ax
    mov [es:si+30], ax
    call fsk_wdir
    jmp .done
.brkend:
    mov cx, [es:P_N]
    call fsk_cutsize
    call fsk_wdir
    mov ax, [es:P_LAST]
    mov dx, DSKW_EOC16
    call fsk_set
    call fsk_flush
    jmp .done
.ended:
    ; --- a proper chain of CX clusters, against the size -----------------
    mov [es:P_N], cx
    mov si, [es:P_E]
    call fsk_needcl             ; DX = the clusters the size needs
    mov [es:P_NEED], dx
    mov ax, [es:P_N]
    cmp ax, dx
    je .done
    jb .short
    ; --- LONG: more chain than size --------------------------------------
    call fsk_qp_ent
    mov cx, [es:P_N]
    mov di, [es:P_NEED]
    call fsk_fix
    mov bl, FSKF_LONG
    mov ax, FSV_WARN << 8 | 2
    call fsk_q0ds
    or bh, bh
    jz .done
    inc word [es:FSW_FIXED]
    mov si, [es:P_E]
    mov cx, [es:P_N]
    mov dx, [es:P_NEED]
    sub [es:FSW_USED], cx
    add [es:FSW_USED], dx
    or dx, dx
    jnz .cut
    mov ax, [es:si+26]          ; the size is 0: the entry loses its chain,
    mov [es:si+26], dx          ; then the chain is freed
    call fsk_wdir
    push ax
    call fsk_unmark
    pop ax
    KC dskw_free_chain
    call fsk_flush
    jmp .done
.cut:
    mov ax, [es:si+26]
    mov cx, dx
    dec cx
    call fsk_nth                ; AX = the last cluster the size needs
    mov [es:P_LAST], ax
    call fsk_fat                ; AX = the first one it does not
    push ax
    mov cx, [es:P_N]
    sub cx, [es:P_NEED]
    call fsk_unmark
    mov ax, [es:P_LAST]
    mov dx, DSKW_EOC16
    call fsk_set
    pop ax
    KC dskw_free_chain
    call fsk_flush
    jmp .done
.short:
    ; --- SHORT: the size says more than the chain holds (AX = n) ---------
    mov [es:P_N], ax
    mov si, [es:P_E]
    call fsk_qp_ent
    mov ax, [es:P_N]
    mul word [es:P_CB]          ; B = what the chain holds
    mov di, ax
    mov si, dx
    mov bp, [es:P_E]
    mov cx, [es:bp+28]          ; A = the size
    mov dx, [es:bp+30]
    call fsk_fix
    mov bl, FSKF_SHORT
    mov ax, FSV_BROKEN << 8 | 2
    call fsk_q
    or bh, bh
    jz .done
    inc word [es:FSW_FIXED]
    mov si, [es:P_E]
    mov cx, [es:P_N]
    call fsk_cutsize
    call fsk_wdir
.done:
    jmp fsk_eback

; fsk_needcl - DX = the clusters the size of the entry at ES:SI needs,
; saturated at 0FFFFh. Keeps SI
fsk_needcl:
    mov ax, [es:si+28]
    mov dx, [es:si+30]
    mov cx, [es:P_CB]
    add ax, cx                  ; + cluster - 1, in 32 bits
    adc dx, 0
    sub ax, 1
    sbb dx, 0
    cmp dx, cx
    jae .sat                    ; the quotient would not fit a word
    div cx
    xchg ax, dx
    ret
.sat:
    mov dx, 0FFFFh
    ret

; fsk_cutsize - the entry at ES:SI's size becomes min(size, CX clusters)
fsk_cutsize:
    push ax
    push dx
    mov ax, cx
    mul word [es:P_CB]          ; DX:AX = what CX clusters hold
    cmp dx, [es:si+30]
    ja .keep
    jb .cut
    cmp ax, [es:si+28]
    jae .keep
.cut:
    mov [es:si+28], ax
    mov [es:si+30], dx
.keep:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fsk_treedone - the walk is over: what comes next
; -----------------------------------------------------------------------------
fsk_treedone:
    test byte [es:P_FLAGS], PF_TREEDEF
    jz .scan
    jmp fsk_defpass
.scan:
    mov byte [es:FSW_PHASE], FSKP_SCAN
    mov byte [es:P_SWEEP], 0
    mov word [es:P_IDX], 2
    mov ax, [es:P_MAXC]
    mov [es:FSW_TOTAL], ax
    mov word [es:FSW_DONE], 0
    ret

; =============================================================================
; FSKP_SCAN - the FAT itself: free, bad and lost, FSK_SCANN clusters a step
; sweep 0 counts and marks what lost chains point at (BM2); sweep 1 counts
; the chains, a lost cluster nothing lost points at being a chain's head
; =============================================================================
FSK_SCANN   equ 1024

fsk_ph_scan:
    call fsk_bm2                ; SI = BM2
    mov cx, FSK_SCANN
.c:
    mov ax, [es:P_IDX]
    cmp ax, [es:P_MAXC]
    ja .swept
    inc word [es:P_IDX]
    call fsk_fatdx              ; AX = the cluster, DX = its entry
    jnz .nfree
    cmp byte [es:P_SWEEP], 0
    jne .n
    inc word [es:FSW_FREE]
    jmp short .n
.nfree:
    xchg ax, dx
    call fsk_isbad
    xchg ax, dx
    jne .nbad
    cmp byte [es:P_SWEEP], 0
    jne .n
    inc word [es:FSW_BAD]
    jmp short .n
.nbad:
    call fsk_bt1
    jnz .n                      ; reached by a file or folder: fine
    cmp byte [es:P_SWEEP], 0
    jne .heads
    inc word [es:FSW_LOST]
    cmp dx, 2                   ; ...and what it points at is not a head
    jb .n
    cmp dx, [es:P_MAXC]
    ja .n
    xchg ax, dx
    call fsk_bs
    xchg ax, dx
    jmp short .n
.heads:
    call fsk_bt                 ; BM2: something lost points here
    jnz .n
    inc word [es:FSW_LOSTC]
.n:
    loop .c
    mov ax, [es:P_IDX]
    mov [es:FSW_DONE], ax
    ret
.swept:
    cmp byte [es:P_SWEEP], 0
    jne .report
    cmp word [es:FSW_LOST], 0
    je .report
    mov byte [es:P_SWEEP], 1    ; a second sweep, for the chains
    mov word [es:P_IDX], 2
    ret
.report:
    call fsk_qp_vol
    mov cx, [es:FSW_LOST]
    jcxz .space
    mov di, [es:FSW_LOSTC]
    call fsk_fix
    mov bl, FSKF_LOST
    mov ax, FSV_WARN << 8 | 4
    call fsk_q0ds
.space:
    mov ax, [es:FSW_FREE]       ; free KB = clusters * spc / 2
    call fsk_kb
    mov cx, ax
    push dx
    mov ax, [es:FSW_NCLUS]
    call fsk_kb
    mov di, ax
    mov si, dx
    pop dx
    mov bx, FSKF_SPACE
    mov ax, FSV_NOTE << 8 | 5
    call fsk_q
    mov cx, [es:FSW_BAD]
    jcxz .frag
    mov bx, FSKF_BADCL
    mov ax, FSV_NOTE << 8 | 5
    call fsk_q0
.frag:
    mov cx, [es:FSW_FRAG]
    mov di, [es:FSW_FILES]
    mov bx, FSKF_FRAGS
    mov ax, FSV_NOTE << 8 | 6
    call fsk_q0ds
    ; --- and next ------------------------------------------------------
    mov al, [es:FSW_MODE]
    test al, FSKM_FIX
    jz .nofix
    cmp word [es:FSW_LOST], 0
    je .nolost
    mov byte [es:FSW_PHASE], FSKP_LOST
    mov byte [es:P_SWEEP], 0
    mov word [es:P_IDX], 2
    ret
.nolost:
    jmp fsk_aftlost
.nofix:
    test al, FSKM_DEFRAG
    jz .notdef
    jmp fsk_defstart
.notdef:
    jmp fsk_notdef

; fsk_kb - DX:AX = AX clusters in KB
fsk_kb:
    mul word [es:P_SPC]
    shr dx, 1
    rcr ax, 1
    ret

; fsk_notdef - the run ends with the Defragment row's 'not asked'
fsk_notdef:
    call fsk_qp_vol
    xor cx, cx
    mov bx, FSKF_NOTDEF
    mov ax, FSV_NOTE << 8 | 7
    call fsk_q0
    mov byte [es:FSW_PHASE], FSKP_DONE
    ret

; =============================================================================
; FSKP_LOST - save every lost chain as \FILEnnnn.CHK (SPEC.md 118.14.4)
; sweep 0: number past every FILEnnnn.CHK in the root; sweep 1: one chain a
; step; sweep 2: free what is left (loops with no head)
; =============================================================================
fsk_ph_lost:
    mov al, [es:P_SWEEP]
    or al, al
    jz fsk_lost_num
    cmp al, 1
    je fsk_lost_chain
    jmp fsk_lost_rest

fsk_lost_num:
    xor ax, ax
.sec:
    cmp ax, [es:P_ROOTSECS]
    jae .done
    push ax
    call fsk_rootsec
    mov si, FSK_DBUF
.e:
    cmp byte [es:si], 0
    je .last
    cmp word [es:si], 'FI'
    jne .n
    cmp word [es:si+2], 'LE'
    jne .n
    cmp word [es:si+8], 'CH'
    jne .n
    cmp byte [es:si+10], 'K'
    jne .n
    xor ax, ax                  ; the number in the name, +1
    mov cx, 4
    mov bx, 4
.d:
    mov dl, [es:si+bx]
    sub dl, '0'
    cmp dl, 9
    ja .n
    mov dh, 10
    push dx
    mul dh
    pop dx
    xor dh, dh
    add ax, dx
    inc bx
    loop .d
    inc ax
    cmp ax, [es:P_CHKN]
    jbe .n
    mov [es:P_CHKN], ax
.n:
    add si, 32
    cmp si, FSK_DBUF + 512
    jb .e
    pop ax
    inc ax
    jmp short .sec
.last:
    pop ax
.done:
    mov byte [es:P_SWEEP], 1
    mov word [es:P_IDX], 2
    ret

fsk_lost_chain:
    call fsk_bm2                ; SI = BM2
.find:
    mov ax, [es:P_IDX]
    cmp ax, [es:P_MAXC]
    jbe .look
    mov byte [es:P_SWEEP], 2    ; every head is saved
    mov word [es:P_IDX], 2
    ret
.look:
    inc word [es:P_IDX]
    call fsk_bt1
    jnz .find
    call fsk_bt
    jnz .find
    call fsk_fatdx
    jz .find
    xchg ax, dx
    call fsk_isbad
    xchg ax, dx
    je .find
    ; --- AX is a head: follow it, mark it, end it properly ------------
    push ax                     ; the head
    xor cx, cx                  ; CX = its clusters
.f:
    call fsk_bs1
    inc cx
    call fsk_fatdx              ; AX = this cluster, DX = its link
    xchg ax, dx
    call fsk_isend
    xchg ax, dx
    jc .ended
    cmp dx, 2
    jb .cutit
    cmp dx, [es:P_MAXC]
    ja .cutit
    xchg ax, dx
    call fsk_bt1                ; reached already, or by a file: stop here
    xchg ax, dx
    jnz .cutit
    push ax                     ; this
    push dx                     ; the link
    mov ax, dx
    call fsk_fat                ; ...or what it points at is free or bad
    or ax, ax
    jz .cutp
    call fsk_isbad
    je .cutp
    pop ax                      ; on to the link
    pop dx
    cmp cx, [es:P_MAXC]
    jb .f
    jmp short .ended
.cutp:
    pop dx
    pop ax
.cutit:
    mov dx, DSKW_EOC16
    call fsk_set
.ended:
    call fsk_flush              ; the chain is well formed, still lost: FAT
    pop ax                      ; first, the entry second (118.14.4)
    add [es:FSW_USED], cx
    call fsk_save               ; AX = the head, CX = its clusters
    ret

; fsk_save - give the lost chain at AX (CX clusters) a root entry, or free it
fsk_save:
    push ax
    push cx
    xor ax, ax
.sec:
    cmp ax, [es:P_ROOTSECS]
    jae .full
    push ax
    call fsk_rootsec
    pop ax
    mov si, FSK_DBUF
.e:
    mov dl, [es:si]
    or dl, dl
    jz .slot
    cmp dl, 0E5h
    je .slot
    add si, 32
    cmp si, FSK_DBUF + 512
    jb .e
    inc ax
    jmp short .sec
.full:
    ; no free root entry: the chain goes back to free space
    pop cx
    pop ax
    push cx
    call fsk_unmark
    pop cx
    sub [es:FSW_USED], cx
    KC dskw_free_chain
    call fsk_flush
    add [es:P_FREED], cx
    inc word [es:FSW_FIXED]
    ret
.slot:
    ; ES:SI = the slot. Taking the end mark keeps the next one an end mark
    or dl, dl
    jnz .take
    lea di, [si+32]
    cmp di, FSK_DBUF + 512
    jae .take
    mov byte [es:di], 0
.take:
    mov di, si
    mov word [es:di], 'FI'
    mov word [es:di+2], 'LE'
    mov ax, [es:P_CHKN]
    push ax
    mov bx, 7                   ; four digits, right to left
.dig:
    xor dx, dx
    mov cx, 10
    div cx
    add dl, '0'
    mov [es:di+bx], dl
    dec bx
    cmp bx, 4
    jae .dig
    pop ax
    inc word [es:P_CHKN]
    mov word [es:di+8], 'CH'
    mov byte [es:di+10], 'K'
    mov byte [es:di+11], 20h    ; archive
    push di
    add di, 12                  ; bytes 12..21: zero
    xor ax, ax
    mov cx, 5
    rep stosw                   ; (DF is clear: fsk_entry's cld)
    pop di
    KC dskw_now
    mov ax, [dskw_time]
    mov [es:di+22], ax
    mov ax, [dskw_date]
    mov [es:di+24], ax
    pop cx                      ; clusters
    pop ax                      ; head
    mov [es:di+26], ax
    push ax
    mov ax, cx
    mul word [es:P_CB]
    mov [es:di+28], ax
    mov [es:di+30], dx
    pop ax
    call fsk_wdir
    inc word [es:FSW_FIXED]
    ; ...and say so: \FILEnnnn.CHK
    push di
    push si
    mov si, di
    mov di, P_QPATH
    mov byte [es:di], '\'
    inc di
    push cx
    mov cx, P_QPATH + FSK_PATHN - 1
    call fsk_name
    pop cx
    mov byte [es:di], 0
    pop si
    pop di
    mov di, [es:P_CHKN]
    dec di
    mov bx, 1 << 8 | FSKF_SAVED
    mov ax, FSV_NOTE << 8 | 4
    call fsk_q0ds
    ret

fsk_lost_rest:
    mov cx, FSK_SCANN
.c:
    mov ax, [es:P_IDX]
    cmp ax, [es:P_MAXC]
    ja .end
    inc word [es:P_IDX]
    call fsk_bt1
    jnz .n
    call fsk_fatdx
    jz .n
    xchg ax, dx
    call fsk_isbad
    xchg ax, dx
    je .n
    xor dx, dx                  ; a loop with no head: freed
    call fsk_set
    inc word [es:P_FREED]
.n:
    loop .c
    call fsk_flush
    ret
.end:
    call fsk_flush
    mov cx, [es:P_FREED]
    jcxz .aft
    call fsk_qp_vol
    inc word [es:FSW_FIXED]
    mov bx, 1 << 8 | FSKF_FREED
    mov ax, FSV_NOTE << 8 | 4
    call fsk_q0
.aft:
    jmp fsk_aftlost

; fsk_aftlost - after the lost chains: the second FAT copy, or the end
fsk_aftlost:
    test byte [es:P_FLAGS], PF_SYNC
    jz .done
    mov byte [es:FSW_PHASE], FSKP_SYNC
    mov word [es:P_IDX], 0
    ret
.done:
    jmp fsk_notdef

; =============================================================================
; FSKP_SYNC - the first FAT copy over the second, FSK_CBSECS sectors a step
; =============================================================================
fsk_ph_sync:
    mov cx, [es:P_FATSZ]
    mov ax, [es:P_IDX]
    sub cx, ax
    jbe .end
    cmp cx, FSK_CBSECS
    jbe .n
    mov cx, FSK_CBSECS
.n:
    add [es:P_IDX], cx
    add ax, [es:P_FATLBA]
    xor dx, dx
    mov bx, FSK_CBUF
    call fsk_rd
    add ax, [es:P_FATSZ]
    call fsk_wr
    ret
.end:
    inc word [es:FSW_FIXED]
    jmp fsk_notdef

; =============================================================================
; FSKP_DEFRAG - SPEC.md 118.14.5
; =============================================================================
; fsk_defstart - only after a check with nothing Broken
fsk_defstart:
    mov cx, [es:FSW_ERRS]
    jcxz .go
    call fsk_qp_vol
    mov bx, FSKF_SKIPDEF
    mov ax, FSV_WARN << 8 | 7
    call fsk_q0
    mov byte [es:FSW_PHASE], FSKP_DONE
    ret
.go:
    ; FOLDERS MOVE ONLY WITH NOTHING ELSE OPEN (SPEC.md 118.14.5): a program
    ; may have banked a folder's cluster (OSAPI_FILE_HERE) where the kernel
    ; cannot see it to put right. Every package instance but the caller's
    ; counts; the kernel's own words (fsk_krefs) are patched
    mov bp, inst_caller         ; DH = the caller's slot (BP is nobody's
    call KERNEL_SEG:spw_near    ; here: KC's scratch - kernel size pass 17)
    xor ax, ax                  ; AX = the others
    xor bx, bx                  ; BL = the slot
    mov si, inst_tab
    mov cx, INST_MAX
.inst:
    cmp byte [si+I_STATE], 1
    jne .nx
    test byte [si+I_KIND], 80h  ; a package
    jz .nx
    cmp bl, dh
    je .nx
    inc ax
.nx:
    inc bx
    add si, I_RECSZ
    loop .inst
    mov [es:FSW_OTHERS], ax
    cmp ax, 1
    sbb al, al                  ; FF none open / 00 some
    and al, 1
    mov [es:P_FOLDOK], al
    mov byte [es:FSW_PHASE], FSKP_DEFRAG
    mov byte [es:P_SWEEP], 0
    mov word [es:P_IDX], 0
    ret

fsk_ph_defrag:
    cmp byte [es:P_SWEEP], 0
    je fsk_defmap
    jmp fsk_ph_tree             ; sweep 1: the walk, with PF_TREEDEF

; fsk_defmap - BM1 becomes the map of clusters IN USE (the FAT's non-zero),
; FSK_SCANN a step; then the first pass begins
fsk_defmap:
    mov cx, FSK_SCANN
.c:
    mov ax, [es:P_IDX]
    cmp ax, [es:P_MAXC]
    ja .end
    inc word [es:P_IDX]
    cmp ax, 2
    jb .set                     ; clusters 0 and 1 are never free
    call fsk_fatdx
    jz .clr
.set:
    call fsk_bs1
    jmp short .n
.clr:
    call fsk_bc1
.n:
    loop .c
    ret
.end:
    mov byte [es:P_SWEEP], 1
    mov byte [es:P_DPASS], 0
    or byte [es:P_FLAGS], PF_TREEDEF
    ; fall into a fresh walk
fsk_walkreset:
    xor ax, ax
    mov byte [es:P_DEPTH], al
    mov byte [es:P_DMOVED], al
    mov [es:P_STK+LV_DIR], ax
    mov [es:P_STK+LV_CLUS], ax
    mov [es:P_STK+LV_SEC], ax
    mov [es:P_STK+LV_ENT], ax
    mov word [es:P_STK+LV_LEFT], 0FFFFh
    mov word [es:P_PLEN], 1
    ret

; fsk_defpass - a walk of the defragmenter's is over
fsk_defpass:
    mov al, [es:P_DPASS]
    or al, al
    jz .again                   ; pass 1 done: the first packing pass
    cmp byte [es:P_DMOVED], 0
    je .done
    cmp al, 3
    jae .done
.again:
    inc byte [es:P_DPASS]
    jmp fsk_walkreset
.done:
    call fsk_qp_vol
    mov cx, [es:FSW_MOVED]
    mov di, [es:FSW_STUCK]
    mov bx, FSKF_MOVED
    mov ax, FSV_NOTE << 8 | 7
    call fsk_q0ds
    mov cx, [es:FSW_MOVEDF]
    jcxz .nof
    mov bx, FSKF_FOLDERS
    mov ax, FSV_NOTE << 8 | 7
    call fsk_q0
.nof:
    cmp byte [es:P_FOLDOK], 0
    jne .fin
    mov cx, [es:FSW_OTHERS]
    mov bx, FSKF_NOFOLD
    mov ax, FSV_NOTE << 8 | 7
    call fsk_q0
.fin:
    mov byte [es:FSW_PHASE], FSKP_DONE
    ret

; -----------------------------------------------------------------------------
; fsk_defent - one entry, for the defragmenter (ES:SI, BX = the level)
; out: CF=1 = the step is over (a folder entered, a move begun)
; -----------------------------------------------------------------------------
fsk_defent:
    mov al, [es:si]
    cmp al, '.'
    je .skip
    cmp al, 20h
    jb .skip
    mov al, [es:si+11]
    test al, 10h
    jz .file
    ; --- a folder: move it (118.14.5), then walk in ----------------------
    mov ax, [es:si+26]
    cmp ax, 2
    jb .skip
    cmp ax, [es:P_MAXC]
    ja .skip
    push si
    push bx
    mov cl, 0
    call fsk_walk               ; AL, CX = its clusters, DX = pieces
    pop bx
    pop si
    or al, al
    jnz .skip
    jcxz .skip
    cmp byte [es:P_FOLDOK], 0
    je .descend
    mov ax, [es:si+26]
    cmp byte [es:P_DPASS], 0
    jne .fpack
    cmp dx, 1                   ; pass 1: in pieces, to the lowest run
    jbe .descend
    push dx
    mov dx, [es:P_MAXC]
    inc dx
    call fsk_findrun
    pop dx
    jnc .fmove
    inc word [es:FSW_STUCK]
    jmp short .descend
.fpack:
    cmp dx, 1                   ; packing: in one piece, lower down
    jne .descend
    mov dx, ax
    call fsk_findrun
    jc .descend
.fmove:
    mov byte [es:M_DIR], 1      ; ...and it is entered when the move is
    mov byte [es:M_KERN], 0     ; done: .movego steps LV_ENT back to it
    jmp .movego
.descend:
    mov al, [es:P_DEPTH]
    inc ax
    cmp al, FSK_MAXDEP
    jae .skip
    jmp fsk_dirent.deep_ok      ; ...the check's own descent, CX = clusters
.skip:
    clc
    ret
.file:
    mov byte [es:M_DIR], 0
    ; WHAT MAY MOVE (SPEC.md 118.14.5): every file the system reads BY NAME,
    ; which is every file but two kinds. A mounted *.IMG is read through the
    ; extents taken when it was mounted (18.101); HIBERNAT.IMG is walked
    ; afresh at a resume, so it may. KERNEL.SYS is read by POSITION, by a
    ; boot sector, and moves only where that sector can be told (fsk_kernok)
    mov byte [es:M_KERN], 0
    cmp word [es:si+8], 'IM'
    jne .notimg
    cmp byte [es:si+10], 'G'
    jne .notimg
    mov di, fsk_n_hib
    call fsk_nameis
    jne .skip
.notimg:
    mov di, fsk_n_kern
    call fsk_nameis
    jne .ok
    call fsk_kernok             ; CF = 1: it stays where its boot sector
    jc .skip                    ; looks for it
    mov byte [es:M_KERN], 1
.ok:
    mov ax, [es:si+26]
    cmp ax, 2
    jb .skip
    push si
    push bx
    mov cl, 0
    call fsk_walk               ; CX = n, DX = pieces
    pop bx
    pop si
    or al, al
    jnz .skip
    jcxz .skip
    mov ax, [es:si+26]          ; AX = where it starts
    cmp byte [es:P_DPASS], 0
    jne .pack
    cmp dx, 1                   ; pass 1: files in pieces, anywhere
    jbe .skip
    mov dx, [es:P_MAXC]
    inc dx
    call fsk_findrun
    jnc .move
    inc word [es:FSW_STUCK]
    clc
    ret
.pack:
    cmp dx, 1                   ; packing: files in one piece, lower down
    jne .skip
    mov dx, ax
    call fsk_findrun
    jc .skip
.move:
.movego:
    ; --- begin the move: AX = where to, CX = clusters --------------------
    mov [es:M_DST], ax
    mov [es:M_N], cx
    mov ax, [es:si+26]
    mov [es:M_OLD], ax
    mov [es:M_SRC], ax
    xor ax, ax
    mov [es:M_K], ax
    mov [es:M_KS], ax
    mov ax, [es:P_LBA]
    mov [es:M_DLBA], ax
    mov ax, [es:P_LBA+2]
    mov [es:M_DLBA+2], ax
    mov ax, si
    sub ax, FSK_DBUF
    mov [es:M_DOFF], ax
    or byte [es:P_FLAGS], PF_MOVING
    cmp byte [es:M_DIR], 0      ; a FOLDER is taken again when it has
    je .began                   ; moved, so the walk goes into it where it
    dec word [es:bx+LV_ENT]     ; now is (and packs it lower still, or not)
.began:
    stc
    ret

; fsk_nameis - ZF = 1 the entry at ES:SI is named the 11 bytes at CS:DI
fsk_nameis:
    push cx
    push si
    push di
    mov cx, 11
.c:
    mov al, [es:si]
    cmp al, [cs:di]
    jne .out
    inc si
    inc di
    loop .c
    cmp al, al                  ; ALL ELEVEN: ZF = 1 - the `inc`s above left
.out:                           ; it 0, which read every name as different
    pop di                      ; and moved KERNEL.SYS like any other file
    pop si                      ; (tests/fsckhd.py caught it)
    pop cx
    ret
fsk_n_kern: db 'KERNEL  SYS'
fsk_n_hib:  db 'HIBERNAT'       ; ...and 'IMG', which the caller has asked
            db 'IMG'

; -----------------------------------------------------------------------------
; fsk_kernok - may KERNEL.SYS at ES:SI move? CF = 0 yes, CF = 1 no
;
; A boot sector reads KERNEL.SYS as ONE FLAT RUN, not a chain (boot/
; boothd.asm), from where it is told: the hard disk's VBR carries the run's
; offset into the data area at BOOTHD_KOFS (504) and its sectors at
; BOOTHD_KSECS (508), patched by the installer (SPEC.md 52.10.15). So it
; moves only when the volume's own sector 0 IS that sector and says THIS
; file: the signature, the count matching the file's size and the offset
; matching where it starts - and on a hard disk (volume C: or later): a
; floppy's sector reads KERNEL.SYS from cluster 2 and is told nothing.
; Keeps every register but the flags; reads into the copy buffer, which is
; free while no move is under way
; -----------------------------------------------------------------------------
fsk_kernok:
    push ax
    push bx
    push cx
    push dx
    cmp byte [es:P_DEPTH], 0
    jne .no                     ; ...in the root
    cmp byte [es:P_VOL], 2
    jb .no
    mov ax, [es:si+28]          ; its sectors, as the VBR counts them
    mov dx, [es:si+30]
    add ax, 511
    adc dx, 0
    mov cx, 9
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    or dx, dx
    jnz .no
    push ax
    call fsk_rd0c               ; the volume's sector 0
    pop ax
    cmp word [es:FSK_CBUF+510], 0AA55h
    jne .no
    cmp [es:FSK_CBUF+508], ax
    jne .no
    mov ax, [es:si+26]
    call fsk_kofs               ; DX:AX = where that says it starts
    cmp [es:FSK_CBUF+504], ax
    jne .no
    cmp [es:FSK_CBUF+506], dx
    jne .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fsk_kofs - DX:AX = (cluster AX - 2) x sectors a cluster: BOOTHD_KOFS's value
fsk_kofs:
    sub ax, 2
    mul word [es:P_SPC]
    ret

; fsk_findrun - AX = the lowest cluster of CX free ones (BM1 clear) that all
; lie below DX; CF=1 none. Keeps CX, DX, SI, BX
fsk_findrun:
    push bx
    push si
    push di
    push dx
    cmp dx, [es:P_MAXC]
    jbe .lim
    mov dx, [es:P_MAXC]
    inc dx
.lim:
    mov si, FSK_BM1
    mov ax, 2                   ; AX = the cluster under test
    xor di, di                  ; DI = the free run's length so far
    xor bx, bx                  ; BX = where it began
.c:
    cmp ax, dx
    jae .no
    test al, 7                  ; a whole byte of used ones: eight at once
    jnz .bit
    push bx
    mov bx, ax
    push cx
    mov cl, 3
    shr bx, cl
    pop cx
    cmp byte [es:si+bx], 0FFh
    pop bx
    jne .bit
    xor di, di
    add ax, 8
    jmp short .c
.bit:
    call fsk_bt
    jz .free
    xor di, di
    inc ax
    jmp short .c
.free:
    or di, di
    jnz .grow
    mov bx, ax
.grow:
    inc di
    inc ax
    cmp di, cx
    jb .c
    xchg ax, bx                 ; AX = the run's start
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; fsk_move - one step of a move (SPEC.md 118.14.5): a buffer of data, or the
; link once every cluster is copied
; -----------------------------------------------------------------------------
fsk_move:
    mov ax, [es:M_K]
    cmp ax, [es:M_N]
    jb .copy
    jmp fsk_link
.copy:
    ; the destination is still free (the generation check is the rest)
    mov ax, [es:M_DST]
    add ax, [es:M_K]
    call fsk_fat
    or ax, ax
    jz .free
    mov ax, FSKE_CHANGED
    jmp fsk_die
.free:
    mov cx, [es:P_SPC]
    cmp cx, FSK_CBSECS
    jbe .run
    ; --- a cluster bigger than the buffer: a buffer of it a step ---------
    mov cx, [es:P_SPC]
    sub cx, [es:M_KS]
    cmp cx, FSK_CBSECS
    jbe .bn
    mov cx, FSK_CBSECS
.bn:
    push cx
    mov ax, [es:M_SRC]
    call fsk_c2l
    add ax, [es:M_KS]
    adc dx, 0
    mov bx, FSK_CBUF
    call fsk_rd
    call fsk_dotpatch           ; a folder's copy names ITSELF
    mov ax, [es:M_DST]
    add ax, [es:M_K]
    call fsk_c2l
    add ax, [es:M_KS]
    adc dx, 0
    call fsk_wr
    pop cx
    add [es:M_KS], cx
    mov ax, [es:M_KS]
    cmp ax, [es:P_SPC]
    jb .out
    mov word [es:M_KS], 0
    inc word [es:M_K]
    mov ax, [es:M_SRC]
    call fsk_fat
    mov [es:M_SRC], ax
.out:
    ret
.run:
    ; --- a run of the source's contiguous clusters, as many as fit --------
    mov ax, FSK_CBSECS
    xor dx, dx
    div word [es:P_SPC]         ; AX = clusters a buffer holds
    mov bx, [es:M_N]
    sub bx, [es:M_K]
    cmp ax, bx
    jbe .cap
    mov ax, bx
.cap:
    xchg ax, di                 ; DI = the most this step takes
    mov ax, [es:M_SRC]
    mov cx, 1                   ; CX = clusters in the run
.ext:
    cmp cx, di
    jae .go
    push ax
    call fsk_fat
    pop dx
    inc dx
    cmp ax, dx
    jne .go
    inc cx
    jmp short .ext
.go:
    push cx
    mov ax, cx
    mul word [es:P_SPC]
    xchg ax, cx                 ; CX = sectors
    mov ax, [es:M_SRC]
    call fsk_c2l
    mov bx, FSK_CBUF
    call fsk_rd
    call fsk_dotpatch           ; a folder's copy names ITSELF
    mov ax, [es:M_DST]
    add ax, [es:M_K]
    call fsk_c2l
    call fsk_wr
    pop cx
    add [es:M_K], cx
    mov ax, [es:M_SRC]          ; the next source: past the run
    dec cx
    call fsk_nth
    call fsk_fat
    mov [es:M_SRC], ax
    ret

; fsk_dotpatch - a FOLDER's copy, its first sector in the buffer: its '.'
; names the new run, not the old (118.14.5). Keeps every register
fsk_dotpatch:
    cmp byte [es:M_DIR], 0
    je .out
    cmp word [es:M_K], 0
    jne .out
    cmp word [es:M_KS], 0
    jne .out
    cmp word [es:FSK_CBUF], '. '    ; '.' then a space
    jne .out
    push ax
    mov ax, [es:M_DST]
    mov [es:FSK_CBUF+26], ax
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; fsk_kids - the folder that just moved: every folder IN it names it as '..',
; and that '..' now names the new run (118.14.5). Its entries are read where
; they now are - the copy, DST onward - into DBUF; each child's first sector
; into CBUF. Die on an error
; -----------------------------------------------------------------------------
fsk_kids:
    mov ax, [es:M_DST]
    mov [es:P_KC], ax
.clus:
    mov word [es:P_KS], 0
.sec:
    mov ax, [es:P_KC]
    call fsk_c2l
    add ax, [es:P_KS]
    adc dx, 0
    call fsk_rd1d
    mov word [es:P_KE], 0
.ent:
    mov si, [es:P_KE]
    cmp si, 16
    jae .nsec
    inc word [es:P_KE]
    mov cl, 5
    shl si, cl
    add si, FSK_DBUF
    mov al, [es:si]
    or al, al
    jz .out                     ; the folder's end
    cmp al, 0E5h
    je .ent
    cmp al, '.'
    je .ent
    mov al, [es:si+11]
    cmp al, 0Fh
    je .ent
    test al, 10h
    jz .ent
    mov ax, [es:si+26]          ; a child folder: its '..'
    cmp ax, 2
    jb .ent
    cmp ax, [es:P_MAXC]
    ja .ent
    call fsk_c2l
    call fsk_rd1c
    cmp word [es:FSK_CBUF+32], '..'
    jne .ent
    push ax
    mov ax, [es:M_DST]
    mov [es:FSK_CBUF+32+26], ax
    pop ax
    call fsk_wr                 ; DX:AX, BX, CX: the read's
    jmp short .ent
.nsec:
    inc word [es:P_KS]
    mov ax, [es:P_KS]
    cmp ax, [es:P_SPC]
    jb .sec
    inc word [es:P_KC]
    mov ax, [es:P_KC]
    sub ax, [es:M_DST]
    cmp ax, [es:M_N]
    jb .clus
.out:
    ret

; fsk_krefs - the KERNEL's own words that name the folder's old cluster on
; this volume, made to name the new one: every instance's folder (19.2.1),
; every Disk window's, the clipboard's and the mount's. A program's own
; banked copy is out of reach - which is why folders move only with nothing
; else open (fsk_defstart)
fsk_krefs:
    mov dl, [es:P_VOL]
    mov ax, [es:M_OLD]
    mov cx, [es:M_DST]
    xor bx, bx
.inst:
    cmp dl, [inst_fdrv+bx]
    jne .in
    push bx
    shl bx, 1
    cmp ax, [inst_fcwd+bx]
    jne .ip
    mov [inst_fcwd+bx], cx
.ip:
    pop bx
.in:
    inc bx
    cmp bx, INST_MAX
    jb .inst
    mov si, inst_tab
    push cx
    mov cx, INST_MAX
.win:
    cmp byte [si+I_STATE], 1
    jne .wn
    cmp byte [si+I_KIND], KIND_FILES
    jne .wn
    mov di, [si+I_SPTR]
    cmp dl, [di+FS_DRV]
    jne .wn
    cmp ax, [di+FS_CWD]
    jne .wn
    pop bx
    push bx
    mov [di+FS_CWD], bx
.wn:
    add si, I_RECSZ
    loop .win
    pop cx
    cmp byte [fcp_cbop], 0      ; the clipboard: a Cut or Copy waiting
    je .mnt
    cmp dl, [fcp_cbdrv]
    jne .mnt
    cmp ax, [fcp_cbcwd]
    jne .mnt
    mov [fcp_cbcwd], cx
.mnt:
    cmp dl, [disk_drive]
    jne .out
    cmp ax, [dsk_cwd]
    jne .out
    mov [dsk_cwd], cx
.out:
    ret

; fsk_link - the copy is whole: new chain, then the entry, then free the old
fsk_link:
    mov ax, [es:M_DST]
    mov cx, [es:M_N]
.l:
    mov dx, ax
    inc dx
    cmp cx, 1
    jne .set
    mov dx, DSKW_EOC16
.set:
    call fsk_set
    call fsk_bs1
    inc ax
    loop .l
    call fsk_flush              ; 2. the new chain, in both copies
    cmp byte [es:M_KERN], 0
    je .entry
    ; 2a. KERNEL.SYS: the boot sector is told BEFORE the entry is, so a stop
    ; between any two writes boots: before this, the old run (intact and
    ; still allocated); after it, the new one (whole, linked)
    call fsk_rd0c
    mov ax, [es:M_OLD]
    call fsk_kofs
    cmp [es:FSK_CBUF+504], ax   ; still the sector that boots the old run
    jne .changed
    cmp [es:FSK_CBUF+506], dx
    jne .changed
    mov ax, [es:M_DST]
    call fsk_kofs
    mov [es:FSK_CBUF+504], ax
    mov [es:FSK_CBUF+506], dx
    xor ax, ax
    xor dx, dx
    call fsk_wr                 ; BX, CX: the read's
.entry:
    mov ax, [es:M_DLBA]         ; 3. the entry, read afresh
    mov dx, [es:M_DLBA+2]
    call fsk_rdlba
    mov si, FSK_DBUF
    add si, [es:M_DOFF]
    mov ax, [es:si+26]
    cmp ax, [es:M_OLD]
    je .same
.changed:
    mov ax, FSKE_CHANGED
    jmp fsk_die
.same:
    mov ax, [es:M_DST]
    mov [es:si+26], ax
    call fsk_wdir
    cmp byte [es:M_DIR], 0
    je .old
    ; 3a. A FOLDER: its children's '..', then the kernel's own words - both
    ; after the entry, so a stop here leaves '..' entries a repair rewrites
    ; (DOT) and never a folder nothing names
    call fsk_kids
    call fsk_krefs
.old:
    mov ax, [es:M_OLD]          ; 4. the old chain goes
    mov cx, [es:M_N]
    push ax
    call fsk_unmark
    pop ax
    KC dskw_free_chain
    call fsk_flush
    add word [dsk_mgen], 2      ; every READ_SEQ cursor walks again (18.4.8)
    cmp byte [es:M_DIR], 0
    je .file
    inc word [es:FSW_MOVEDF]
    call fsk_markwins           ; its cached listings name it by cluster
    jmp short .counted
.file:
    inc word [es:FSW_MOVED]
.counted:
    inc byte [es:P_DMOVED]
    and byte [es:P_FLAGS], ~PF_MOVING
    mov ax, [es:FSW_MOVED]
    mov [es:FSW_DONE], ax
    ret

modfs_end:
MODFS_SIZE equ modfs_end - $$
section .text

%endif                          ; OS88_FSCKIMG
