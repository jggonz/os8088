; =============================================================================
; os8088 - apps/xtmp3/x3conv.inc - DECODE FIRST (SPEC.md 121.7)
;
; When nothing plays live - an 8088, or a machine with no card - or the user
; asks for it (D, Playback > Decode First), XTMP3 converts the whole track to
; a WAV on a hard drive, finalises it, and only then plays it. Never a partial
; file, never progressive playback.
;
;   1. THE DRIVE DIALOG, a window of our own: the brief's sentence, every
;      VK_FIXED volume (OSAPI_VOL_KIND) with its letter, free space (GOTO_QM +
;      VOL_STAT) and whether it can be written; the quality, the PCM it makes
;      and the disk it needs (a '~' when the duration is a guess); Decode and
;      Play, Cancel, and - when more of the queue follows - the batch switch.
;      It never assumes C:, never lists a floppy, and says so when there is
;      no hard drive. Volume LABELS are not shown: no slot answers a FAT label
;      to a package (the kernel skips the label entry), and a driver's desktop
;      label is the driver's - docs/xtmp3/ENGINE.md has the gap.
;   2. THE STANDARD FILE SAVE DIALOG (SPEC.md 38), standing in that volume -
;      the last destination's folder when it is on it - for the folder and the
;      name. A folder on anything but a writable hard drive, a name already
;      there, or too little room is refused and the dialog comes back.
;   3. THE CONVERSION: X3nnnnnn.TMP in that folder, its first cluster the WAV
;      header (a JUNK chunk pads the data to the cluster, so every write after
;      it is whole clusters), then the worker decodes into a two-half stage
;      and the UI task writes each half with OSAPI_FILE_WRITE_SEQ (SPEC.md
;      18.4.9) - the worker never touches a file (20.6 rule 7). The progress
;      window shows the output, the quality, decoded audio against the track,
;      the percentage, the wall time and the speed, twice a second. Esc or
;      Cancel stops it within a granule and deletes ONLY this temporary file.
;   4. FINALISATION: the stream committed, the header rewritten with the
;      lengths (OSAPI_FILE_WRITE_AT), the source's fingerprint taken again (a
;      changed source is a failed job), the file re-read and checked, then
;      renamed to the .WAV, entered in SYSTEM\APPDATA\XTMP3.DAT - and only
;      then played (x3_file_play: the decoder dropped first).
;   5. THE CACHE: the 'xtm3' chunk is the identity (SPEC.md 121.7), and the
;      index maps fingerprints to finished files, so a track converted once
;      plays again with no decoding and no question. A record whose file is
;      gone, changed or incomplete is dropped (the file is left alone).
;      File > Decoded Files lists them and deletes only files whose own xtm3
;      chunk says XTMP3 made them.
; =============================================================================

; --- the WAV XTMP3 writes ---------------------------------------------------------
;   0  'RIFF' size 'WAVE'
;  12  'fmt ' 16: PCM, channels, rate, bytes a second, frame bytes, bits
;  36  'xtm3' X3C_IDN: the identity (XI_* below)
;  88  'JUNK' to the cluster's end, less 8
; clu-8 'data' size, and the samples from the cluster's start
X3C_FMT     equ 12
X3C_XTM     equ 36
X3C_IDN     equ 44                  ; the identity's bytes
X3C_JUNK    equ X3C_XTM + 8 + X3C_IDN   ; 88
XI_VER      equ 0                   ; db 1
XI_SIZE     equ 2                   ; dd the source's bytes  \
XI_CRCH     equ 6                   ; dd CRC-32 of its first  > the
XI_CRCT     equ 10                  ; dd ...and last 16 KB   /  fingerprint
XI_DECV     equ 14                  ; dw X3_DECVER
XI_TABV     equ 16                  ; dw X3_TABVER
XI_BE       equ 18                  ; db the backend (X3K_STD / X3K_XT)
XI_PROF     equ 19                  ; db its profile
XI_DIV      equ 20                  ; db the divisor
XI_RUNG     equ 21                  ; db X3Q_*
XI_RATE     equ 22                  ; dw the output: rate...
XI_CH       equ 24                  ; db ...channels...
XI_BITS     equ 25                  ; db ...bits
XI_FRAMES   equ 26                  ; dd output frames in the data
XI_STEM     equ 30                  ; 9 db the source's name, its stem
XI_RES      equ 39                  ; ...to X3C_IDN

; --- SYSTEM\APPDATA\XTMP3.DAT ------------------------------------------------------
X3C_NIX     equ 16                  ; records
XR_FP       equ 0                   ; 12 the fingerprint
XR_DECV     equ 12                  ; dw
XR_TABV     equ 14                  ; dw
XR_RUNG     equ 16                  ; db X3Q_*
XR_CH       equ 17                  ; db
XR_RATE     equ 18                  ; dw
XR_BITS     equ 20                  ; db
XR_VOL      equ 21                  ; db where: the volume...
XR_DIR      equ 22                  ; dw ...the folder's cluster...
XR_NAME     equ 24                  ; 13 ...the WAV's name
XR_STEM     equ 37                  ; 9 the source's stem
XR_USED     equ 46                  ; db 1 = a record
XR_SIZE     equ 48
X3C_IXHDR   equ 8                   ; 'X3DT', version, count
X3C_IXSZ    equ X3C_IXHDR + X3C_NIX * XR_SIZE

; --- the dialog window -------------------------------------------------------------
XD_DRIVE    equ 1                   ; the modes of [x3c_dmode]
XD_PROG     equ 2
XD_FILES    equ 3
XD_W        equ 464                 ; content width: 58 cells
XD_RH       equ 11                  ; a list row
XD_DROWS    equ 6                   ; the drive list, rows at most
XD_FROWS    equ 8                   ; Decoded Files
XD_BW       equ 120                 ; a button
XD_BH       equ 16
XD_MAXV     equ 26

XC_DRAWT    equ 9                   ; ticks between progress redraws (~2 a s)
XC_STAGEKB  equ 8                   ; a stage half, at least: 8 KB
XC_STAGEXT  equ 4                   ; ...and under the XT backend, 4 KB

; =============================================================================
; THE FINGERPRINT: size, CRC-32 of the first and of the last 16 KB
; =============================================================================

; x3c_fprint - the current entry's print, made once a session (it reads the
; file's two ends: ~1 s on an XT). The instance stands in its folder and the
; ring's claim is free. out: CF = 1 nothing could be read
x3c_fprint:
    call x3_fpcur                   ; DI -> its row
    mov al, [x3_st+X3S_CUR]
    xor ah, ah
    mov bx, ax
    cmp byte [x3_qfpok+bx], 0
    jne .ok
    push bx
    call x3c_fpcalc                 ; into [DI]
    pop bx
    jc .no
    mov byte [x3_qfpok+bx], 1
.ok:
    clc
    ret
.no:
    stc
    ret

; x3c_fpcalc - x3_fname's print into DI's 12 bytes, read through the ring's
; claim. out: CF = 1 unreadable. keeps DI
x3c_fpcalc:
    push di
    mov ax, [x3_flen]
    mov dx, [x3_flen+2]
    mov [di], ax
    mov [di+2], dx
    xor ax, ax                      ; the head
    xor dx, dx
    call x3c_crcat
    jc .no
    pop di
    push di
    mov [di+4], ax
    mov [di+6], dx
    mov ax, [x3_flen]               ; the tail: from max(0, size - 16 KB)
    mov dx, [x3_flen+2]
    sub ax, 16384
    sbb dx, 0
    jae .t
    xor ax, ax
    xor dx, dx
.t:
    call x3c_crcat
    jc .no
    pop di
    mov [di+8], ax
    mov [di+10], dx
    clc
    ret
.no:
    pop di
    stc
    ret

; x3c_crcat - DX:AX = an offset: CRC-32 of min(16 KB, what is left) from it,
; read in as many pieces as the ring holds (16 KB under the XT backend, less
; the offset's place in its cluster). out: DX:AX = the CRC; CF = 1 unreadable
x3c_crcat:
    push bx
    push cx
    push si
    push di
    push es
    mov [x3c_cro], ax
    mov [x3c_cro+2], dx
    mov si, [x3_flen]               ; the bytes: min(16384, size - offset)
    mov di, [x3_flen+2]
    sub si, ax
    sbb di, dx
    mov cx, 16384
    or di, di
    jnz .c
    cmp si, cx
    jae .c
    mov cx, si
.c:
    mov [x3c_crn], cx
    mov ax, 0xFFFF
    mov [x3c_crs], ax
    mov [x3c_crs+2], ax
.piece:
    cmp word [x3c_crn], 0
    je .done
    mov ax, [x3c_cro]
    mov dx, [x3c_cro+2]
    call x3_readat                  ; ES:BX, CX = bytes from the offset
    jc .out
    jcxz .no                        ; (nothing there: the file is short)
    cmp cx, [x3c_crn]
    jbe .n
    mov cx, [x3c_crn]
.n:
    sub [x3c_crn], cx
    add [x3c_cro], cx
    adc word [x3c_cro+2], 0
    mov ax, [x3c_crs]
    mov dx, [x3c_crs+2]
    call x3c_crc
    mov [x3c_crs], ax
    mov [x3c_crs+2], dx
    jmp short .piece
.done:
    mov ax, [x3c_crs]
    mov dx, [x3c_crs+2]
    not ax
    not dx
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; x3c_crc - DX:AX = the running CRC-32 (reflected, EDB88320h), ES:BX -> CX
; bytes. out: DX:AX. A NIBBLE at a time through 64 bytes of table: twice the
; steps of a byte table's 1 KB, ~0.5 s more a track on an 8088, once a session
; (the print is kept per queue entry). keeps the rest
x3c_crc:
    push bx
    push cx
    push si
    jcxz .out
.l:
    xor al, [es:bx]
    call .nib
    call .nib
    inc bx
    loop .l
.out:
    pop si
    pop cx
    pop bx
    ret
.nib:
    mov si, ax
    and si, 0x0F
    shl si, 1
    shl si, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    xor ax, [x3c_crcn+si]
    xor dx, [x3c_crcn+si+2]
    ret

x3c_crcn:   dd 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC
            dd 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C
            dd 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C
            dd 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C

; =============================================================================
; THE INDEX: SYSTEM\APPDATA\XTMP3.DAT (apps/os88data.inc, SPEC.md 19.9.1)
; =============================================================================

; x3c_ixload - the index into x3c_ix, once a session. A file that is not
; there, or not ours, is an empty index. keeps everything
x3c_ixload:
    cmp byte [x3c_ixok], 0
    jne .out
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov byte [x3c_ixok], 1
    mov di, x3c_ix
    mov cx, X3C_IXSZ
    xor al, al
    call x3_fillb
    call x3c_ixhome                 ; the package's own volume
    push ds
    pop es
    mov si, x3c_s_ixf
    mov bx, x3c_ix
    mov cx, X3C_IXSZ
    xor dx, dx
    call od_read
    pushf
    call x3c_ixback
    popf
    jc .empty
    cmp word [x3c_ix], 'X3'
    jne .empty
    cmp word [x3c_ix+2], 'DT'
    jne .empty
    cmp word [x3c_ix+4], 1
    je .done
.empty:
    mov di, x3c_ix
    mov cx, X3C_IXSZ
    xor al, al
    call x3_fillb
.done:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; x3c_ixsave - the index written back. out: CF = 1 it could not be (the
; package's volume is read-only): the WAV stands, only the cache forgets it
x3c_ixsave:
    push bx
    push cx
    push si
    push di
    push es
    mov word [x3c_ix], 'X3'
    mov word [x3c_ix+2], 'DT'
    mov word [x3c_ix+4], 1
    call x3c_ixhome                 ; the package's own volume
    push ds
    pop es
    mov si, x3c_s_ixf
    mov bx, x3c_ix
    mov cx, X3C_IXSZ
    mov di, x3c_ixst
    call od_write
    pushf
    call x3c_ixback
    popf
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; x3c_ixhome / x3c_ixback - stand in the folder XTMP3 was launched from, and
; back where we stood. os88data.inc works on the volume the instance stands
; on, and after a conversion that is the DESTINATION; the index is the
; program's own (SPEC.md 19.9). keep everything but the flags
x3c_ixhome:
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE
    mov [x3c_ixbd], dx
    mov [x3c_ixbv], bl
    mov dx, [x3_homed]
    mov bl, [x3_homev]
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop bx
    pop ax
    ret

x3c_ixback:
    push ax
    push bx
    push dx
    mov dx, [x3c_ixbd]
    mov bl, [x3c_ixbv]
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop bx
    pop ax
    ret

; x3c_ixrow - AL = a record -> SI -> it. keeps the rest
x3c_ixrow:
    push ax
    push dx
    xor ah, ah
    mov si, XR_SIZE
    mul si
    add ax, x3c_ix + X3C_IXHDR
    mov si, ax
    pop dx
    pop ax
    ret

; =============================================================================
; THE CACHE: a finished file for this source, playable on this device
; =============================================================================

; -----------------------------------------------------------------------------
; x3c_cachefind - the current entry's print against the index: the decoder
; and table versions ours, the format one THIS device plays, the quality the
; user asked for (any under Auto) - and then the FILE checked (x3c_cacheok).
; A record whose file fails the check is dropped. out: CF = 0 [x3c_hit*] set
; -----------------------------------------------------------------------------
x3c_cachefind:
    call x3c_ixload
    xor al, al
.r:
    cmp al, X3C_NIX
    jae .none
    call x3c_ixrow
    cmp byte [si+XR_USED], 0
    je .n
    push ax
    call x3_fpcur
    push si
    mov cx, X3_FPSZ
    call x3_memeq
    pop si
    pop ax
    jne .n
    cmp word [si+XR_DECV], X3_DECVER
    jne .n
    cmp word [si+XR_TABV], X3_TABVER
    jne .n
    mov ah, [x3_st+X3S_QWANT]
    cmp ah, X3Q_AUTO
    je .q
    cmp ah, [si+XR_RUNG]
    jne .n
.q:
    call x3c_devok                  ; this device plays its format?
    jc .n
    cmp byte [x3c_strict], 0        ; ...and, strictly, is it the format this
    je .ok                          ; device is given for that rung?
    call x3c_fmtis
    jc .n
.ok:
    push ax
    call x3c_cacheok                ; the FILE: there, complete, the same
    pop ax
    jnc .hit
    mov byte [si+XR_USED], 0        ; STALE: forgotten (the file left alone)
    push ax
    call x3c_ixsave
    pop ax
.n:
    inc al
    jmp short .r
.hit:
    clc
    ret
.none:
    stc
    ret

; x3c_devok - the record SI's format, on this device? CF = 1 not
x3c_devok:
    cmp byte [x3_card], 0
    jne .card
    cmp byte [si+XR_CH], 1          ; the speaker and the Covox: 8-bit mono
    jne .no
    cmp byte [si+XR_BITS], 8
    jne .no
    jmp short .yes
.card:
    cmp byte [si+XR_BITS], 16
    jne .ch
    test word [x3_caps], SND_CAP_PCM16
    jz .no
.ch:
    cmp byte [si+XR_CH], 2
    jne .rate
    test word [x3_caps], SND_CAP_STEREO
    jz .no
.rate:
    cmp word [si+XR_RATE], X3_DEVMIN
    jb .no
    cmp word [si+XR_RATE], X3_DEVMAX
    jbe .yes
    test word [x3_caps], SND_CAP_PCM_HI
    jz .no
.yes:
    clc
    ret
.no:
    stc
    ret

; x3c_fmtis - the record SI's rate, channels and bits the very ones x3_ofmt
; gives its rung on this device and source? CF = 1 not. keeps all but flags
x3c_fmtis:
    push ax
    push bx
    push cx
    mov al, [x3_rung]               ; (the engine's output, kept)
    mov ah, [x3_och]
    mov bl, [x3_obits]
    mov bh, [x3_odiv]
    mov cx, [x3_orate]
    push ax
    push bx
    push cx
    mov al, [si+XR_RUNG]
    mov [x3_rung], al
    call x3_ofmt
    jc .no
    mov ax, [x3_orate]
    cmp ax, [si+XR_RATE]
    jne .no
    mov al, [x3_och]
    cmp al, [si+XR_CH]
    jne .no
    mov al, [x3_obits]
    cmp al, [si+XR_BITS]
    jne .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    mov [x3_orate], cx              ; (CF is kept: movs do not touch it)
    mov [x3_rung], al
    mov [x3_och], ah
    mov [x3_obits], bl
    mov [x3_odiv], bh
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3c_cacheok - the record SI's file: stood in its folder, its header read
; and parsed, its xtm3 identity the record's, its lengths the file's. out:
; CF = 0 [x3c_hit*] its data range and format, the instance standing there.
; Every failure is a stale record
; -----------------------------------------------------------------------------
x3c_cacheok:
    push si
    mov dx, [si+XR_DIR]
    mov bl, [si+XR_VOL]
    call OSAPI_FILE_GOTO_QM
    jc .no
    pop si
    push si
    mov [x3c_hvol], bl
    mov [x3c_hdir], dx
    lea si, [si+XR_NAME]
    mov di, x3c_hname
    mov cx, 13
    call x3_putn
    mov byte [di], 0
    mov si, x3c_hname
    call x3c_wavchk                 ; the header, its lengths
    pop si
    jc .no2
    push si                         ; the identity is the record's
    mov di, si
    mov si, x3c_widn + XI_SIZE      ; (the print: 12 bytes)
    mov cx, X3_FPSZ
    call x3_memeq
    pop si
    jne .no2
    mov al, [x3c_widn+XI_RUNG]
    cmp al, [si+XR_RUNG]
    jne .no2
    mov [x3c_wprof], al
    clc
    ret
.no:
    pop si
.no2:
    stc
    ret

; -----------------------------------------------------------------------------
; x3c_wavchk - SI = a WAV's name in the folder we stand in: its first cluster
; read and walked (RIFF, fmt, xtm3, data), every length checked against the
; file's own. out: CF = 0 and [x3c_wdoff]/[x3c_wdlen] the data, [x3c_widn]
; the identity, x3_orate/och/obits/fby its format; CF = 1 not one of ours,
; or incomplete
; -----------------------------------------------------------------------------
x3c_wavchk:
    push si
    call x3_findsz                  ; DX:AX = its bytes
    pop si
    jc .no
    mov [x3c_wsize], ax
    mov [x3c_wsize+2], dx
    call x3_cluget                  ; the cluster: where the data starts
    jc .no
    call x3c_room                   ; the header lands in the ring's claim
    jc .no
    push si
    mov es, [ap_la_seg]
    xor bx, bx
    mov cx, [ap_clu]
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_READ_AT         ; DX:AX = bytes
    pop si
    push ds
    pop es
    jc .no
    or dx, dx
    jnz .got
    cmp ax, [ap_clu]
    jb .no
.got:
    push ds
    mov ds, [ap_la_seg]
    cmp word [0], 'RI'
    jne .bad
    cmp word [2], 'FF'
    jne .bad
    cmp word [8], 'WA'
    jne .bad
    cmp word [12], 'fm'
    jne .bad
    cmp word [X3C_XTM], 'xt'
    jne .bad
    cmp word [X3C_XTM+2], 'm3'
    jne .bad
    cmp word [X3C_FMT+8], 1         ; PCM
    jne .bad
    mov ax, [4]                     ; RIFF's size + 8 = the file's
    mov dx, [6]
    pop ds
    add ax, 8
    adc dx, 0
    cmp ax, [x3c_wsize]
    jne .no
    cmp dx, [x3c_wsize+2]
    jne .no
    push ds                         ; the format and the identity, out
    push si
    mov si, X3C_FMT + 8
    mov ds, [ap_la_seg]
    mov ax, [si+2]                  ; channels
    mov cx, [si+4]                  ; rate
    mov dx, [si+14]                 ; bits
    pop si
    pop ds
    mov [x3_och], al
    mov [x3_orate], cx
    mov [x3_obits], dl
    call x3_fbytes
    mov [x3_fby], ax
    push si
    push ds
    pop es
    mov di, x3c_widn
    mov si, X3C_XTM + 8
    mov cx, X3C_IDN
    push ds
    mov ds, [ap_la_seg]
    cld
    rep movsb
    pop ds
    pop si
    mov bx, [ap_clu]                ; 'data' sits in the cluster's last 8
    push ds
    mov ds, [ap_la_seg]
    cmp word [bx-8], 'da'
    jne .bad
    cmp word [bx-6], 'ta'
    jne .bad
    mov ax, [bx-4]
    mov dx, [bx-2]
    pop ds
    mov [x3c_wdlen], ax
    mov [x3c_wdlen+2], dx
    mov [x3c_wdoff], bx
    mov word [x3c_wdoff+2], 0
    add ax, bx                      ; the data's end is the file's
    adc dx, 0
    cmp ax, [x3c_wsize]
    jne .no
    cmp dx, [x3c_wsize+2]
    jne .no
    clc
    ret
.bad:
    pop ds
.no:
    push ds
    pop es
    stc
    ret

; x3c_room - the ring's claim, holding one cluster of [ap_clu] (the header's
; read). A conversion's holds its stage, a cluster at least (x3_plan); any
; other that is too small is made the cluster's size - with the XT backend
; dropped first when it is resident, the ceiling having no room for both
; (SPEC.md 121.10): a cache that matches plays with no decoder anyway, and
; one that does not costs the job one fetch. CF = 1 no memory
x3c_room:
    push ax
    cmp word [x3_laxkb], 0
    jne .ok
    mov ax, [x3_lasz]
    add ax, X3_PCMRES
    cmp ax, [ap_clu]
    jae .ok
    cmp byte [x3_bkind], X3K_XT
    jne .sz
    call x3_be_drop
.sz:
    mov ax, [ap_clu]
    call x3_lasize                  ; (the claim given back: a new size)
.ok:
    pop ax
    jmp x3_la_claim

; -----------------------------------------------------------------------------
; x3c_wavopen - the hit (x3c_cacheok's) made the producer's source: standing
; in its folder, the ring over its data. out: CF = 1 it went (x3_st says why)
; -----------------------------------------------------------------------------
x3c_wavopen:
    mov dx, [x3c_hdir]
    mov bl, [x3c_hvol]
    mov [ap_trk_dir], dx
    mov [ap_trk_vol], bl
    call OSAPI_FILE_GOTO_QM
    jc .no
    mov si, x3c_hname
    mov di, x3_fname
    mov cx, 13
    call x3_putn
    mov byte [di], 0
    mov word [apw_name], x3_fname
    mov si, x3_fname
    call x3c_wavchk                 ; (again: it is the one we play)
    jc .no
    mov ax, [x3c_wdoff]
    mov dx, [x3c_wdoff+2]
    mov [ap_data_off], ax
    mov [ap_data_off+2], dx
    add ax, [x3c_wdlen]
    adc dx, [x3c_wdlen+2]
    mov [ap_data_end], ax
    mov [ap_data_end+2], dx
    and byte [x3_st+X3S_FLAGS], ~X3SF_DEVLIM
    clc
    ret
.no:
    mov word [x3_st+X3S_ERR], x3_s_noread
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    stc
    ret

; =============================================================================
; DECODE FIRST: the dialog
; =============================================================================

; -----------------------------------------------------------------------------
; x3c_begin - AL = the conversion's rung (x3l_offpick's). Its output format,
; the disk it needs, the estimate - then the dialog; or, in an approved batch,
; straight to the conversion in the batch's folder
; -----------------------------------------------------------------------------
x3c_begin:
    mov [x3c_rung], al
    mov [x3_rung], al
    call x3_ofmt                    ; the PCM it makes, here
    call x3_fbytes
    mov [x3_fby], ax
    call x3c_needkb                 ; [x3c_need] KB, [x3c_nest] its '~'
    mov bl, [x3c_rung]
    xor bh, bh
    shl bx, 1
    mov ax, [x3l_meas+bx]
    mov [x3c_est], ax               ; its measured speed (FFFFh: none)
    mov byte [x3_md], MD_CHOOSE
    mov byte [x3_st+X3S_STATE], X3ST_CHOOSE
    mov al, [x3c_rung]
    mov [x3_st+X3S_QUAL], al
    mov al, [x3_och]
    mov [x3_st+X3S_CHANS], al
    mov al, [x3_obits]
    mov [x3_st+X3S_BITS], al
    mov ax, [x3_orate]
    mov [x3_st+X3S_RATE], ax
    cmp byte [x3_c_batch], 0        ; AN APPROVED BATCH: its folder, no dialog
    je .ask
    call x3c_batchgo
    jnc .out
.ask:
    call x3c_dlgdrive
.out:
    ret

; x3c_needkb - the WAV's KB: seconds x rate x frame bytes / 1024 + its header
; cluster and a margin - overflow-safe (x3_mdiv's 48 bits) - and a '~' when
; the duration is a guess
x3c_needkb:
    mov ax, [x3_orate]
    mul word [x3_fby]               ; DX:AX = bytes a second
    mov cx, [x3_info+X3I_DUR]
    inc cx                          ; (rounded up)
    mov bx, 1024
    call x3_mdiv
    add ax, 33                      ; the header's cluster, 32 KB at most
    adc dx, 0
    mov [x3c_need], ax
    mov [x3c_need+2], dx
    mov byte [x3c_nest], 0
    test byte [x3_info+X3I_FLAGS], X3IF_EXACT
    jnz .o
    mov byte [x3c_nest], 1
.o:
    ret

; -----------------------------------------------------------------------------
; x3c_vscan - every VK_FIXED volume: its letter, its transport, its free KB
; and its cluster (standing at its root, then home again). Floppies are not
; listed at all. Rows in x3c_v*, [x3c_nv]
; -----------------------------------------------------------------------------
x3c_vscan:
    call OSAPI_FILE_HERE            ; home, for after
    mov [x3c_home], dx
    mov [x3c_homev], bl
    mov byte [x3c_nv], 0
    xor dl, dl
.v:
    mov al, dl
    call OSAPI_VOL_KIND
    jc .next
    cmp al, VK_FIXED
    jne .next                       ; a floppy, or an image on one
    mov bl, [x3c_nv]
    xor bh, bh
    mov [x3c_vv+bx], dl
    mov byte [x3c_vf+bx], XV_RO     ; a cable's volume, or a CD: no write-at
    cmp ah, VT_FILE
    je .row
    push dx
    mov bl, dl
    xor dx, dx
    call OSAPI_FILE_GOTO_QM         ; its root
    jc .gone
    call OSAPI_VOL_STAT             ; AX = sectors/cluster, BX = free
    jc .gone
    mov cx, ax                      ; free KB = free x spc / 2
    mul bx
    shr dx, 1
    rcr ax, 1
    pop bx                          ; (BL = the volume)
    push bx
    mov bl, [x3c_nv]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov [x3c_vkb+bx], ax
    mov [x3c_vkb+bx+2], dx
    shr bx, 1
    shr bx, 1
    mov byte [x3c_vf+bx], 0
    cmp cx, 64                      ; 32 KB clusters at most: the stage is
    jbe .big                        ; one, or two, of them (x3_plan)
    mov byte [x3c_vf+bx], XV_CLU
.big:
    cmp dx, [x3c_need+2]            ; room for it?
    ja .room
    jb .small
    cmp ax, [x3c_need]
    jae .room
.small:
    or byte [x3c_vf+bx], XV_SMALL
.room:
    pop dx
    jmp short .row
.gone:
    pop dx
    mov bl, [x3c_nv]
    xor bh, bh
    mov byte [x3c_vf+bx], XV_GONE
.row:
    inc byte [x3c_nv]
.next:
    inc dl
    cmp dl, XD_MAXV
    jb .v
    mov dx, [x3c_home]              ; home again
    mov bl, [x3c_homev]
    call OSAPI_FILE_GOTO_QM
    ret

XV_RO       equ 1                   ; a row's flags: read-only...
XV_SMALL    equ 2                   ; ...too small...
XV_CLU      equ 4                   ; ...clusters too large...
XV_GONE     equ 8                   ; ...unreadable

; -----------------------------------------------------------------------------
; x3c_dlgdrive - THE DRIVE DIALOG: the volumes scanned, the last destination's
; row selected (else the first usable), the window up
; -----------------------------------------------------------------------------
x3c_dlgdrive:
    call x3c_vscan
    mov byte [x3c_vsel], 0
    xor bx, bx                      ; the last destination's row, if usable
.f:
    cmp bl, [x3c_nv]
    jae .first
    mov al, [x3c_vv+bx]
    cmp al, [x3c_lvol]
    jne .fn
    cmp byte [x3c_lset], 0
    je .fn
    cmp byte [x3c_vf+bx], 0
    je .sel
.fn:
    inc bx
    jmp short .f
.first:
    xor bx, bx
.g:
    cmp bl, [x3c_nv]
    jae .open
    cmp byte [x3c_vf+bx], 0
    je .sel
    inc bx
    jmp short .g
.sel:
    mov [x3c_vsel], bl
.open:
    mov byte [x3c_bqueue], 0
    mov al, XD_DRIVE
    jmp x3c_dopen

; x3c_vusable - the selected row writable and big enough? CF = 1 not.
; keeps everything
x3c_vusable:
    push bx
    mov bl, [x3c_vsel]
    xor bh, bh
    cmp bl, [x3c_nv]
    jae .no
    cmp byte [x3c_vf+bx], 0
    jne .no
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; x3c_dopen - the dialog window in mode AL (XD_*): its height for the mode, a
; window of ours, unbound (no dock tile), not zooming open, its close box a
; Cancel. Any window of the dialog's already up is replaced
; -----------------------------------------------------------------------------
x3c_dopen:
    push ax
    call x3c_dlgclose
    pop ax
    mov [x3c_dmode], al
    mov cx, 3                       ; the button row
    mov dx, 6 + 10 + 10 + 6 + 20 + 6 + XD_BH + 6    ; DRIVE: two lines, rows,
    cmp al, XD_DRIVE                ; two lines, the buttons
    jne .np
    mov al, [x3c_nv]
    cmp al, XD_DROWS
    jbe .r
    mov al, XD_DROWS
.r:
    or al, al
    jnz .r1
    inc al                          ; (the no-hard-drive sentence)
.r1:
    mov ah, XD_RH
    mul ah
    add dx, ax
    jmp short .h
.np:
    cmp al, XD_PROG
    jne .nf
    mov dx, 6 + 4 * 10 + 8 + XD_BH + 6
    jmp short .h
.nf:
    mov dx, 6 + 12 + XD_FROWS * XD_RH + 6 + 20 + XD_BH + 6
.h:
    add dx, TITLE_H + 1
    mov [x3c_dtpl+6], dx
    call OSAPI_VIDEO                ; centred on the live screen (SPEC.md 39)
    sub ax, XD_W + 2
    sar ax, 1
    and ax, 0xFFF8                  ; content on the byte grid
    add ax, 7
    mov [x3c_dtpl], ax
    sub bx, dx
    sar bx, 1
    cmp bx, MBAR_H + 4
    jge .y
    mov bx, MBAR_H + 4
.y:
    mov [x3c_dtpl+2], bx
    mov ax, x3c_t_drive
    cmp byte [x3c_dmode], XD_PROG
    jne .t1
    mov ax, x3c_t_prog
.t1:
    cmp byte [x3c_dmode], XD_FILES
    jne .t2
    mov ax, x3c_t_files
.t2:
    mov [x3c_dtpl+8], ax
    mov si, x3c_dtpl
    call OSAPI_WM_CREATE
    jc .out
    mov [x3c_dwin], bx
    call OSAPI_WM_NOANIM
    mov ax, x3c_dclose
    call OSAPI_WM_ONCLOSE
    mov byte [x3c_btrec+OS88UI_BT_N], 0
    mov ax, bx
    push bx
    mov bx, x3c_btrec
    mov si, x3c_dup
    mov di, x3c_ddrag
    mov dx, x3c_dclick
    call os88ui_btninit
    pop bx
    call OSAPI_WM_SHOW
.out:
    ret

; x3c_dlgclose - the dialog window down, if it is up. keeps everything
x3c_dlgclose:
    push bx
    mov bx, [x3c_dwin]
    or bx, bx
    jz .out
    mov word [x3c_dwin], 0
    mov byte [x3c_dmode], 0
    call OSAPI_WM_DESTROY
.out:
    pop bx
    ret

; x3c_dfclose - (Decoded Files shares the dialog's window)
x3c_dfclose:
    cmp byte [x3c_dmode], XD_FILES
    jne .out
    call x3c_dlgclose
.out:
    ret

; x3c_dclose - the dialog's close box: a Cancel. The window is gone when
; this answers, so it answers REFUSED (os88ui_aclose's reason)
x3c_dclose:
    push bx
    mov al, [x3c_dmode]
    cmp al, XD_PROG
    je .prog
    call x3c_dlgclose
    cmp al, XD_DRIVE
    jne .out
    call x3c_chooseoff              ; the choice abandoned: READY
    jmp short .out
.prog:
    mov al, XU_CANCEL               ; (the main window's wake does it)
    call x3c_post
.out:
    mov bx, [x3_win]
    call x3f_update
    pop bx
    stc
    ret

; x3c_chooseoff - no conversion after all: the track READY, nothing played
x3c_chooseoff:
    mov byte [x3_md], MD_IDLE
    mov byte [x3_st+X3S_STATE], X3ST_READY
    mov byte [x3c_savepend], 0
    mov byte [x3_c_batch], 0
    ret

; -----------------------------------------------------------------------------
; THE DIALOG'S PAINT: every line one opaque font_run (SPEC.md 6.1), the list's
; rows padded to the width so a row is one call, the buttons the library's
; in:  SI = the window; gfx lock held
; -----------------------------------------------------------------------------
x3c_dpaint:
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [x3c_dox], ax
    mov [x3c_doy], dx
    mov al, [x3c_dmode]
    cmp al, XD_DRIVE
    jne .np
    call x3c_pdrive
    jmp short .btn
.np:
    cmp al, XD_PROG
    jne .nf
    call x3c_pprog
    jmp short .btn
.nf:
    call x3c_pfiles
.btn:
    call x3c_bset                   ; the buttons for the mode, drawn
    pop si
    ret

; x3c_line - the NUL text at SI on line DX (content y), padded to XD_W, at x 8.
; AL = 1 inverse. keeps everything
x3c_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, x3c_lbuf
    mov cx, XD_W / 8 - 2
    push ax
    call x3_putn
    mov al, ' '
    jcxz .p
    call x3_fillb
    add di, cx
.p:
    mov byte [di], 0
    pop ax
    mov bx, (CWHITE << 8) | CBLACK
    or al, al
    jz .c
    mov bx, (CBLACK << 8) | CWHITE
.c:
    mov ax, bx
    mov cx, [x3c_dox]
    add cx, 8
    add dx, [x3c_doy]
    mov si, x3c_lbuf
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3c_pdrive - the drive dialog's text
x3c_pdrive:
    mov si, x3c_s_need1             ; THE SENTENCE (SPEC.md 121.7)
    mov dx, 6
    xor al, al
    call x3c_line
    mov si, x3c_s_need2
    mov dx, 16
    call x3c_line
    mov dx, 32
    cmp byte [x3c_nv], 0
    jne .rows
    mov si, x3c_s_nohd
    call x3c_line
    add dx, XD_RH
    jmp short .info
.rows:
    xor cx, cx
.r:
    cmp cl, [x3c_nv]
    jae .info
    cmp cl, XD_DROWS
    jae .info
    call x3c_vrow                   ; row CL's text, at DX
    add dx, XD_RH
    inc cx
    jmp short .r
.info:
    add dx, 6
    push dx
    call x3c_qline                  ; 'Quality: XT STANDARD   PCM: 11,025 Hz...'
    pop dx
    mov si, x3c_lbuf2
    xor al, al
    call x3c_line
    add dx, 10
    push dx
    call x3c_nline                  ; 'Needs ~1,985 KB   About 17:00 to decode'
    pop dx
    mov si, x3c_lbuf2
    xor al, al
    jmp x3c_line

; x3c_vrow - drive row CX at y DX: '  C:  Hard disk    12,345 KB free  read-only'
x3c_vrow:
    push cx
    push dx
    mov bx, cx
    mov di, x3c_lbuf2
    mov ax, '  '
    mov [di], ax
    mov al, [x3c_vv+bx]
    add al, 'A'
    mov ah, ':'
    mov [di+2], ax
    add di, 4
    mov si, x3c_s_hd
    call x3_put
    test byte [x3c_vf+bx], XV_RO | XV_GONE
    jnz .flags
    push bx
    shl bx, 1
    shl bx, 1
    mov ax, [x3c_vkb+bx]
    mov dx, [x3c_vkb+bx+2]
    pop bx
    call dec_u32
    mov si, x3c_s_kbfree
    call x3_put
.flags:
    mov al, [x3c_vf+bx]
    mov si, x3c_s_ro
    test al, XV_RO
    jnz .f
    mov si, x3c_s_gone
    test al, XV_GONE
    jnz .f
    mov si, x3c_s_clu
    test al, XV_CLU
    jnz .f
    mov si, x3c_s_small
    test al, XV_SMALL
    jz .z
.f:
    call x3_put
.z:
    mov byte [di], 0
    pop dx
    pop cx
    xor al, al
    cmp cl, [x3c_vsel]
    jne .d
    inc al
.d:
    mov si, x3c_lbuf2
    jmp x3c_line

; x3c_qline - x3c_lbuf2 = 'Quality: <rung>   PCM: <rate> Hz <mono|stereo> <n>-bit'
x3c_qline:
    mov di, x3c_lbuf2
    mov si, x3_s_quality
    call x3_put
    mov al, [x3c_rung]
    call x3f_qname
    call x3_put
    mov si, x3c_s_pcm
    call x3_put
    mov ax, [x3_orate]
    xor dx, dx
    call dec_u32
    mov si, x3c_s_hz
    call x3_put
    mov si, x3c_s_mono
    cmp byte [x3_och], 2
    jne .m
    mov si, x3c_s_stereo
.m:
    call x3_put
    mov si, x3c_s_8
    cmp byte [x3_obits], 16
    jne .b
    mov si, x3c_s_16
.b:
    call x3_put
    mov byte [di], 0
    ret

; x3c_nline - x3c_lbuf2 = 'Needs ~N KB' and, when it was measured, 'About M
; min to decode' - an estimate only where it is justified (SPEC.md 121.7)
x3c_nline:
    mov di, x3c_lbuf2
    mov si, x3c_s_needs
    call x3_put
    cmp byte [x3c_nest], 0
    je .e
    mov byte [di], '~'
    inc di
.e:
    mov ax, [x3c_need]
    mov dx, [x3c_need+2]
    call dec_u32
    mov si, x3c_s_kb
    call x3_put
    mov ax, [x3c_est]
    cmp ax, 0xFFFF
    je .z
    or ax, ax
    jz .z
    mov bx, ax                      ; minutes = duration x 100 / speed / 60
    mov ax, [x3_info+X3I_DUR]
    mov cx, 100
    mul cx
    cmp dx, bx
    jae .z
    div bx                          ; AX = seconds of converting
    inc ax                          ; (a second at least)
    push ax
    mov si, x3c_s_about
    call x3_put
    pop ax
    call x3_mmss                    ; mm:ss
    dec di
    mov si, x3c_s_mindec
    call x3_put
.z:
    mov byte [di], 0
    ret

; -----------------------------------------------------------------------------
; THE BUTTONS: a record per window (os88ui.inc's, the standard look). DRIVE:
; Cancel, Decode and Play (greyed unless the row can take it; the default) and
; the batch switch when more of the queue follows. PROG: Cancel. FILES:
; Delete, Close
; -----------------------------------------------------------------------------
x3c_bset:
    mov bx, [x3c_dwin]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [x3c_dox], ax
    mov [x3c_doy], dx
    push bx
    call OSAPI_WM_GEOM              ; the content's height
    pop bx
    mov al, [x3c_dmode]
    xor cx, cx                      ; CL = buttons
    mov word [x3c_blbl], x3c_s_cancel
    mov word [x3c_bflg], 0
    mov cl, 1
    cmp al, XD_PROG
    je .lay
    cmp al, XD_FILES
    jne .drv
    mov word [x3c_blbl], x3c_s_close
    mov word [x3c_blbl+2], x3c_s_delete
    mov word [x3c_bflg+2], 0
    call x3c_frowok                 ; a row to delete?
    jnc .fd
    mov word [x3c_bflg+2], OS88UI_DIS
.fd:
    mov cl, 2
    jmp short .lay
.drv:
    mov word [x3c_blbl+2], x3c_s_decode
    mov word [x3c_bflg+2], OS88UI_DEF
    call x3c_vusable
    jnc .dok
    mov word [x3c_bflg+2], OS88UI_DIS
.dok:
    mov cl, 2
    mov al, [x3_st+X3S_CUR]         ; the batch switch: more queue follows
    inc al
    cmp al, [x3_qn]
    jae .lay
    mov word [x3c_blbl+4], x3c_s_qno
    cmp byte [x3c_bqueue], 0
    je .q
    mov word [x3c_blbl+4], x3c_s_qyes
.q:
    mov word [x3c_bflg+4], 0
    mov cl, 3
.lay:
    mov [x3c_btrec+OS88UI_BT_N], cl
    ; the row: from the right, XD_BW wide, 8 apart, XD_BH tall, 6 from the foot
    mov bx, [x3c_dwin]
    push cx
    mov si, bx
    mov ax, [es:si+W_H]
    pop cx
    mov dx, [x3c_doy]
    add dx, ax
    sub dx, TITLE_H + 1 + 6
    mov [x3c_by2], dx
    sub dx, XD_BH - 1
    mov [x3c_by1], dx
    mov ax, [x3c_dox]
    add ax, XD_W - 8                ; the first button's right edge
    xor bx, bx
    mov si, x3c_brect
.b:
    or cl, cl
    jz .draw
    mov [si+4], ax
    mov dx, ax
    sub dx, XD_BW - 1
    mov [si], dx
    mov dx, [x3c_by1]
    mov [si+2], dx
    mov dx, [x3c_by2]
    mov [si+6], dx
    sub ax, XD_BW + 8
    add si, 8
    dec cl
    jmp short .b
.draw:
    mov cl, [x3c_btrec+OS88UI_BT_N]
    mov al, 1
.d:
    cmp al, cl
    ja .out
    mov bx, x3c_btrec
    call os88ui_btn
    inc al
    jmp short .d
.out:
    ret

; the gesture: a button fires on the RELEASE
x3c_ddrag:
    mov bx, x3c_btrec
    jmp os88ui_btndrag

x3c_dup:
    mov bx, x3c_btrec
    call os88ui_btnup               ; AX = what fired + 1, 0 = none
    or ax, ax
    jz .out
    dec ax
    push si
    call x3c_bfire
    pop si
.out:
    ret

; x3c_bfire - button AL of the dialog. gfx lock held
x3c_bfire:
    mov ah, [x3c_dmode]
    cmp ah, XD_DRIVE
    je .drv
    cmp ah, XD_PROG
    je .prog
    or al, al                       ; FILES: Close, Delete
    jz .close
    jmp x3c_fdel
.close:
    jmp x3c_dlgclose
.prog:
    mov al, XU_CANCEL
    jmp x3c_post
.drv:
    or al, al
    jz .cancel
    cmp al, 1
    je .go
    xor byte [x3c_bqueue], 1        ; the batch switch
    jmp x3c_bset
.cancel:
    call x3c_dlgclose
    call x3c_chooseoff
    mov bx, [x3_win]
    jmp x3f_update
.go:
    call x3c_vusable
    jc .out
    mov al, XU_SAVE                 ; the Save dialog, from the main window's
    jmp x3c_post                    ; wake (the instance stands there)
.out:
    ret

; x3c_dclick - a press that is not a button's: a list row selects
x3c_dclick:
    sub dx, [x3c_doy]
    mov al, [x3c_dmode]
    cmp al, XD_PROG
    je .no
    mov ax, 32                      ; the list's first row
    cmp byte [x3c_dmode], XD_FILES
    jne .y
    mov ax, 6 + 12
.y:
    sub dx, ax
    jb .no
    mov ax, dx
    mov cl, XD_RH
    div cl                          ; AL = the row
    cmp byte [x3c_dmode], XD_FILES
    je .f
    cmp al, [x3c_nv]
    jae .no
    mov [x3c_vsel], al
    jmp short .re
.f:
    call x3c_fpick
    jc .no
.re:
    push si
    call x3c_dpaint
    pop si
    stc
    ret
.no:
    clc
    ret

; x3c_dkey - the dialog's keys: Up / Down move, Enter is the default, Esc
; Cancel, Q the batch switch
x3c_dkey:
    push si
    mov bl, [x3c_dmode]
    cmp ah, KSC_ESC
    je .esc
    cmp bl, XD_PROG
    je .out
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .dn
    cmp al, 0x0D
    je .ent
    cmp bl, XD_DRIVE
    jne .out
    and al, 0xDF
    cmp al, 'Q'
    jne .out
    mov al, 2
    jmp short .fire
.esc:
    xor al, al
    cmp bl, XD_FILES
    jne .fire
    jmp short .fire
.ent:
    mov al, 1
.fire:
    call x3c_bfire
    jmp short .out
.up:
    mov al, -1
    jmp short .mv
.dn:
    mov al, 1
.mv:
    cmp bl, XD_FILES
    je .fm
    add al, [x3c_vsel]
    cmp al, [x3c_nv]
    jae .out
    mov [x3c_vsel], al
    jmp short .rp
.fm:
    call x3c_fmove
.rp:
    pop si
    push si
    call x3c_dpaint
.out:
    pop si
    ret

; =============================================================================
; THE SAVE DIALOG: the folder and the name, standing in the chosen volume
; =============================================================================

; x3c_savedlg - the drive dialog down, the instance standing in the volume
; (in the last destination's folder when it is there), the Standard File
; Save dialog up with the track's stem.WAV. Its window slot is banked, so a
; Cancel - which calls nothing back (SPEC.md 38.6) - is seen by x3c_tick
; (Telnet's way, SPEC.md 70.11.4)
x3c_savedlg:
    mov bl, [x3c_vsel]
    xor bh, bh
    mov bl, [x3c_vv+bx]
    mov [x3c_dvol], bl
    call x3c_dlgclose
    xor dx, dx
    cmp byte [x3c_lset], 0
    je .root
    cmp bl, [x3c_lvol]
    jne .root
    mov dx, [x3c_ldir]
.root:
    call OSAPI_FILE_GOTO_QM
    jnc .stand
    xor dx, dx                      ; the folder went: the root
    call OSAPI_FILE_GOTO_QM
.stand:
    call x3c_stemwav                ; x3c_wname = STEM.WAV
    call x3c_wmap                   ; the window slots before...
    mov [x3c_wmap0], ax
    mov al, FDLG_SAVE
    mov bx, [x3_win]
    mov di, x3c_onsave
    mov si, x3c_wname
    call OSAPI_FILE_DLG
    jc .refused
    call x3c_wmap                   ; ...and after: the new one is the dialog's
    xor ax, [x3c_wmap0]
    mov [x3c_dslot], ax
    mov byte [x3c_savepend], 1
    ret
.refused:
    jmp x3c_chooseoff

; x3c_wmap - AX = a bitmap of the live window slots (Telnet's tz_wmap)
x3c_wmap:
    push bx
    push cx
    push dx
    xor bx, bx
    xor cx, cx
.s:
    mov al, cl
    call OSAPI_WM_OWNSEG
    jc .n
    mov ax, 1
    shl ax, cl
    or bx, ax
.n:
    inc cl
    cmp cl, MAX_WIN
    jb .s
    mov ax, bx
    pop dx
    pop cx
    pop bx
    ret

; x3c_stemwav - x3c_wname = the source's stem + '.WAV'. keeps BX
x3c_stemwav:
    mov si, x3_fname
    mov di, x3c_wname
    mov cx, 8
.c:
    lodsb
    or al, al
    jz .e
    cmp al, '.'
    je .e
    mov [di], al
    inc di
    loop .c
.e:
    mov si, x3c_s_dwav
    call x3_put
    mov byte [di], 0
    ret

; -----------------------------------------------------------------------------
; x3c_onsave - the Save dialog's answer (SPEC.md 38.6): AL = mode, SI = our
; window, DI = the name (the kernel's, for this call), DX:CX = a file's size
; there (0: none). The folder must be on a writable hard drive, the name new,
; the room there; else a toast and the dialog again
; -----------------------------------------------------------------------------
x3c_onsave:
    mov byte [x3c_savepend], 0
    mov si, di                      ; the name, ours, with .WAV forced
    mov di, x3c_wname
    mov cx, 8
.c:
    mov al, [es:si]
    or al, al
    jz .e
    cmp al, '.'
    je .e
    mov [di], al
    inc si
    inc di
    loop .c
.e:
    cmp di, x3c_wname
    je .bad                         ; (no stem at all)
    push si
    mov si, x3c_s_dwav
    call x3_put
    pop si
    mov byte [di], 0
    push ds
    pop es
    call OSAPI_FILE_HERE            ; THE FOLDER: a hard drive's, writable
    mov [x3c_dvol], bl
    mov [x3c_ddir], dx
    mov al, bl
    call OSAPI_VOL_KIND
    jc .nothd
    cmp al, VK_FIXED
    jne .nothd
    cmp ah, VT_FILE
    je .nothd
    mov si, x3c_wname               ; THE NAME: not there already
    call x3_fsize
    jnc .taken
    call OSAPI_VOL_STAT             ; THE ROOM, again, here
    jc .nothd
    mul bx
    shr dx, 1
    rcr ax, 1
    cmp dx, [x3c_need+2]
    ja .room
    jb .full
    cmp ax, [x3c_need]
    jb .full
.room:
    mov al, [x3c_dvol]              ; remembered, for the next job
    mov [x3c_lvol], al
    mov ax, [x3c_ddir]
    mov [x3c_ldir], ax
    mov byte [x3c_lset], 1
    cmp byte [x3c_bqueue], 0        ; THE BATCH: approved for the queue
    je .go
    mov byte [x3_c_batch], 1
    mov al, [x3c_dvol]
    mov [x3c_bvol], al
    mov ax, [x3c_ddir]
    mov [x3c_bdir], ax
.go:
    jmp x3c_start
.nothd:
    mov si, x3c_s_nothd
    jmp short .again
.taken:
    mov si, x3c_s_taken
    jmp short .again
.full:
    mov si, x3c_s_full
    jmp short .again
.bad:
    push ds
    pop es
    mov si, x3c_s_badnm
.again:
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    jmp x3c_savedlg.stand           ; the dialog again, where we stand

; x3c_batchgo - an approved batch: the batch's folder, the stem's name (a
; digit for the stem's last letter while it is taken), the room checked.
; out: CF = 1 it cannot (the dialog is asked instead)
x3c_batchgo:
    mov dx, [x3c_bdir]
    mov bl, [x3c_bvol]
    call OSAPI_FILE_GOTO_QM
    jc .no
    mov [x3c_dvol], bl
    mov [x3c_ddir], dx
    call x3c_stemwav
    mov si, x3c_wname
    call x3_fsize
    jc .free
    call x3c_unique                 ; STEM -> STE1 .. STE9
    jc .no
.free:
    call OSAPI_VOL_STAT
    jc .no
    mul bx
    shr dx, 1
    rcr ax, 1
    cmp dx, [x3c_need+2]
    ja .go
    jb .no
    cmp ax, [x3c_need]
    jb .no
.go:
    jmp x3c_start
.no:
    mov byte [x3_c_batch], 0        ; the batch ends where it cannot go on
    stc
    ret

; x3c_unique - x3c_wname taken: its stem's last character made 1..9 until it
; is free. CF = 1 none was
x3c_unique:
    mov di, x3c_wname
.f:
    cmp byte [di], '.'
    je .d
    inc di
    jmp short .f
.d:
    dec di                          ; DI -> the stem's last
    mov byte [di], '1'
.t:
    mov si, x3c_wname
    call x3_fsize
    jc .ok
    inc byte [di]
    cmp byte [di], '9'
    jbe .t
    stc
    ret
.ok:
    clc
    ret

; =============================================================================
; THE CONVERSION
; =============================================================================

; -----------------------------------------------------------------------------
; x3c_start - standing in the destination folder, with x3c_wname chosen: the
; backend opened on the rung, the stream from its start, the temporary file
; made with the header cluster, the stage claimed, the worker on it, the
; progress window up. On any refusal x3_st says why and nothing is left
; -----------------------------------------------------------------------------
x3c_start:
    mov word [x3_st+X3S_PEAK], 0    ; the peak is this job's (x3_memck)
    call x3_memck
    call x3_cluget                  ; the DESTINATION's cluster
    jc .clu
    mov ax, [ap_clu]
    mov [x3c_clu], ax
    mov [x3c_planc], ax             ; THE STAGE is planned by x3_rungset once
                                    ; the decoder is in (x3_plan): two halves
                                    ; of max(8 KB, 4 under XT, the cluster), or
                                    ; one, under the XT path's ceiling; it
                                    ; rides in the ring's claim (x3_la_claim)
    mov si, [x3_trkent]             ; THE DECODER FIRST (SPEC.md 121.10's
    mov dx, [si+QE_DIR]             ; claim order: the part, its work and
    mov bl, [si+QE_VOL]             ; OPEN, then the ring, then the stage),
    mov [ap_trk_dir], dx            ; on the rung, from the start. The ring
    mov [ap_trk_vol], bl            ; reads by name, and aplook.inc stands
    mov word [apw_name], x3_fname   ; in the source's folder itself
    call OSAPI_FILE_GOTO_QM
    jc .rderr
    call x3_cluget                  ; (the SOURCE's cluster, for the ring)
    jc .rderr
    mov al, [x3c_rung]
    call x3_rungset
    mov word [x3c_planc], 0         ; (the plan is this job's alone)
    jc .be
    xor ax, ax
    xor dx, dx
    call x3_resync
    jc .be
    mov bl, [x3c_dvol]              ; back to the destination
    mov dx, [x3c_ddir]
    call OSAPI_FILE_GOTO_QM
    jc .wrerr
    mov ax, [x3_lasz]               ; the stage: past the ring and the PCM
    add ax, X3_PCMRES
    mov cl, 4
    shr ax, cl
    add ax, [ap_la_seg]
    mov [x3c_sseg], ax
    call x3c_tmpname                ; X3nnnnnn.TMP, free here
    jc .wrerr
    call x3c_hdr                    ; the header cluster at the stage's 0
    mov si, x3c_tname               ; THE FILE: created with it
    mov es, [x3c_sseg]
    xor bx, bx
    mov cx, [x3c_clu]
    xor dx, dx
    call OSAPI_FILE_WRITE
    push ds
    pop es
    jc .wrerr
    mov byte [x3c_tmade], 1
    xor ax, ax
    mov [x3c_dlen], ax              ; nothing written yet but the header
    mov [x3c_dlen+2], ax
    mov [x3c_frms], ax
    mov [x3c_frms+2], ax
    mov [x3c_fpos], ax
    mov [x3c_pend], ax
    mov [x3c_tok], ax
    mov [x3c_fh], al
    mov [x3c_wh], al
    mov [x3c_rdy], al
    mov [x3c_eof], al
    mov [x3c_canc], al
    mov [x3c_err], ax
    mov byte [x3_rderr], 0
    call OSAPI_GET_TICKS
    mov [x3c_t0], ax
    mov [x3c_drawt], ax
    sub word [x3c_drawt], XC_DRAWT
    call x3_wspawn
    jc .notask
    mov byte [x3_md], MD_CONV
    mov byte [x3_st+X3S_STATE], X3ST_DECODE
    mov byte [x3_st+X3S_PCT], 0
    mov word [x3_st+X3S_ELAP], 0
    mov al, XD_PROG
    call x3c_dopen
    mov byte [x3_job], WJ_CONV      ; LAST: the worker starts on it
    ret
.clu:
    mov word [x3c_err], x3c_s_clu2
    jmp short .fail
.nomem:
    mov word [x3c_err], ap_s_nomem
    jmp short .fail
.wrerr:
    mov word [x3c_err], x3c_s_cantw
    jmp short .fail
.rderr:
    mov word [x3c_err], x3_s_noread
    jmp short .fail
.notask:
    mov word [x3c_err], x3_s_notask
    jmp short .fail
.be:
    mov ax, [x3_st+X3S_ERR]
    mov [x3c_err], ax
.fail:
    call x3c_teardown
    mov ax, [x3c_err]
    mov [x3_st+X3S_ERR], ax
    mov byte [x3_md], MD_IDLE
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    ret

; x3c_tmpname - x3c_tname = X3nnnnnn.TMP, nnnnnn the tick count and a count,
; in hex, and not there already in the folder we stand in. CF = 1 none free
x3c_tmpname:
    call OSAPI_GET_TICKS
    mov bx, ax
    mov cx, 64
.t:
    mov di, x3c_tname
    mov word [di], 'X3'
    add di, 2
    mov ax, bx
    call x3c_hex4
    mov al, cl
    call x3c_hex2
    mov si, x3c_s_dtmp
    call x3_put
    mov byte [di], 0
    mov si, x3c_tname
    push bx
    push cx
    call x3_fsize
    pop cx
    pop bx
    jc .free
    loop .t
    stc
    ret
.free:
    clc
    ret

; x3c_hex4 / x3c_hex2 - AX / AL as hex at DI. DI past it
x3c_hex4:
    push ax
    mov al, ah
    call x3c_hex2
    pop ax
x3c_hex2:
    push ax
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call .d
    mov al, ah
    and al, 15
    call .d
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .s
    add al, 7
.s:
    mov [di], al
    inc di
    ret

; -----------------------------------------------------------------------------
; x3c_hdr - THE HEADER CLUSTER at the stage's offset 0: RIFF, fmt, xtm3 (the
; identity), JUNK to the cluster's end, 'data' - with the lengths so far
; ([x3c_dlen]: 0 at the start, the whole at finalisation)
; -----------------------------------------------------------------------------
x3c_hdr:
    push es
    mov es, [x3c_sseg]
    xor di, di
    mov cx, [x3c_clu]               ; zeroed first: the JUNK is zeros
    shr cx, 1
    xor ax, ax
    cld
    rep stosw
    mov word [es:0], 'RI'
    mov word [es:2], 'FF'
    mov ax, [x3c_dlen]              ; RIFF's size: cluster - 8 + data
    mov dx, [x3c_dlen+2]
    add ax, [x3c_clu]
    adc dx, 0
    sub ax, 8
    sbb dx, 0
    mov [es:4], ax
    mov [es:6], dx
    mov word [es:8], 'WA'
    mov word [es:10], 'VE'
    mov word [es:X3C_FMT], 'fm'
    mov word [es:X3C_FMT+2], 't '
    mov word [es:X3C_FMT+4], 16
    mov word [es:X3C_FMT+8], 1      ; PCM
    mov al, [x3_och]
    xor ah, ah
    mov [es:X3C_FMT+10], ax
    mov ax, [x3_orate]
    mov [es:X3C_FMT+12], ax
    mul word [x3_fby]
    mov [es:X3C_FMT+16], ax         ; bytes a second
    mov [es:X3C_FMT+18], dx
    mov ax, [x3_fby]
    mov [es:X3C_FMT+20], ax
    mov al, [x3_obits]
    xor ah, ah
    mov [es:X3C_FMT+22], ax
    mov word [es:X3C_XTM], 'xt'     ; THE IDENTITY
    mov word [es:X3C_XTM+2], 'm3'
    mov word [es:X3C_XTM+4], X3C_IDN
    mov di, X3C_XTM + 8
    mov byte [es:di+XI_VER], 1
    push di
    lea di, [di+XI_SIZE]
    call x3_fpcur                   ; (DI clobbered: the print's row)
    mov si, di
    pop di
    push di
    lea di, [di+XI_SIZE]
    mov cx, X3_FPSZ
    rep movsb
    pop di
    mov word [es:di+XI_DECV], X3_DECVER
    mov word [es:di+XI_TABV], X3_TABVER
    call x3_rungrow                 ; BX -> the rung's row
    mov al, [bx+RG_BE]
    mov [es:di+XI_BE], al
    mov al, [bx+RG_PROF]
    mov [es:di+XI_PROF], al
    mov al, [x3_odiv]
    mov [es:di+XI_DIV], al
    mov al, [x3c_rung]
    mov [es:di+XI_RUNG], al
    mov ax, [x3_orate]
    mov [es:di+XI_RATE], ax
    mov al, [x3_och]
    mov [es:di+XI_CH], al
    mov al, [x3_obits]
    mov [es:di+XI_BITS], al
    mov ax, [x3c_frms]
    mov [es:di+XI_FRAMES], ax
    mov ax, [x3c_frms+2]
    mov [es:di+XI_FRAMES+2], ax
    push di
    lea di, [di+XI_STEM]
    mov si, x3_fname
    mov cx, 8
.st:
    lodsb
    or al, al
    jz .se
    cmp al, '.'
    je .se
    stosb
    loop .st
.se:
    pop di
    mov word [es:X3C_JUNK], 'JU'    ; THE PAD: the data on the cluster
    mov word [es:X3C_JUNK+2], 'NK'
    mov ax, [x3c_clu]
    sub ax, X3C_JUNK + 8 + 8
    mov [es:X3C_JUNK+4], ax
    mov bx, [x3c_clu]
    mov word [es:bx-8], 'da'
    mov word [es:bx-6], 'ta'
    mov ax, [x3c_dlen]
    mov [es:bx-4], ax
    mov ax, [x3c_dlen+2]
    mov [es:bx-2], ax
    pop es
    ret

; -----------------------------------------------------------------------------
; x3c_wpass - THE WORKER'S CONVERSION PASS: granules decoded into the stage's
; half being filled, a whole half handed to the UI task to write (X3R_WRITE),
; the next one filled meanwhile; the end of the stream hands the last, short
; half and X3R_END. A Cancel is seen between granules. Never a file, never the
; heap (SPEC.md 20.6 rule 7)
; -----------------------------------------------------------------------------
x3c_wpass:
    mov byte [x3c_wn], 4            ; granules a pass, at most
    call x3_lalow                   ; the next gulp asked for in good time
.loop:
    cmp byte [x3_job], WJ_CONV
    jne .out
    cmp byte [x3c_canc], 0
    jne .out
    mov al, [x3c_fh]                ; the half being filled: still written?
    call x3c_hbit
    test [x3c_rdy], al
    jnz .out                        ; ...the writer is behind: wait
    mov cx, [x3c_pend]              ; A GRANULE'S REST, held in the PCM while
    jcxz .room                      ; the one half was written (single-
    mov si, [x3c_psrc]              ; buffered): into the half first
    mov word [x3c_pend], 0
    call x3c_put1
    jmp short .loop
.room:
    cmp byte [x3c_one], 0           ; (one half: a granule's rest waits in
    jne .dec                        ; the PCM, x3c_put1)
    mov ax, [x3c_half]              ; room for a granule in this half, or the
    sub ax, [x3c_fpos]              ; other half free to run on into
    cmp ax, X3_PCMSZ
    jae .dec
    mov al, [x3c_fh]
    xor al, 1
    call x3c_hbit
    test [x3c_rdy], al
    jnz .out
.dec:
    call x3_gran1                   ; CX bytes of PCM
    jc .dry
    call x3c_put                    ; ...into the stage
    dec byte [x3c_wn]
    jnz .loop
.out:
    ret
.dry:
    cmp al, 2
    jne .out                        ; starved (a gulp is asked for) or refused
    mov ax, [x3c_fpos]              ; THE END: the last half, short
    mov [x3c_lastn], ax
    mov al, [x3c_fh]
    call x3c_hbit
    or [x3c_rdy], al
    mov byte [x3c_eof], 1
    mov byte [x3_job], WJ_NONE
    mov al, X3R_WRITE | X3R_END
    jmp x3_kick

; x3c_hbit - AL = a half (0/1) -> AL = its bit in [x3c_rdy]. keeps the rest
x3c_hbit:
    inc al
    ret

; x3c_put - CX bytes of the granule's PCM into the stage at the filling half, spilling
; into the other; a filled half is handed to the writer. The worker's
x3c_put:
    push ax
    push dx
    mov ax, cx                      ; the frames, counted for the identity
    xor dx, dx
    div word [x3_fby]
    add [x3c_frms], ax
    adc word [x3c_frms+2], 0
    pop dx
    pop ax
    push si
    mov si, X3_PCMOFF               ; (in the ring's claim, x3eng.inc)
    call x3c_put1
    pop si
    ret

; x3c_put1 - CX bytes of PCM at SI (the ring's claim) into the stage: what
; a busy half has no room for is left in [x3c_pend] / [x3c_psrc], which the
; next pass puts first. The worker's. keeps everything
x3c_put1:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [x3c_sseg]
.p:
    jcxz .out
    mov al, [x3c_fh]                ; the half to fill still being written
    call x3c_hbit                   ; (ONE half, just handed over): the rest
    test [x3c_rdy], al              ; waits where it is
    jz .free
    mov [x3c_pend], cx
    mov [x3c_psrc], si
    jmp short .out
.free:
    mov bx, [x3c_half]
    sub bx, [x3c_fpos]              ; BX = room in the half
    mov ax, cx
    cmp ax, bx
    jbe .c
    mov ax, bx
.c:
    push cx
    mov cx, ax
    mov di, [x3c_fpos]
    cmp byte [x3c_fh], 0
    je .h0
    add di, [x3c_half]
.h0:
    cld
    push ds
    mov ds, [ap_la_seg]
    rep movsb
    pop ds
    pop cx
    sub cx, ax
    add [x3c_fpos], ax
    mov bx, [x3c_fpos]
    cmp bx, [x3c_half]
    jb .p
    mov al, [x3c_fh]                ; A HALF: to the writer
    call x3c_hbit
    or [x3c_rdy], al
    cmp byte [x3c_one], 0           ; (one half: the same one, once written)
    jne .one
    xor byte [x3c_fh], 1
.one:
    mov word [x3c_fpos], 0
    push cx
    mov al, X3R_WRITE
    call x3_kick
    pop cx
    jmp short .p
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3c_write - X3R_WRITE (the UI task, from the wake): every staged half
; written, in order, through OSAPI_FILE_WRITE_SEQ - held, and kept on a fixed
; disk (SPEC.md 18.4.9.3) - standing in the destination first. A refusal
; stops the job with its reason (x3c_fail). The bit is cleared LAST
; -----------------------------------------------------------------------------
x3c_write:
.h:
    cmp byte [x3_md], MD_CONV
    jne .clear
    mov al, [x3c_wh]
    call x3c_hbit
    test [x3c_rdy], al
    jz .clear
    mov cx, [x3c_half]
    cmp byte [x3c_eof], 0
    je .full
    mov al, [x3c_wh]                ; the end: is this the short last half?
    cmp al, [x3c_fh]
    jne .full
    mov cx, [x3c_lastn]
.full:
    push cx
    call x3c_stand                  ; the destination, else the volume left
    pop cx
    jc .gone
    jcxz .done1
    mov si, x3c_tname
    mov es, [x3c_sseg]
    xor bx, bx
    cmp byte [x3c_wh], 0
    je .b
    mov bx, [x3c_half]
.b:
    mov al, WSEQF_HELD | WSEQF_KEEP
    mov di, [x3c_tok]
    push cx
    call OSAPI_FILE_WRITE_SEQ
    pop cx
    push ds
    pop es
    jc .err
    mov [x3c_tok], di
    add [x3c_dlen], cx
    adc word [x3c_dlen+2], 0
.done1:
    mov al, [x3c_wh]
    call x3c_hbit
    not al
    and [x3c_rdy], al               ; the half free again (the worker's)
    cmp byte [x3c_one], 0
    jne .h
    xor byte [x3c_wh], 1
    jmp short .h
.gone:
    mov ax, FERR_NODISK
.err:
    call x3c_fail
.clear:
    and byte [x3_req], ~X3R_WRITE   ; LAST
    ret

; x3c_stand - the instance in the destination folder (a word compare when it
; is there already). CF = 1 the volume or the folder is gone
x3c_stand:
    call OSAPI_FILE_HERE
    cmp dx, [x3c_ddir]
    jne .go
    cmp bl, [x3c_dvol]
    je .ok
.go:
    mov dx, [x3c_ddir]
    mov bl, [x3c_dvol]
    call OSAPI_FILE_GOTO_QM
    ret
.ok:
    clc
    ret

; -----------------------------------------------------------------------------
; x3c_fail - AX = a FERR_*: the job stops with its reason - the disk full, the
; drive gone, a write refused - its temporary file deleted, and nothing plays
; -----------------------------------------------------------------------------
x3c_fail:
    mov bx, x3c_s_full2
    cmp ax, FERR_FULL
    je .s
    mov bx, x3c_s_gone2
    cmp ax, FERR_NODISK
    je .s
    mov bx, x3c_s_cantw
.s:
    mov [x3c_err], bx
    call x3c_teardown
    mov ax, [x3c_err]
    mov [x3_st+X3S_ERR], ax
    mov byte [x3_md], MD_IDLE
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    mov byte [x3_c_batch], 0
    ret

; x3c_cancel - Esc, or the progress window's Cancel: answered within one
; granule (the worker sees the flag between granules, and x3_park waits that
; one out); ONLY this job's temporary file deleted; the track READY
x3c_cancel:
    cmp byte [x3_md], MD_CONV
    jne .out
    mov byte [x3c_canc], 1
    call x3c_teardown
    mov byte [x3_md], MD_IDLE
    mov byte [x3_st+X3S_STATE], X3ST_READY
    mov byte [x3_c_batch], 0
    mov word [x3_st+X3S_ELAP], 0
    mov byte [x3_st+X3S_PCT], 0
.out:
    ret

; x3c_abort - x3_stopall's: the same, without the state
x3c_abort:
    mov byte [x3c_canc], 1
    jmp x3c_teardown

; x3c_teardown - the worker parked, the write stream closed, the temporary
; file deleted (this job's, by its own name, in its own folder), the stage
; freed, the progress window down. keeps nothing
x3c_teardown:
    call x3_park
    and byte [x3_req], ~(X3R_WRITE | X3R_END)
    cmp byte [x3c_tmade], 0
    je .stage
    call x3c_stand
    jc .gone
    cmp word [x3c_tok], 0
    je .del
    mov si, x3c_tname               ; the hold committed: then deleted
    xor cx, cx
    mov al, WSEQF_HELD | WSEQF_KEEP
    mov di, [x3c_tok]
    call OSAPI_FILE_WRITE_SEQ
.del:
    mov si, x3c_tname
    call OSAPI_FILE_DELETE
.gone:
    mov byte [x3c_tmade], 0
    mov word [x3c_tok], 0
.stage:
    mov word [x3c_sseg], 0          ; (the ring's claim: x3_la_free's)
    mov word [x3_laxkb], 0
.win:
    cmp byte [x3c_dmode], XD_PROG
    jne .out
    call x3c_dlgclose
.out:
    ret

; -----------------------------------------------------------------------------
; x3c_tick - the timer's turn while CHOOSING (a cancelled Save dialog is seen
; here: its window slot freed with no answer) and while CONVERTING (x3_st's
; progress fields every pass; the progress window twice a second)
; -----------------------------------------------------------------------------
x3c_tick:
    cmp byte [x3_md], MD_CHOOSE
    jne .conv
    cmp byte [x3c_savepend], 0
    je .out
    mov ax, [x3c_dslot]             ; THE SAVE DIALOG'S SLOT, asked: free and
    or ax, ax                       ; no answer is a Cancel (one UI-task call
    jz .out                         ; chain destroys it and answers, so this
    xor cl, cl                      ; dispatch never sees it between)
.b:
    shr ax, 1
    jc .slot
    inc cl
    jmp short .b
.slot:
    mov al, cl
    call OSAPI_WM_OWNSEG
    jnc .out
    call x3c_chooseoff
    jmp short .out
.conv:
    cmp byte [x3_md], MD_CONV
    jne .out
    call x3c_prog                   ; x3_st's fields
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [x3c_drawt]
    cmp ax, XC_DRAWT
    jb .out
    mov [x3c_drawt], bx
    mov bx, [x3c_dwin]
    or bx, bx
    jz .out
    call OSAPI_WM_CLIP_SET
    jc .out
    call OSAPI_WM_CONTENT
    mov [x3c_dox], ax
    mov [x3c_doy], dx
    call x3c_pprog2                 ; the two lines that move
.out:
    clc
    ret

; x3c_prog - a conversion's figures into x3_st: decoded seconds (X3S_ELAP),
; the percentage of the source's bytes consumed (VBR-proof), the speed, the
; gauge
x3c_prog:
    mov ax, [x3c_frms]              ; decoded seconds
    mov dx, [x3c_frms+2]
    mov cx, [x3_orate]
    or cx, cx
    jz .pct
    cmp dx, cx
    jae .pct
    div cx
    mov [x3_st+X3S_ELAP], ax
.pct:
    mov ax, [x3_ring+X3R_POS]       ; consumed / the audio's bytes
    mov dx, [x3_ring+X3R_POS+2]
    sub ax, [x3_info+X3I_AUDIO]
    sbb dx, [x3_info+X3I_AUDIO+2]
    mov bx, [x3_info+X3I_AEND]
    mov cx, [x3_info+X3I_AEND+2]
    sub bx, [x3_info+X3I_AUDIO]
    sbb cx, [x3_info+X3I_AUDIO+2]
    call x3_ratio                   ; AX = percent
    cmp ax, 100
    jbe .p
    mov ax, 100
.p:
    mov [x3_st+X3S_PCT], al
    mov ah, 0                       ; the gauge: 0..255
    mov cx, 255
    mul cx
    mov cl, 100
    div cl
    mov [x3_st+X3S_LEVEL], al
    call x3c_speed
    mov [x3_st+X3S_SPEED], ax
    ret

; x3c_speed - AX = decoded audio a wall second x 100, since the start
x3c_speed:
    push bx
    push cx
    push dx
    mov ax, [x3c_frms]              ; the audio, ms
    mov dx, [x3c_frms+2]
    mov cx, 1000
    mov bx, [x3_orate]
    or bx, bx
    jz .z
    call x3_mdiv
    push dx
    push ax
    call OSAPI_GET_TICKS            ; the wall, ms (a tick is 54.925)
    sub ax, [x3c_t0]
    xor dx, dx
    mov cx, 54925
    mov bx, 1000
    call x3_mdiv
    mov bx, ax
    mov cx, dx
    pop ax
    pop dx
    call x3_ratio
    jmp short .out
.z:
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; x3c_pprog - the progress window's four lines
x3c_pprog:
    mov di, x3c_lbuf2               ; 'To:      C:  NAME.WAV'
    mov si, x3c_s_to
    call x3_put
    mov al, [x3c_dvol]
    add al, 'A'
    mov ah, ':'
    mov [di], ax
    mov word [di+2], '  '
    add di, 4
    mov si, x3c_wname
    call x3_put
    mov byte [di], 0
    mov si, x3c_lbuf2
    mov dx, 6
    xor al, al
    call x3c_line
    call x3c_qline
    mov si, x3c_lbuf2
    mov dx, 16
    xor al, al
    call x3c_line
x3c_pprog2:
    mov di, x3c_lbuf2               ; 'Audio:   01:23 of 04:38   30%'
    mov si, x3c_s_audio
    call x3_put
    mov ax, [x3_st+X3S_ELAP]
    call x3_mmss
    mov si, x3c_s_of
    call x3_put
    mov ax, [x3_st+X3S_TOTAL]
    cmp ax, 0xFFFF
    jne .t
    mov si, x3c_s_unk
    call x3_put
    jmp short .p
.t:
    test byte [x3_st+X3S_FLAGS], X3SF_EST
    jz .x
    mov byte [di], '~'
    inc di
.x:
    call x3_mmss
.p:
    mov al, [x3_st+X3S_PCT]
    xor ah, ah
    xor dx, dx
    call dec_u32
    mov byte [di], '%'
    mov byte [di+1], 0
    mov si, x3c_lbuf2
    mov dx, 26
    xor al, al
    call x3c_line
    mov di, x3c_lbuf2               ; 'Elapsed: 04:12   Speed: 0.31x'
    mov si, x3c_s_elap
    call x3_put
    call OSAPI_GET_TICKS
    sub ax, [x3c_t0]
    mov cx, 10                      ; ticks -> seconds: x 10 / 182
    mul cx
    mov cx, 182
    div cx
    call x3_mmss
    mov si, x3c_s_speed
    call x3_put
    mov ax, [x3_st+X3S_SPEED]
    xor dx, dx
    mov cx, 100
    div cx
    push dx
    xor dx, dx
    call dec_u32
    mov byte [di], '.'
    inc di
    pop ax
    call x3_2dig
    mov byte [di], 'x'
    mov byte [di+1], 0
    mov si, x3c_lbuf2
    mov dx, 36
    xor al, al
    jmp x3c_line

; =============================================================================
; FINALISATION (SPEC.md 121.7)
; =============================================================================

; -----------------------------------------------------------------------------
; x3c_final - X3R_END, on the UI task WITHOUT the lock (it is all file I/O):
; the stream committed, the header written with the lengths, the source's
; print taken again, the file re-read and checked, renamed to the .WAV and
; entered in the index. [x3c_err] 0 on success, else the reason - and then
; the temporary file is already deleted
; -----------------------------------------------------------------------------
x3c_final:
    cmp byte [x3_md], MD_CONV
    jne .out
    cmp byte [x3c_eof], 0
    je .out
    test byte [x3_req], X3R_WRITE   ; (the last half not written yet)
    jnz .out
    mov byte [x3c_eof], 0
    mov word [x3c_err], 0
    cmp byte [x3_rderr], 0          ; a bad read ended the source: a failure,
    je .r                           ; never a shorter file
    mov word [x3c_err], x3c_s_rdbad
    jmp .fail
.r:
    call x3c_stand
    jc .gone
    mov si, x3c_tname               ; 1. THE STREAM, committed
    xor cx, cx
    mov al, WSEQF_HELD | WSEQF_KEEP
    mov di, [x3c_tok]
    call OSAPI_FILE_WRITE_SEQ
    mov word [x3c_tok], 0
    jc .wr
    call x3c_hdr                    ; 2. THE HEADER, with the lengths
    mov si, x3c_tname
    mov es, [x3c_sseg]
    xor bx, bx
    mov cx, [x3c_clu]
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_WRITE_AT
    push ds
    pop es
    jc .wr
    mov si, [x3_trkent]             ; 3. THE SOURCE, unchanged?
    mov dx, [si+QE_DIR]
    mov bl, [si+QE_VOL]
    call OSAPI_FILE_GOTO_QM
    jc .chg
    mov si, x3_fname
    call x3_fsize
    jc .chg
    mov [x3_flen], ax
    mov [x3_flen+2], dx
    call x3_cluget
    jc .chg
    mov di, x3c_fp2
    call x3c_fpcalc
    jc .chg
    call x3_fpcur
    mov si, x3c_fp2
    mov cx, X3_FPSZ
    call x3_memeq
    jne .chg
    call x3c_stand                  ; 4. THE FILE, re-read and checked
    jc .gone
    mov si, x3c_tname
    call x3c_wavchk
    jc .bad
    mov ax, [x3c_wdlen]
    cmp ax, [x3c_dlen]
    jne .bad
    mov ax, [x3c_wdlen+2]
    cmp ax, [x3c_dlen+2]
    jne .bad
    mov si, x3c_tname               ; 5. RENAMED: complete, at last
    mov di, x3c_wname
    call OSAPI_FILE_RENAME
    jc .ren
    mov byte [x3c_tmade], 0
    call x3c_ixput                  ; 6. THE INDEX (its failure is not the
    jnc .out                        ; file's: the WAV stands)
    mov word [x3c_err], 0
.out:
    ret
.gone:
    mov word [x3c_err], x3c_s_gone2
    jmp short .fail
.wr:
    mov word [x3c_err], x3c_s_cantw
    jmp short .fail
.chg:
    mov word [x3c_err], x3c_s_chg
    jmp short .fail
.bad:
    mov word [x3c_err], x3c_s_verify
    jmp short .fail
.ren:
    mov word [x3c_err], x3c_s_ren
.fail:
    mov byte [x3c_eof], 0
    ret

; x3c_ixput - the finished file into the index: the first free record (else
; the oldest, record 0, moves out). out: CF = 1 not saved
x3c_ixput:
    call x3c_ixload
    xor al, al
.f:
    call x3c_ixrow
    cmp byte [si+XR_USED], 0
    je .got
    inc al
    cmp al, X3C_NIX
    jb .f
    push si                         ; full: every record up one, the last free
    push di
    push cx
    push es
    push ds
    pop es
    mov di, x3c_ix + X3C_IXHDR
    lea si, [di+XR_SIZE]
    mov cx, (X3C_NIX - 1) * XR_SIZE
    cld
    rep movsb
    pop es
    pop cx
    pop di
    pop si
.got:
    push si
    mov di, si
    mov cx, XR_SIZE
    xor al, al
    call x3_fillb
    call x3_fpcur                   ; the print
    xchg si, di
    pop di
    push di
    push es
    push ds
    pop es
    mov cx, X3_FPSZ
    cld
    rep movsb
    pop es
    pop si
    mov word [si+XR_DECV], X3_DECVER
    mov word [si+XR_TABV], X3_TABVER
    mov al, [x3c_rung]
    mov [si+XR_RUNG], al
    mov al, [x3_och]
    mov [si+XR_CH], al
    mov ax, [x3_orate]
    mov [si+XR_RATE], ax
    mov al, [x3_obits]
    mov [si+XR_BITS], al
    mov al, [x3c_dvol]
    mov [si+XR_VOL], al
    mov ax, [x3c_ddir]
    mov [si+XR_DIR], ax
    mov byte [si+XR_USED], 1
    push si
    lea di, [si+XR_NAME]
    mov si, x3c_wname
    mov cx, 12
    call x3_putn
    pop si
    push si
    lea di, [si+XR_STEM]
    mov si, x3_fname
    mov cx, 8
.st:
    lodsb
    or al, al
    jz .se
    cmp al, '.'
    je .se
    mov [di], al
    inc di
    loop .st
.se:
    pop si
    mov al, [x3c_dvol]              ; the hit, for the play that follows
    mov [x3c_hvol], al
    mov ax, [x3c_ddir]
    mov [x3c_hdir], ax
    push si
    mov si, x3c_wname
    mov di, x3c_hname
    mov cx, 13
    call x3_putn
    mov byte [di], 0
    pop si
    mov al, [x3c_rung]
    mov [x3c_wprof], al
    jmp x3c_ixsave

; -----------------------------------------------------------------------------
; x3c_ended - X3R_END, now with the lock (after x3c_final did the I/O): the
; finished file played - only now (SPEC.md 121.7) - or the failure shown
; -----------------------------------------------------------------------------
x3c_ended:
    cmp word [x3c_err], 0
    jne .fail
    cmp byte [x3c_eof], 0
    jne .out                        ; (x3c_final has not run: a write pends)
    call x3c_teardown               ; the stage, the progress window
    mov byte [x3_md], MD_IDLE
    xor ax, ax
    mov [x3_startms], ax
    mov [x3_startms+2], ax
    call x3_snddev
    jmp x3_file_play
.fail:
    call x3c_teardown
    mov ax, [x3c_err]
    mov [x3_st+X3S_ERR], ax
    mov byte [x3_md], MD_IDLE
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    mov byte [x3_c_batch], 0
.out:
    ret

; =============================================================================
; DECODED FILES (File > Decoded Files...): the index's files, listed; Delete
; removes one - only a file whose own xtm3 chunk names the record, so nothing
; XTMP3 did not make is ever deleted - and its record
; =============================================================================

; x3c_dfopen - the window, the first record selected
x3c_dfopen:
    call x3c_ixload
    mov byte [x3c_fsel], 0xFF
    xor al, al
    call x3c_fnext                  ; the first used record
    jc .o
    mov [x3c_fsel], al
.o:
    mov byte [x3c_ftop], 0
    mov al, XD_FILES
    jmp x3c_dopen

; x3c_fnext - AL = a record: the first used one at or after it. CF = 1 none
x3c_fnext:
.f:
    cmp al, X3C_NIX
    jae .no
    call x3c_ixrow
    cmp byte [si+XR_USED], 0
    jne .ok
    inc al
    jmp short .f
.ok:
    clc
    ret
.no:
    stc
    ret

; x3c_frowok - a record selected? CF = 1 none
x3c_frowok:
    cmp byte [x3c_fsel], 0xFF
    je .no
    clc
    ret
.no:
    stc
    ret

; x3c_pfiles - the list: each used record a row, '  NAME.WAV  C:  from STEM
; XT STANDARD 11025 Hz'
x3c_pfiles:
    mov si, x3c_s_fhead
    mov dx, 6
    xor al, al
    call x3c_line
    mov dx, 6 + 12
    xor al, al                      ; record
    xor cx, cx                      ; rows drawn
.r:
    cmp cl, XD_FROWS
    jae .out
    call x3c_fnext
    jc .blank
    push ax
    push cx
    push dx
    call x3c_frow                   ; x3c_lbuf2 = the row's text
    pop dx
    pop cx
    pop ax
    push ax
    mov ah, al
    xor al, al
    cmp ah, [x3c_fsel]
    jne .d
    inc al
.d:
    mov si, x3c_lbuf2
    call x3c_line
    pop ax
    inc al
    jmp short .n
.blank:
    push ax
    mov si, x3c_s_none
    xor al, al
    or cx, cx
    jnz .b2
    call x3c_line                   ; (none at all: the first row says so)
    jmp short .b3
.b2:
    mov si, x3c_s_empty
    call x3c_line
.b3:
    pop ax
.n:
    add dx, XD_RH
    inc cx
    jmp short .r
.out:
    ret

; x3c_frow - record AL's text into x3c_lbuf2
x3c_frow:
    call x3c_ixrow                  ; SI
    mov bx, si
    mov di, x3c_lbuf2
    mov word [di], '  '
    add di, 2
    lea si, [bx+XR_NAME]
    mov cx, 12
    call x3_putn
    mov al, ' '
    mov cx, 2
    add cx, cx
    call x3_fillb
    add di, cx
    mov al, [bx+XR_VOL]
    add al, 'A'
    mov ah, ':'
    mov [di], ax
    add di, 2
    mov si, x3c_s_from
    call x3_put
    lea si, [bx+XR_STEM]
    mov cx, 8
    call x3_putn
    mov byte [di], ' '
    inc di
    mov al, [bx+XR_RUNG]
    call x3f_qname
    call x3_put
    mov byte [di], 0
    ret

; x3c_fpick - row AL of the list -> the record there selected. CF = 1 none
x3c_fpick:
    mov cl, al
    xor al, al
.f:
    call x3c_fnext
    jc .no
    or cl, cl
    jz .ok
    dec cl
    inc al
    jmp short .f
.ok:
    mov [x3c_fsel], al
    clc
    ret
.no:
    stc
    ret

; x3c_fmove - AL = -1 / +1: the selection to the previous / next record
x3c_fmove:
    mov ah, [x3c_fsel]
    cmp ah, 0xFF
    je .out
    cmp al, 1
    je .dn
.up:
    or ah, ah
    jz .out
    dec ah
    mov al, ah
    call x3c_ixrow
    cmp byte [si+XR_USED], 0
    je .up
    mov [x3c_fsel], ah
    ret
.dn:
    mov al, ah
    inc al
    call x3c_fnext
    jc .out
    mov [x3c_fsel], al
.out:
    ret

; x3c_fdel - Delete: asked first (os88ui_ask, Cancel the default)
x3c_fdel:
    call x3c_frowok
    jc .out
    mov al, OS88UI_ADANGER
    mov bx, [x3_win]
    mov si, x3c_s_delq
    mov di, x3c_fdel2
    call os88ui_ask
.out:
    ret

; x3c_fdel2 - the alert's answer: AL = 1 Proceed - done by the main window's
; wake, the alert being unbound too
x3c_fdel2:
    cmp al, 1
    jne .out
    mov al, XU_DELETE
    jmp x3c_post
.out:
    ret

; x3c_fdelgo - the file checked to be the record's own (its xtm3 chunk),
; deleted, and the record forgotten
x3c_fdelgo:
    mov al, [x3c_fsel]
    cmp al, 0xFF
    je .out
    call x3c_ixrow
    push si
    call x3c_cacheok                ; standing there, and it is ours?
    pop si
    jc .forget                      ; gone or not ours: the record only
    push si
    mov si, x3c_hname
    call OSAPI_FILE_DELETE
    pop si
.forget:
    mov byte [si+XR_USED], 0
    call x3c_ixsave
    mov al, [x3c_fsel]
    call x3c_fnext
    jnc .s
    mov al, 0xFF
.s:
    mov [x3c_fsel], al
    mov bx, [x3c_dwin]
    or bx, bx
    jz .out
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, bx
    call x3c_dpaint
.out:
    ret

; -----------------------------------------------------------------------------
; x3c_post / x3c_uiop - A DIALOG'S ACTION, CARRIED TO THE MAIN WINDOW. The
; dialog, the progress window and the alert are UNBOUND windows (SPEC.md
; 20.2's second window): their callbacks are billed to no instance, so
; OSAPI_FILE_GOTO_QM there moves the MACHINE and marks nobody, and the Save
; dialog then opens wherever the instance last stood. So the action is posted
; (AL = XU_*) and the main window's wake - ours - does it with the lock taken
; -----------------------------------------------------------------------------
XU_SAVE     equ 1                   ; Decode and Play: the Save dialog
XU_CANCEL   equ 2                   ; the conversion's Cancel
XU_DELETE   equ 3                   ; Decoded Files' Delete, confirmed

x3c_post:
    mov [x3_uiop], al
    mov al, X3R_UIOP
    jmp x3_kick

x3c_uiop:
    mov al, [x3_uiop]
    mov byte [x3_uiop], 0
    cmp al, XU_SAVE
    jne .c
    jmp x3c_savedlg
.c:
    cmp al, XU_CANCEL
    jne .d
    jmp x3c_cancel
.d:
    cmp al, XU_DELETE
    jne .out
    jmp x3c_fdelgo
.out:
    ret

; =============================================================================
; THE FACE'S PREDICATES (x3_bok's rows: SPEC.md 47 rule 2's one predicate)
; =============================================================================

; x3c_okdec - Decode First: a track, and nothing under way. CF = 1 refused
x3c_okdec:
    cmp byte [x3_qn], 0
    je .no
    cmp byte [x3_md], MD_IDLE
    jne .no
    clc
    ret
.no:
    stc
    ret

; x3c_okfiles - Decoded Files: not while a dialog or a conversion owns it
x3c_okfiles:
    cmp byte [x3_md], MD_CHOOSE
    je .no
    cmp byte [x3_md], MD_CONV
    je .no
    clc
    ret
.no:
    stc
    ret

; x3c_actdec - D: the selection, decoded first (a cache plays instead)
x3c_actdec:
    mov al, [x3_qsel]
    cmp al, 0xFF
    jne .s
    xor al, al
.s:
    push ax
    call x3_stopall
    pop ax
    call x3_cue1
    xor ax, ax
    mov [x3_startms], ax
    mov [x3_startms+2], ax
    mov byte [x3_wantdf], 1
    call x3_go
    clc
    ret

; x3c_actfiles - File > Decoded Files...
x3c_actfiles:
    call x3c_dfopen
    clc
    ret

; =============================================================================
; DATA
; =============================================================================
x3c_dtpl:   dw 0, 0, XD_W + 2, 0
            dw x3c_t_drive, x3c_dpaint, x3c_dkey, 0
x3c_t_drive: db 'Decode First', 0
x3c_t_prog:  db 'Decoding to Disk', 0
x3c_t_files: db 'Decoded Files', 0

    OS88UI_BTNREC x3c_btrec, x3c_brect, x3c_blbl, x3c_bflg, 3

x3c_s_need1: db 'This computer needs to decode this track before playing.', 0
x3c_s_need2: db 'Choose a hard drive for the decoded audio.', 0
x3c_s_nohd:  db '  This machine has no hard drive to decode to.', 0
x3c_s_hd:    db '  Hard disk   ', 0
x3c_s_kbfree: db ' KB free', 0
x3c_s_ro:    db '  read-only', 0
x3c_s_gone:  db '  cannot be read', 0
x3c_s_clu:   db '  clusters too large', 0
x3c_s_small: db '  too little room', 0
x3c_s_pcm:   db '   PCM: ', 0
x3c_s_hz:    db ' Hz ', 0
x3c_s_mono:  db 'mono ', 0
x3c_s_stereo: db 'stereo ', 0
x3c_s_8:     db '8-bit', 0
x3c_s_16:    db '16-bit', 0
x3c_s_needs: db 'Needs ', 0
x3c_s_kb:    db ' KB', 0
x3c_s_about: db '   About ', 0
x3c_s_mindec: db ' to decode', 0
x3c_s_cancel: db 'Cancel', 0
x3c_s_decode: db 'Decode and Play', 0
x3c_s_close: db 'Close', 0
x3c_s_delete: db 'Delete', 0
x3c_s_qno:   db 'Queue too: No', 0
x3c_s_qyes:  db 'Queue too: Yes', 0
x3c_s_dwav:  db '.WAV', 0
x3c_s_dtmp:  db '.TMP', 0
x3c_s_nothd: db 'Choose a hard drive folder', 0
x3c_s_taken: db 'That name is taken', 0
x3c_s_full:  db 'Not enough room there', 0
x3c_s_badnm: db 'Give it a name', 0
x3c_s_to:    db 'To:      ', 0
x3c_s_audio: db 'Audio:   ', 0
x3c_s_of:    db 'of ', 0
x3c_s_unk:   db '--:-- ', 0
x3c_s_elap:  db 'Elapsed: ', 0
x3c_s_speed: db '  Speed: ', 0
x3c_s_fhead: db 'Files XTMP3 decoded (it deletes only these):', 0
x3c_s_none:  db '  (none)', 0
x3c_s_empty: db '', 0
x3c_s_from:  db '  from ', 0
x3c_s_delq:  db 'Delete this decoded file?', 0
x3c_s_ixf:   db 'XTMP3.DAT', 0
x3c_s_clu2:  db 'The drive is not usable', 0
x3c_s_cantw: db 'Cannot write the file', 0
x3c_s_full2: db 'The disk is full', 0
x3c_s_gone2: db 'The drive went away', 0
x3c_s_rdbad: db 'Cannot read the MP3 file', 0
x3c_s_chg:   db 'The MP3 changed meanwhile', 0
x3c_s_verify: db 'The decoded file is wrong', 0
x3c_s_ren:   db 'Cannot name the file', 0

%define DEC_U32
%define DEC_SEP
%include "os88dec.inc"
%include "os88data.inc"

; =============================================================================
; BSS (xtmp3.asm's X3B list, through X3_ENGBSS)
; =============================================================================
%macro X3_CONVBSS 0
    X3B x3_c_batch, 1               ; an approved batch: its folder...
    X3B x3c_bvol, 1
    X3B x3c_bdir, 2
    X3B x3c_bqueue, 1               ; ...the dialog's switch for it
    X3B x3c_strict, 1               ; x3c_cachefind: the device's own format
    X3B x3c_lset, 1                 ; the last destination, preselected
    X3B x3c_lvol, 1
    X3B x3c_ldir, 2
    X3B x3c_rung, 1                 ; the job: its rung...
    X3B x3c_need, 4                 ; ...KB it needs, '~'
    X3B x3c_nest, 1
    X3B x3c_est, 2                  ; ...its measured speed
    X3B x3c_dvol, 1                 ; ...where: the volume, the folder...
    X3B x3c_ddir, 2
    X3B x3c_wname, 14               ; ...the final name, the temporary one
    X3B x3c_tname, 14
    X3B x3c_tmade, 1
    X3B x3c_clu, 2                  ; ...its cluster, a stage half, the stage
    X3B x3c_half, 2
    X3B x3c_sseg, 2
    X3B x3c_fh, 1                   ; the half the worker fills, its bytes...
    X3B x3c_fpos, 2
    X3B x3c_wh, 1                   ; ...the half the writer writes next...
    X3B x3c_rdy, 1                  ; ...the halves staged, a bit each
    X3B x3c_lastn, 2                ; the last half's bytes
    X3B x3c_one, 1                  ; 1 = ONE half (x3_plan: a large cluster)
    X3B x3c_pend, 2                 ; ...a granule's rest waiting in the PCM,
    X3B x3c_psrc, 2                 ; and where
    X3B x3c_planc, 2                ; the destination's cluster while x3_plan
                                    ; sizes a stage (0: none, not converting)
    X3B x3c_eof, 1                  ; the stream ended
    X3B x3c_wn, 1                   ; granules left this pass
    X3B x3c_canc, 1
    X3B x3_uiop, 1                  ; a dialog's action, posted (XU_*)
    X3B x3c_err, 2                  ; the failure's sentence, 0 none
    X3B x3c_tok, 2                  ; the WRITE_SEQ token
    X3B x3c_dlen, 4                 ; data bytes written
    X3B x3c_frms, 4                 ; output frames decoded
    X3B x3c_t0, 2                   ; ticks at the start, at the last draw
    X3B x3c_drawt, 2
    X3B x3c_fp2, X3_FPSZ            ; the source's print, taken again
    X3B x3c_cro, 4                  ; x3c_crcat: the offset, the CRC so far,
    X3B x3c_crs, 4                  ; the bytes still to read
    X3B x3c_crn, 2
    X3B x3c_savepend, 1             ; the Save dialog is up: its slot...
    X3B x3c_dslot, 2
    X3B x3c_wmap0, 2
    X3B x3c_home, 2                 ; where the instance stood (the scan)
    X3B x3c_homev, 1
    X3B x3c_nv, 1                   ; THE DRIVE LIST: rows, selection...
    X3B x3c_vsel, 1
    X3B x3c_vv, XD_MAXV             ; ...each row's volume, flags, free KB
    X3B x3c_vf, XD_MAXV
    X3B x3c_vkb, XD_MAXV * 4
    X3B x3c_dwin, 2                 ; THE DIALOG'S WINDOW: it, its mode...
    X3B x3c_dmode, 1
    X3B x3c_dox, 2                  ; ...its content's origin...
    X3B x3c_doy, 2
    X3B x3c_by1, 2                  ; ...the button row
    X3B x3c_by2, 2
    X3B x3c_brect, 3 * 8            ; ...the buttons
    X3B x3c_blbl, 3 * 2
    X3B x3c_bflg, 3 * 2
    X3B x3c_lbuf, XD_W / 8          ; a line, padded; a line, composed
    X3B x3c_lbuf2, 80
    X3B x3c_fsel, 1                 ; Decoded Files' selection, its top
    X3B x3c_ftop, 1
    X3B x3c_hvol, 1                 ; THE HIT: a cache's file
    X3B x3c_hdir, 2
    X3B x3c_hname, 14
    X3B x3c_wprof, 1                ; ...its rung
    X3B x3c_wsize, 4                ; x3c_wavchk's: the file's bytes, the
    X3B x3c_wdoff, 4                ; data's offset and length, the identity
    X3B x3c_wdlen, 4
    X3B x3c_widn, X3C_IDN
    X3B x3c_ixbd, 2                 ; (where we stood around an index visit)
    X3B x3c_ixbv, 1
    X3B x3c_ixok, 1                 ; THE INDEX: loaded, its state byte, it
    X3B x3c_ixst, 1
    X3B x3c_ix, X3C_IXSZ
%endmacro
