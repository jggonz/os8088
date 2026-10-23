; =============================================================================
; os8088 - apps/audio/aplook.inc - THE LOOK-AHEAD RING (SPEC.md 86.5, 86.5.2)
;
; A byte range of a file streamed into a power-of-two ring in a heap claim:
; the UI task reads a gulp at a time STRAIGHT INTO THE RING through os88_rseq
; (one FAT link a gulp), the worker or the bracket takes bytes out of it. The
; Audio Player's, factored out of apengine.inc so a second package can include
; the same source rather than carry a copy - XTMP3 (SPEC.md 121) streams its
; MP3 and its finished WAV through it. The Audio Player assembles the very
; bytes it did before the split: apengine.inc includes this file twice, once
; for each half, where the halves always stood.
;
;   APLOOK_GET     ap_la_avail, ap_src_eof, ap_la_get (any task)
;   APLOOK_REFILL  ap_refill_chunk (the UI task only, SPEC.md 20.6 rule 7)
;
; Define one or both before each %include. WHAT THE INCLUDER PROVIDES - the
; names are the Audio Player's, and a second includer defines them for itself:
;   constants  AP_LA_SZ, AP_LA_MASK (the ring, a power of two and a whole
;              number of clusters), AP_RD_CHUNK (the most a gulp reads)
;   state      ap_la_seg, ap_la_wr, ap_la_rd (free-running words), ap_lg_got,
;              ap_src_done, ap_skip, ap_clu, ap_rd_len, ap_rd_pos (dword),
;              ap_rd_cur (FSEQ_SIZE), ap_got, ap_data_off, ap_data_end
;              (dwords), ap_trk_dir, ap_trk_vol, apw_name (the file's NUL
;              name), ap_msg
;   code       ap_ret6 (pop DI SI DX CX BX AX, ret), os88_rseq (os88rseq.inc),
;              apw_e_io (the I/O error's sentence), APCNT and APADD (counter
;              macros, empty where nothing is counted)
; =============================================================================

%ifdef APLOOK_GET
; =============================================================================
; ap_la_avail -> AX = bytes queued in the look-ahead ring
; preserves every register except AX.
; =============================================================================
ap_la_avail:
    mov ax, [ap_la_wr]
    sub ax, [ap_la_rd]
    ret

; =============================================================================
; ap_src_eof -> CF = 1 when no more audio will ever arrive
; preserves every register.
; =============================================================================
ap_src_eof:
    push ax
    cmp byte [ap_src_done], 0
    je .no
    mov ax, [ap_la_wr]
    cmp ax, [ap_la_rd]
    jne .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; =============================================================================
; ap_la_get - copy up to CX bytes out of the look-ahead ring into DS:DI
; in:  CX = max bytes, DI = destination offset (in this segment)
; out: AX = bytes actually copied; ap_la_rd advanced by AX
; clobbers: nothing else (all pushed).  ANY-TASK.
; =============================================================================
ap_la_get:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    mov ax, [ap_la_wr]
    sub ax, [ap_la_rd]
    cmp ax, cx
    jbe .g1
    mov ax, cx
.g1:
    mov [ap_lg_got], ax
    or ax, ax
    jz .ret
    mov si, [ap_la_rd]
    and si, AP_LA_MASK
    mov bx, [ap_la_seg]
    mov dx, AP_LA_SZ
    sub dx, si                     ; DX = contiguous run to the ring seam
    mov cx, [ap_lg_got]
    push ds
    pop es                         ; ES = this segment (the destination)
    mov ds, bx                     ; DS = ring segment
    cld
    cmp cx, dx
    jbe .single
    push cx
    mov cx, dx
    rep movsb
    pop cx
    sub cx, dx
    xor si, si
    rep movsb
    jmp .fin
.single:
    rep movsb
.fin:
    pop ds                         ; restore this segment
    pop es
    mov ax, [ap_lg_got]
    add [ap_la_rd], ax
    jmp .out
.ret:
    pop ds
    pop es
.out:
    mov ax, [ap_lg_got]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

%endif

%ifndef AP_RSEQ                    ; the gulp's read: os88_rseq, or an
%define AP_RSEQ os88_rseq           ; includer's with its registers (XTMP3's
%endif                              ; x3_rseq copies out of its hold)
%ifdef APLOOK_REFILL
; =============================================================================
; ap_refill_chunk - read ONE gulp of the data chunk STRAIGHT INTO THE RING,
;                   the first gulp's head skipped and the last one's tail
;                   clamped at ap_data_end.  UI task only.
;
; The disk lands at DISK = ap_la_wr - ap_skip, the ring index of the next file
; byte: a head skip still owed (the first gulp of a track, whose data does not
; start on a cluster) is bytes the disk writes and nobody publishes, so the
; track starts with ap_la_wr = ap_la_rd = ap_skip (ap_prep_track) and DISK at
; 0. DISK only ever moves by whole clusters until the gulp that ends the data,
; and the ring is a whole number of clusters, so a gulp is floored to the seam
; as well and never splits (in practice every gulp is AP_RD_CHUNK: a refill is
; asked for with more than that free, SPEC.md 86.5.2). The read writes only
; free bytes, and ap_la_wr moves after it - the worker sees nothing early.
; out: nothing (ap_src_done latched when the whole chunk has been read)
; =============================================================================
ap_refill_chunk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [ap_src_done], 0
    jne .out
    ; --- how big a gulp? min(AP_RD_CHUNK, free, run to the seam), floored to
    ; a cluster. A read holds sch_lock and costs ~1-2 disk revolutions
    ; whatever it moves (SPEC.md 34.5 / 86.5.2), so we want the FEWEST,
    ; biggest reads the ring has room for - not one cluster at a time.
    mov di, [ap_la_wr]
    sub di, [ap_skip]              ; DI = DISK, free-running
    mov bx, AP_LA_SZ
    add bx, [ap_la_rd]
    sub bx, di                     ; BX = free bytes from DISK
    and di, AP_LA_MASK             ; DI = the read's offset in the ring
    mov ax, AP_LA_SZ
    sub ax, di                     ; AX = the run to the seam
    cmp bx, ax
    jbe .seam
    mov bx, ax
.seam:
    cmp bx, AP_RD_CHUNK
    jbe .cap
    mov bx, AP_RD_CHUNK
.cap:
    mov ax, [ap_clu]
    dec ax
    not ax
    and bx, ax                     ; floor to a whole number of clusters
    cmp bx, [ap_clu]
    jb .out                        ; not room for even one cluster - later
    mov [ap_rd_len], bx

    ; still standing in the track's folder? The Open dialog moves the
    ; instance to whatever folder the user picked (SPEC.md 38.10), and the
    ; name below resolves THERE - not found reads as EOF, a same-named file
    ; as audio. GOTO back first; only a user who browsed away pays it.
    call OSAPI_FILE_HERE
    cmp dx, [ap_trk_dir]
    jne .goback
    cmp bl, [ap_trk_vol]
    je .inplace
.goback:
    mov dx, [ap_trk_dir]
    mov bl, [ap_trk_vol]
    call OSAPI_FILE_GOTO_QM        ; the quiet stand that MOVES us: a word
    jc .ioerr                      ; compare inside one volume, a quiet mount
                                   ; across; CF: the folder is gone - the track
                                   ; ends, and says why
.inplace:
    ; read [ap_rd_pos, +ap_rd_len) into the ring at DI
    mov es, [ap_la_seg]
    mov bx, di
    mov si, [apw_name]
    mov cx, [ap_rd_len]
    mov ax, [ap_rd_pos]
    mov dx, [ap_rd_pos+2]
    mov di, ap_rd_cur              ; one FAT link a gulp, not a walk from the
    call AP_RSEQ                   ; front (SPEC.md 18.4.8.1); out CF, DX:AX
    APCNT apc_readat
    APCNT apc_refill
    jc .ioerr                     ; an I/O error ends the data - and says why
    APADD apc_rbytes, ax
    ; DX:AX = got; it fits a word (AP_RD_CHUNK <= 16384)
    mov cx, ax                     ; CX = got (low word; DX must be 0)
    or dx, dx
    jz .g0
    mov cx, [ap_rd_len]
.g0:
    jcxz .done_all                ; nothing delivered -> at/after EOF

    ; usable = got - head-skip, then clamp so we never pass ap_data_end
    mov ax, [ap_skip]
    sub cx, ax
    jae .noskip
    neg cx                        ; the whole gulp was pre-data: skip -= got,
    mov [ap_skip], cx             ; so DISK moves by got and nothing is
    jmp short .advance            ; published
.noskip:
    mov word [ap_skip], 0
    ; bytes of real audio still wanted = ap_data_end - (ap_data_off + ap_got)
    mov ax, [ap_data_off]
    mov dx, [ap_data_off+2]
    add ax, [ap_got]
    adc dx, [ap_got+2]            ; DX:AX = file offset of the next audio byte
    mov bx, [ap_data_end]
    mov di, [ap_data_end+2]
    sub bx, ax
    sbb di, dx                     ; DI:BX = bytes of data left
    or di, di
    jnz .putall                   ; more than 64 KB left - CX is fine as is
    cmp bx, cx
    jae .putall
    mov cx, bx                     ; clamp to the tail
.putall:
    add [ap_la_wr], cx            ; PUBLISH, last: the bytes are in already
    add [ap_got], cx              ; ap_got += CX
    adc word [ap_got+2], 0
.advance:
    ; ap_rd_pos += ap_rd_len
    mov ax, [ap_rd_len]
    xor dx, dx
    add [ap_rd_pos], ax
    adc [ap_rd_pos+2], dx
    ; done if the next read position is at/after the data end - which is
    ; the data's END and not an error: this used to fall into .ioerr below,
    ; so every track's last gulp put "Cannot read the file" on the status
    ; line for the seconds the ring took to play out
    mov ax, [ap_rd_pos]
    mov dx, [ap_rd_pos+2]
    sub ax, [ap_data_end]
    sbb dx, [ap_data_end+2]
    jb .out
    jmp short .done_all
.ioerr:
    mov word [ap_msg], apw_e_io    ; a bad sector or a pulled disk: the status
                                   ; line says so, not "End of playlist"
.done_all:
    mov byte [ap_src_done], 1
.out:
    pop es
    jmp ap_ret6

%endif
