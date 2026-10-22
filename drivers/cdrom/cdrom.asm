; =============================================================================
; os8088 - CDROM.DRV (SPEC.md 119)
;
; An ATAPI CD-ROM drive: the disc's ISO 9660 file system as a read-only volume
; on the desktop, and its audio tracks played through the drive's own audio
; output. A DRVC_CD driver - the third file class (SPEC.md 119.1) - so the
; kernel asks it FSV_* questions and never reads a sector of the volume, and a
; package plays the audio through the door in cdpkg.inc.
;
;   cdpkg.inc   the package door's ABI, shared with apps/cdrack
;   atapi.inc   the drive: the probe, the packet command, its waits and sense
;   cddisc.inc  the disc: the TOC, the volume descriptor, the path table
;   cdiso.inc   the volume: records, names, handles and the FSV_* verbs
;   cdaudio.inc the door's verbs: play, pause, stop, where, eject, volume
;
; ATTACH PROBES AND REGISTERS NOTHING; READY registers the one volume, disc or
; none, because the drive is a drive whether or not a disc is in it. Nothing
; reads the disc until the volume is opened.
;
; ES IS KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.1) - except the
; package door, where it is the CALLING PACKAGE's (SPEC.md 20.11).
; =============================================================================

%include "os88drv.inc"
%include "cdpkg.inc"

    OS88_DRIVER 'CD-ROM', DRVC_CD, cd_entry

; -----------------------------------------------------------------------------
; The service table (SPEC.md 51.2). DSV_BLK is 0: a file class has no sectors
; for the kernel to ask for. DSV_CAPS is FSCAP_ICON and not FSCAP_LOCAL - a
; CD's read is a seek and a turn, so the mount takes the cache-only icon pass
; (62.9.2.1), and our zone wears our own picture (26.4.2).
; -----------------------------------------------------------------------------
cd_svc:
    dw FSCAP_ICON               ; DSV_CAPS: our volume draws its own picture,
                                ; a disc and not a hard disk (cdico.inc,
                                ; SPEC.md 26.4.2) - and NOT FSCAP_LOCAL: a
                                ; CD's read is a seek and a turn
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw cd_name                  ; DSV_NAME
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS
    dw 0                        ; DSV_BLK     - NO SECTORS (SPEC.md 119)
    dw 0                        ; DSV_CPNAME  - no Control Panel page
    dw 0                        ; DSV_CPPAINT
    dw 0                        ; DSV_CPCLICK
    dw 0                        ; DSV_CPCLOSE
    dw cd_fsv                   ; DSV_FS      - the verbs
    dw 0                        ; DSV_CPKEY
    dw 0                        ; DSV_CPUP
    dw 0                        ; DSV_CPDRAG
    dw cd_pkg                   ; DSV_PKGCALL - the audio door (SPEC.md 119.4)
    times DSV_SIZE - ($ - cd_svc) db 0
%if $ - cd_svc != DSV_SIZE
  %error "cdrom: the service table is not DSV_SIZE bytes"
%endif

; --- the FSV_* table DSV_FS points at (SPEC.md 62.9.1, 119.3.4) --------------
cd_fsv:
    dw cd_list                  ; FSV_LIST
    dw cd_chdir                 ; FSV_CHDIR
    dw cd_stat                  ; FSV_STAT
    dw cd_read                  ; FSV_READ
    dw cd_wprot                 ; FSV_WRITE   - a CD is read-only...
    dw cd_wprot                 ; FSV_APPEND
    dw cd_readat                ; FSV_READAT
    dw cd_wprot                 ; FSV_DELETE
    dw cd_wprot                 ; FSV_RENAME
    dw cd_wprot                 ; FSV_MKDIR
    dw cd_wprot                 ; FSV_RMDIR   ...and says so in FAT's words
    dw cd_dfree                 ; FSV_DFREE
    dw cd_enum                  ; FSV_ENUM
    dw 0                        ; FSV_COPY    - the kernel's fallback
    dw 0                        ; FSV_RMTREE  - ...and here, never reached
cd_fsv_end:
%if cd_fsv_end - cd_fsv != FSV_SIZE
  %error "cdrom: the FSV_* table is not FSV_SIZE bytes - a swallowed row?"
%endif

cd_name:    db 'CD-ROM', 0
cd_label:   db 'CD-ROM', 0      ; DV_LBL is 7 bytes inline (SPEC.md 18.7.3)

; =============================================================================
; ENTRY
; =============================================================================
cd_entry:
    cmp al, DRVV_ATTACH
    je cd_attach
    cmp al, DRVV_DETACH
    je cd_detach
    cmp al, DRVV_READY
    je cd_ready
    cmp al, DRVV_HWINFO         ; "nothing to say": there is no class-9 record
    cmc                         ; layout (os88drv.inc), so HWINFO and any
    ret                         ; later verb answer CF = 1, the rest CF = 0
%if DRVV_DETACH != 1 || DRVV_TIER != 2 || DRVV_READY != 3 || DRVV_HWINFO != 4
  %error "cd_entry's cmc answers CF = 0 below DRVV_HWINFO and CF = 1 from it"
%endif

; -----------------------------------------------------------------------------
; DRVV_ATTACH - find the drive (SPEC.md 119.2)
; out: CF = 0 and SI = the service table; CF = 1 and AL = DRVE_HW
;
; ALL-OR-NOTHING (SPEC.md 51.6 rule 1): the probe selects devices and sends
; IDENTIFY PACKET DEVICE, which change nothing a refusal would have to put
; back; no vector is hooked and no memory claimed.
; -----------------------------------------------------------------------------
cd_attach:
    push bx
    push cx
    push dx
    cmp word [cd_base], 0
    jne .twice                  ; attached already: touch nothing (51.2.2)
    call OSAPI_CPU_INFO         ; AL = the tier
    cmp al, CPU_286
    jb .no                      ; a 16-bit data port on an 8-bit bus loses
                                ; every high byte (SPEC.md 52.1, 119.2)
    call cd_probe
    jc .no
    mov si, cd_svc
    clc
    jmp short .out
.twice:
    mov al, DRVE_TWICE
    stc
    jmp short .out
.no:
    mov al, DRVE_HW
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; DRVV_READY - the volume, registered now that the fence answers (51.2.2)
;
; A refusal to register is quiet: the driver stays loaded with its door open,
; so CDRack still plays a disc on a machine whose volume table is full.
; -----------------------------------------------------------------------------
cd_ready:
    push ax
    push cx
    push dx
    push si
    xor al, al                  ; our own volume handle: there is one drive
    xor cx, cx                  ; no sector count - the kernel reads none
    xor dx, dx                  ; a plain add, not a take (SPEC.md 52.1.1)
    mov si, cd_label
    call OSAPI_VOL_ADD
    jc .out
    mov [cd_vol], al
.out:
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6 rule 1)
;
; The volume goes. A disc that is playing is LEFT playing (SPEC.md 119.4): the
; drive plays on its own, and unticking the driver is not a request for
; silence. Nothing else was taken.
; -----------------------------------------------------------------------------
cd_detach:
    push ax
    mov al, [cd_vol]
    cmp al, 0xFF
    je .out
    call OSAPI_VOL_DEL
    mov byte [cd_vol], 0xFF
.out:
    pop ax
    clc
    ret

%include "atapi.inc"
%include "cddisc.inc"
%include "cdiso.inc"
%include "cdaudio.inc"
%include "cdico.inc"

; =============================================================================
; STATE - the volume index is the one cell whose load-time value is not 0
; =============================================================================
cd_vol:     db 0xFF             ; the volume index we registered, FF = none
cd_stag:    dw 0, 0xFFFF        ; the sector buffer's LBA; FFFFh in the high
                                ; word names no sector (cd_rdsec)

    OS88_STATE                  ; every cell from here is 0 at load
cd_base:    dw 0                ; the drive: its task file...
cd_unit:    db 0                ; ...unit...
cd_pkw:     dw 0                ; ...and packet length in WORDS (6 or 8)
cd_w0:      dw 0                ; IDENTIFY's word 0, banked across the drain
cd_model:   times CD_MODELSZ db 0

; --- the waits (atapi.inc) ---------------------------------------------------
cd_tlast:   dw 0                ; the BIOS tick's low word, last seen
cd_tleft:   dw 0                ; ticks left in the bound
cd_ilo:     dw 0                ; ...and polls left: the net under it
cd_ihi:     db 0
cd_tries:   db 0                ; cd_cmd's "becoming ready" retries
cd_uatry:   db 0                ; ...and its UNIT ATTENTION retry

; --- one command: the packet and its transfer, ADJACENT (cd_sense banks the
; whole run as CD_PKT_W words and puts it back) ---------------------------------
cd_pkt:     times 16 db 0
cd_xseg:    dw 0
cd_xoff:    dw 0
cd_xmax:    dw 0
cd_xout:    db 0, 0
cd_pkt_end:
cd_pkt2:    times 24 db 0       ; cd_sense's bank of the run above
cd_xgot:    dw 0                ; bytes the data phase moved
cd_lkey:    db 0                ; the last refusal's sense key (or CD_TMO)...
cd_lop:     db 0                ; ...and the command it answered
cd_snsbuf:  times 18 db 0       ; REQUEST SENSE's answer
%if cd_pkt_end - cd_pkt != 24
  %error "cdrom: cd_sense banks the packet and the transfer as 12 words"
%endif

; --- the disc (cddisc.inc) ---------------------------------------------------
cd_gen:     db 0                ; the media generation (SPEC.md 119.3.4)
cd_valid:   db 0                ; 1 = the tables below describe the disc
cd_stale:   db 0                ; 1 = handles refuse until the root is mounted
cd_iso:     db 0                ; 1 = the disc carries a readable volume
cd_first:   db 0                ; the TOC's first and last track
cd_last:    db 0
cd_kind:    db 0                ; CDK_*
cd_tsum:    dw 0                ; a sum of the last TOC read, and whether one
cd_tsok:    db 0                ; was: a TOC that differs is a changed disc
cd_vdlba:   dw 0, 0             ; the last data track's start
cd_rlba:    dw 0, 0             ; the root directory's extent...
cd_rlen:    dw 0, 0             ; ...and length in bytes
cd_dbase:   dw 0, 0             ; the lowest directory extent (119.3.3)
cd_ptlba:   dw 0, 0             ; the type-L path table
cd_ptlen:   dw 0, 0
cd_toc:     times CD_TOCSZ db 0 ; READ TOC's answer, raw

; --- the sector buffer, tagged by cd_stag above ------------------------------
cd_sbuf:    times 2048 db 0

; --- the volume (cdiso.inc) --------------------------------------------------
cd_cwd:     dw 0                ; the folder we stand in: its handle...
cd_cwdlba:  dw 0, 0             ; ...extent...
cd_cwdlen:  dw 0, 0             ; ...and length
cd_it_lba:  dw 0, 0             ; the directory walk: this sector...
cd_it_n:    dw 0                ; ...sectors left, this one included...
cd_it_off:  dw 0                ; ...and the next record's offset
cd_r_lba:   dw 0, 0             ; cd_resolve's answer: extent...
cd_r_len:   dw 0, 0             ; ...length...
cd_r_fl:    db 0                ; ...ISO flags...
cd_r_cda:   db 0                ; ...and the audio track, 0 = a real record
cd_hcache:  times CD_HC_N * CD_HC_SZ db 0
cd_hcnext:  db 0                ; the next cache row to overwrite
cd_ent:     times 32 db 0       ; the staged SPEC.md 19.1 entry
cd_nm:      times 16 db 0       ; a mangled name
cd_want:    times 16 db 0       ; FSV_STAT's name, staged and upper-cased
cd_hash:    db 0                ; cd_mangle's scratch
cd_opos:    dw 0
cd_dot:     dw 0
cd_nlen:    dw 0
cd_pos:     dw 0, 0             ; cd_bytes: the offset into the file...
cd_left:    dw 0, 0             ; ...bytes still to move...
cd_bseg:    dw 0                ; ...and where they go
cd_boff:    dw 0
cd_prog:    db 0                ; 1 = report progress (FSV_READ)
cd_cda:     times 44 db 0       ; a TRACKnn.CDA's bytes, composed
cd_tmp:     dw 0, 0             ; a parent walk's scratch
cd_tmp2:    dw 0, 0
cd_ord:     dw 0                ; FSV_ENUM's ordinal

; --- the door (cdaudio.inc) --------------------------------------------------
cd_abuf:    times CD_MPLEN db 0 ; SUB-CHANNEL and MODE SENSE answers
cd_vpage:   dw 0                ; ...and where MODE SENSE's page 0Eh is

%if cd_rlen - cd_rlba != 4 || cd_cwdlen - cd_cwdlba != 4 || cd_r_len - cd_r_lba != 4
  %error "cdrom: cd_resolve and cd_setcwd copy an extent and its length as one run"
%endif

    OS88_DRV_END
