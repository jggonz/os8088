; =============================================================================
; os8088 - HTTPFS.DRV (SPEC.md 122)
;
; A read-only drive whose files are fetched over HTTP. The fourth file
; redirector (DRVC_HTTP = 10) beside the cable, the RAM disk and the CD-ROM:
; the kernel asks it FSV_* questions (SPEC.md 62.9) and it answers them from
; an index the server carries, OS88.IDX, and one GET per file.
;
;   hfpkg.inc   the package door's ABI - an installer mounts an address itself
;   hfnet.inc   the address, and one HTTP GET over the socket verbs
;   hfidx.inc   the index, and the volume's verbs
;   hfpage.inc  HTTPDRV.CFG, and the thunks to the page, HTTPPAGE.DRV
;               (httppage.asm) - read in by the first paint, freed at the
;               panel's close
;
; IT OWNS NO HARDWARE AND OPENS NO SOCKET UNTIL ASKED. The sockets are
; ETHER.DRV's - or the parallel cable's, NET.DRV answering the same verbs
; (SPEC.md 62.11) - reached through OSAPI_DRV_CALL exactly as a package
; reaches them; so attach probes nothing and a mounted drive costs the wire
; nothing until it is opened. NO ICON HARVEST: DSV_CAPS is 0, so a mount takes
; SPEC.md 62.9.2.1's cache-only pass, and a listing is answered out of the
; index with no request at all.
;
; ES IS KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.1) - except the
; package door, where it is the CALLING PACKAGE's (SPEC.md 20.11).
; =============================================================================

%include "os88drv.inc"
%include "netpkg.inc"           ; the socket verbs, as a CLIENT: NET_CLASS is
                                ; os88sock.inc's [net_cls], not a constant
%include "hfpkg.inc"

    OS88_DRIVER 'HTTP Drive', DRVC_HTTP, hf_entry

; -----------------------------------------------------------------------------
; The service table (SPEC.md 51.2). DSV_CAPS is FSCAP_SEL alone: hf_bnorm
; folds no selector, so kern_mega hands a buffer above 1MB over as it is
; rather than one READAT - one GET - a sector (SPEC.md 62.9.2.4). Not
; FSCAP_LOCAL - a read is a round trip on a wire - so the mount takes the
; cache-only icon pass, and not FSCAP_ICON, so the zone wears the kernel's
; own drive picture.
; -----------------------------------------------------------------------------
hf_svc:
    dw FSCAP_SEL                ; DSV_CAPS
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw 0                        ; DSV_NAME    - read by nothing (driver.inc,
                                ;               drv_publish): the page letters
                                ;               its own heading
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS
    dw 0                        ; DSV_BLK     - NO SECTORS
    dw hf_cpname                ; DSV_CPNAME  - the page: HTTPPAGE.DRV's,
                                ;               and these cells its thunks
    dw hf_cp_paint              ; DSV_CPPAINT
    dw hf_cp_click              ; DSV_CPCLICK
    dw hf_cp_close              ; DSV_CPCLOSE - HTTPDRV.CFG is written here
    dw hf_fsv                   ; DSV_FS      - the verbs
    dw hf_cp_key                ; DSV_CPKEY   - the address is typed
    dw hf_cp_up                 ; DSV_CPUP
    dw hf_cp_drag               ; DSV_CPDRAG
    dw hf_pkg                   ; DSV_PKGCALL - the door (hfpkg.inc)
    times DSV_SIZE - ($ - hf_svc) db 0
%if $ - hf_svc != DSV_SIZE
  %error "httpfs: the service table is not DSV_SIZE bytes"
%endif

; --- the FSV_* table DSV_FS points at (SPEC.md 62.9.1, 122.4) ----------------
hf_fsv:
%ifdef HFTRACE                  ; HFTRACE=1's diagnostic build, never shipped:
    dw hft_0, hft_2, hft_4, hft_6   ; every verb through hf_trace (below)
    dw hf_wprot, hf_wprot, hft_12, hf_wprot, hf_wprot, hf_wprot, hf_wprot
    dw hft_22, hft_24, 0, 0
%else
    dw hf_list                  ; FSV_LIST
    dw hf_chdir                 ; FSV_CHDIR
    dw hf_stat                  ; FSV_STAT
    dw hf_read                  ; FSV_READ
    dw hf_wprot                 ; FSV_WRITE   - read-only...
    dw hf_wprot                 ; FSV_APPEND
    dw hf_readat                ; FSV_READAT
    dw hf_wprot                 ; FSV_DELETE
    dw hf_wprot                 ; FSV_RENAME
    dw hf_wprot                 ; FSV_MKDIR
    dw hf_wprot                 ; FSV_RMDIR   ...and says so in FAT's words
    dw hf_dfree                 ; FSV_DFREE
    dw hf_enum                  ; FSV_ENUM
    dw 0                        ; FSV_COPY    - the kernel's fallback
    dw 0                        ; FSV_RMTREE  - never reached
%endif
hf_fsv_end:
%if hf_fsv_end - hf_fsv != FSV_SIZE
  %error "httpfs: the FSV_* table is not FSV_SIZE bytes - a swallowed row?"
%endif

hf_cpname:  db 'Web Drive', 0   ; ...and its Control Panel list name: nine
                                ; glyphs is all the list holds before its
                                ; divider (ctrl.inc, beside cp_items)
hf_label:   db 'HTTP', 0        ; the desktop zone's caption (DV_LBL)

; =============================================================================
; ENTRY
; =============================================================================
hf_entry:
    cmp al, DRVV_ATTACH
    je hf_attach
    cmp al, DRVV_DETACH
    je hf_detach
    cmp al, DRVV_READY
    je hf_ready
    cmp al, DRVV_HWINFO         ; nothing to say: CF = 1 from HWINFO up
    cmc
    ret
%if DRVV_DETACH != 1 || DRVV_TIER != 2 || DRVV_READY != 3 || DRVV_HWINFO != 4
  %error "hf_entry's cmc answers CF = 0 below DRVV_HWINFO and CF = 1 from it"
%endif

; DRVV_ATTACH - nothing to find: it owns no hardware. out: CF=0, SI = svc
hf_attach:
    mov si, hf_svc
    clc
    ret

; -----------------------------------------------------------------------------
; DRVV_READY - the remembered address, mounted LAZILY: the volume is added
; and nothing is fetched until the drive is opened, so a dead server or a
; missing card costs the boot no time at all (SPEC.md 122.1)
; -----------------------------------------------------------------------------
hf_ready:
    push ax
    call hf_cfg_load
    jc .out
    test al, 1
    jz .out
    cmp byte [hf_url], 0
    je .out
    call hf_parse
    jc .out
    call hf_vadd
.out:
    pop ax
    clc
    ret

; DRVV_DETACH - cannot fail (SPEC.md 51.6 rule 1)
hf_detach:
    call hp_drop                ; the page's image, if the panel left it
    call hf_abort
    call hf_unmount
    clc
    ret

; -----------------------------------------------------------------------------
; hf_vadd - the volume, added. out: CF=0 and [hf_vol]; CF=1 and AX =
;           HFE_VOL. A volume already up is RE-POINTED: its index goes, so
;           the next open fetches the new address's, and the VOLUME goes and
;           comes back, because the kernel's drive and folder - and every
;           instance's banked one - are handles into the OLD index. A row
;           kept would leave them standing in a folder hf_cwd no longer is
;           (SPEC.md 122.4); given back, a drive standing on it falls to
;           A: and each one is asked again through FSV_CHDIR, which checks
;           it against the new index. The letter is VOL_ADD's first free one
; -----------------------------------------------------------------------------
hf_vadd:
    call hf_unmount             ; a KEPT connection is to the OLD address,
                                ; and the index and the row are the OLD tree's
    mov word [hf_err], 0        ; ...and the OLD address's last failure
    push cx
    push dx
    push si
    xor al, al                  ; our own volume handle: there is one drive
    xor cx, cx                  ; no sector count - there are no sectors
    xor dx, dx                  ; a plain add, not a take
    mov si, hf_label
    call OSAPI_VOL_ADD
    pop si
    pop dx
    pop cx
    jc .full
    mov [hf_vol], al
    clc
    ret
.full:
    mov ax, HFE_VOL
    stc
    ret

; --- hf_unmount - the volume and the index back. keeps every register ------
hf_unmount:
    call hf_abort               ; ...and holds a socket slot of ETHER.DRV's
    push ax
    call hf_drop_idx
    mov al, 0xFF
    xchg al, [hf_vol]
    cmp al, 0xFF
    je .out
    call OSAPI_VOL_DEL
.out:
    mov byte [hf_failed], 0
    pop ax
    ret

; =============================================================================
; THE PACKAGE DOOR (hfpkg.inc). BL = the verb; ES = the CALLER's segment
; =============================================================================
hf_pkg:
    mov ax, HFE_ARG
    cmp bl, HFV_MAX
    ja hf_pno
    mov bh, 0                   ; BX is spent: no verb takes it in
    shl bx, 1
    jmp [bx+hf_ptab]
hf_ptab:    dw hf_pident, hf_pstate, hf_pmount, hf_punmnt, hf_pfetch
            dw hf_purl, hf_cfg_save
%ifdef HFDIAG
            dw hf_pdiag
%endif
%if ($ - hf_ptab) / 2 != HFV_MAX + 1
  %error "httpfs: hf_ptab is indexed by the HFV_* numbers"
%endif

hf_pident:
    mov ax, HF_SIG
    mov cx, HF_ABI
    clc
    ret

hf_pstate:
    mov al, [hf_vol]
    mov ah, HFS_IDLE
    cmp al, 0xFF
    je .st
    mov ah, HFS_READY
    cmp word [hf_iseg], 0
    jne .st
    mov ah, HFS_FAILED
    cmp byte [hf_failed], 0
    jne .st
    mov ah, HFS_LAZY
.st:
    mov cx, [hf_nrec]
    mov dx, [hf_status]
    mov si, [hf_err]
    clc
    ret

hf_pmount:
    push cx
    push si
    push di
    mov di, hf_url              ; ES:SI, the caller's, into our buffer
    mov cx, HF_URLMAX - 1
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    pop di
    pop si
    pop cx
    call hf_parse
    jnc .parsed
    call hf_abort               ; REFUSED, after hf_parse wrote some of the
    call hf_drop_idx            ; new host and port over the old: a kept
    mov byte [hf_failed], 1     ; connection and the old index would send the
    jmp hf_pno                  ; next GET to that mixture. Dropped, the next
                                ; open re-parses and fails cleanly (HFE_URL)
.parsed:
    call hf_vadd
    jc hf_pno
    mov al, [hf_vol]
    ret

hf_punmnt:
    call hf_unmount
    clc
    ret

hf_pfetch:
    call hf_fetch
    jc hf_pno
    mov cx, [hf_nrec]
    ret

hf_purl:                        ; ES:DI = HF_URLMAX bytes of the caller's
    push cx
    push si
    push di
    mov si, hf_url
    mov cx, HF_URLMAX
    cld
    rep movsb
    pop di
    pop si
    pop cx
    clc
    ret

hf_pno:
    mov [hf_err], ax            ; ...kept, so HFV_STATE says it again
    stc
    ret

%include "hfnet.inc"
%include "hfidx.inc"
%ifdef HFDIAG
%include "hfdiag.inc"
%endif

%ifdef HFTRACE
; A ring of (verb, AX in, AX out, flags out): 64 records of 8 bytes at
; hft_ring, [hft_n] counting every call. A gate's diagnostic, never shipped.
%macro HFT 2
hft_%1:
    mov [hft_ax], ax
    mov byte [hft_v], %1
    call hft_in
    call %2
    jmp hft_out
%endmacro
    HFT 0, hf_list
    HFT 2, hf_chdir
    HFT 4, hf_stat
    HFT 6, hf_read
    HFT 12, hf_readat
    HFT 22, hf_dfree
    HFT 24, hf_enum
hft_in:
    push bx
    pushf
    call hft_slot
    mov al, [hft_v]
    mov [hft_ring+bx], al
    mov ax, [hft_ax]
    mov [hft_ring+bx+2], ax
    inc word [hft_n]
    popf
    pop bx
    ret
hft_out:
    push bx
    pushf
    dec word [hft_n]
    call hft_slot
    inc word [hft_n]
    mov [hft_ring+bx+4], ax
    pop word [hft_ring+bx+6]
    push word [hft_ring+bx+6]
    popf
    pop bx
    ret
hft_slot:                       ; BX = [hft_n] mod 64, times 8
    mov bx, [hft_n]
    and bx, 63
    shl bx, 1
    shl bx, 1
    shl bx, 1
    ret
%endif
%define OS88SOCK_STATE          ; ...and net_find asks NETV_STATE: ZF=1 = no
%include "os88sock.inc"         ; link. net_find, net_cls - the card first
%include "hfpage.inc"

; =============================================================================
; STATE
; =============================================================================
hf_vol:     db 0xFF             ; the volume index we registered, FF = none

    OS88_STATE                  ; every cell from here is 0 at load
hf_cfg:     times 4 db 0        ; HTTPDRV.CFG's head - magic, version, flags -
hf_url:     times HF_URLMAX db 0 ; ...and the address: ONE record, one write
%if hf_url - hf_cfg != 4
  %error "httpfs: HTTPDRV.CFG is the four-byte head and the address, adjacent"
%endif
hf_host:    times NET_HOSTMAX db 0
hf_port:    dw 0
hf_pfx:     times HF_PFXMAX db 0
hf_path:    times HF_PATHMAX db 0
hf_lbuf:    times HF_LINEMAX db 0
hf_sock:    db 0                ; the socket handle, 0 = none
hf_live:    db 0                ; 1 = it is KEPT, idle, for the next GET
hf_kept:    db 0                ; 1 = this GET rides a kept connection
hf_keep:    db 0                ; 1 = this reply's server agreed to keep it
hf_reqn:    dw 0                ; the request's length, for a resend
hf_tbase:   dw 0                ; the idle bound's start tick
hf_hbase:   dw 0                ; ...and the headers' deadline's (HF_HDRT)
hf_rpos:    dw 0                ; the reader: next byte...
hf_rlen:    dw 0                ; ...and bytes staged
hf_status:  dw 0                ; the last HTTP status
hf_clen:    dw 0, 0             ; its Content-Length...
hf_hasl:    db 0                ; ...if it sent one
hf_rng:     db 0                ; 1 = the request carries Range:
hf_rlo:     dw 0, 0             ; ...from here...
hf_rn:      dw 0, 0             ; ...this many bytes
hf_left:    dw 0, 0             ; hf_body: bytes still to deliver...
hf_skip:    dw 0, 0             ; ...after skipping these...
hf_bseg:    dw 0                ; ...to here
hf_boff:    dw 0
hf_prog:    db 0                ; 1 = step the progress bar
hf_err:     dw 0                ; the last HFE_*, 0 = none
hf_failed:  db 0                ; 1 = the last fetch of the index failed
; --- the index (hfidx.inc) ---------------------------------------------------
hf_iseg:    dw 0                ; its claim, 0 = none held
hf_tseg:    dw 0                ; ...one being fetched
hf_nrec:    dw 0                ; its records
hf_nwant:   dw 0                ; ...what the length promised
hf_cwd:     dw 0                ; the current folder's handle
hf_fsize:   dw 0, 0             ; the file a read is about
hf_chain:   times HF_DEPTH dw 0 ; hf_mkpath's walk
hf_ent:     times 32 db 0       ; the staged SPEC.md 19.1 entry
hf_want:    times 11 db 0       ; FSV_STAT's name, in FAT form
; --- HTTPDRV.CFG's goto (hfpage.inc) ----------------------------------------
hf_bdrv:    db 0                ; where we stood before HTTPDRV.CFG...
hf_bclus:   dw 0                ; ...and we go back there
%ifdef HFTRACE
hft_ax:     dw 0
hft_v:      db 0
hft_n:      dw 0
hft_ring:   times 64 * 8 db 0
%endif
hf_rbuf:    times HF_RBUF db 0  ; the reader's staging - and the request
                                ; is composed here before it is sent

    OS88_DRV_END
