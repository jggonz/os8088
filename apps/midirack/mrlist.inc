; =============================================================================
; os8088 - apps/midirack/mrlist.inc
;
; THE PLAYLIST AND THE LOADS (SPEC.md 105.3). Prefix mrl_. The Audio
; Player's store (SPEC.md 86.10): an entry is a NAME, a FOLDER and a VOLUME,
; never a path - a song is reached by OSAPI_FILE_GOTO_QM and then by name.
;
;   [0..12]  the 8.3 name, NUL-terminated
;   [13]     the volume
;   [14..15] the folder's first cluster (0 = the root)
;
; A SONG IS READ WHOLE (mrl_load): a Standard MIDI File is small - the ten
; shipped ones are 1-12 KB - and a sequencer that streamed would pay a disk
; read under the gfx lock in the middle of a phrase. So the file goes into
; one claim of its own size, up to MR_FILEMAX KB, and is then walked twice:
; a DRY RUN with no output (mrl_dry) that measures it - its length, its
; measures, the program each channel starts on, its notes, its first text -
; and then the play.
; =============================================================================

MRL_MAX     equ 64                  ; entries
MRL_ENT     equ 16

    MRBUF mrl_ent, MRL_MAX * MRL_ENT
    MRB mrl_n                       ; entries
    MRB mrl_sel                     ; the highlighted one (and what Play plays)
    MRB mrl_top                     ; the first row on show
    MRBUF mrl_find, OSAPI_FIND_SZ   ; OSAPI_FILE_FIND's record
    MRBUF mrl_path, 40              ; "B:\MIDI\" for the pane's title
    MRW mrl_tdir
    MRB mrl_tvol

mrl_init:
    call mru_list_owe               ; the rows show other entries now
    mov byte [mrl_n], 0
    mov byte [mrl_sel], 0
    mov byte [mrl_top], 0
    mov byte [mr_loaded], 0
    mov byte [mrl_path], 0
    ret

; mrl_ptr - AL = an index -> BX = its entry. Preserves the rest
mrl_ptr:
    push cx
    xor ah, ah
    mov bx, ax
    mov cl, 4
    shl bx, cl
    add bx, mrl_ent
    pop cx
    ret

; -----------------------------------------------------------------------------
; mrl_add - SI = a NUL name (ours), DX = its folder, BL = its volume.
; out: CF = 0 and AL = its index (an entry already there is not added twice);
;      CF = 1 the list is full
; -----------------------------------------------------------------------------
mrl_add:
    call mru_list_owe
    push bx
    push cx
    push si
    push di
    mov [mrl_tdir], dx
    mov [mrl_tvol], bl
    xor cx, cx                      ; already there?
.f:
    cmp cl, [mrl_n]
    jae .new
    mov al, cl
    call mrl_ptr
    mov ax, [bx+14]
    cmp ax, [mrl_tdir]
    jne .fn
    mov al, [bx+13]
    cmp al, [mrl_tvol]
    jne .fn
    mov di, bx
    call mr_nameeq
    je .dup
.fn:
    inc cx
    jmp short .f
.dup:
    mov al, cl
    jmp short .ok
.new:
    mov al, [mrl_n]
    cmp al, MRL_MAX
    jae .full
    call mrl_ptr
    mov di, bx
    mov cx, 12
.cp:
    mov al, [si]
    mov [di], al
    or al, al
    jz .cd
    inc si
    inc di
    loop .cp
    mov byte [di], 0
.cd:
    mov al, [mrl_tvol]
    mov [bx+13], al
    mov ax, [mrl_tdir]
    mov [bx+14], ax
    mov al, [mrl_n]
    inc byte [mrl_n]
.ok:
    pop di
    pop si
    pop cx
    pop bx
    clc
    ret
.full:
    pop di
    pop si
    pop cx
    pop bx
    stc
    ret

; mrl_remove - the highlighted entry out of the list (the song playing from
; it is stopped by the caller first)
mrl_remove:
    call mru_list_owe
    push ax
    push bx
    push cx
    push si
    push di
    mov al, [mrl_sel]
    cmp al, [mrl_n]
    jae .out
    call mrl_ptr
    mov di, bx
    lea si, [bx+MRL_ENT]
    mov al, [mrl_n]
    sub al, [mrl_sel]
    dec al
    xor ah, ah
    mov cl, 4
    shl ax, cl
    mov cx, ax
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    dec byte [mrl_n]
    mov al, [mrl_sel]
    cmp al, [mrl_n]
    jb .s
    or al, al
    jz .s
    dec byte [mrl_sel]
.s:
    mov byte [mr_loaded], 0         ; indices moved: nothing is loaded
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mr_nameeq - DS:SI and DS:DI, NUL strings, case-insensitive: ZF = 1 equal
; mr_ismid  - SI = a NUL name: ZF = 1 it ends .MID or .SMF
; -----------------------------------------------------------------------------
mr_nameeq:
    push ax
    push si
    push di
.l:
    mov al, [si]
    call mr_upc
    mov ah, al
    mov al, [di]
    call mr_upc
    cmp al, ah
    jne .done
    or al, al
    jz .done
    inc si
    inc di
    jmp short .l
.done:
    pop di
    pop si
    pop ax
    ret

mr_upc:
    cmp al, 'a'
    jb .r
    cmp al, 'z'
    ja .r
    sub al, 0x20
.r:
    ret

mr_ismid:
    push ax
    push si
.e:
    cmp byte [si], 0
    je .end
    inc si
    jmp short .e
.end:
    sub si, 4
    cmp byte [si], '.'
    jne .no
    mov al, [si+1]
    call mr_upc
    mov ah, al
    mov al, [si+2]
    call mr_upc
    cmp ax, 'IM'                    ; 'M','I' read as a word
    jne .smf
    mov al, [si+3]
    call mr_upc
    cmp al, 'D'
    jmp short .out
.smf:
    cmp ax, 'MS'
    jne .no
    mov al, [si+3]
    call mr_upc
    cmp al, 'F'
    jmp short .out
.no:
    or si, si                       ; ZF = 0 (SI is never 0 here)
    jnz .out
    or al, 1
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrl_adddir - every .MID in the folder this instance is standing in, onto
; the list (one already on it is not added twice). out: AX = how many .MIDs
; the folder holds
; -----------------------------------------------------------------------------
mrl_adddir:
    push bx
    push cx
    push dx
    push si
    push di
    xor ax, ax
    mov [mrl_cnt], ax
    call mrl_here                   ; DX = the folder, BL = the volume
    mov [mrl_hdir], dx
    mov [mrl_hvol], bl
    xor cx, cx
.f:
    call mrl_ffind                  ; CX = the next ordinal
    jc .done
    cmp word [mrl_find+14], OSAPI_FT_DIR
    jae .f                          ; a folder, or '..'
    mov si, mrl_find
    call mr_ismid
    jne .f
    mov dx, [mrl_hdir]
    mov bl, [mrl_hvol]
    call mrl_add                    ; (one already there counts: the folder
    jc .done                        ; HAS it, which is the question)
    inc word [mrl_cnt]
    jmp short .f
.done:
    call mrl_takepath
    mov ax, [mrl_cnt]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

    MRW mrl_cnt
    MRW mrl_hdir
    MRB mrl_hvol

; mrl_here - DX = this instance's folder, BL = its volume
mrl_here:
    call OSAPI_FILE_HERE
    ret

; mrl_takepath - "B:\MIDI\" into mrl_path, for the pane's title
mrl_takepath:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call OSAPI_FILE_HERE            ; BL = the volume
    mov al, bl
    add al, 'A'
    mov [mrl_path], al
    mov byte [mrl_path+1], ':'
    push ds
    pop es
    mov di, mrl_path + 2
    mov cx, 30
    call OSAPI_FILE_PATH            ; "\MIDI" (the root: "\")
    jnc .got
    mov word [mrl_path+2], '\'      ; (no walker: say the root's shape)
    mov cx, 1
.got:
    mov di, mrl_path + 2
    add di, cx
    cmp byte [di-1], '\'
    je .t
    mov byte [di], '\'
    inc di
.t:
    mov byte [di], 0
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrl_enter - into the folder SI names, in the folder we stand in.
; out: CF = 1 there is no such folder (nothing moved)
; -----------------------------------------------------------------------------
mrl_enter:
    push ax
    push bx
    push cx
    push dx
    push di
    call OSAPI_FILE_HERE
    mov [mrl_hvol], bl
    xor cx, cx
.f:
    call mrl_ffind
    jc .no
    cmp word [mrl_find+14], OSAPI_FT_DIR
    jne .f
    mov di, mrl_find
    call mr_nameeq
    jne .f
    mov dx, [mrl_find+16]
    mov bl, [mrl_hvol]
    call OSAPI_FILE_GOTO_QM
    jc .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mrl_root - to the root of the volume we stand on
mrl_root:
    push bx
    push dx
    call OSAPI_FILE_HERE
    xor dx, dx
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mrl_autoload - launched with nothing to open: the songs that ship, from the
; first place that has any - MIDI\ beside us, then \MEDIA\MIDI, then \MIDI,
; then the folder we stand in (SPEC.md 105.3.1). out: AX = songs added
; -----------------------------------------------------------------------------
mrl_autoload:
    push bx
    push dx
    push si
    call OSAPI_FILE_HERE
    mov [mrl_adir], dx
    mov [mrl_avol], bl
    mov si, mrl_s_midi
    call mrl_enter
    jc .media
    call mrl_adddir
    or ax, ax
    jnz .out
.media:
    call mrl_home
    call mrl_root
    mov si, mrl_s_media
    call mrl_enter
    jc .rmidi
    mov si, mrl_s_midi
    call mrl_enter
    jc .rmidi
    call mrl_adddir
    or ax, ax
    jnz .out
.rmidi:
    call mrl_root
    mov si, mrl_s_midi
    call mrl_enter
    jc .here
    call mrl_adddir
    or ax, ax
    jnz .out
.here:
    call mrl_home
    call mrl_adddir
.out:
    pop si
    pop dx
    pop bx
    ret

; mrl_home - back where autoload started
mrl_home:
    push bx
    push dx
    mov dx, [mrl_adir]
    mov bl, [mrl_avol]
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop bx
    ret

    MRW mrl_adir
    MRB mrl_avol
mrl_s_midi:  db 'MIDI', 0
mrl_s_media: db 'MEDIA', 0

; -----------------------------------------------------------------------------
; mrl_load - entry AL read into the song's claim, checked and measured.
; UI task (file slots). out: CF = 1 refused, [mr_msg] says why
; -----------------------------------------------------------------------------
mrl_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [mr_loaded], 0
    cmp al, [mrl_n]
    jae .bad
    mov [mr_lidx], al
    call mrl_ptr                    ; BX = the entry
    mov si, bx
    push bx
    mov dx, [bx+14]
    mov bl, [bx+13]
    call OSAPI_FILE_GOTO_QM         ; into its folder
    pop bx
    jc .rd
    call mrl_size                   ; DX:AX = its size
    jc .rd
    or dx, dx
    jnz .big
    cmp ax, MR_FILEMAX * 1024
    ja .big
    mov [mr_fsize], ax
    add ax, 1023                    ; the claim, in KB
    mov cl, 10
    shr ax, cl
    or ax, ax
    jnz .kb
    inc ax
.kb:
    cmp ax, [mr_fkb]
    jbe .claimed                    ; the last song's claim is big enough
    push ax
    mov dx, [mr_fseg]
    or dx, dx
    jz .free
    call OSAPI_MEM_FREE
    mov word [mr_fseg], 0
    mov word [mr_fkb], 0
.free:
    pop ax
    push ax
    call OSAPI_MEM_CLAIM            ; DX = the segment
    pop ax
    jc .nomem
    mov [mr_fseg], dx
    mov [mr_fkb], ax
.claimed:
    mov es, [mr_fseg]               ; ES:BX = the claim, DX:CX = its room
    xor bx, bx
    mov cx, [mr_fkb]
    xor dx, dx
    push cx
    mov cl, 10
    pop ax
    push ax
    shl ax, cl                      ; KB x 1024 (63 KB at most: a word)
    mov cx, ax
    pop ax
    call OSAPI_FILE_READ            ; SI = the name
    jc .rd
    or dx, dx
    jnz .rd
    mov [mr_fsize], ax
    call mrq_load                   ; CF: AX = the reason
    jnc .parsed
    mov [mr_msg], ax
    jmp short .bad
.parsed:
    call mrl_dry
    mov byte [mr_loaded], 1
    mov word [mr_msg], mr_s_ready
    clc
    jmp short .out
.big:
    mov word [mr_msg], mr_s_big
    jmp short .bad
.nomem:
    mov word [mr_msg], mr_s_nomem
    jmp short .bad
.rd:
    mov word [mr_msg], mr_s_rderr
.bad:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mrl_size - SI = a name in the folder we stand in: DX:AX = its size.
; CF = 1 there is none
mrl_size:
    push bx
    push cx
    push di
    xor cx, cx
.f:
    call mrl_ffind
    jc .no
    cmp word [mrl_find+14], OSAPI_FT_DIR
    jae .f
    mov di, mrl_find
    call mr_nameeq
    jne .f
    mov ax, [mrl_find+18]
    mov dx, [mrl_find+20]
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop cx
    pop bx
    ret

; mrl_ffind - OSAPI_FILE_FIND of ordinal CX into mrl_find, out as that cell.
; FIND fills ES:DI, so ES is OURS for the call and given back after - the
; three walks above each did that inline, with DI, around one far call
mrl_ffind:
    push es
    push ds
    pop es
    mov di, mrl_find
    call OSAPI_FILE_FIND
    pop es
    ret

; -----------------------------------------------------------------------------
; mrl_dry - the song walked end to end with NO output (SPEC.md 105.5.5):
; its length in ms, its measures, the first program of every channel, its
; notes and its first text. Then rewound, and its tick-0 events played dry
; too, so the window shows the tempo and the instruments it starts with.
; -----------------------------------------------------------------------------
mrl_dry:
    push ax
    push bx
    push dx
    mov byte [mr_dry], 1
    mov word [mrq_vt], mr_nullvt
    mov word [mrq_urate], 1000      ; units: milliseconds
    mov byte [mr_text], 0
    call mr_first_reset
    call mrq_rewind
    call mr_chan_reset
.s:
    call mrq_step
    jnc .s
    mov ax, [mrq_u1]
    mov dx, [mrq_u2]
    mov [mr_lenlo], ax
    mov [mr_lenhi], dx
    call mrq_meas
    inc ax
    mov [mr_meas], ax
    call mrl_dryprime               ; ...and back, the tick-0 events dry
    pop dx
    pop bx
    pop ax
    ret

; mrl_dryprime - the loaded song rewound and its tick-0 events played with no
; output: the tempo and the instruments it starts with, on show
mrl_dryprime:
    push ax
    push bx
    push dx
    mov byte [mr_dry], 1
    mov word [mrq_vt], mr_nullvt
    mov word [mrq_urate], 1000
    call mrq_rewind
    call mr_chan_reset
.z:
    call mrq_next                   ; DX:AX = the next tick
    jc .done
    or ax, dx
    jnz .done
    call mrq_step
    jmp short .z
.done:
    pop dx
    pop bx
    pop ax
    ret
