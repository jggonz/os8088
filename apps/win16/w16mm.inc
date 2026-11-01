; =============================================================================
; os8088 - apps/win16/w16mm.inc
;
; MMSYSTEM (SPEC.md 124.32): the clock, the devices and mmio, for the kern_mega
; arm alone - every program that imports it is a protected-mode one, so the
; real-mode build has no such module and is byte for byte what it was. In
; part 9 with the other system libraries (w16syslib.inc's header says how a
; handler here is reached): DS is the image, ES:BX the arguments.
;
; An HMMIO is a fixed global block of the program's own heap (124.32.2): the
; 66 bytes of an MMIOINFO, then WIN16's words. Its pointers are the program's
; far pointers - pchBuffer is the selector of the buffer's block, or the
; program's own buffer for a memory file - and a position is lBufOffset plus
; how far pchNext is into the buffer. A buffer is under 64 KB, so the four
; pointers share one selector and the arithmetic is on their offsets. One
; call runs at a time (the UI task's), so its working words are the bss's.
; =============================================================================

section .w16sys

MIO_FLAGS    equ 0                   ; MMIOINFO: dwFlags
MIO_FCC      equ 4                   ;   fccIOProc
MIO_ERR      equ 12                  ;   wErrorRet
MIO_CCH      equ 16                  ;   cchBuffer, a long
MIO_BUF      equ 20                  ;   pchBuffer
MIO_NEXT     equ 24                  ;   pchNext
MIO_ENDR     equ 28                  ;   pchEndRead
MIO_ENDW     equ 32                  ;   pchEndWrite
MIO_BUFOFF   equ 36                  ;   lBufOffset
MIO_DISKOFF  equ 40                  ;   lDiskOffset
MIO_HMMIO    equ 64                  ;   hmmio
MIO_LEN      equ 66
MMS_SIG     equ 66                  ; WIN16's: W16MM_SIG, so a stray number is
MMS_FD      equ 68                  ;   refused; the DOS handle
MMS_KIND    equ 70                  ; 0 a DOS file, 1 a memory file
MMS_REP     equ 71                  ; 1 the buffer is the program's to see
MMS_BUFH    equ 72                  ; the buffer block's handle, 0 none
MMS_SCR     equ 74                  ; 20 bytes: an MMCKINFO, worked on here
MMS_ZERO    equ 94                  ; a byte that stays 0: a chunk's pad
MMS_LEN     equ 96
W16MM_SIG    equ 0x4D4D
W16MM_BUF    equ 8192                ; MMIO_DEFAULTBUFFER

MMIO_RWMODE     equ 0x0003          ; dwFlags, the low word's bits
MMIO_SHARE      equ 0x0070
MMIO_PARSE      equ 0x0100
MMIO_DELETE     equ 0x0200
MMIO_CREATE     equ 0x1000
MMIO_EXIST      equ 0x4000
MMIO_ALLOCBUF   equ 0x0001          ; ...the high word's
MMIO_GETTEMP    equ 0x0002
MMIO_DIRTYH     equ 0x1000          ; MMIO_DIRTY, in a chunk's dwFlags
MMIO_WRITEADV   equ 0x0001          ; mmioAdvance's MMIO_WRITE
MMIO_FINDCHUNK  equ 0x10
MMIO_FINDRIFF   equ 0x20
MMIO_FINDLIST   equ 0x40
MMIO_CREATERIFF equ 0x20
MMIO_CREATELIST equ 0x40
MMIO_TOUPPER    equ 0x10
MMIOERR_OUTOFMEMORY     equ 258
MMIOERR_CANNOTOPEN      equ 259
MMIOERR_CANNOTREAD      equ 261
MMIOERR_CANNOTWRITE     equ 262
MMIOERR_CANNOTSEEK      equ 263
MMIOERR_CHUNKNOTFOUND   equ 265
MMIOERR_UNBUFFERED      equ 266
MMSYSERR_BADDEVICEID    equ 2
MMSYSERR_INVALHANDLE    equ 5
W16MM_RIFF   equ 'RI'                ; the two FOURCCs Descend reads a form
W16MM_LIST   equ 'LI'                ;   type after: their first two letters,
W16MM_FF     equ 'FF'                ;   and the last two of each
W16MM_ST     equ 'ST'
W16MM_MEM    equ 'ME'                ; FOURCC_MEM, 'MEM '
W16MM_MEM2   equ 'M '
W16MM_DOS    equ 'DO'                ; FOURCC_DOS, 'DOS '
W16MM_DOS2   equ 'S '

; =============================================================================
; The module, the clock and the devices (124.32.1)
; =============================================================================

; mmsystemGetVersion (MMSYSTEM.5): 3.10
W16SYS w16mm_ver
    mov ax, 0x030A
    xor dx, dx
    ret

; timeGetTime (MMSYSTEM.607): DX:AX = milliseconds
W16SYS w16mm_time
    call w16mm_now
    ret

; timeGetSystemTime (MMSYSTEM.601) - args: +0 wSize, +2 lpTime: an MMTIME,
; TIME_MS. 0
W16SYS w16mm_systime
    push di
    ICALL w16_args
    cmp word [es:bx], 6
    jb .out
    les di, [es:bx + 2]
    call w16mm_now
    mov word [es:di], 1             ; TIME_MS
    mov [es:di + 2], ax
    mov [es:di + 4], dx
.out:
    pop di
    xor ax, ax
    xor dx, dx
    ret

; timeGetDevCaps (MMSYSTEM.604) - args: +0 wSize, +2 lpCaps: 1 to 65535 ms.
; 0, or TIMERR_STRUCT (129) for a struct too small
W16SYS w16mm_tcaps
    push di
    ICALL w16_args
    mov ax, 129
    cmp word [es:bx], 4
    jb .out
    les di, [es:bx + 2]
    mov word [es:di], 1
    mov word [es:di + 2], 0xFFFF
    xor ax, ax
.out:
    pop di
    xor dx, dx
    ret

; w16mm_now - DX:AX = milliseconds from GetTickCount's origin: the BIOS tick
; times 55, and the PIT's counter 0 for the part of this tick that has
; passed. apps/os88pit.inc's read - the tick before and after, and IRQ0
; pending means the tick is owed - with the tick whole, a dword. Two
; differences, because kern_mega's IRQ0 can wait longer than half a tick to
; be taken: the tick is owed unless the count is in the period's last 3 ms
; (where the latch can have come just before the reload), and the answer
; never goes below the last one. Keeps the rest
w16mm_now:
    push bx
    push cx
    push si
    push es
    mov cx, 0x40                    ; the BDA: an identity selector on kern_mega
    mov es, cx
.again:
    mov si, [es:0x6C]
    mov bx, [es:0x6E]
    pushf
    cli
    mov al, 0
    out 0x43, al                    ; latch counter 0
    jmp short $+2
    in al, 0x40
    mov cl, al
    jmp short $+2
    in al, 0x40
    mov ch, al
    mov al, 0x0A                    ; OCW3: the IRR
    out 0x20, al
    jmp short $+2
    in al, 0x20
    popf
    cmp si, [es:0x6C]
    jne .again
    neg cx                          ; counts into the tick
    test al, 1
    jz .co
    cmp cx, 0xF000
    jae .co
    add si, 1                       ; the tick that is owed
    adc bx, 0
.co:
    mov ax, cx
    xor dx, dx
    mov cx, 1193                    ; counts a millisecond
    div cx                          ; AX = 0..54
    push ax
    mov cx, 55
    mov ax, bx
    mul cx
    mov bx, ax                      ; the high word's, modulo 2^32
    mov ax, si
    mul cx
    add dx, bx
    pop cx
    add ax, cx
    adc dx, 0
    cmp dx, [w16mm_last + 2]        ; never before the last answer
    jne .cmp
    cmp ax, [w16mm_last]
.cmp:
    jae .new
    mov ax, [w16mm_last]
    mov dx, [w16mm_last + 2]
.new:
    mov [w16mm_last], ax
    mov [w16mm_last + 2], dx
    pop es
    pop si
    pop cx
    pop bx
    ret

; timeBeginPeriod (MMSYSTEM.605), timeEndPeriod (606): the PIT is the
; kernel's. TIMERR_NOERROR
W16SYS w16mm_period
    xor ax, ax
    xor dx, dx
    ret

; waveOutGetNumDevs (MMSYSTEM.401): 1 with a stream to play into
W16SYS w16mm_wonum
    mov dx, SND_CAP_PCM_BG
    jmp short w16mm_ndevs

; waveInGetNumDevs (MMSYSTEM.501): 1 with one to record from
W16SYS w16mm_winum
    mov dx, SND_CAP_PCM_IN
w16mm_ndevs:
    push dx
    call OSAPI_SND_CAPS             ; AX = the caps word (BX and DX its too)
    pop dx
    test ax, dx
    mov ax, 0
    jz .out
    inc ax
.out:
    xor dx, dx
    ret

; waveOutGetDevCaps (MMSYSTEM.402) - args: +0 wSize, +2 lpCaps, +6 wDeviceID:
; a WAVEOUTCAPS, 48 bytes. waveInGetDevCaps (502): a WAVEINCAPS, 44. 0, or
; MMSYSERR_BADDEVICEID
W16SYS w16mm_wocaps
    mov dx, SND_CAP_PCM_BG | (48 << 8)
    jmp short w16mm_caps
W16SYS w16mm_wicaps
    mov dx, SND_CAP_PCM_IN | (44 << 8)
w16mm_caps:
    push si
    push di
    push dx                         ; [the bit it needs, the struct's size]
    call OSAPI_SND_CAPS             ; AX = the caps word (BX and DX its too)
    mov si, ax                      ; SI = the caps
    ICALL w16_args
    pop dx
    test al, dl
    mov ax, MMSYSERR_BADDEVICEID
    jz .out
    cmp word [es:bx + 6], 0
    jne .out
    mov cx, [es:bx]                 ; CX = what the program has room for
    mov al, dh
    xor ah, ah
    cmp cx, ax
    jbe .sz
    mov cx, ax
.sz:
    les di, [es:bx + 2]
    push cx
    push di
    xor al, al                      ; the struct zero, then filled
    cld
    rep stosb
    pop di
    pop cx
    cmp cx, 38                      ; wMid, wPid, vDriverVersion, szPname
    jb .ok
    mov word [es:di], 1             ; MM_MICROSOFT and its generic ids: the
    mov word [es:di + 2], 2         ;   name says whose it is
    mov word [es:di + 4], 0x0100
    push si
    push di
    add di, 6
    mov si, w16mm_s_dev              ; XSEC-OK: the part's string, read at cs:
.nm:
    cs lodsb
    stosb
    or al, al
    jnz .nm
    pop di
    pop si
    cmp cx, 44                      ; dwFormats and wChannels
    jb .ok
    mov ax, 0x0011                  ; 8-bit mono at 11 and 22 kHz
    mov dx, 1
    test si, SND_CAP_PCM_HI
    jz .f1
    or ax, 0x0100                   ; ...and 44
.f1:
    test si, SND_CAP_STEREO
    jz .f2
    or ax, 0x0022                   ; stereo, 11 and 22
    inc dx
.f2:
    test si, SND_CAP_PCM16
    jz .f3
    or ax, 0x0044                   ; 16-bit mono, 11 and 22
    test si, SND_CAP_STEREO
    jz .f3
    or ax, 0x0088                   ; ...and stereo
.f3:
    mov [es:di + 38], ax
    mov [es:di + 42], dx
.ok:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

w16mm_s_dev: db 'os8088 Sound', 0

; mciGetErrorString (MMSYSTEM.706) - args: +0 wLength, +2 lpstrBuffer,
; +6 dwError: its sentence, cut to the buffer. TRUE for one the arm answers
W16SYS w16mm_mcierr
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx]
    jcxz .no
    mov ax, [es:bx + 6]
    les di, [es:bx + 2]
    mov si, w16mm_s_mcix             ; XSEC-OK: the part's strings, at cs:
    xor dx, dx                      ; DX = the answer
    or ax, ax
    jz .gen
    sub ax, 256                     ; MCIERR_BASE
    cmp ax, 2
    jae .gen
    inc dx
    mov si, w16mm_s_mci0             ; XSEC-OK
    or ax, ax
    jz .cp
    mov si, w16mm_s_mci1             ; XSEC-OK
    jmp short .cp
.gen:
    or ax, ax                       ; 0 is no error at all
    jnz .cp
    inc dx
    mov si, w16mm_s_ok               ; XSEC-OK
.cp:
    dec cx                          ; room for the NUL
    jcxz .nul
.c:
    cs lodsb
    or al, al
    jz .nul
    stosb
    loop .c
.nul:
    mov byte [es:di], 0
    mov ax, dx
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

w16mm_s_ok:   db 'The specified command was carried out.', 0
w16mm_s_mci0: db 'The device is not ready.', 0
w16mm_s_mci1: db 'Undefined external error.', 0
w16mm_s_mcix: db 'The device reported an error.', 0

; =============================================================================
; mmio (124.32.2)
; =============================================================================

; mmioStringToFOURCC (MMSYSTEM.1220) - args: +0 wFlags, +2 sz: four
; characters, spaces after a short name; upper case with MMIO_TOUPPER
W16SYS w16mm_fourcc
    push si
    push di
    ICALL w16_args
    mov dl, [es:bx]
    mov si, [es:bx + 2]
    mov ax, [es:bx + 4]
    push ds
    push ds
    pop es                          ; ES:DI = the bss's four
    mov di, w16mm_ck4
    mov ds, ax                      ; DS:SI = the name
    mov cx, 4
.c:
    lodsb
    or al, al
    jnz .ch
    dec si                          ; the NUL again, and spaces from here
    mov al, ' '
.ch:
    test dl, MMIO_TOUPPER
    jz .st
    cmp al, 'a'
    jb .st
    cmp al, 'z'
    ja .st
    sub al, 0x20
.st:
    stosb
    loop .c
    pop ds
    mov ax, [w16mm_ck4]
    mov dx, [w16mm_ck4 + 2]
    pop di
    pop si
    ret

; mmioOpen (MMSYSTEM.1210) - args: +0 dwOpenFlags, +4 lpmmioinfo, +8
; szFilename: an HMMIO, or 0 with lpmmioinfo's wErrorRet saying why.
; MMIO_DELETE and MMIO_EXIST answer TRUE or FALSE instead (124.32.2)
W16SYS w16mm_open
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16mm_rem], ax              ; [w16mm_rem] = the flags while it opens,
    mov ax, [es:bx + 2]             ;   [w16mm_ock] the program's MMIOINFO
    mov [w16mm_rem + 2], ax          ;   and [w16mm_lim] the name
    les ax, [es:bx + 4]
    mov [w16mm_ock], ax
    mov [w16mm_ock + 2], es
    ICALL w16_args
    les ax, [es:bx + 8]
    mov [w16mm_lim], ax
    mov [w16mm_lim + 2], es
    mov bp, MMIOERR_CANNOTOPEN      ; BP = the error until it opens
    test word [w16mm_rem], MMIO_PARSE
    jnz .refuse
    test word [w16mm_rem + 2], MMIO_GETTEMP
    jnz .refuse
    test word [w16mm_rem], MMIO_DELETE
    jz .exist
    mov ah, 0x41                    ; DELETE: TRUE when the DOS did
    call w16mm_named
    jmp short .tf
.exist:
    test word [w16mm_rem], MMIO_EXIST
    jz .make
    mov ax, 0x3D00                  ; EXIST: TRUE when it opens
    call w16mm_named
    jc .tf
    mov bx, ax
    mov ah, 0x3E
    ICALL w16_dcall
    clc
.tf:
    mov ax, 0
    jc .ret
    inc ax
.ret:
    jmp .out

.make:
    mov bp, MMIOERR_OUTOFMEMORY
    mov ax, MMS_LEN                 ; the state
    xor dx, dx
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; AX = the handle, the block's paragraph
    or ax, ax
    jz .refuse
    mov si, ax                      ; SI = the HMMIO from here
    W16SEG es, dx
    mov word [es:MMS_SIG], W16MM_SIG
    mov [es:MIO_HMMIO], si
    mov word [es:MMS_FD], 0xFFFF
    mov ax, [w16mm_ock]              ; the program's MMIOINFO in, when it
    or ax, [w16mm_ock + 2]           ;   gave one: its IO procedure, its
    jz .noinfo                      ;   buffer and their size
    push ds
    push si
    xor di, di
    lds si, [w16mm_ock]
    mov cx, MIO_LEN / 2
    cld
    rep movsw
    pop si
    pop ds
.noinfo:
    mov ax, [w16mm_rem]
    mov [es:MIO_FLAGS], ax
    mov ax, [w16mm_rem + 2]
    mov [es:MIO_FLAGS + 2], ax
    mov word [es:MIO_ERR], 0
    mov bp, MMIOERR_CANNOTOPEN
    mov ax, [w16mm_lim]              ; no name, or FOURCC_MEM: a memory file
    or ax, [w16mm_lim + 2]
    jz .mem
    cmp word [es:MIO_FCC], W16MM_MEM
    jne .notmem
    cmp word [es:MIO_FCC + 2], W16MM_MEM2
    je .mem
.notmem:
    mov ax, [es:MIO_FCC]             ; ...or no procedure, or the DOS one
    or ax, [es:MIO_FCC + 2]
    jz .dos
    cmp word [es:MIO_FCC], W16MM_DOS
    jne .drop
    cmp word [es:MIO_FCC + 2], W16MM_DOS2
    jne .drop
.dos:
    mov ah, 0x3C                    ; CREATE, or the access and the sharing
    xor cx, cx                      ;   it asked for
    test word [w16mm_rem], MMIO_CREATE
    jnz .dop
    mov ax, [w16mm_rem]
    and al, MMIO_RWMODE | MMIO_SHARE
    mov ah, 0x3D
.dop:
    call w16mm_named                 ; AX = the DOS handle
    jc .drop
    mov [es:MMS_FD], ax
    mov bp, MMIOERR_OUTOFMEMORY
    mov ax, W16MM_BUF                ; the buffer: its size the program's
    test word [w16mm_rem + 2], MMIO_ALLOCBUF
    jz .bsz                         ;   when it asked for one of a size
    mov byte [es:MMS_REP], 1
    cmp word [es:MIO_CCH + 2], 0
    jne .bsz
    mov cx, [es:MIO_CCH]
    jcxz .bsz
    cmp cx, 0x8000
    ja .bsz
    mov ax, cx
.bsz:
    mov [es:MIO_CCH], ax
    mov word [es:MIO_CCH + 2], 0
    push es
    xor dx, dx
    xor cl, cl                      ; GMEM_FIXED
    ICALL w16_gnew                  ; AX = its handle, DX = its paragraph
    pop es
    or ax, ax
    jz .drop
    mov [es:MMS_BUFH], ax
    mov ax, dx
    push es
    ICALL w16_pblk                  ; AX = its selector, the program's too
    pop es
    mov word [es:MIO_BUF], 0
    mov [es:MIO_BUF + 2], ax
    mov [es:MIO_NEXT + 2], ax
    mov [es:MIO_ENDR + 2], ax
    mov [es:MIO_ENDW + 2], ax
    xor ax, ax
    xor dx, dx
    call w16mm_empty                 ; at its start
    jmp short .opened
.mem:
    mov byte [es:MMS_KIND], 1       ; the program's buffer is the file, read
    mov byte [es:MMS_REP], 1        ;   and written in place, all of it
    cmp word [es:MIO_CCH + 2], 0
    jne .drop
    mov ax, [es:MIO_BUF]
    mov dx, [es:MIO_BUF + 2]
    or dx, dx
    jz .drop
    mov [es:MIO_NEXT], ax
    mov [es:MIO_NEXT + 2], dx
    mov [es:MIO_ENDR + 2], dx
    mov [es:MIO_ENDW + 2], dx
    add ax, [es:MIO_CCH]
    jc .drop
    mov [es:MIO_ENDR], ax
    mov [es:MIO_ENDW], ax
    mov word [es:MIO_BUFOFF], 0
    mov word [es:MIO_BUFOFF + 2], 0
.opened:
    mov ax, si
    jmp short .out
.drop:
    mov ax, si                      ; the state given back, and the file
    call w16mm_free
.refuse:
    mov ax, [w16mm_ock]              ; wErrorRet, when there is somewhere
    or ax, [w16mm_ock + 2]           ;   to say it
    jz .zero
    les di, [w16mm_ock]
    mov [es:di + MIO_ERR], bp
.zero:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; w16mm_named - AX as int 21h takes it, CX too, with DS:DX = the name at
; [w16mm_lim]: WIN16's DOS asked. out as it answers. Keeps ES
w16mm_named:
    push es
    mov dx, [w16mm_lim]
    mov es, [w16mm_lim + 2]
    ICALL w16_dcall
    pop es
    ret

; w16mm_free - AX = an HMMIO, ES = its state: its file closed, its buffer and
; its state given back. Keeps all but ES
w16mm_free:
    push ax
    push bx
    mov bx, [es:MMS_FD]
    cmp bx, 0xFFFF
    je .nofd
    push ax
    mov ah, 0x3E
    ICALL w16_dcall
    pop ax
.nofd:
    push ax
    mov ax, [es:MMS_BUFH]
    or ax, ax
    jz .nobuf
    ICALL w16_gfreeh
.nobuf:
    pop ax
    mov word [es:MMS_SIG], 0        ; never an HMMIO again
    ICALL w16_gfreeh
    pop bx
    pop ax
    ret

; w16mm_st - AX = an HMMIO -> ES = its state, CF=0; CF=1 not one. Keeps all
; but ES
w16mm_st:
    push si
    or ax, ax
    jz .no
    ICALL w16_gfind                 ; ES = its header; CF=1 not ours
    jc .no
    W16SEG es, ax                   ; a fixed block: its handle is its place
    cmp word [es:MMS_SIG], W16MM_SIG
    jne .no
    clc
    pop si
    ret
.no:
    stc
    pop si
    ret

; mmioClose (MMSYSTEM.1211) - args: +0 wFlags, +2 hmmio: 0
W16SYS w16mm_close
    ICALL w16_args
    mov ax, [es:bx + 2]
    call w16mm_st
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    mov ax, [es:MIO_HMMIO]
    call w16mm_free
    xor ax, ax
.out:
    xor dx, dx
    ret

; mmioFlush (MMSYSTEM.1218): a write is never held, so nothing is waiting. 0
W16SYS w16mm_flush
    xor ax, ax
    xor dx, dx
    ret

; mmioRead (MMSYSTEM.1212) - args: +0 cch, +4 pch, +8 hmmio: DX:AX = the
; bytes read, 0 at the end, -1 refused. mmioWrite (1213) the same, written
W16SYS w16mm_read
    xor cl, cl
    jmp short w16mm_rw
W16SYS w16mm_write
    mov cl, 1
w16mm_rw:
    push si
    push di
    push cx
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16mm_rem], ax
    mov ax, [es:bx + 2]
    mov [w16mm_rem + 2], ax
    mov ax, [es:bx + 4]
    mov [w16mm_boff], ax
    mov ax, [es:bx + 6]
    mov [w16mm_bsel], ax
    mov ax, [es:bx + 8]
    pop cx                          ; CL = which
    call w16mm_st
    jc .bad
    call w16mm_xfer
    jnc .got
    mov ax, [w16mm_done]             ; refused part way: what was moved
    or ax, [w16mm_done + 2]
    jz .bad
.got:
    mov ax, [w16mm_done]
    mov dx, [w16mm_done + 2]
    jmp short .out
.bad:
    mov ax, -1
    cwd
.out:
    pop di
    pop si
    ret

; w16mm_xfer - CL = 0 read, 1 write: [w16mm_rem] bytes between the file at its
; position and the program at [w16mm_bsel]:[w16mm_boff], [w16mm_done] set to
; what moved. A memory file is its buffer alone; a DOS file is read from the
; buffer first and then from the DOS, and written to the DOS. CF=1 the DOS
; refused. ES = the state. Keeps all but AX
w16mm_xfer:
    push bx
    push dx
    xor ax, ax
    mov [w16mm_done], ax
    mov [w16mm_done + 2], ax
    mov bl, cl
    cmp byte [es:MMS_KIND], 0
    jne .mem
    or bl, bl
    jnz .dw
    call w16mm_bio                   ; what the buffer holds, then the rest
    mov ax, [w16mm_rem]
    or ax, [w16mm_rem + 2]
    jz .ok
    mov ax, [es:MIO_ENDR]            ; ...from after it
    sub ax, [es:MIO_BUF]
    xor dx, dx
    add ax, [es:MIO_BUFOFF]
    adc dx, [es:MIO_BUFOFF + 2]
    mov bl, 0x3F
    call w16mm_dio
    jmp short .out
.dw:
    call w16mm_pos                   ; written where it stands
    mov bl, 0x40
    call w16mm_dio
    jmp short .out
.mem:
    call w16mm_bio
.ok:
    clc
.out:
    pop dx
    pop bx
    ret

; w16mm_pos - ES = the state -> DX:AX = its position. Keeps the rest
w16mm_pos:
    mov ax, [es:MIO_NEXT]
    sub ax, [es:MIO_BUF]
    xor dx, dx
    add ax, [es:MIO_BUFOFF]
    adc dx, [es:MIO_BUFOFF + 2]
    ret

; w16mm_empty - DX:AX = a position in a DOS file: the buffer empty there, with
; room to write in it when the file is open to be written. ES = the state.
; Keeps all
w16mm_empty:
    push cx
    mov [es:MIO_BUFOFF], ax
    mov [es:MIO_BUFOFF + 2], dx
    mov cx, [es:MIO_BUF]
    mov [es:MIO_NEXT], cx
    mov [es:MIO_ENDR], cx
    test byte [es:MIO_FLAGS], MMIO_RWMODE
    jz .w
    add cx, [es:MIO_CCH]
.w:
    mov [es:MIO_ENDW], cx
    pop cx
    ret

; w16mm_goto - DX:AX = a position (not below 0): the file there. Inside a
; DOS file's buffer it is pchNext moved; past it the buffer is emptied there.
; A memory file goes no further than its end. CF=1 refused. ES = the state.
; Keeps all
w16mm_goto:
    push ax
    push cx
    push dx
    or dx, dx
    js .no
    cmp byte [es:MMS_KIND], 0
    jne .mem
    sub ax, [es:MIO_BUFOFF]          ; how far into the buffer
    sbb dx, [es:MIO_BUFOFF + 2]
    jb .out_                        ; before it
    jnz .out_
    mov cx, [es:MIO_ENDR]
    sub cx, [es:MIO_BUF]
    cmp ax, cx
    ja .out_                        ; after what it holds
    add ax, [es:MIO_BUF]
    mov [es:MIO_NEXT], ax
    jmp short .ok
.out_:
    pop dx
    pop cx
    pop ax
    call w16mm_empty
    clc
    ret
.mem:
    or dx, dx
    jnz .no
    cmp ax, [es:MIO_CCH]
    ja .no
    add ax, [es:MIO_BUF]
    mov [es:MIO_NEXT], ax
.ok:
    clc
    jmp short .r
.no:
    stc
.r:
    pop dx
    pop cx
    pop ax
    ret

; w16mm_dio - BL = 3Fh read or 40h write, DX:AX = a position in the DOS file:
; [w16mm_rem] bytes between it and the program at [w16mm_bsel]:[w16mm_boff], in
; pieces that do not cross the program's 64 KB - its selector stepped by
; __AHINCR, 8 (124.31.6), at each - [w16mm_done] adding them up. A short read
; is the file's end. The buffer is emptied where it stops. CF=1 the DOS
; refused. ES = the state. Keeps all
w16mm_dio:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov bp, bx                      ; BP = the function, in its low byte
    mov si, ax                      ; DX:SI = where it is
    push es
    mov bx, [es:MMS_FD]
    mov cx, dx
    mov dx, ax
    mov ax, 0x4200
    ICALL w16_dcall
    pop es
    mov dx, cx
    jc .out
.lp:
    mov cx, [w16mm_rem]
    or cx, [w16mm_rem + 2]
    jz .end
    mov cx, 0x8000                  ; a piece: 32 KB at most...
    mov ax, [w16mm_boff]
    neg ax                          ; ...short of the program's 64 KB...
    jz .p1
    cmp ax, cx
    jae .p1
    mov cx, ax
.p1:
    cmp word [w16mm_rem + 2], 0      ; ...and no more than is left
    jne .p2
    cmp [w16mm_rem], cx
    jae .p2
    mov cx, [w16mm_rem]
.p2:
    push dx
    push es
    mov ax, bp
    mov ah, al
    mov bx, [es:MMS_FD]
    mov dx, [w16mm_boff]
    mov es, [w16mm_bsel]
    ICALL w16_dcall                 ; AX = moved
    pop es
    pop dx
    jc .out
    add si, ax
    adc dx, 0
    add [w16mm_done], ax
    adc word [w16mm_done + 2], 0
    sub [w16mm_rem], ax
    sbb word [w16mm_rem + 2], 0
    add [w16mm_boff], ax
    jnc .same
    add word [w16mm_bsel], 8         ; __AHINCR
.same:
    cmp ax, cx
    je .lp
.end:
    mov ax, si
    call w16mm_empty
    clc
.out:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16mm_bio - BL = 0 from the buffer to the program, 1 the other way: as much
; of [w16mm_rem] as lies between pchNext and pchEndRead (reading) or
; pchEndWrite (writing), pchNext moved on and [w16mm_done] adding it up. ES =
; the state. Keeps all
w16mm_bio:
    push ax
    push cx
    push dx
    push si
    push di
.lp:
    mov cx, [es:MIO_ENDR]
    or bl, bl
    jz .e
    mov cx, [es:MIO_ENDW]
.e:
    sub cx, [es:MIO_NEXT]            ; CX = what the buffer has, or its room
    jbe .out
    cmp word [w16mm_rem + 2], 0
    jne .r
    cmp [w16mm_rem], cx
    jae .r
    mov cx, [w16mm_rem]
.r:
    jcxz .out
    mov ax, [w16mm_boff]             ; within the program's 64 KB
    neg ax
    jz .b
    cmp ax, cx
    jae .b
    mov cx, ax
.b:
    push ds
    push es
    mov si, [es:MIO_NEXT]
    mov dx, [es:MIO_BUF + 2]         ; DX = the buffer's selector
    mov di, [w16mm_boff]
    mov ax, [w16mm_bsel]
    or bl, bl
    jnz .w
    mov ds, dx
    mov es, ax
    jmp short .mv
.w:
    xchg si, di
    mov ds, ax
    mov es, dx
.mv:
    push cx
    cld
    rep movsb
    pop cx
    pop es
    pop ds
    add [es:MIO_NEXT], cx
    add [w16mm_done], cx
    adc word [w16mm_done + 2], 0
    sub [w16mm_rem], cx
    sbb word [w16mm_rem + 2], 0
    add [w16mm_boff], cx
    jnc .lp
    add word [w16mm_bsel], 8         ; __AHINCR
    jmp .lp
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mmioSeek (MMSYSTEM.1214) - args: +0 iOrigin, +2 lOffset, +6 hmmio: DX:AX =
; the new position, -1 refused
W16SYS w16mm_seek
    ICALL w16_args
    mov cx, [es:bx]
    push word [es:bx + 2]
    push word [es:bx + 4]
    mov ax, [es:bx + 6]
    call w16mm_st
    jc .bad2
    xor ax, ax                      ; SEEK_SET: from 0
    xor dx, dx
    jcxz .add
    dec cx
    jnz .end
    call w16mm_pos                   ; SEEK_CUR: from here
    jmp short .add
.end:
    cmp byte [es:MMS_KIND], 0       ; SEEK_END: from its length
    jne .mend
    push es
    push cx
    mov bx, [es:MMS_FD]
    xor cx, cx
    xor dx, dx
    mov ax, 0x4202
    ICALL w16_dcall                 ; DX:AX = the length
    pop cx
    pop es
    jc .bad2
    jmp short .add
.mend:
    mov ax, [es:MIO_CCH]
    xor dx, dx
.add:
    pop cx                          ; the offset's high word
    pop bx                          ;   ...and its low
    add ax, bx
    adc dx, cx
    call w16mm_goto
    jnc .out
    jmp short .bad
.bad2:
    pop cx
    pop cx
.bad:
    mov ax, -1
    cwd
.out:
    ret

; mmioGetInfo (MMSYSTEM.1215) - args: +0 wFlags, +2 lpmmioinfo, +6 hmmio:
; the MMIOINFO copied out - with no buffer in it for a file the program did
; not ask to be buffered. 0
W16SYS w16mm_ginfo
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 6]
    les di, [es:bx + 2]
    push es
    call w16mm_st
    mov dx, es
    pop es
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    call w16mm_cpout
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; w16mm_cpout - DX = the state, ES:DI = the program's MMIOINFO: copied out,
; the buffer hidden unless it is the program's to see. Keeps all but SI
w16mm_cpout:
    push ax
    push cx
    push di
    push ds
    mov ds, dx
    xor si, si
    mov cx, MIO_LEN / 2
    cld
    rep movsw
    cmp byte [MMS_REP], 0
    pop ds
    pop di
    jne .seen
    push di
    add di, MIO_CCH                  ; cchBuffer and the four pointers: 0
    xor ax, ax
    mov cx, (MIO_BUFOFF - MIO_CCH) / 2
    rep stosw
    pop di
.seen:
    pop cx
    pop ax
    ret

; mmioSetInfo (MMSYSTEM.1216) - args: +0 wFlags, +2 lpmmioinfo, +6 hmmio:
; pchNext, pchEndRead and pchEndWrite taken back, as the program moved them
; through a buffer it was given. 0
W16SYS w16mm_sinfo
    ICALL w16_args
    mov ax, [es:bx + 6]
    push es
    push bx
    call w16mm_st
    pop bx
    pop dx                          ; DX:BX -> the arguments
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    call w16mm_cpin
    xor ax, ax
.out:
    xor dx, dx
    ret

; w16mm_cpin - DX:BX = the arguments (+2 lpmmioinfo, or 0), ES = the state:
; the program's three pointers taken back when there is an MMIOINFO and the
; buffer is the program's to see. Keeps all
w16mm_cpin:
    push ax
    push si
    push ds
    mov ds, dx
    lds si, [bx + 2]
    mov ax, ds
    or ax, ax
    jz .out
    cmp byte [es:MMS_REP], 0
    je .out
    mov ax, [si + MIO_NEXT]
    mov [es:MIO_NEXT], ax
    mov ax, [si + MIO_ENDR]
    mov [es:MIO_ENDR], ax
    mov ax, [si + MIO_ENDW]
    mov [es:MIO_ENDW], ax
.out:
    pop ds
    pop si
    pop ax
    ret

; mmioAdvance (MMSYSTEM.1219) - args: +0 wFlags, +2 lpmmioinfo, +6 hmmio:
; the buffer written out with MMIO_WRITE (pchBuffer to pchNext, at
; lBufOffset) and otherwise filled with what follows it, and the MMIOINFO
; copied out again. 0, MMIOERR_UNBUFFERED, or the read's or the write's
; refusal. A memory file is all in its buffer: there is no more of it
W16SYS w16mm_adv
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 6]
    mov cl, [es:bx]
    push es
    push bx
    call w16mm_st
    pop bx
    pop dx                          ; DX:BX -> the arguments
    mov ax, MMSYSERR_INVALHANDLE
    jc .ret
    mov ax, MMIOERR_UNBUFFERED
    cmp byte [es:MMS_REP], 0
    je .ret
    call w16mm_cpin
    cmp byte [es:MMS_KIND], 0
    jne .info
    push dx
    push bx
    mov ax, [es:MIO_BUF]             ; the program's end of the move is the
    mov [w16mm_boff], ax             ;   buffer itself
    mov ax, [es:MIO_BUF + 2]
    mov [w16mm_bsel], ax
    xor ax, ax
    mov [w16mm_done], ax
    mov [w16mm_done + 2], ax
    mov [w16mm_rem + 2], ax
    test cl, MMIO_WRITEADV
    jz .rd
    mov ax, [es:MIO_NEXT]            ; WRITE: what the program put there
    sub ax, [es:MIO_BUF]
    mov [w16mm_rem], ax
    mov ax, [es:MIO_BUFOFF]
    mov dx, [es:MIO_BUFOFF + 2]
    mov bl, 0x40
    call w16mm_dio                   ; written, the buffer emptied after it
    mov ax, MMIOERR_CANNOTWRITE
    jmp short .done
.rd:
    mov ax, [es:MIO_ENDR]            ; READ: what follows the buffer
    sub ax, [es:MIO_BUF]
    xor dx, dx
    add ax, [es:MIO_BUFOFF]
    adc dx, [es:MIO_BUFOFF + 2]
    mov cx, [es:MIO_CCH]
    mov [w16mm_rem], cx
    mov bl, 0x3F
    call w16mm_dio                   ; (which empties it where it stops)
    pushf
    call w16mm_empty                 ; ...so: at where it was read from,
    mov cx, [es:MIO_BUF]             ;   holding what came
    add cx, [w16mm_done]
    mov [es:MIO_ENDR], cx
    popf
    mov ax, MMIOERR_CANNOTREAD
.done:
    pop bx
    pop dx
    jc .ret
.info:
    mov ax, [es:MIO_ENDR]            ; lDiskOffset: where the DOS stands
    sub ax, [es:MIO_BUF]
    xor cx, cx
    add ax, [es:MIO_BUFOFF]
    adc cx, [es:MIO_BUFOFF + 2]
    mov [es:MIO_DISKOFF], ax
    mov [es:MIO_DISKOFF + 2], cx
    mov ax, es                      ; AX = the state
    push ds
    mov ds, dx
    les di, [bx + 2]                ; ES:DI = the program's MMIOINFO, or 0
    pop ds
    mov dx, es
    or dx, dx
    jz .zero
    mov dx, ax
    call w16mm_cpout
.zero:
    xor ax, ax
.ret:
    xor dx, dx
    pop di
    pop si
    ret

; w16mm_ckin - the program's MMCKINFO at [w16mm_ock] into the state's
; scratch. ES = the state. Keeps all
w16mm_ckin:
    push cx
    push si
    push di
    push ds
    mov di, MMS_SCR
    lds si, [w16mm_ock]
    mov cx, 10
    cld
    rep movsw
    pop ds
    pop di
    pop si
    pop cx
    ret

; w16mm_ckout - the scratch back out to the program's MMCKINFO. ES = the
; state. Keeps all
w16mm_ckout:
    push cx
    push si
    push di
    push ds
    push es
    mov si, MMS_SCR
    mov cx, es                      ; CX = the state
    les di, [w16mm_ock]
    mov ds, cx
    mov cx, 10
    cld
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    ret

; w16mm_scr / w16mm_scw - AX = bytes, DX = where in the state: read from the
; file there, or written to it from there. CF=1 fewer moved. ES = the
; state. Keeps all
w16mm_scw:
    push cx
    mov cl, 1
    jmp short w16mm_sc
w16mm_scr:
    push cx
    xor cl, cl
w16mm_sc:
    push ax
    mov [w16mm_rem], ax
    mov word [w16mm_rem + 2], 0
    mov [w16mm_boff], dx
    mov [w16mm_bsel], es
    call w16mm_xfer
    pop ax
    pop cx
    jc .r
    cmp [w16mm_done], ax
    je .r                           ; (equal: CF = 0)
    stc
.r:
    ret

; w16mm_ckat - AX = where a far MMCKINFO is in the arguments, DX = where the
; HMMIO is: the chunk's pointer banked in [w16mm_ock], CL = the flags (+0)
; and ES = the state. CF=1 not an HMMIO. Clobbers AX, BX, DX
w16mm_ckat:
    ICALL w16_args
    mov cl, [es:bx]
    push bx
    add bx, ax
    les ax, [es:bx]
    mov [w16mm_ock], ax
    mov [w16mm_ock + 2], es
    ICALL w16_args
    pop bx
    add bx, dx
    mov ax, [es:bx]
    call w16mm_st
    ret

; mmioDescend (MMSYSTEM.1223) - args: +0 wFlags, +2 lpckParent, +6 lpck,
; +10 hmmio: the next chunk - or the first that MMIO_FINDCHUNK,
; MMIO_FINDRIFF or MMIO_FINDLIST asks for - inside the parent's data, lpck
; filled and the file at its data (past the form type of a RIFF or a LIST).
; 0, or MMIOERR_CHUNKNOTFOUND
W16SYS w16mm_desc
    push si
    push di
    ICALL w16_args
    mov ax, 0xFFFF                  ; the parent's end, or none
    mov [w16mm_lim], ax
    mov [w16mm_lim + 2], ax
    les di, [es:bx + 2]
    mov ax, es
    or ax, ax
    jz .nop
    mov ax, [es:di + 12]            ; its dwDataOffset + cksize
    mov dx, [es:di + 14]
    add ax, [es:di + 4]
    adc dx, [es:di + 6]
    mov [w16mm_lim], ax
    mov [w16mm_lim + 2], dx
.nop:
    mov ax, 6
    mov dx, 10
    call w16mm_ckat
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    call w16mm_ckin                  ; what it looks for: its id and type
    mov ax, [es:MMS_SCR]
    mov [w16mm_ck4], ax
    mov ax, [es:MMS_SCR + 2]
    mov [w16mm_ck4 + 2], ax
    mov ax, [es:MMS_SCR + 8]
    mov [w16mm_ckt], ax
    mov ax, [es:MMS_SCR + 10]
    mov [w16mm_ckt + 2], ax
.next:
    call w16mm_pos                   ; DX:AX = where this chunk starts
    push dx
    push ax
    add ax, 8
    adc dx, 0
    cmp dx, [w16mm_lim + 2]          ; its head must be inside the parent
    ja .nf
    jb .rd
    cmp ax, [w16mm_lim]
    ja .nf
.rd:
    xor ax, ax                      ; (no form type unless one is read)
    mov [es:MMS_SCR + 8], ax
    mov [es:MMS_SCR + 10], ax
    mov ax, 8
    mov dx, MMS_SCR
    call w16mm_scr                   ; the head: ckid, cksize
    jc .nf
    mov ax, [es:MMS_SCR]
    mov dx, [es:MMS_SCR + 2]
    cmp ax, W16MM_RIFF
    jne .lst
    cmp dx, W16MM_FF
    je .form
.lst:
    cmp ax, W16MM_LIST
    jne .test
    cmp dx, W16MM_ST
    jne .test
.form:
    mov ax, 4
    mov dx, MMS_SCR + 8
    call w16mm_scr                   ; ...and the form type
    jc .nf
.test:
    test cl, MMIO_FINDCHUNK
    jz .t2
    mov ax, [w16mm_ck4]
    mov dx, [w16mm_ck4 + 2]
    jmp short .id
.t2:
    test cl, MMIO_FINDRIFF | MMIO_FINDLIST
    jz .found                       ; no test: the next one
    mov ax, [w16mm_ckt]              ; the form type it asked for...
    cmp [es:MMS_SCR + 8], ax
    jne .skip
    mov ax, [w16mm_ckt + 2]
    cmp [es:MMS_SCR + 10], ax
    jne .skip
    mov ax, W16MM_RIFF               ; ...under RIFF or LIST
    mov dx, W16MM_FF
    test cl, MMIO_FINDRIFF
    jnz .id
    mov ax, W16MM_LIST
    mov dx, W16MM_ST
.id:
    cmp [es:MMS_SCR], ax
    jne .skip
    cmp [es:MMS_SCR + 2], dx
    je .found
.skip:
    pop ax                          ; past it, and its pad byte
    pop dx
    add ax, 8
    adc dx, 0
    add ax, [es:MMS_SCR + 4]
    adc dx, [es:MMS_SCR + 6]
    test byte [es:MMS_SCR + 4], 1
    jz .go
    add ax, 1
    adc dx, 0
.go:
    call w16mm_goto
    jnc .next
    jmp short .nf0
.found:
    pop ax
    pop dx
    add ax, 8                       ; dwDataOffset: past the id and size
    adc dx, 0
    mov [es:MMS_SCR + 12], ax
    mov [es:MMS_SCR + 14], dx
    xor ax, ax
    mov [es:MMS_SCR + 16], ax
    mov [es:MMS_SCR + 18], ax
    call w16mm_ckout
    jmp short .out
.nf:
    pop ax
    pop ax
.nf0:
    mov ax, MMIOERR_CHUNKNOTFOUND
.out:
    xor dx, dx
    pop di
    pop si
    ret

; mmioAscend (MMSYSTEM.1224) - args: +0 wFlags, +2 lpck, +6 hmmio: out of
; the chunk, to just past its end and its pad byte. One mmioCreateChunk made
; is given its size first, when what was written differs, and its pad. 0,
; or MMIOERR_CANNOTWRITE
W16SYS w16mm_asc
    push si
    push di
    mov ax, 2
    mov dx, 6
    call w16mm_ckat
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    call w16mm_ckin
    test word [es:MMS_SCR + 18], MMIO_DIRTYH
    jz .clean
    and word [es:MMS_SCR + 18], ~MMIO_DIRTYH
    call w16mm_pos                   ; DX:AX = where the writing stopped
    push dx
    push ax
    sub ax, [es:MMS_SCR + 12]
    sbb dx, [es:MMS_SCR + 14]       ; DX:AX = what was written
    cmp ax, [es:MMS_SCR + 4]
    jne .size
    cmp dx, [es:MMS_SCR + 6]
    je .pad
.size:
    mov [es:MMS_SCR + 4], ax
    mov [es:MMS_SCR + 6], dx
    mov ax, [es:MMS_SCR + 12]       ; the size, just before the data
    mov dx, [es:MMS_SCR + 14]
    sub ax, 4
    sbb dx, 0
    call w16mm_goto
    mov ax, 4
    mov dx, MMS_SCR + 4
    call w16mm_scw
.pad:
    pop ax
    pop dx
    jc .nw
    test byte [es:MMS_SCR + 4], 1
    jz .clean
    call w16mm_goto                  ; an odd chunk's pad byte, after it
    mov ax, 1
    mov dx, MMS_ZERO
    call w16mm_scw
    jc .nw
.clean:
    mov ax, [es:MMS_SCR + 12]       ; past the data and the pad
    mov dx, [es:MMS_SCR + 14]
    add ax, [es:MMS_SCR + 4]
    adc dx, [es:MMS_SCR + 6]
    test byte [es:MMS_SCR + 4], 1
    jz .g
    add ax, 1
    adc dx, 0
.g:
    call w16mm_goto
    call w16mm_ckout
    xor ax, ax
    jmp short .out
.nw:
    mov ax, MMIOERR_CANNOTWRITE
.out:
    xor dx, dx
    pop di
    pop si
    ret

; mmioCreateChunk (MMSYSTEM.1225) - args: +0 wFlags, +2 lpck, +6 hmmio: the
; chunk's head written where the file stands - with its form type when
; MMIO_CREATERIFF or MMIO_CREATELIST names its id - and lpck made dirty for
; mmioAscend. 0, or MMIOERR_CANNOTWRITE
W16SYS w16mm_create
    push si
    push di
    mov ax, 2
    mov dx, 6
    call w16mm_ckat
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    call w16mm_ckin
    mov si, 8                       ; SI = the head's bytes
    mov ax, W16MM_RIFF
    mov dx, W16MM_FF
    test cl, MMIO_CREATERIFF
    jnz .form
    mov ax, W16MM_LIST
    mov dx, W16MM_ST
    test cl, MMIO_CREATELIST
    jz .w
.form:
    mov [es:MMS_SCR], ax
    mov [es:MMS_SCR + 2], dx
    mov si, 12
.w:
    call w16mm_pos
    add ax, 8
    adc dx, 0
    mov [es:MMS_SCR + 12], ax       ; dwDataOffset: past the id and size
    mov [es:MMS_SCR + 14], dx
    mov word [es:MMS_SCR + 16], 0
    mov word [es:MMS_SCR + 18], MMIO_DIRTYH
    mov ax, si
    mov dx, MMS_SCR
    call w16mm_scw
    mov ax, MMIOERR_CANNOTWRITE
    jc .out
    call w16mm_ckout
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

section .text
