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
    cmp dl, SND_CAP_PCM_IN          ; waveIn: 8-bit mono at 11 kHz alone
    mov ax, 1
    mov dx, ax
    je .f3
    mov ax, 0x0011                  ; 8-bit mono at 11 and 22 kHz
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

; mciGetErrorString (MMSYSTEM.706) is MCI's, in w16mci.inc (124.32.4)

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

; =============================================================================
; waveOut and sndPlaySound (124.32.3)
; =============================================================================
; An HWAVEOUT is a fixed global block of the program's heap, as an HMMIO is;
; one is open at a time, and [w16mm_wo] is its handle. Its queue is the
; program's own WAVEHDRs, linked through lpNext; reserved is where a block
; ends in the stream's bytes once it has all been copied. The ring is a grant
; of SOUND.DRV's, copied into on WIN16's turns: a write, a position asked,
; and the tick while [w16mm_wq] says a block is queued.

WH_DATA     equ 0                   ; WAVEHDR: lpData
WH_LEN      equ 4                   ;   dwBufferLength
WH_FLAGS    equ 16                  ;   dwFlags
WH_LOOPS    equ 20                  ;   dwLoops
WH_NEXT     equ 24                  ;   lpNext: the queue's link
WH_END      equ 28                  ;   reserved: where it ends, copied
WHDR_DONE       equ 0x01
WHDR_PREPARED   equ 0x02
WHDR_BEGINLOOP  equ 0x04
WHDR_ENDLOOP    equ 0x08
WHDR_INQUEUE    equ 0x10

WO_SIG      equ 0                   ; W16MM_WSIG
WO_SELF     equ 2                   ; the HWAVEOUT
WO_HAND     equ 4                   ; the stream's handle
WO_OPEN     equ 5                   ; 1 while the stream is open
WO_FL       equ 6                   ; WOF_*
WO_CBT      equ 7                   ; the callback's kind, 0 to 3
WO_GRANT    equ 8                   ; the ring: its offset in the grant
WO_RL       equ 10                  ;   and its length
WO_HALF     equ 12                  ; the card's half, or more
WO_RATE     equ 14                  ; frames a second
WO_FMT      equ 16                  ; SND_FMT_*
WO_SH       equ 17                  ; a frame's bytes, as a shift
WO_CONS     equ 18                  ; the stream's consumed count, last read
WO_CB       equ 20                  ; dwCallback
WO_PLAY     equ 24                  ; bytes played since the open or a reset
WO_STG      equ 28                  ; bytes copied into the ring
WO_SBASE    equ 32                  ; [WO_STG] when the stream was opened
WO_HEAD     equ 36                  ; the queue: its first block
WO_TAIL     equ 40                  ;   its last
WO_CUR      equ 44                  ;   the one being copied, 0 when all are
WO_COFF     equ 48                  ;   how far into it
WO_LOOPS    equ 52                  ;   passes of it still to copy
WO_SNDH     equ 56                  ; sndPlaySound's block, 0 none
WO_HDR      equ 58                  ; sndPlaySound's WAVEHDR, 32 bytes
W16MM_INST  equ 90                  ; dwInstance, waveIn's too (124.32.6)
WO_SIL      equ 94                  ; W16MM_SILN bytes of silence
WO_LEN      equ WO_SIL + W16MM_SILN
WOF_PAUSE   equ 1
WOF_DEAD    equ 2                   ; the stream would not open: nothing plays
WOF_SND     equ 4                   ; sndPlaySound's own
WOF_MCI     equ 8                   ; a waveaudio play's (w16mci.inc, 124.32.4)
W16MM_WSIG  equ 0x4F57
W16MM_SILN  equ 256
MM_WOM_OPEN     equ 0x3BB
MM_WOM_CLOSE    equ 0x3BC
MM_WOM_DONE     equ 0x3BD
MMSYSERR_ALLOCATED      equ 4
MMSYSERR_NOMEM          equ 7
MMSYSERR_NOTSUPPORTED   equ 8
MMSYSERR_BADERRNUM      equ 9
MMSYSERR_INVALPARAM     equ 11
WAVERR_BADFORMAT        equ 32
WAVERR_STILLPLAYING     equ 33
WAVERR_UNPREPARED       equ 34
W16MM_SNDASYNC  equ 0x01            ; sndPlaySound's flags
W16MM_SNDMEM    equ 0x04
W16MM_SNDLOOP   equ 0x08
W16MM_SNDNOSTOP equ 0x10

; waveOutOpen (MMSYSTEM.404) - args: +0 dwFlags, +4 dwInstance, +8
; dwCallback, +12 lpFormat, +16 wDeviceID, +18 lphWaveOut: 0 and the
; HWAVEOUT, or why not
W16SYS w16mm_woopen
    push si
    push di
    call OSAPI_SND_CAPS             ; AX = the caps word (BX and DX its too)
    mov si, ax                      ; SI = the caps
    ICALL w16_args
    mov ax, MMSYSERR_BADDEVICEID
    test si, SND_CAP_PCM_BG
    jz .out
    mov cx, [es:bx + 16]
    inc cx
    cmp cx, 1                       ; 0, or WAVE_MAPPER (-1)
    ja .out
    push es
    les di, [es:bx + 12]
    call w16mm_wfmt                 ; DX = the rate, AL the format, AH shift
    pop es
    mov cx, WAVERR_BADFORMAT
    jc .err
    test byte [es:bx], 1            ; WAVE_FORMAT_QUERY: no more than that
    jnz .ok
    mov cx, MMSYSERR_ALLOCATED
    cmp word [w16mm_wo], 0
    jne .err
    mov si, [es:bx + 8]             ; DI:SI = dwCallback
    mov di, [es:bx + 10]
    mov cl, [es:bx + 2]             ; CL = its kind
    and cl, 7
    call w16mm_cbfn                 ; CALLBACK_FUNCTION's block
    jc .out
    push word [es:bx + 6]           ; dwInstance
    push word [es:bx + 4]
    call w16mm_wmake                ; ES = the state, AX = the handle
    pop si
    pop di
    jc .out
    mov [es:W16MM_INST], si
    mov [es:W16MM_INST + 2], di
    push es
    push ax
    ICALL w16_args
    les di, [es:bx + 18]
    mov cx, es
    or cx, di
    pop ax
    jz .nop
    mov [es:di], ax
.nop:
    pop es
    mov ax, MM_WOM_OPEN
    xor cx, cx
    xor dx, dx
    call w16mm_wnote
.ok:
    xor ax, ax
    jmp short .out
.err:
    mov ax, cx
.out:
    xor dx, dx
    pop di
    pop si
    ret

; waveOutClose (MMSYSTEM.405) - args: +0 hWaveOut: 0, or WAVERR_STILLPLAYING
; while a block is queued
W16SYS w16mm_woclose
    call w16mm_warg1
    jc .out
    call w16mm_wfeed
    mov ax, WAVERR_STILLPLAYING
    cmp word [es:WO_HEAD + 2], 0
    jne .out
    mov ax, MM_WOM_CLOSE
    xor cx, cx
    xor dx, dx
    call w16mm_wnote
    call w16mm_wkill
    xor ax, ax
.out:
    xor dx, dx
    ret

; waveOutPrepareHeader (MMSYSTEM.406) - args: +0 wSize, +2 lpWaveHdr, +6
; hWaveOut: WHDR_PREPARED. 0
W16SYS w16mm_woprep
    call w16mm_wargh                ; DX:CX = the block
w16mm_prep:
    push si
    push ds
    jc .out
    mov ds, dx
    mov si, cx
    or byte [si + WH_FLAGS], WHDR_PREPARED
    xor ax, ax
.out:
    pop ds
    pop si
    xor dx, dx
    ret

; waveOutUnprepareHeader (MMSYSTEM.407) - args as Prepare's: WHDR_PREPARED
; cleared. 0, or WAVERR_STILLPLAYING for a queued block
W16SYS w16mm_wounprep
    call w16mm_wargh
w16mm_unprep:
    push si
    push ds
    jc .out
    mov ds, dx
    mov si, cx
    mov ax, WAVERR_STILLPLAYING
    test byte [si + WH_FLAGS], WHDR_INQUEUE
    jnz .out
    and byte [si + WH_FLAGS], ~WHDR_PREPARED & 0xFF
    xor ax, ax
.out:
    pop ds
    pop si
    xor dx, dx
    ret

; waveOutWrite (MMSYSTEM.408) - args as Prepare's: the block queued and the
; copy begun. 0, or WAVERR_UNPREPARED
W16SYS w16mm_wowrite
    call w16mm_wargh
    jc .out
    call w16mm_wpush
.out:
    xor dx, dx
    ret

; waveOutPause (MMSYSTEM.409) - args: +0 hWaveOut: no more is copied, and
; the ring is padded to the card's half. 0
W16SYS w16mm_wopause
    call w16mm_warg1
    jc .out
    or byte [es:WO_FL], WOF_PAUSE
    call w16mm_wfeed
    xor ax, ax
.out:
    xor dx, dx
    ret

; waveOutRestart (MMSYSTEM.410) - args: +0 hWaveOut: the copy again. 0
W16SYS w16mm_worestart
    call w16mm_warg1
    jc .out
    and byte [es:WO_FL], ~WOF_PAUSE & 0xFF
    call w16mm_wfeed
    ICALL w16_tarm
    xor ax, ax
.out:
    xor dx, dx
    ret

; waveOutReset (MMSYSTEM.411) - args: +0 hWaveOut: the stream closed, every
; block done and noticed, the position 0. 0
W16SYS w16mm_woreset
    call w16mm_warg1
    jc .out
    call w16mm_wstop
    call w16mm_wdrop
    call w16mm_wdone
    xor ax, ax
    mov [es:WO_PLAY], ax
    mov [es:WO_PLAY + 2], ax
    mov [es:WO_STG], ax
    mov [es:WO_STG + 2], ax
    mov [es:WO_SBASE], ax
    mov [es:WO_SBASE + 2], ax
    and byte [es:WO_FL], ~WOF_DEAD & 0xFF
    mov [w16mm_wq], al
.out:
    xor dx, dx
    ret

; waveOutBreakLoop (MMSYSTEM.419) - args: +0 hWaveOut: the pass of a loop
; being copied is its last. 0
W16SYS w16mm_wobreak
    call w16mm_warg1
    jc .out
    xor ax, ax
    mov [es:WO_LOOPS], ax
    mov [es:WO_LOOPS + 2], ax
.out:
    xor dx, dx
    ret

; waveOutGetPosition (MMSYSTEM.412) - args: +0 wSize, +2 lpmmt, +6
; hWaveOut: the bytes played, as TIME_BYTES, TIME_SAMPLES or TIME_MS; any
; other kind as TIME_BYTES, wType saying so. 0
W16SYS w16mm_wopos
    push si
    push ds
    call w16mm_wargh                ; DX:CX = the MMTIME
    jc .out
    call w16mm_wfeed
    mov si, cx
    push dx
    mov ax, [es:WO_PLAY]
    mov dx, [es:WO_PLAY + 2]
    mov cl, [es:WO_SH]
    mov bx, [es:WO_RATE]
    pop ds                          ; DS:SI = the MMTIME
    call w16mm_tpos
.out:
    pop ds
    pop si
    xor dx, dx
    ret

; w16mm_tpos - DS:SI = an MMTIME, DX:AX = bytes, CL = a frame's shift, BX =
; frames a second: the time written as TIME_BYTES, TIME_SAMPLES or TIME_MS,
; any other kind as TIME_BYTES and wType saying so. AX = 0
w16mm_tpos:
    cmp word [si], 2                ; TIME_SAMPLES
    je .smp
    cmp word [si], 1                ; TIME_MS
    je .ms
    mov word [si], 4                ; TIME_BYTES
    jmp short .put
.ms:
    call w16mm_wfr
    call w16mm_ms
    jmp short .put
.smp:
    call w16mm_wfr
.put:
    mov [si + 2], ax
    mov [si + 4], dx
    xor ax, ax
    ret

; waveOutGetID (MMSYSTEM.420) - args: +0 lpuDeviceID, +4 hWaveOut: 0, the
; one device
W16SYS w16mm_woid
    push di
    ICALL w16_args
    mov ax, [es:bx + 4]
    push es
    call w16mm_wpst
    pop es
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    les di, [es:bx]
    mov word [es:di], 0
    xor ax, ax
.out:
    xor dx, dx
    pop di
    ret

; waveOutGetPitch, SetPitch, GetVolume, SetVolume, GetPlaybackRate,
; SetPlaybackRate (MMSYSTEM.413-418), waveOutMessage (421)
W16SYS w16mm_nosup
    mov ax, MMSYSERR_NOTSUPPORTED
    xor dx, dx
    ret

; waveOutGetErrorText (MMSYSTEM.403) - args: +0 wSize, +2 lpText, +6 wError:
; its sentence, cut to the buffer. 0, or MMSYSERR_BADERRNUM
W16SYS w16mm_woerr
    push si
    push di
    ICALL w16_args
    mov dx, [es:bx + 6]
    mov cx, [es:bx]
    les di, [es:bx + 2]
    or dh, dh
    jnz .bad
    mov si, w16mm_werrs              ; XSEC-OK: the part's table, at cs:
.f:
    cs lodsb
    cmp al, 0xFF                    ; (the table's end: AL is not the answer)
    je .bad
    cmp al, dl
    je .hit
.skip:
    cs lodsb
    or al, al
    jnz .skip
    jmp short .f
.hit:
    jcxz .ok
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
.ok:
    xor ax, ax
    jmp short .out
.bad:
    mov ax, MMSYSERR_BADERRNUM
.out:
    xor dx, dx
    pop di
    pop si
    ret

w16mm_werrs:
    db 0, 'The specified command was carried out.', 0
    db 1, 'Undefined external error.', 0
    db 2, 'The device ID is out of range for your system.', 0
    db 4, 'The specified device is already in use.', 0
    db 5, 'The specified device handle is invalid.', 0
    db 7, 'Unable to allocate or lock memory.', 0
    db 8, 'The function requested is not supported.', 0
    db 11, 'An invalid parameter was passed to a system function.', 0
    db 32, 'The wave format is not supported by this device.', 0
    db 33, 'The sound is still playing.', 0
    db 34, 'The wave header was not prepared.', 0
    db 64, 'The MIDI header was not prepared.', 0
    db 65, 'Media data is still playing.', 0
    db 67, 'The port is transmitting data to the device.', 0
    db 0xFF

; =============================================================================
; waveIn (124.32.5): one device over SOUND.DRV's external record ring
; (§34.6.4). The HWAVEIN is a fixed block of the program's heap; the ring is a
; DMA claim of WIN16's own, 16 KB and its two control words, that the card
; writes in place. The queue is the program's WAVEHDRs, linked through
; lpNext, filled from the ring on WIN16's turns.
; =============================================================================

WH_REC      equ 8                   ; WAVEHDR: dwBytesRecorded

WI_SIG      equ 0                   ; W16MM_ISIG
WI_SELF     equ 2                   ; the HWAVEIN (WO_SELF's place: w16mm_wnote)
WI_HAND     equ 4                   ; the stream's handle
WI_OPEN     equ 5                   ; 1 while the stream is open
WI_FL       equ 6                   ; WIF_*
WI_CBT      equ 7                   ; the callback's kind (WO_CBT's place)
WI_RSEG     equ 8                   ; the ring's segment
WI_CAP      equ 10                  ; the card's count, last read
WI_RD       equ 12                  ; ...and how much of it is copied or lost
WI_RATE     equ 14                  ; samples a second
WI_HEAD     equ 16                  ; the queue: its first block
WI_CB       equ 20                  ; dwCallback (WO_CB's place)
WI_POS      equ 24                  ; bytes captured since the open or a reset
WI_TAIL     equ 28                  ; the queue's last block
WI_LEN      equ W16MM_INST + 4      ; ...and dwInstance where waveOut's is
WIF_START   equ 1
W16MM_ISIG  equ 0x4957
W16MM_IRL   equ 16384               ; the ring, RL's code 2
W16MM_ILAG  equ W16MM_IRL - 2048    ; more unread than this: overwritten
MM_WIM_OPEN     equ 0x3BE
MM_WIM_CLOSE    equ 0x3BF
MM_WIM_DATA     equ 0x3C0
MMSYSERR_ERROR  equ 1

; waveInOpen (MMSYSTEM.504) - args: +0 dwFlags, +4 dwInstance, +8 dwCallback,
; +12 lpFormat, +16 wDeviceID, +18 lphWaveIn: 0 and the HWAVEIN, or why not
W16SYS w16mm_wiopen
    push si
    push di
    call OSAPI_SND_CAPS             ; AX = the caps word (BX and DX its too)
    mov si, ax
    ICALL w16_args
    mov ax, MMSYSERR_BADDEVICEID
    test si, SND_CAP_PCM_IN
    jz .out
    mov cx, [es:bx + 16]
    inc cx
    cmp cx, 1                       ; 0, or WAVE_MAPPER (-1)
    ja .out
    push es
    les di, [es:bx + 12]
    call w16mm_wifmt                ; DX = the rate
    pop es
    mov cx, WAVERR_BADFORMAT
    jc .err
    test byte [es:bx], 1            ; WAVE_FORMAT_QUERY
    jnz .ok
    mov cx, MMSYSERR_ALLOCATED
    cmp word [w16mm_wi], 0
    jne .err
    cmp word [w16mm_wo], 0          ; half-duplex: the output device open
    jne .err
    mov si, [es:bx + 8]             ; DI:SI = dwCallback
    mov di, [es:bx + 10]
    mov cl, [es:bx + 2]
    and cl, 7
    call w16mm_cbfn                 ; CALLBACK_FUNCTION's block
    jc .out
    push word [es:bx + 6]           ; dwInstance
    push word [es:bx + 4]
    call w16mm_wimake               ; ES = the state, AX = the handle
    pop si
    pop di
    jc .out
    mov [es:W16MM_INST], si
    mov [es:W16MM_INST + 2], di
    push es
    push ax
    ICALL w16_args
    les di, [es:bx + 18]
    mov cx, es
    or cx, di
    pop ax
    jz .nop
    mov [es:di], ax
.nop:
    pop es
    mov ax, MM_WIM_OPEN
    xor cx, cx
    xor dx, dx
    call w16mm_wnote
.ok:
    xor ax, ax
    jmp short .out
.err:
    mov ax, cx
.out:
    xor dx, dx
    pop di
    pop si
    ret

; waveInClose (MMSYSTEM.505) - args: +0 hWaveIn: 0, or WAVERR_STILLPLAYING
; while a block is queued
W16SYS w16mm_wiclose
    call w16mm_iarg1
    jc .out
    call w16mm_wipoll
    mov ax, WAVERR_STILLPLAYING
    cmp word [es:WI_HEAD + 2], 0
    jne .out
    mov ax, MM_WIM_CLOSE
    xor cx, cx
    xor dx, dx
    call w16mm_wnote
    call w16mm_wikill
    xor ax, ax
.out:
    xor dx, dx
    ret

; waveInPrepareHeader, waveInUnprepareHeader (MMSYSTEM.506, 507) - args +0
; wSize, +2 lpWaveInHdr, +6 hWaveIn: waveOut's (w16mm_prep, w16mm_unprep)
W16SYS w16mm_wiprep
    call w16mm_iargh
    jmp w16mm_prep
W16SYS w16mm_wiunprep
    call w16mm_iargh
    jmp w16mm_unprep

; waveInAddBuffer (MMSYSTEM.508) - args as Prepare's: the block queued, empty,
; and what the card has captured copied. 0, or why not
W16SYS w16mm_wiadd
    push si
    push ds
    call w16mm_iargh                ; DX:CX = the block
    jc .out
    mov ds, dx
    mov si, cx
    mov ax, WAVERR_UNPREPARED
    test byte [si + WH_FLAGS], WHDR_PREPARED
    jz .out
    mov ax, WAVERR_STILLPLAYING
    test byte [si + WH_FLAGS], WHDR_INQUEUE
    jnz .out
    and byte [si + WH_FLAGS], ~WHDR_DONE & 0xFF
    or byte [si + WH_FLAGS], WHDR_INQUEUE
    xor ax, ax
    mov [si + WH_REC], ax
    mov [si + WH_REC + 2], ax
    mov [si + WH_NEXT], ax
    mov [si + WH_NEXT + 2], ax
    lds si, [es:WI_TAIL]            ; after the last, or the first
    mov ax, ds
    or ax, ax
    jz .first
    mov [si + WH_NEXT], cx
    mov [si + WH_NEXT + 2], dx
    jmp short .tail
.first:
    mov [es:WI_HEAD], cx
    mov [es:WI_HEAD + 2], dx
.tail:
    mov [es:WI_TAIL], cx
    mov [es:WI_TAIL + 2], dx
    pop ds
    push ds
    call w16mm_wipoll
    xor ax, ax
.out:
    pop ds
    pop si
    xor dx, dx
    ret

; waveInStart (MMSYSTEM.509) - args: +0 hWaveIn: the stream opened, verb 4
; with SND_OPENF_EXT into the ring, and the tick asked for. 0, or
; MMSYSERR_ALLOCATED when the card is busy, MMSYSERR_ERROR when it cannot
; capture in place
W16SYS w16mm_wistart
    call w16mm_iarg1
    jc .ret
    test byte [es:WI_FL], WIF_START
    jnz .ok
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_SND_CAPS             ; AX = the caps word (BX and DX its too)
    mov bx, ax
    mov ah, (2 << SND_OPENF_RLSH) | SND_OPENF_RING | SND_OPENF_EXT
    test bl, SND_CAP_EXTBLK
    jz .o
    or ah, 2 << SND_OPENF_BLKSH     ; 512-byte blocks
.o:
    mov al, 4
    xor si, si
    mov di, [es:WI_RSEG]
    xor cx, cx
    mov dx, [es:WI_RATE]
    call w16mm_snd                  ; AL = 0 and AH = the handle, else why
    or al, al
    jnz .no
    mov [es:WI_HAND], ah
    mov byte [es:WI_OPEN], 1
    or byte [es:WI_FL], WIF_START
    xor ax, ax                      ; the card's count starts at 0
    mov [es:WI_CAP], ax
    mov [es:WI_RD], ax
    mov byte [w16mm_wq], 1
    ICALL w16_tarm
    jmp short .out
.no:
    mov ah, al
    mov al, MMSYSERR_ALLOCATED
    cmp ah, 1                       ; busy
    je .out
    mov al, MMSYSERR_ERROR
.out:
    xor ah, ah
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    jmp short .ret
.ok:
    xor ax, ax
.ret:
    xor dx, dx
    ret

; waveInStop (MMSYSTEM.510) - args: +0 hWaveIn: what is captured copied, the
; stream closed, the block being filled returned if it holds anything. 0
W16SYS w16mm_wistop
    call w16mm_iarg1
    jc .out
    call w16mm_wipoll
    call w16mm_wiend
    xor ax, ax
.out:
    xor dx, dx
    ret

; waveInReset (MMSYSTEM.511) - args: +0 hWaveIn: stopped, every block
; returned, the position 0. 0
W16SYS w16mm_wireset
    call w16mm_iarg1
    jc .out
    call w16mm_wipoll
    call w16mm_wihalt
.lp:
    cmp word [es:WI_HEAD + 2], 0
    je .z
    call w16mm_wiret
    jmp short .lp
.z:
    xor ax, ax
    mov [es:WI_POS], ax
    mov [es:WI_POS + 2], ax
.out:
    xor dx, dx
    ret

; waveInGetPosition (MMSYSTEM.512) - args: +0 wSize, +2 lpmmt, +6 hWaveIn:
; the bytes captured while started, as waveOutGetPosition answers. 0
W16SYS w16mm_wipos
    push si
    push ds
    call w16mm_iargh                ; DX:CX = the MMTIME
    jc .out
    call w16mm_wipoll
    mov si, cx
    mov ds, dx
    mov ax, [es:WI_POS]
    mov dx, [es:WI_POS + 2]
    xor cl, cl                      ; 8-bit mono: a sample is a byte
    mov bx, [es:WI_RATE]
    call w16mm_tpos
.out:
    pop ds
    pop si
    xor dx, dx
    ret

; waveInGetID (MMSYSTEM.513) - args: +0 lpuDeviceID, +4 hWaveIn: 0, the one
; device
W16SYS w16mm_wiid
    push di
    ICALL w16_args
    mov ax, [es:bx + 4]
    push es
    call w16mm_wist
    pop es
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    les di, [es:bx]
    mov word [es:di], 0
    xor ax, ax
.out:
    xor dx, dx
    pop di
    ret

; w16mm_wifmt - ES:DI = a PCMWAVEFORMAT: CF=0 DX = its rate when it is 8-bit
; mono PCM at 4,000 to 15,000 Hz, the record verb's; CF=1 not. Keeps the rest
w16mm_wifmt:
    mov dx, es
    or dx, di
    jz .no
    cmp word [es:di], 1             ; WAVE_FORMAT_PCM
    jne .no
    cmp word [es:di + 2], 1         ; nChannels
    jne .no
    cmp word [es:di + 14], 8        ; wBitsPerSample
    jne .no
    cmp word [es:di + 6], 0
    jne .no
    mov dx, [es:di + 4]             ; nSamplesPerSec
    cmp dx, 4000
    jb .no
    cmp dx, 15000
    ja .no
    clc
    ret
.no:
    stc
    ret

; w16mm_wimake - DX = the rate, CL = the callback's kind, DI:SI = dwCallback:
; the ring claimed and an HWAVEIN made. CF=0 ES = its state, AX = its handle,
; and it is [w16mm_wi]; CF=1 AX = MMSYSERR_NOMEM. Keeps BX, CX, DX
w16mm_wimake:
    push bx
    push cx
    push dx
    push bp
    mov bp, dx                      ; BP = the rate
    push cx
    mov ax, W16MM_IRL / 1024 + 1    ; the ring and its words, all the head
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA_HI     ; DX = its segment
    pop cx
    jc .nomem
    push dx
    push cx
    mov ax, WI_LEN
    xor dx, dx
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; AX = the handle, DX = the block
    pop cx
    pop bx                          ; BX = the ring
    or ax, ax
    jz .noblk
    W16SEG es, dx
    mov word [es:WI_SIG], W16MM_ISIG
    mov [es:WI_SELF], ax
    mov [es:WI_RSEG], bx
    mov [es:WI_RATE], bp
    mov [es:WI_CBT], cl
    mov [es:WI_CB], si
    mov [es:WI_CB + 2], di
    mov [w16mm_wi], ax
    clc
    jmp short .out
.noblk:
    mov dx, bx
    call OSAPI_MEM_FREE
.nomem:
    mov ax, MMSYSERR_NOMEM
    stc
.out:
    pop bp
    pop dx
    pop cx
    pop bx
    ret

; w16mm_wikill - ES = the state: the stream closed BEFORE the ring is let go,
; the state freed. No notice. Keeps all but ES
w16mm_wikill:
    push ax
    push dx
    call w16mm_wihalt
    mov dx, [es:WI_RSEG]
    call OSAPI_MEM_FREE
    mov word [es:WI_SIG], 0
    mov ax, [es:WI_SELF]
    ICALL w16_gfreeh
    mov word [w16mm_wi], 0
    pop dx
    pop ax
    ret

; w16mm_wihalt - ES = the state: the stream closed if it is open; stopped.
; Keeps all
w16mm_wihalt:
    and byte [es:WI_FL], ~WIF_START & 0xFF
    cmp byte [es:WI_OPEN], 0
    je .ret
    push ax
    mov al, 2
    mov ah, [es:WI_HAND]
    call w16mm_snd
    mov byte [es:WI_OPEN], 0
    pop ax
.ret:
    ret

; w16mm_wiend - ES = the state: halted, and the block at the head returned if
; it holds anything (waveInStop's). Keeps all
w16mm_wiend:
    call w16mm_wihalt
    push ax
    push si
    push ds
    lds si, [es:WI_HEAD]
    mov ax, ds
    or ax, ax
    jz .out
    mov ax, [si + WH_REC]
    or ax, [si + WH_REC + 2]
.out:
    pop ds
    jz .ret
    call w16mm_wiret
.ret:
    pop si
    pop ax
    ret

; w16mm_wiret - ES = the state, a block queued: the head returned, WHDR_DONE
; and not WHDR_INQUEUE, and MM_WIM_DATA sent. Keeps all
w16mm_wiret:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    lds si, [es:WI_HEAD]
    and byte [si + WH_FLAGS], ~WHDR_INQUEUE & 0xFF
    or byte [si + WH_FLAGS], WHDR_DONE
    mov cx, si
    mov dx, ds
    mov ax, [si + WH_NEXT]
    mov bx, [si + WH_NEXT + 2]
    pop ds
    push ds
    mov [es:WI_HEAD], ax
    mov [es:WI_HEAD + 2], bx
    or bx, bx
    jnz .n
    mov [es:WI_TAIL], bx
    mov [es:WI_TAIL + 2], bx
.n:
    mov ax, MM_WIM_DATA
    call w16mm_wnote                ; DX:CX = the block
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16mm_wipoll - ES = the state: the card's count read and what it captured
; copied into the queue; a stream the watchdog ended, or that is gone,
; stopped as waveInStop stops it. Keeps all
w16mm_wipoll:
    cmp byte [es:WI_OPEN], 0
    je .ret
    push ax
    mov al, 3
    mov ah, [es:WI_HAND]
    call w16mm_snd                  ; AX = the state
    call w16mm_wigrab
    cmp ax, SND_ST_ENDED
    jb .out
    cmp ax, SND_ST_STALE            ; gone: its handle is not ours to close
    jne .end
    mov byte [es:WI_OPEN], 0
.end:
    call w16mm_wiend
.out:
    pop ax
.ret:
    ret

; w16mm_wigrab - ES = the state, the stream open: the card's count folded
; into the position, and the ring copied into the blocks at the queue's head,
; each one full returned. With no block queued what it captured is lost; past
; W16MM_ILAG unread the oldest is. Keeps all
w16mm_wigrab:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    mov ds, [es:WI_RSEG]
    mov dx, [W16MM_IRL + SND_EXT_CONS]  ; DX = the card's count
    pop ds
    push ds
    mov ax, dx
    sub ax, [es:WI_CAP]
    mov [es:WI_CAP], dx
    add [es:WI_POS], ax
    adc word [es:WI_POS + 2], 0
    mov ax, dx
    sub ax, [es:WI_RD]
    cmp ax, W16MM_ILAG
    jbe .lp
    sub dx, W16MM_ILAG
    mov [es:WI_RD], dx              ; overwritten: skipped
.lp:
    pop ds
    push ds
    mov ax, [es:WI_HEAD + 2]
    or ax, ax
    jz .lost
    lds si, [es:WI_HEAD]
    mov ax, [si + WH_LEN]           ; AX = the room left in it, 64 KB at most
    mov bx, [si + WH_LEN + 2]
    sub ax, [si + WH_REC]
    sbb bx, [si + WH_REC + 2]
    jc .full
    or bx, bx
    jz .rm
    mov ax, 0xFFFF
.rm:
    or ax, ax
    jz .full
    mov cx, [es:WI_CAP]
    sub cx, [es:WI_RD]              ; CX = unread
    jz .out
    cmp cx, ax
    jbe .r1
    mov cx, ax
.r1:
    mov di, [si + WH_DATA]          ; BX:DI = where it goes
    mov bx, [si + WH_DATA + 2]
    add di, [si + WH_REC]
    mov ax, [si + WH_REC + 2]
    adc ax, 0
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add bx, ax                      ; __AHINCR, 8, for each 64 KB
    mov ax, di
    neg ax                          ; ...to its 64 KB's end
    jz .r2
    cmp cx, ax
    jbe .r2
    mov cx, ax
.r2:
    mov ax, [es:WI_RD]
    and ax, W16MM_IRL - 1           ; AX = the ring's place
    mov dx, W16MM_IRL
    sub dx, ax
    cmp cx, dx                      ; ...to the ring's end
    jbe .r3
    mov cx, dx
.r3:
    add [si + WH_REC], cx
    adc word [si + WH_REC + 2], 0
    add [es:WI_RD], cx
    push es
    mov si, ax
    mov ds, [es:WI_RSEG]
    mov es, bx
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    pop es
    jmp .lp
.full:
    pop ds
    push ds
    call w16mm_wiret
    jmp .lp
.lost:
    mov ax, [es:WI_CAP]
    mov [es:WI_RD], ax
.out:
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16mm_iarg1, w16mm_iargh - waveOut's w16mm_warg1 and w16mm_wargh, for an
; HWAVEIN
w16mm_iarg1:
    push bx
    ICALL w16_args
    mov ax, [es:bx]
    pop bx
    jmp short w16mm_iargs
w16mm_iargh:
    push bx
    ICALL w16_args
    mov cx, [es:bx + 2]
    mov dx, [es:bx + 4]
    mov ax, [es:bx + 6]
    pop bx
    or dx, dx
    jnz w16mm_iargs
    mov ax, MMSYSERR_INVALPARAM
    stc
    ret
w16mm_iargs:
    call w16mm_wist
    jnc .ret
    mov ax, MMSYSERR_INVALHANDLE
.ret:
    ret

; w16mm_wist - AX = an HWAVEIN -> ES = its state, CF=0; CF=1 it is not the
; one open. Keeps all but ES
w16mm_wist:
    or ax, ax
    jz .no
    cmp ax, [w16mm_wi]
    jne .no
    W16SEG es, ax                   ; a fixed block: its handle is its place
    cmp word [es:WI_SIG], W16MM_ISIG
    jne .no
    clc
    ret
.no:
    stc
    ret

; sndPlaySound (MMSYSTEM.2) - args: +0 wFlags, +2 lpszSound: the WAV played
; through waveOut's path, the device closing itself when it ends. TRUE, or
; FALSE when it cannot be (124.32.3)
W16SYS w16mm_sndplay
    push si
    push di
    push bp
    ICALL w16_args
    mov bp, [es:bx]                 ; BP = the flags
    les ax, [es:bx + 2]
    mov [w16mm_lim], ax              ; the name, or the sound in memory
    mov [w16mm_lim + 2], es
    mov ax, [w16mm_wo]
    call w16mm_wst
    jc .idle
    test byte [es:WO_FL], WOF_SND
    jz .busy                        ; the program's own waveOut
    cmp word [w16mm_lim + 2], 0
    je .kill
    test bp, W16MM_SNDNOSTOP
    jnz .false
.kill:
    call w16mm_wkill
.idle:
    cmp word [w16mm_lim + 2], 0
    je .true                        ; NULL: stopped, or nothing was playing
    call w16mm_sget                 ; ES:DI = its fmt chunk, BX = the block
    jc .false
    push bx
    call OSAPI_SND_CAPS             ; AX = the caps word (BX and DX its too)
    mov si, ax
    call w16mm_wfmt                 ; DX = the rate, AL the format, AH shift
    pop bx
    jc .drop
    xor si, si                      ; no callback
    xor di, di
    xor cx, cx
    call w16mm_wmake                ; ES = the state
    jc .drop
    or byte [es:WO_FL], WOF_SND
    mov [es:WO_SNDH], bx
    mov ax, [w16mm_boff]             ; its WAVEHDR: the data chunk
    mov [es:WO_HDR + WH_DATA], ax
    mov ax, [w16mm_bsel]
    mov [es:WO_HDR + WH_DATA + 2], ax
    mov ax, [w16mm_rem]
    mov [es:WO_HDR + WH_LEN], ax
    mov ax, [w16mm_rem + 2]
    mov [es:WO_HDR + WH_LEN + 2], ax
    mov al, WHDR_PREPARED
    test bp, W16MM_SNDLOOP
    jz .h
    mov al, WHDR_PREPARED | WHDR_BEGINLOOP | WHDR_ENDLOOP
    mov word [es:WO_HDR + WH_LOOPS], 0xFFFF
    mov word [es:WO_HDR + WH_LOOPS + 2], 0xFFFF
.h:
    mov [es:WO_HDR + WH_FLAGS], al
    mov cx, WO_HDR
    mov dx, es
    call w16mm_wpush
    test bp, W16MM_SNDASYNC | W16MM_SNDLOOP
    jnz .true
.wait:                              ; without SND_ASYNC: here until it ends
    cmp word [es:WO_HEAD + 2], 0
    je .end
    mov ax, 1
    call OSAPI_TASK_SLEEP
    mov ax, [w16mm_wo]
    call w16mm_wst
    jc .true
    call w16mm_wfeed
    jmp short .wait
.end:
    call w16mm_wkill
    jmp short .true
.busy:
    cmp word [w16mm_lim + 2], 0
    je .true
    jmp short .false
.drop:
    or bx, bx
    jz .false
    mov ax, bx
    ICALL w16_gfreeh
.false:
    xor ax, ax
    jmp short .ret
.true:
    mov ax, 1
.ret:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; w16mm_wtick - WIN16's tick while a block is queued or an MCI device plays
; (the arm's w16_ontimer): the copy, sndPlaySound's device closed once its
; sound has played, and MCI's turn (w16mm_mtick). Keeps all
w16mm_wtick:
    push ax
    push es
    mov ax, [w16mm_wo]
    call w16mm_wst
    jc .none
    call w16mm_wfeed
    test byte [es:WO_FL], WOF_SND
    jz .mci
    cmp word [es:WO_HEAD + 2], 0
    jne .mci
    call w16mm_wkill
    jmp short .mci
.none:
    mov byte [w16mm_wq], 0
.mci:
    call w16mm_mtick                ; AL = 1 while a device plays
    or [w16mm_wq], al
    call w16mm_tlive                ; an event armed or a call owed: the
    jc .in                          ;   tick goes on (124.32.6)
    mov byte [w16mm_wq], 1
.in:
    mov ax, [w16mm_wi]              ; waveIn's copy while it is started
    call w16mm_wist
    jc .out
    call w16mm_wipoll
    test byte [es:WI_FL], WIF_START
    jz .out
    mov byte [w16mm_wq], 1
.out:
    pop es
    pop ax
    ret

; w16mm_wend - the program's end (w16_freeprog): its devices closed, with no
; notice, midiOut's interface given back, and MCI's forgotten. Keeps all
w16mm_wend:
    push ax
    push es
    mov ax, [w16mm_wo]
    call w16mm_wst
    jc .in
    call w16mm_wkill
.in:
    mov ax, [w16mm_wi]
    call w16mm_wist
    jc .out
    call w16mm_wikill               ; the card stopped, then the ring freed
.out:
    mov ax, [w16mm_mo]              ; midiOut's interface given back
    call w16mm_most
    jc .mo
    call w16mm_mokill
.mo:
    xor ax, ax
    mov [w16mm_wo], ax
    mov [w16mm_wi], ax
    mov [w16mm_mo], ax
    mov [w16mm_tev], ax             ; the calls' block goes with the heap
    mov [w16mm_wq], ax
    mov [w16mm_mci], ax             ; MCI's block goes with the arenas
    pop es
    pop ax
    ret

; w16mm_warg1 - the argument hWaveOut, at +0: CF=0 ES = its state; CF=1 AX =
; MMSYSERR_INVALHANDLE. Keeps the rest
w16mm_warg1:
    push bx
    ICALL w16_args
    mov ax, [es:bx]
    pop bx
    jmp short w16mm_wargs

; w16mm_wargh - the arguments +0 wSize, +2 a far pointer, +6 hWaveOut: CF=0
; ES = the state, DX:CX = the pointer; CF=1 AX = why not. Keeps the rest
w16mm_wargh:
    push bx
    ICALL w16_args
    mov cx, [es:bx + 2]
    mov dx, [es:bx + 4]
    mov ax, [es:bx + 6]
    pop bx
    or dx, dx
    jnz w16mm_wargs
    mov ax, MMSYSERR_INVALPARAM
    stc
    ret
w16mm_wargs:
    call w16mm_wpst
    jnc .ret
    mov ax, MMSYSERR_INVALHANDLE
.ret:
    ret

; w16mm_wst - AX = an HWAVEOUT -> ES = its state, CF=0; CF=1 it is not the
; one open. w16mm_wpst: and not sndPlaySound's or MCI's. Keeps all but ES
w16mm_wpst:
    call w16mm_wst
    jc .ret
    test byte [es:WO_FL], WOF_SND | WOF_MCI ; (CF=0)
    jz .ret
    stc
.ret:
    ret
w16mm_wst:
    or ax, ax
    jz .no
    cmp ax, [w16mm_wo]
    jne .no
    W16SEG es, ax                   ; a fixed block: its handle is its place
    cmp word [es:WO_SIG], W16MM_WSIG
    jne .no
    clc
    ret
.no:
    stc
    ret

; w16mm_wfmt - ES:DI = a PCMWAVEFORMAT, SI = OSAPI_SND_CAPS's word: CF=0 DX =
; its rate, AL = its SND_FMT_* and AH = a frame's bytes as a shift, when the
; card plays it (124.32.3); CF=1 it does not, or there is no stream at all.
; Keeps the rest
w16mm_wfmt:
    test si, SND_CAP_PCM_BG         ; no stream to play into
    jz .no
    mov ax, es
    or ax, di
    jz .no
    xor ax, ax
    cmp word [es:di], 1             ; WAVE_FORMAT_PCM
    jne .no
    cmp word [es:di + 6], 0
    jne .no
    mov dx, [es:di + 4]             ; nSamplesPerSec
    cmp dx, 4000
    jb .no
    cmp dx, 44100
    ja .no
    cmp dx, 22222
    jbe .ch
    test si, SND_CAP_PCM_HI
    jz .no
.ch:
    cmp word [es:di + 2], 1         ; nChannels
    je .bits
    cmp word [es:di + 2], 2
    jne .no
    test si, SND_CAP_STEREO
    jz .no
    test si, SND_CAP_PCM16          ; an SB Pro: 22,050 a channel at most
    jnz .st
    cmp dx, 22050
    ja .no
.st:
    or al, SND_FMT_STEREO
    inc ah
.bits:
    cmp word [es:di + 14], 8        ; wBitsPerSample
    je .ok
    cmp word [es:di + 14], 16
    jne .no
    test si, SND_CAP_PCM16
    jz .no
    or al, SND_FMT_16
    inc ah
.ok:
    clc
    ret
.no:
    stc
    ret

; w16mm_wmake - DX = the rate, AL = SND_FMT_*, AH = a frame's shift, CL =
; the callback's kind, DI:SI = dwCallback: an HWAVEOUT made and its ring
; granted - 8 KB, 16 KB above 22,050 bytes a second, 32 KB above 44,100,
; halved down to two halves. CF=0 ES = its state, AX = its handle, and it is
; [w16mm_wo]; CF=1 AX = why not. Keeps BX, CX, DX
w16mm_wmake:
    cmp word [w16mm_wi], 0          ; half-duplex: the input device open
    je .go                          ;   (124.32.5)
    mov ax, MMSYSERR_ALLOCATED
    stc
    ret
.go:
    push bx
    push cx
    push dx
    push bp
    mov bp, ax                      ; BP = the format and the shift
    push dx
    push cx
    mov ax, WO_LEN
    xor dx, dx
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; AX = the handle, DX = the block
    pop cx
    mov bx, dx
    pop dx
    or ax, ax
    jz .nomem
    W16SEG es, bx
    mov word [es:WO_SIG], W16MM_WSIG
    mov [es:WO_SELF], ax
    mov [es:WO_RATE], dx
    mov [es:WO_FMT], bp             ; and WO_SH
    mov [es:WO_CBT], cl
    mov [es:WO_CB], si
    mov [es:WO_CB + 2], di
    mov bx, 2048                    ; the half: 4 KB above 22,222 Hz, and
    test bp, 0x00FF                 ;   for a format (the SB16's at any rate)
    jnz .h4
    cmp dx, 22222
    jbe .h
.h4:
    mov bx, 4096
.h:
    mov [es:WO_HALF], bx
    shl bx, 1                       ; BX = two halves, the least ring
    push ax
    mov di, WO_SIL                  ; silence: 80h, or 0 for 16 bits
    mov cx, W16MM_SILN
    mov al, 0x80
    test bp, SND_FMT_16
    jz .s8
    xor al, al
.s8:
    cld
    rep stosb
    mov ax, dx                      ; DX:AX = its bytes a second
    xor dx, dx
    mov cx, bp
    mov cl, ch
    xor ch, ch
    jcxz .bps
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.bps:
    mov cx, 8192
    or dx, dx
    jnz .r32
    cmp ax, 22050
    jbe .grant
    mov cx, 16384
    cmp ax, 44100
    jbe .grant
.r32:
    mov cx, 32768
.grant:
    mov ax, 7                       ; verb 7, sub-op 0: a grant of CX
    call w16mm_snd                  ; AX = 0, SI = its offset
    or ax, ax
    jz .got
    shr cx, 1
    cmp cx, bx
    jae .grant
    pop ax
    ICALL w16_gfreeh
.nomem:
    mov ax, MMSYSERR_NOMEM
    stc
    jmp short .out
.got:
    mov [es:WO_GRANT], si
    mov [es:WO_RL], cx
    pop ax
    mov [w16mm_wo], ax
    clc
.out:
    pop bp
    pop dx
    pop cx
    pop bx
    ret

; w16mm_wkill - ES = the state: the stream closed, the grant and
; sndPlaySound's block given back, the state freed. No device is open after.
; Keeps all but ES
w16mm_wkill:
    push ax
    push si
    call w16mm_wstop
    mov si, [es:WO_GRANT]
    mov ax, 0x0107                  ; verb 7, sub-op 1: the grant freed
    call w16mm_snd
    mov ax, [es:WO_SNDH]
    or ax, ax
    jz .nosnd
    ICALL w16_gfreeh
.nosnd:
    mov word [es:WO_SIG], 0         ; never an HWAVEOUT again
    mov ax, [es:WO_SELF]
    ICALL w16_gfreeh
    xor ax, ax
    mov [w16mm_wo], ax
    mov [w16mm_wq], al
    pop si
    pop ax
    ret

; w16mm_wnote - AX = MM_WOM_*, DX:CX = its lParam, ES = the state: the
; notice posted to the callback's window, or its task's queue with no
; window. A function is batch 4's. Keeps all
w16mm_wnote:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov bl, [es:WO_CBT]
    cmp bl, 1                       ; CALLBACK_WINDOW
    jb .out
    cmp bl, 2                       ; CALLBACK_TASK
    ja .fn
    mov [w16_sm_msg], ax
    mov ax, [es:WO_SELF]
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], cx
    mov [w16_sm_lp + 2], dx
    mov ax, [es:WO_CB]
    cmp bl, 2
    je .task
    mov [w16_sm_hwnd], ax
    xor al, al                      ; at the back
    ICALL w16_post
    jmp short .out
.task:
    mov word [w16_sm_hwnd], 0
    xor dl, dl
    cmp byte [w16_ntk], 2
    jae .find
    mov al, dl
    ICALL w16_postq
    jmp short .out
.find:
    call w16t_find                  ; SI = its record
    jc .out
    mov al, dl
    call w16t_qpost
    jmp short .out
.fn:
    cmp bl, 3                       ; CALLBACK_FUNCTION: owed (124.32.6)
    jne .out
    call w16mm_tq
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16mm_wpush - DX:CX = a WAVEHDR, ES = the state: queued (WHDR_INQUEUE,
; WHDR_DONE cleared), the copy run and the tick asked for. AX = 0, or why
; not. Keeps the rest
w16mm_wpush:
    push si
    push ds
    mov ds, dx
    mov si, cx
    mov ax, WAVERR_UNPREPARED
    test byte [si + WH_FLAGS], WHDR_PREPARED
    jz .no
    mov ax, WAVERR_STILLPLAYING
    test byte [si + WH_FLAGS], WHDR_INQUEUE
    jnz .no
    and byte [si + WH_FLAGS], ~WHDR_DONE & 0xFF
    or byte [si + WH_FLAGS], WHDR_INQUEUE
    xor ax, ax
    mov [si + WH_NEXT], ax
    mov [si + WH_NEXT + 2], ax
    dec ax
    mov [si + WH_END], ax
    mov [si + WH_END + 2], ax
    lds si, [es:WO_TAIL]            ; after the last, or the first
    mov ax, ds
    or ax, ax
    jz .first
    mov [si + WH_NEXT], cx
    mov [si + WH_NEXT + 2], dx
    jmp short .tail
.first:
    mov [es:WO_HEAD], cx
    mov [es:WO_HEAD + 2], dx
.tail:
    mov [es:WO_TAIL], cx
    mov [es:WO_TAIL + 2], dx
    pop ds
    cmp word [es:WO_CUR + 2], 0
    jne .feed
    call w16mm_wcur
.feed:
    call w16mm_wfeed
    ICALL w16_tarm
    xor ax, ax
    pop si
    ret
.no:
    pop ds
    pop si
    ret

; w16mm_wfeed - ES = the state: the card's count read, the queue copied as
; far as the ring has room (or, paused, the ring padded), the stream opened
; or fed, the blocks it has played done; [w16mm_wq] says whether one is still
; queued. Keeps all
w16mm_wfeed:
    push ax
    push cx
    call w16mm_wpoll
    test byte [es:WO_FL], WOF_DEAD
    jz .live
    call w16mm_wdrop                ; nothing will play: all done now
    jmp short .done
.live:
    test byte [es:WO_FL], WOF_PAUSE
    jnz .pause
    call w16mm_wcopy
    call w16mm_wopen
    jmp short .feed
.pause:
    call w16mm_wpad
.feed:
    cmp byte [es:WO_OPEN], 0
    je .done
    mov cx, [es:WO_STG]             ; verb 1: the stream's new total
    sub cx, [es:WO_SBASE]
    mov al, 1
    mov ah, [es:WO_HAND]
    call w16mm_snd
.done:
    call w16mm_wdone
    xor al, al
    cmp word [es:WO_HEAD + 2], 0
    je .q
    inc ax
.q:
    mov [w16mm_wq], al
    pop cx
    pop ax
    ret

; w16mm_wpoll - ES = the state: the card's count folded into [WO_PLAY]; a
; stream that ended, or is gone, closed. Keeps all
w16mm_wpoll:
    cmp byte [es:WO_OPEN], 0
    je .ret
    push ax
    push bx
    push dx
    mov al, SND_V_PPOS
    mov ah, [es:WO_HAND]
    call w16mm_snd                  ; AX = the state, DX = played, exactly
    jnc .got
    mov al, 3
    mov ah, [es:WO_HAND]
    call w16mm_snd
.got:
    cmp ax, SND_ST_STALE            ; gone: its handle is not ours to close
    jne .st
    mov byte [es:WO_OPEN], 0
.st:
    cmp ax, SND_ST_ENDED
    jae .end
    mov bx, dx
    sub bx, [es:WO_CONS]
    mov [es:WO_CONS], dx
    add [es:WO_PLAY], bx
    adc word [es:WO_PLAY + 2], 0
    jmp short .out
.end:
    call w16mm_wstop
.out:
    pop dx
    pop bx
    pop ax
.ret:
    ret

; w16mm_wstop - ES = the state: the stream closed if it is open, and what was
; copied counted as played, so the next opens on an empty ring. Keeps all
w16mm_wstop:
    push ax
    cmp byte [es:WO_OPEN], 0
    je .base
    mov al, 2
    mov ah, [es:WO_HAND]
    call w16mm_snd
    mov byte [es:WO_OPEN], 0
.base:
    mov ax, [es:WO_STG]
    mov [es:WO_PLAY], ax
    mov [es:WO_SBASE], ax
    mov ax, [es:WO_STG + 2]
    mov [es:WO_PLAY + 2], ax
    mov [es:WO_SBASE + 2], ax
    mov word [es:WO_CONS], 0
    pop ax
    ret

; w16mm_wopen - ES = the state: the stream opened once a half is in the ring,
; its format set first (verb 11) when it is not 8-bit mono. A refusal is
; WOF_DEAD: nothing will play. Keeps all
w16mm_wopen:
    cmp byte [es:WO_OPEN], 0
    jne .ret
    push ax
    push cx
    push dx
    push si
    push di
    call w16mm_wlead                ; AX = the stream's total: none played
    cmp ax, [es:WO_HALF]
    jb .out
    mov cx, ax
    mov ah, [es:WO_FMT]
    or ah, ah
    jz .o
    mov al, SND_V_FMT
    call w16mm_snd
    or ax, ax
    jnz .dead
.o:
    mov al, 0
    mov ah, SND_OPENF_RING
    mov dx, [es:WO_RATE]
    mov si, [es:WO_GRANT]
    call w16mm_snd                  ; AL = 0 and AH = the handle, else why
    or al, al
    jnz .dead
    mov [es:WO_HAND], ah
    mov byte [es:WO_OPEN], 1
    jmp short .out
.dead:
    or byte [es:WO_FL], WOF_DEAD
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.ret:
    ret

; w16mm_wcopy - ES = the state: the queue copied into the ring as far as it
; has room, in pieces that cross neither the ring's end nor a block's 64 KB
; (its selector stepped by __AHINCR, 8); once it is all in, silence to the
; card's next half. Keeps all
w16mm_wcopy:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
.lp:
    cmp word [es:WO_CUR + 2], 0
    je .pad
    call w16mm_wlead                ; AX = the lead, DI = the ring's place
    mov cx, [es:WO_RL]
    sub cx, ax                      ; CX = the room
    jz .out
    mov ax, [es:WO_RL]
    sub ax, di
    cmp cx, ax
    jbe .r1
    mov cx, ax                      ; ...to the ring's end
.r1:
    push ds
    lds si, [es:WO_CUR]
    mov ax, [si + WH_LEN]
    mov dx, [si + WH_LEN + 2]
    mov bx, [si + WH_DATA + 2]
    mov si, [si + WH_DATA]          ; BX:SI = its data
    pop ds
    sub ax, [es:WO_COFF]
    sbb dx, [es:WO_COFF + 2]        ; DX:AX = what is left of it
    jc .next
    or dx, dx
    jnz .r2
    or ax, ax
    jz .next                        ; all of it copied
    cmp cx, ax
    jbe .r2
    mov cx, ax
.r2:
    add si, [es:WO_COFF]            ; BX:SI = where the copy is
    mov ax, [es:WO_COFF + 2]
    adc ax, 0
    push cx
    mov cl, 3
    shl ax, cl
    pop cx
    add bx, ax                      ; __AHINCR, 8, for each 64 KB
    mov ax, si
    neg ax                          ; ...to its 64 KB's end
    jz .r3
    cmp cx, ax
    jbe .r3
    mov cx, ax
.r3:
    call w16mm_wput
    add [es:WO_COFF], cx
    adc word [es:WO_COFF + 2], 0
    jmp .lp
.next:
    call w16mm_wnext
    jmp .lp
.pad:
    call w16mm_wpad
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16mm_wpad - ES = the state: silence copied to the card's next half, as far
; as the ring has room. The halves lie on the ring's own, so none crosses its
; end. Keeps all
w16mm_wpad:
    push ax
    push bx
    push cx
    push si
    push di
.lp:
    call w16mm_wlead                ; AX = the lead, DI = the ring's place
    mov cx, [es:WO_RL]
    sub cx, ax                      ; CX = the room
    jz .out
    mov ax, [es:WO_STG]
    sub ax, [es:WO_SBASE]
    mov bx, [es:WO_HALF]
    dec bx
    and ax, bx                      ; how far into the half
    jz .out
    neg ax
    add ax, bx
    inc ax                          ; ...and to its end
    cmp cx, ax
    jbe .p1
    mov cx, ax
.p1:
    cmp cx, W16MM_SILN
    jbe .p2
    mov cx, W16MM_SILN
.p2:
    mov bx, es
    mov si, WO_SIL
    call w16mm_wput
    jmp short .lp
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; w16mm_wlead - ES = the state -> AX = the bytes copied and not yet played,
; DI = where the next goes in the ring. Keeps the rest
w16mm_wlead:
    mov ax, [es:WO_STG]
    sub ax, [es:WO_SBASE]           ; the stream's total
    mov di, [es:WO_RL]
    dec di
    and di, ax
    sub ax, [es:WO_CONS]
    ret

; w16mm_wput - BX:SI = CX bytes, DI = their place in the ring, ES = the
; state: staged into the grant (verb 6, from the segment DS names), [WO_STG]
; moved on. Keeps all
w16mm_wput:
    push ax
    push di
    push ds
    add di, [es:WO_GRANT]
    mov ds, bx
    mov ax, 6
    call w16mm_snd
    pop ds
    pop di
    add [es:WO_STG], cx
    adc word [es:WO_STG + 2], 0
    pop ax
    ret

; w16mm_wnext - ES = the state: the block being copied is all in. Again from
; its start while its loop has passes left; else its end noted and the next
; block's turn. Keeps all
w16mm_wnext:
    push ax
    push cx
    push dx
    push si
    push ds
    mov ax, [es:WO_COFF]            ; an empty block has no pass to repeat
    or ax, [es:WO_COFF + 2]
    jz .end
    mov ax, [es:WO_LOOPS]
    or ax, [es:WO_LOOPS + 2]
    jz .end
    sub word [es:WO_LOOPS], 1
    sbb word [es:WO_LOOPS + 2], 0
    xor ax, ax
    mov [es:WO_COFF], ax
    mov [es:WO_COFF + 2], ax
    jmp short .out
.end:
    lds si, [es:WO_CUR]
    mov ax, [es:WO_STG]
    mov [si + WH_END], ax
    mov ax, [es:WO_STG + 2]
    mov [si + WH_END + 2], ax
    mov cx, [si + WH_NEXT]
    mov dx, [si + WH_NEXT + 2]
    call w16mm_wcur
.out:
    pop ds
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16mm_wcur - DX:CX = a block, or 0, ES = the state: it is the one copied
; next, from its start; its loop's passes counted when it has both
; WHDR_BEGINLOOP and WHDR_ENDLOOP. Keeps all
w16mm_wcur:
    push ax
    push si
    push ds
    mov [es:WO_CUR], cx
    mov [es:WO_CUR + 2], dx
    xor ax, ax
    mov [es:WO_COFF], ax
    mov [es:WO_COFF + 2], ax
    mov [es:WO_LOOPS], ax
    mov [es:WO_LOOPS + 2], ax
    or dx, dx
    jz .out
    mov ds, dx
    mov si, cx
    mov al, [si + WH_FLAGS]
    and al, WHDR_BEGINLOOP | WHDR_ENDLOOP
    cmp al, WHDR_BEGINLOOP | WHDR_ENDLOOP
    jne .out
    mov ax, [si + WH_LOOPS]
    mov si, [si + WH_LOOPS + 2]
    sub ax, 1
    sbb si, 0
    jc .out                         ; 0 passes: once
    mov [es:WO_LOOPS], ax
    mov [es:WO_LOOPS + 2], si
.out:
    pop ds
    pop si
    pop ax
    ret

; w16mm_wdrop - ES = the state: nothing more is copied, and every queued
; block ends where the count already is, so w16mm_wdone finishes them all.
; Keeps all
w16mm_wdrop:
    push ax
    push bx
    push si
    push ds
    xor ax, ax
    mov [es:WO_CUR], ax
    mov [es:WO_CUR + 2], ax
    mov si, [es:WO_HEAD]
    mov bx, [es:WO_HEAD + 2]
.lp:
    or bx, bx
    jz .out
    mov ds, bx
    mov [si + WH_END], ax
    mov [si + WH_END + 2], ax
    mov bx, [si + WH_NEXT + 2]
    mov si, [si + WH_NEXT]
    jmp short .lp
.out:
    pop ds
    pop si
    pop bx
    pop ax
    ret

; w16mm_wdone - ES = the state: each block at the queue's head that is all
; copied and played to its end gets WHDR_DONE, loses WHDR_INQUEUE, and its
; notice goes. Keeps all
w16mm_wdone:
    push ax
    push bx
    push cx
    push dx
    push si
.lp:
    mov cx, [es:WO_HEAD]
    mov dx, [es:WO_HEAD + 2]
    or dx, dx
    jz .out
    cmp cx, [es:WO_CUR]
    jne .cp
    cmp dx, [es:WO_CUR + 2]
    je .out                         ; still being copied
.cp:
    push ds
    mov ds, dx
    mov si, cx
    mov ax, [es:WO_PLAY]
    mov bx, [es:WO_PLAY + 2]
    sub ax, [si + WH_END]
    sbb bx, [si + WH_END + 2]       ; CF: not played to its end yet
    jc .yet
    and byte [si + WH_FLAGS], ~WHDR_INQUEUE & 0xFF
    or byte [si + WH_FLAGS], WHDR_DONE
    mov ax, [si + WH_NEXT]
    mov bx, [si + WH_NEXT + 2]
    pop ds
    mov [es:WO_HEAD], ax
    mov [es:WO_HEAD + 2], bx
    or bx, bx
    jnz .note
    mov [es:WO_TAIL], bx
    mov [es:WO_TAIL + 2], bx
.note:
    mov ax, MM_WOM_DONE
    call w16mm_wnote                ; DX:CX = the block
    jmp short .lp
.yet:
    pop ds
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16mm_wfr - DX:AX = bytes, CL = a frame's shift -> DX:AX = frames. Keeps
; the rest
w16mm_wfr:
    push cx
    xor ch, ch
    jcxz .out
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
.out:
    pop cx
    ret

; w16mm_ms - DX:AX = frames, BX = frames a second -> DX:AX = milliseconds,
; to the nearest as Windows' MulDiv gives them: whole seconds times 1000, and
; the rest's. Keeps the rest
w16mm_ms:
    push cx
    push si
    mov cx, ax
    mov ax, dx
    xor dx, dx
    div bx                          ; AX = the seconds' high word
    mov si, ax
    mov ax, cx
    div bx                          ; ...their low, DX = the frames left
    push dx
    mov cx, 1000
    mul cx                          ; DX:AX = the low word's milliseconds
    xchg ax, si
    push dx
    mul cx
    pop dx
    add dx, ax                      ; DX:SI = the seconds', in milliseconds
    pop ax
    push dx
    mul cx
    push bx
    shr bx, 1
    add ax, bx
    adc dx, 0
    pop bx
    div bx                          ; AX = the rest's, 1000 at most
    pop dx
    add ax, si
    adc dx, 0
    pop si
    pop cx
    ret

; w16mm_snd - OSAPI_SND_STREAM, ES kept
w16mm_snd:
    push es
    call OSAPI_SND_STREAM
    pop es
    ret

; w16mm_sget - sndPlaySound's sound at [w16mm_lim], BP = its flags: a file
; read whole into a block of the program's heap, or SND_MEMORY's where it
; lies, and its chunks found. CF=0 ES:DI = its fmt chunk's data,
; [w16mm_bsel]:[w16mm_boff] and [w16mm_rem] its data's, BX = the block (0
; none); CF=1 it is not a WAV, and no block is held. Clobbers AX, CX, DX, SI
w16mm_sget:
    xor bx, bx
    mov cx, 0xFFFF                  ; CX = how far it may be read, at most
    test bp, W16MM_SNDMEM
    jz .file
    les di, [w16mm_lim]
    call w16mm_sfind
    ret
.file:
    mov ax, 0x3D00
    call w16mm_named                ; AX = its DOS handle
    jc .no
    mov si, ax                      ; SI = the handle
    mov bx, ax
    mov ax, 0x4202
    xor cx, cx
    xor dx, dx
    ICALL w16_dcall                 ; DX:AX = its length
    jc .close
    mov [w16mm_done], ax             ; [w16mm_done] = the length, and the
    mov [w16mm_done + 2], dx         ;   bytes left to read
    mov [w16mm_rem], ax
    mov [w16mm_rem + 2], dx
    mov ax, 0x4200
    mov bx, si
    xor cx, cx
    xor dx, dx
    ICALL w16_dcall
    jc .close
    mov ax, [w16mm_done]
    mov dx, [w16mm_done + 2]
    or dx, dx
    jnz .big
    cmp ax, 44                      ; a header and a chunk at least
    jb .close
.big:
    xor cl, cl                      ; GMEM_FIXED
    ICALL w16_gnew                  ; AX = the handle, DX = the block
    or ax, ax
    jz .close
    mov bx, ax                      ; BX = the block from here
    mov ax, dx
    ICALL w16_pblk                  ; AX = its selector, tiled over 64 KB
    mov di, ax                      ; DI = its first
    mov [w16mm_bsel], ax
    mov word [w16mm_boff], 0
.rd:
    mov cx, [w16mm_rem]
    or cx, [w16mm_rem + 2]
    jz .rdone
    mov cx, 0x8000                  ; 32 KB a read, so none crosses a 64 KB
    cmp word [w16mm_rem + 2], 0
    jne .r1
    cmp [w16mm_rem], cx
    jae .r1
    mov cx, [w16mm_rem]
.r1:
    push bx
    push es
    mov bx, si
    mov ah, 0x3F
    mov dx, [w16mm_boff]
    mov es, [w16mm_bsel]
    ICALL w16_dcall                 ; AX = read
    pop es
    pop bx
    jc .free
    cmp ax, cx
    jne .free                       ; short: not the file it was
    sub [w16mm_rem], ax
    sbb word [w16mm_rem + 2], 0
    add [w16mm_boff], ax
    jnc .rd
    add word [w16mm_bsel], 8         ; __AHINCR
    jmp short .rd
.rdone:
    call .shut
    mov es, di
    xor di, di
    mov cx, 0xFFFF
    cmp word [w16mm_done + 2], 0
    jne .fd
    mov cx, [w16mm_done]
.fd:
    call w16mm_sfind
    jc .drop
    mov ax, [w16mm_done]             ; the data no longer than the file holds
    mov dx, [w16mm_done + 2]
    sub ax, [w16mm_boff]
    sbb dx, 0
    cmp dx, [w16mm_rem + 2]
    ja .ok
    jb .cut
    cmp ax, [w16mm_rem]
    jae .ok
.cut:
    mov [w16mm_rem], ax
    mov [w16mm_rem + 2], dx
.ok:
    clc
    ret
.free:
    call .shut
.drop:
    mov ax, bx
    ICALL w16_gfreeh
    jmp short .no
.close:
    call .shut
.no:
    stc
    ret
.shut:                              ; the file closed. Keeps all
    push ax
    push bx
    mov bx, si
    mov ah, 0x3E
    ICALL w16_dcall
    pop bx
    pop ax
    ret

; w16mm_sfind - ES:DI = a WAV image, CX = how much of it may be read (FFFFh:
; to the 64 KB's end): its fmt chunk and its data chunk found among the
; chunks in its first 64 KB. CF=0 ES:DI = the fmt chunk's data,
; [w16mm_bsel]:[w16mm_boff] and [w16mm_rem] the data's; CF=1 it is not a
; RIFF WAVE with both. Keeps BX, BP
w16mm_sfind:
    cmp cx, 12
    jb .no
    cmp word [es:di], W16MM_RIFF
    jne .no
    cmp word [es:di + 2], W16MM_FF
    jne .no
    cmp word [es:di + 8], 'WA'
    jne .no
    cmp word [es:di + 10], 'VE'
    jne .no
    mov ax, [es:di + 4]             ; the RIFF's own end, if it is sooner
    mov dx, [es:di + 6]
    add ax, 8
    adc dx, 0
    jnz .lim
    cmp ax, cx
    jae .lim
    mov cx, ax
.lim:
    add cx, di                      ; CX = the end, as an offset
    jnc .lim1
    mov cx, 0xFFFF
.lim1:
    mov si, di
    add si, 12                      ; SI = a chunk
    xor dx, dx                      ; DX = the fmt chunk's data, 0 none yet
.lp:
    mov ax, si
    add ax, 8                       ; AX = its data
    jc .no
    cmp ax, cx
    ja .no
    cmp word [es:si], 'fm'
    jne .d
    cmp word [es:si + 2], 't '
    jne .d
    push ax
    add ax, 16                      ; a PCMWAVEFORMAT's room
    jc .nofmt
    cmp cx, ax
.nofmt:
    pop ax
    jc .no
    mov dx, ax
    jmp short .next
.d:
    cmp word [es:si], 'da'
    jne .next
    cmp word [es:si + 2], 'ta'
    jne .next
    or dx, dx
    jz .no                          ; data before fmt
    mov [w16mm_boff], ax
    mov [w16mm_bsel], es
    mov ax, [es:si + 4]
    mov [w16mm_rem], ax
    mov ax, [es:si + 6]
    mov [w16mm_rem + 2], ax
    mov di, dx
    clc
    ret
.next:
    cmp word [es:si + 6], 0         ; a chunk over 64 KB before them
    jne .no
    add ax, [es:si + 4]
    jc .no
    inc ax                          ; its pad byte
    jz .no
    and al, 0xFE
    mov si, ax
    jmp short .lp
.no:
    stc
    ret

; =============================================================================
; midiOut (124.32.7): device 0 over SOUND.DRV's MPU-401 verbs (§34.13), as
; MIDIRack drives them (§105.8.5). The HMIDIOUT is a fixed block of the
; program's heap, laid out where w16mm_wnote and the calls read an HWAVEOUT:
; its handle at 2, the callback's kind at 7, its address at 20, dwInstance at
; 90. SNDV_MIDW reads the CALLER'S segment, so every byte goes out of
; w16_scratch, in WIN16's own.
; =============================================================================

%include "drivers/sound/sndpkg.inc" ; SOUND.DRV's door, one ABI (SPEC.md 20.11)

MO_SIG      equ 0                   ; W16MM_OSIG
MO_SELF     equ 2                   ; the HMIDIOUT (WO_SELF's place)
MO_RUN      equ 4                   ; the running status, 0 none
MO_CBT      equ 7                   ; the callback's kind (WO_CBT's place)
MO_CB       equ 20                  ; dwCallback (WO_CB's place)
MO_LEN      equ W16MM_INST + 4      ; ...and dwInstance where waveOut's is
W16MM_OSIG  equ 0x4F4D
MH_DATA     equ 0                   ; MIDIHDR: lpData
MH_LEN      equ 4                   ;   dwBufferLength
MH_FLAGS    equ 16                  ;   dwFlags, a WAVEHDR's place: w16mm_prep
MHDR_DONE   equ 0x01
MHDR_PREPARED equ 0x02
MM_MOM_OPEN     equ 0x3C7
MM_MOM_CLOSE    equ 0x3C8
MM_MOM_DONE     equ 0x3C9
MIDIERR_UNPREPARED equ 64
MIDIERR_NOTREADY   equ 67

; midiOutGetNumDevs (MMSYSTEM.201): 1 with an MPU-401 to send to
W16SYS w16mm_monum
    call w16mm_mhas
    mov ax, 0
    jc .out
    inc ax
.out:
    xor dx, dx
    ret

; midiOutGetDevCaps (MMSYSTEM.202) - args: +0 wSize, +2 lpCaps, +6 wDeviceID:
; a MIDIOUTCAPS, 50 bytes. 0, or MMSYSERR_BADDEVICEID
W16SYS w16mm_mocaps
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 6]
    call w16mm_mdev                 ; AX = why not
    jc .out
    mov cx, [es:bx]                 ; CX = what the program has room for
    cmp cx, 50
    jbe .sz
    mov cx, 50
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
    mov word [es:di], 1             ; MM_MICROSOFT, MM_MPU401_MIDIOUT
    mov word [es:di + 2], 10
    mov word [es:di + 4], 0x0100
    push di
    add di, 6
    mov si, w16mm_s_mpu             ; XSEC-OK: the part's string, read at cs:
.nm:
    cs lodsb
    stosb
    or al, al
    jnz .nm
    pop di
    cmp cx, 46                      ; wTechnology, wVoices, wNotes,
    jb .ok                          ;   wChannelMask
    mov byte [es:di + 38], 1        ; MOD_MIDIPORT
    mov word [es:di + 44], 0xFFFF   ; all sixteen
.ok:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

w16mm_s_mpu: db 'os8088 MPU-401', 0

; midiOutOpen (MMSYSTEM.204) - args: +0 dwFlags, +4 dwInstance, +8
; dwCallback, +12 wDeviceID, +14 lphMidiOut: the interface taken (SNDV_MIDOPEN)
; and MM_MOM_OPEN. 0 and the HMIDIOUT, or why not
W16SYS w16mm_moopen
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 12]
    call w16mm_mdev
    jc .out
    mov ax, MMSYSERR_ALLOCATED
    cmp word [w16mm_mo], 0
    jne .out
    mov si, [es:bx + 8]             ; DI:SI = dwCallback
    mov di, [es:bx + 10]
    mov cl, [es:bx + 2]
    and cl, 7
    call w16mm_cbfn                 ; CALLBACK_FUNCTION's block
    jc .out
    push word [es:bx + 6]           ; dwInstance
    push word [es:bx + 4]
    call w16mm_momake               ; ES = the state, AX = the handle
    pop si
    pop di
    jc .out
    mov [es:W16MM_INST], si
    mov [es:W16MM_INST + 2], di
    push es
    push ax
    ICALL w16_args
    les di, [es:bx + 14]
    mov cx, es
    or cx, di
    pop ax
    jz .nop
    mov [es:di], ax
.nop:
    pop es
    mov ax, MM_MOM_OPEN
    call w16mm_monote0
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; midiOutClose (MMSYSTEM.205) - args: +0 hMidiOut: MM_MOM_CLOSE, and the
; interface given back, every note off. 0
W16SYS w16mm_moclose
    call w16mm_marg1
    jc .out
    mov ax, MM_MOM_CLOSE
    call w16mm_monote0
    call w16mm_mokill
    xor ax, ax
.out:
    xor dx, dx
    ret

; midiOutPrepareHeader, midiOutUnprepareHeader (MMSYSTEM.206, 207) - args +0
; wSize, +2 lpMidiOutHdr, +6 hMidiOut: waveOut's (w16mm_prep, w16mm_unprep).
; A long message is sent before it returns, so a block is never queued
W16SYS w16mm_moprep
    call w16mm_margh
    jmp w16mm_prep
W16SYS w16mm_mounprep
    call w16mm_margh
    jmp w16mm_unprep

; midiOutShortMsg (MMSYSTEM.208) - args: +0 dwMsg, +4 hMidiOut: the status and
; the data bytes it carries, or under the running status the data bytes
; alone. 0, or MIDIERR_NOTREADY
W16SYS w16mm_moshort
    push si
    ICALL w16_args
    mov ax, [es:bx + 4]
    mov cx, [es:bx]                 ; CL = the status, CH and DL its data
    mov dx, [es:bx + 2]
    call w16mm_margs
    jc .out
    mov si, w16_scratch
    mov [si], cx
    mov [si + 2], dl
    mov al, cl
    or al, al
    js .st
    mov al, [es:MO_RUN]             ; running status: CL is the first datum
    or al, al
    jz .ok                          ;   (none: nothing to send)
    call w16mm_mlen
    jmp short .n
.st:
    call w16mm_mlen                 ; AH = the data bytes
    inc ah                          ;   and the status
    cmp al, 0xF0
    jae .sys
    mov [es:MO_RUN], al             ; a channel's: running status from here
    jmp short .n
.sys:
    cmp al, 0xF8                    ; system common ends it; real time leaves
    jae .n                          ;   it be
    mov byte [es:MO_RUN], 0
.n:
    mov cl, ah
    xor ch, ch
    call w16mm_msend                ; AX = 0, or why not
    jmp short .out
.ok:
    xor ax, ax
.out:
    xor dx, dx
    pop si
    ret

; midiOutLongMsg (MMSYSTEM.209) - args: +0 wSize, +2 lpMidiOutHdr, +6
; hMidiOut: dwBufferLength bytes sent, 16 at a time through w16_scratch, then
; MHDR_DONE and MM_MOM_DONE. 0, MIDIERR_UNPREPARED or MIDIERR_NOTREADY
W16SYS w16mm_molong
    push si
    push di
    push bp
    call w16mm_margh                ; DX:CX = the header
    jc .ret
    push ds
    mov ds, dx
    mov si, cx
    mov ax, MIDIERR_UNPREPARED
    test byte [si + MH_FLAGS], MHDR_PREPARED
    jz .nprep
    and byte [si + MH_FLAGS], ~MHDR_DONE & 0xFF
    mov bp, [si + MH_LEN]           ; BP = how many
    cmp word [si + MH_LEN + 2], 0
    je .len
    mov bp, 0xFFFF                  ; (one selector's worth at most)
.len:
    mov di, [si + MH_DATA]          ; BX:DI = the bytes
    mov bx, [si + MH_DATA + 2]
    pop ds
    push dx                         ; [the header]
    push cx
    mov dx, di                      ; BX:DX = the bytes
.lp:
    mov cx, 16
    cmp bp, cx
    jae .c
    mov cx, bp
.c:
    jcxz .done
    sub bp, cx
    push es                         ; [the state]
    push cx
    push ds
    pop es                          ; ES:DI = w16_scratch, in WIN16's segment
    mov di, w16_scratch
    mov si, dx
    push ds
    mov ds, bx
    cld
    rep movsb
    pop ds
    mov dx, si                      ; past them
    pop cx
    mov si, w16_scratch
    call w16mm_msend                ; AX = 0, or why not
    pop es
    or ax, ax
    jz .lp
    add sp, 4                       ; (the header: not done)
    jmp short .ret
.done:
    pop cx                          ; DX:CX = the header
    pop dx
    push ds
    mov ds, dx
    mov si, cx
    or byte [si + MH_FLAGS], MHDR_DONE
    pop ds
    mov ax, MM_MOM_DONE
    call w16mm_monote
    xor ax, ax
    jmp short .ret
.nprep:
    pop ds
.ret:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; midiOutReset (MMSYSTEM.210) - args: +0 hMidiOut: the interface given back
; and taken again, which turns every note off and resets every controller. 0
W16SYS w16mm_moreset
    call w16mm_marg1
    jc .out
    push bx
    mov bl, SNDV_MIDCLOSE
    call w16mm_mpu
    mov bl, SNDV_MIDOPEN
    call w16mm_mpu
    pop bx
    xor ax, ax
    mov [es:MO_RUN], al
.out:
    xor dx, dx
    ret

; midiOutGetID (MMSYSTEM.215) - args: +0 lpuDeviceID, +4 hMidiOut: 0, the one
; device
W16SYS w16mm_moid
    push di
    ICALL w16_args
    mov ax, [es:bx + 4]
    push es
    call w16mm_most
    pop es
    mov ax, MMSYSERR_INVALHANDLE
    jc .out
    les di, [es:bx]
    mov word [es:di], 0
    xor ax, ax
.out:
    xor dx, dx
    pop di
    ret

; w16mm_momake - CL = the callback's kind, DI:SI = dwCallback: the interface
; taken and an HMIDIOUT made. CF=0 ES = its state, AX = its handle, and it is
; [w16mm_mo]; CF=1 AX = MMSYSERR_ALLOCATED (another package's) or
; MMSYSERR_NOMEM. Keeps BX, CX, DX
w16mm_momake:
    push bx
    push cx
    push dx
    mov bl, SNDV_MIDOPEN
    call w16mm_mpu
    mov bx, MMSYSERR_ALLOCATED
    jc .no
    push cx
    mov ax, MO_LEN
    xor dx, dx
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; AX = the handle, DX = the block
    pop cx
    or ax, ax
    jnz .made
    mov bl, SNDV_MIDCLOSE
    call w16mm_mpu
    mov bx, MMSYSERR_NOMEM
.no:
    mov ax, bx
    stc
    jmp short .out
.made:
    W16SEG es, dx
    mov word [es:MO_SIG], W16MM_OSIG
    mov [es:MO_SELF], ax
    mov [es:MO_CBT], cl
    mov [es:MO_CB], si
    mov [es:MO_CB + 2], di
    mov [w16mm_mo], ax
    clc
.out:
    pop dx
    pop cx
    pop bx
    ret

; w16mm_mokill - ES = the state: the interface given back, the state freed.
; No notice. Keeps all but ES
w16mm_mokill:
    push ax
    push bx
    mov bl, SNDV_MIDCLOSE
    call w16mm_mpu
    mov word [es:MO_SIG], 0         ; never an HMIDIOUT again
    mov ax, [es:MO_SELF]
    ICALL w16_gfreeh
    mov word [w16mm_mo], 0
    pop bx
    pop ax
    ret

; w16mm_monote0 - AX = MM_MOM_*, ES = the state: w16mm_monote with lParam 0.
; w16mm_monote - DX:CX its lParam: w16mm_wnote's, and the tick asked for, so a
; procedure's call is made where the program waits. Keeps all
w16mm_monote0:
    push cx
    push dx
    xor cx, cx
    xor dx, dx
    call w16mm_monote
    pop dx
    pop cx
    ret
w16mm_monote:
    call w16mm_wnote
    ICALL w16_tarm
    ret

; w16mm_mlen - AL = a status byte -> AH = the data bytes it carries. Keeps
; the rest
w16mm_mlen:
    mov ah, 2                       ; notes, aftertouch, controllers, the wheel
    cmp al, 0xC0
    jb .r
    cmp al, 0xE0
    jae .e
    dec ah                          ; a program, channel pressure: 1
    ret
.e:
    cmp al, 0xF0
    jb .r
    cmp al, 0xF2                    ; the song position: 2
    je .r
    dec ah
    cmp al, 0xF1                    ; the time code, the song: 1
    je .r
    cmp al, 0xF3
    je .r
    dec ah                          ; the rest: none
.r:
    ret

; w16mm_msend - DS:SI -> CX bytes in WIN16's own segment: sent (SNDV_MIDW).
; AX = 0, or MIDIERR_NOTREADY when the interface stopped taking them. Keeps
; the rest
w16mm_msend:
    push bx
    mov bl, SNDV_MIDW
    call w16mm_mpu
    mov ax, 0
    jnc .r
    mov al, MIDIERR_NOTREADY
.r:
    pop bx
    ret

; w16mm_mdev - AX = a device ID: CF=0 it is device 0 or MIDI_MAPPER and an
; MPU-401 answers; CF=1 AX = MMSYSERR_BADDEVICEID. Keeps the rest
w16mm_mdev:
    inc ax
    cmp ax, 2                       ; 0, or MIDI_MAPPER (-1)
    jae .no
    call w16mm_mhas
    jnc .r
.no:
    mov ax, MMSYSERR_BADDEVICEID
    stc
.r:
    ret

; w16mm_mhas - CF=0 SOUND.DRV is loaded and an MPU-401 answered it. Keeps
; all but AX
w16mm_mhas:
    push bx
    mov bl, SNDV_IDENT
    call w16mm_mpu
    jc .r
    cmp ax, SND_SIG
    stc
    jne .r
    mov bl, SNDV_MIDINFO
    call w16mm_mpu
.r:
    pop bx
    ret

; w16mm_mpu - BL = an SNDV_* verb, CX and SI as it takes them:
; OSAPI_DRV_CALL to SOUND.DRV. Answers in AX and CF (CF=1 AX = 0: none
; loaded). Keeps the rest
w16mm_mpu:
    push bx
    push cx
    push dx
    push si
    push es
    mov bh, DRVC_SOUND
    call OSAPI_DRV_CALL
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16mm_marg1 - the argument hMidiOut, at +0: CF=0 ES = its state; CF=1 AX =
; MMSYSERR_INVALHANDLE. w16mm_margh: +0 wSize, +2 a far pointer, +6
; hMidiOut, and DX:CX = the pointer. w16mm_margs: AX = the handle. Keeps the
; rest
w16mm_marg1:
    push bx
    ICALL w16_args
    mov ax, [es:bx]
    pop bx
    jmp short w16mm_margs
w16mm_margh:
    push bx
    ICALL w16_args
    mov cx, [es:bx + 2]
    mov dx, [es:bx + 4]
    mov ax, [es:bx + 6]
    pop bx
    or dx, dx
    jnz w16mm_margs
    mov ax, MMSYSERR_INVALPARAM
    stc
    ret
w16mm_margs:
    call w16mm_most
    jnc .ret
    mov ax, MMSYSERR_INVALHANDLE
.ret:
    ret

; w16mm_most - AX = an HMIDIOUT -> ES = its state, CF=0; CF=1 it is not the
; one open. Keeps all but ES
w16mm_most:
    or ax, ax
    jz .no
    cmp ax, [w16mm_mo]
    jne .no
    W16SEG es, ax                   ; a fixed block: its handle is its place
    cmp word [es:MO_SIG], W16MM_OSIG
    jne .no
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; timeSetEvent and CALLBACK_FUNCTION (124.32.6): calls owed to the program,
; made where it waits for a message. One fixed block of the program's heap,
; [w16mm_tev]: eight event rows, a queue of eight calls, and a flag while
; one runs. A row and a call are the same sixteen bytes - the procedure and
; its first four arguments; the fifth is always 0.
; =============================================================================

TV_BUSY     equ 0                   ; 1 while a call runs
TV_QN       equ 1                   ; calls queued
TV_QH       equ 2                   ; ...the first of them
TV_NEXTID   equ 4                   ; the last event ID handed out
TV_Q        equ 6                   ; the queue, TV_NQ calls
TV_NQ       equ 8
TV_T        equ TV_Q + TV_NQ * 16   ; the events, TV_NT rows
TV_NT       equ 8
TV_LEN      equ TV_T + TV_NT * TT_SIZE
TC_PROC     equ 0                   ; a call: the procedure,
TC_W1       equ 4                   ;   its UINTs,
TC_W2       equ 6
TC_USER     equ 8                   ;   dwUser (dwInstance),
TC_DW1      equ 12                  ;   and dw1
TT_DUE      equ 16                  ; an event: a call, and when it is due,
TT_PER      equ 20                  ;   its delay,
TT_FL       equ 22                  ;   TTF_*
TT_SIZE     equ 24
TTF_USED    equ 1
TTF_PER     equ 2                   ; TIME_PERIODIC
TIMERR_NOCANDO  equ 97

section .text
; w16_mmre / w16_mmowed - AL = 0 for GetMessage, 1 for PeekMessage: a call
; owed by MMSYSTEM made (124.32.6), the import asked again after it. w16_mmre
; first makes the import that waits current again (w16_reidx), for a call
; from a callback. CF=0 made, and the gate enters it; CF=1 none
w16_mmre:
    cmp word [w16mm_tev], 0
    stc
    je w16_mmowed.ret
    push ax
    call w16_reidx
    pop ax
w16_mmowed:
    cmp word [w16mm_tev], 0
    stc
    je .ret
    SCALL w16mm_tsend
.ret:
    ret

; w16mm_tback - a call made by w16mm_tsend has returned, CX = the import's
; kind: the next may start, and the import asks again
SCONT w16mm_tback
    push es
    call w16mm_tes
    jc .k
    mov byte [es:TV_BUSY], 0
.k:
    pop es
    cmp cl, 1
    je .peek
    ICALL w16u_getmsg
    ret
.peek:
    ICALL w16u_peekmsg
    ret

; timeSetEvent (MMSYSTEM.602) - args: +0 uFlags, +2 dwUser, +6 lpFunction,
; +10 uResolution, +12 uDelay: the event's ID, or 0
W16SYS w16mm_tset
    push si
    push di
    ICALL w16_args
    xor ax, ax
    mov cx, [es:bx + 12]            ; CX = the delay
    mov si, [es:bx + 6]             ; DI:SI = the procedure
    mov di, [es:bx + 8]
    mov dx, si
    or dx, di
    jz .no
    jcxz .no
    call w16mm_tget
    jnc .go
.no:
    jmp .out
.go:
    push word [es:bx + 4]           ; dwUser
    push word [es:bx + 2]
    mov dl, [es:bx]                 ; DL = the flags
    call w16mm_tes
    mov bx, TV_T
    mov ax, TV_NT
.f:
    test byte [es:bx + TT_FL], TTF_USED
    jz .free
    add bx, TT_SIZE
    dec ax
    jnz .f
    add sp, 4                       ; a ninth: 0
    jmp short .out
.free:
    pop word [es:bx + TC_USER]
    pop word [es:bx + TC_USER + 2]
    mov [es:bx + TC_PROC], si
    mov [es:bx + TC_PROC + 2], di
    mov [es:bx + TT_PER], cx
    and dl, 1                       ; TIME_PERIODIC
    shl dl, 1
    or dl, TTF_USED
    mov [es:bx + TT_FL], dl
    xor ax, ax
    mov [es:bx + TC_W2], ax
    mov [es:bx + TC_DW1], ax
    mov [es:bx + TC_DW1 + 2], ax
.id:
    inc word [es:TV_NEXTID]         ; never 0
    jz .id
    call w16mm_now                  ; DX:AX: due a delay from now
    add ax, cx
    adc dx, 0
    mov [es:bx + TT_DUE], ax
    mov [es:bx + TT_DUE + 2], dx
    mov ax, [es:TV_NEXTID]
    mov [es:bx + TC_W1], ax
    mov byte [w16mm_wq], 1          ; the tick looks
    ICALL w16_tarm
.out:
    xor dx, dx
    pop di
    pop si
    ret

; timeKillEvent (MMSYSTEM.603) - args: +0 wID: 0, or TIMERR_NOCANDO
W16SYS w16mm_tkill
    ICALL w16_args
    mov dx, [es:bx]
    mov ax, TIMERR_NOCANDO
    call w16mm_tes
    jc .out
    mov bx, TV_T
    mov cx, TV_NT
.f:
    test byte [es:bx + TT_FL], TTF_USED
    jz .n
    cmp [es:bx + TC_W1], dx
    jne .n
    mov byte [es:bx + TT_FL], 0
    xor ax, ax
    jmp short .out
.n:
    add bx, TT_SIZE
    loop .f
.out:
    xor dx, dx
    ret

; w16mm_tsend - AL = the import's kind (0 GetMessage, 1 PeekMessage), the
; import current: the first call owed - a device's queued, else a due event's
; - begun (w16_cbopen, its arguments, w16_cbcall), w16mm_tback continuing.
; CF=0 begun; CF=1 none, or one runs already. Keeps BX, CX, DX, SI, DI, ES
w16mm_tsend:
    ; STKBALANCE-LOOP: the call's eight words are pushed once and the loop
    ; below pops six of them into the program's stack, the last two after it
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bl, al
    call w16mm_tes
    jc .none
    test byte [es:TV_BUSY], 1
    jnz .none
    cmp byte [es:TV_QN], 0
    jne .q
    call w16mm_tdue                 ; DI = a due event, CF=1 none
    jc .none
    jmp short .go
.q:
    mov al, [es:TV_QH]
    call w16mm_tqat                 ; DI = the call
    inc al
    and al, TV_NQ - 1
    mov [es:TV_QH], al
    dec byte [es:TV_QN]
.go:
    mov byte [es:TV_BUSY], 1
    push word [es:di + TC_PROC + 2] ; its words, in the order they are
    push word [es:di + TC_PROC]     ;   pushed for it below
    push word [es:di + TC_DW1]
    push word [es:di + TC_DW1 + 2]
    push word [es:di + TC_USER]
    push word [es:di + TC_USER + 2]
    push word [es:di + TC_W2]
    push word [es:di + TC_W1]
    mov al, bl
    xor ah, ah
    mov si, w16mm_tback_c
    xor cx, cx
    ICALL w16_cbopen
    mov cx, 6                       ; pascal, the first first: two UINTs,
.a:                                 ;   dwUser, dw1...
    pop ax
    ICALL w16_ppush
    loop .a
    xor ax, ax                      ; ...and dw2, 0
    ICALL w16_ppush
    ICALL w16_ppush
    pop ax
    pop dx
    ICALL w16_cbcall                ; DS its own, as DriverCallback's
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16mm_tdue - ES = the block: DI = the first event due now, rescheduled (a
; periodic one) or freed (a one-shot one, whose words stay until it is
; called); CF=1 none. Clobbers AX, CX, DX
w16mm_tdue:
    call w16mm_now                  ; DX:AX = now
    mov di, TV_T
    mov cx, TV_NT
.f:
    test byte [es:di + TT_FL], TTF_USED
    jz .n
    push ax
    push dx
    sub ax, [es:di + TT_DUE]
    sbb dx, [es:di + TT_DUE + 2]
    pop dx
    pop ax
    jns .due
.n:
    add di, TT_SIZE
    loop .f
    stc
    ret
.due:
    test byte [es:di + TT_FL], TTF_PER
    jnz .per
    mov byte [es:di + TT_FL], 0
    clc
    ret
.per:
    push ax
    push dx
    mov cx, [es:di + TT_PER]
    add [es:di + TT_DUE], cx
    adc word [es:di + TT_DUE + 2], 0
    sub ax, [es:di + TT_DUE]
    sbb dx, [es:di + TT_DUE + 2]
    pop dx
    pop ax
    js .ok                          ; due again later
    add ax, cx                      ; a period behind: from now
    adc dx, 0
    mov [es:di + TT_DUE], ax
    mov [es:di + TT_DUE + 2], dx
.ok:
    clc
    ret

; w16mm_tq - ES = a device's state opened with CALLBACK_FUNCTION, AX = the
; notice, DX:CX = its lParam: the call queued (124.32.6) and the tick asked
; for; a ninth is lost. Keeps all
w16mm_tq:
    push ax
    push di
    push es
    push dx                         ; dw1
    push cx
    push word [es:W16MM_INST + 2]   ; dwInstance
    push word [es:W16MM_INST]
    push ax                         ; the notice
    push word [es:WO_SELF]          ; the handle
    push word [es:WO_CB + 2]        ; the procedure
    push word [es:WO_CB]
    call w16mm_tes
    jc .drop
    mov al, [es:TV_QN]
    cmp al, TV_NQ
    jae .drop
    add al, [es:TV_QH]
    and al, TV_NQ - 1
    call w16mm_tqat
    pop word [es:di + TC_PROC]
    pop word [es:di + TC_PROC + 2]
    pop word [es:di + TC_W1]
    pop word [es:di + TC_W2]
    pop word [es:di + TC_USER]
    pop word [es:di + TC_USER + 2]
    pop word [es:di + TC_DW1]
    pop word [es:di + TC_DW1 + 2]
    inc byte [es:TV_QN]
    mov byte [w16mm_wq], 1
    jmp short .out
.drop:
    add sp, 16
.out:
    pop es
    pop di
    pop ax
    ret

; w16mm_tqat - AL = a queue slot: DI = its call. Keeps the rest
w16mm_tqat:
    push ax
    push cx
    xor ah, ah
    mov di, ax
    mov cl, 4
    shl di, cl
    add di, TV_Q
    pop cx
    pop ax
    ret

; w16mm_tlive - CF=0 an event is armed or a call is queued: the tick keeps
; looking. Keeps all
w16mm_tlive:
    push es
    push di
    push cx
    call w16mm_tes
    jc .out
    cmp byte [es:TV_QN], 0
    jne .yes
    mov di, TV_T + TT_FL
    mov cx, TV_NT
.f:
    test byte [es:di], TTF_USED
    jnz .yes
    add di, TT_SIZE
    loop .f
    stc
    jmp short .out
.yes:
    clc
.out:
    pop cx
    pop di
    pop es
    ret

; w16mm_tget - the block made if it is not yet. CF=1 no memory. Keeps all
w16mm_tget:
    cmp word [w16mm_tev], 0
    jne .ok
    push ax
    push cx
    push dx
    mov ax, TV_LEN
    xor dx, dx
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; AX = the handle
    mov [w16mm_tev], ax
    pop dx
    pop cx
    cmp ax, 1                       ; CF=1: 0, none
    pop ax
    ret
.ok:
    clc
    ret

; w16mm_cbfn - CL = an open's callback kind: CALLBACK_FUNCTION's block made
; first (124.32.6). CF=1 AX = MMSYSERR_NOMEM. Keeps the rest
w16mm_cbfn:
    cmp cl, 3
    clc
    jne .ret
    call w16mm_tget
    jnc .ret
    mov ax, MMSYSERR_NOMEM
.ret:
    ret

; w16mm_tes - ES = the block; CF=1 there is none. Keeps the rest
w16mm_tes:
    push ax
    mov ax, [w16mm_tev]
    cmp ax, 1
    jc .out
    W16SEG es, ax                   ; a fixed block: its handle is its place
.out:
    pop ax
    ret

section .text
