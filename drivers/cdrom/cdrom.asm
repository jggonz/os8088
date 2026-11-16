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

%define OS88_DRV_HIGHOK          ; kern_mega may run this image above 1MB
                                ; (KERN-MEGA-MEMORY-PLAN MC8): no arithmetic
                                ; on its own CS or DS, PIO only - nothing of
                                ; ours reaches a BIOS or a DMA engine - and
                                ; cd_bnorm leaves an LDT selector's offset
                                ; unfolded
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

cd_name:                        ; the driver's name AND the volume's label:
cd_label:   db 'CD-ROM', 0      ; one string, the two being the same seven
                                ; bytes (DV_LBL is 7 bytes inline and the
                                ; kernel copies both - SPEC.md 18.7.3)

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
    stc                         ; TIER, HWINFO and any later verb: REFUSED,
    ret                         ; with nothing to say (SPEC.md 51.2.2)

; -----------------------------------------------------------------------------
; DRVV_ATTACH - find the drive (SPEC.md 119.2)
; out: CF = 0 and SI = the service table; CF = 1 and AL = DRVE_HW
;
; ALL-OR-NOTHING (SPEC.md 51.6 rule 1): the probe selects devices and sends
; IDENTIFY PACKET DEVICE, which change nothing a refusal would have to put
; back; no vector is hooked and no memory claimed.
; -----------------------------------------------------------------------------
cd_attach:
    cmp word [cd_base], 0
    mov al, DRVE_TWICE          ; (a `mov`: the compare's ZF stands)
    jne .fail                   ; attached already: touch nothing (51.2.2)
    call OSAPI_CPU_INFO         ; AL = the tier (cd_bnorm asks for the flags
                                ; itself: KERN-MEGA-PLAN 14.112). It keeps
                                ; every register but AX, and cd_probe keeps
                                ; all of them, so the frame this used to
                                ; build round the two protected nothing
    cmp al, CPU_286
    jb .no                      ; a 16-bit data port on an 8-bit bus loses
                                ; every high byte (SPEC.md 52.1, 119.2)
    call cd_probe
    jc .no
    mov si, cd_svc              ; CF = 0 off the `jc`
    ret
.no:
    mov al, DRVE_HW
.fail:
    stc
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
; STATE - the cells whose load-time value is not 0, then OS88_STATE's zeros
; =============================================================================
cd_vol:     db 0xFF             ; the volume index we registered, FF = none
; EVERY WORD BELOW IS ON AN EVEN ADDRESS, and the order is what does it: on
; the 286 this driver needs, a word at an odd address is two bus cycles, and
; the cells cd_packet and cd_bytes touch per block were all odd. The sizes
; from here to the end are fixed, so one alignment decides every one - and
; it costs nothing while the code above ends odd, as it does, cd_vol taking
; the odd byte; the second one, at cd_sbuf, is a guard.
    align 2, db 0
cd_stag:    dw 0, 0xFFFF        ; the sector buffer's LBA; FFFFh in the high
                                ; word names no sector (cd_rdsec)

; --- a TRACKnn.CDA's 44 bytes (SPEC.md 119.3.5), composed by cd_mkcda --------
; THE FIRST 22 ARE THE SAME FOR EVERY TRACK, so they are the cell's load-time
; value and nothing ever writes them: cd_mkcda fills 22..43 and no more. It
; used to store all 44 a word at a time on every read of every .CDA - eleven
; six-byte immediates for bytes that never change. Bytes 23, 39 and 43 are
; the high byte of the track word and the two MSFs' pad, and they too are 0
; from the load and never written (cd_mkcda, cd_msfle).
cd_cda:     db 'RIFF'
            dd 36
            db 'CDDAfmt '
            dd 24
            dw 1
            times 44 - ($ - cd_cda) db 0

    OS88_STATE                  ; every cell from here is 0 at load
cd_base:    dw 0                ; the drive: its task file...
cd_pkw:     dw 0                ; ...packet length in WORDS (6 or 8) -
cd_model:   times CD_MODELSZ db 0   ; ...whose HIGH BYTE IS cd_ident's trim
                                ; sentinel: always 0, never a space, so the
                                ; trim walking down an all-blank model stops
                                ; on it without a bound of its own
cd_unit:    db 0                ; ...and unit
%if cd_model - cd_pkw != 2
  %error "cdrom: cd_ident's trim stops on cd_pkw's high byte, just below cd_model"
%endif

; --- the waits (atapi.inc) ---------------------------------------------------
cd_tlast:   dw 0                ; the BIOS tick's low word, last seen
cd_tleft:   dw 0                ; ticks left in the bound
cd_ilo:     dw 0                ; ...and polls left: the net under it
cd_ihi:     db 0
cd_tries:   db 0                ; cd_cmd's "becoming ready" retries
cd_uatry:   db 0                ; ...and its UNIT ATTENTION retry - ADJACENT,
                                ; so cd_cmd arms the two with one word store
cd_prog:    db 0                ; cd_bytes: 1 = report progress (FSV_READ)
%if cd_uatry - cd_tries != 1 || cd_ihi - cd_ilo != 2
  %error "cdrom: cd_cmd arms cd_tries/cd_uatry as a word; cd_tmo_set needs cd_ihi after cd_ilo"
%endif

; --- one command: the packet and its transfer, ADJACENT (cd_sense banks the
; whole run as CD_PKT_W words and puts it back) ---------------------------------
cd_pkt:     times 16 db 0
cd_xmax:    dw 0                ; ...and these two right behind the packet, so
cd_xout:    db 0, 0             ; cd_pclear's one fill clears all three
cd_xoff:    dw 0
cd_xseg:    dw 0
cd_pkt_end:
cd_pkt2:    times 24 db 0       ; cd_sense's bank of the run above
cd_xgot:    dw 0                ; bytes the data phase moved
cd_lkey:    db 0                ; the last refusal's sense key (or CD_TMO)...
cd_lop:     db 0                ; ...and the command it answered: ADJACENT, one
                                ; word store in cd_cmd
cd_snsbuf:  times 18 db 0       ; REQUEST SENSE's answer
%if cd_pkt_end - cd_pkt != 24
  %error "cdrom: cd_sense banks the packet and the transfer as 12 words"
%endif
%if cd_xmax - cd_pkt != 16 || cd_xout - cd_pkt != 18 || cd_lop - cd_lkey != 1
  %error "cdrom: cd_pclear zeroes cd_pkt, cd_xmax and cd_xout as ten words; cd_cmd stores cd_lkey/cd_lop as one"
%endif

; --- the disc (cddisc.inc) ---------------------------------------------------
cd_first:   db 0                ; the TOC's first and last track
cd_last:    db 0
cd_iso:     db 0                ; 1 = the disc carries a readable volume
cd_kind:    db 0                ; CDK_*
cd_gen:     db 0                ; the media generation (SPEC.md 119.3.4)
cd_valid:   db 0                ; 1 = the tables below describe the disc
cd_stale:   db 0                ; 1 = handles refuse until the root is mounted
cd_tsok:    db 0                ; 1 = cd_tsum holds a sum of the last TOC read:
cd_tsum:    dw 0                ; a TOC that differs is a changed disc
%if cd_last - cd_first != 1 || cd_kind - cd_iso != 1 || cd_gen - cd_kind != 1 || cd_stale - cd_valid != 1
  %error "cdrom: first/last, iso/kind, kind/gen and valid/stale are each read or written as one word"
%endif
cd_vdlba:   dw 0, 0             ; the last data track's start
cd_rlba:    dw 0, 0             ; the root directory's extent...
cd_rlen:    dw 0, 0             ; ...and length in bytes
cd_dbase:   dw 0, 0             ; the lowest directory extent (119.3.3)
cd_ptlba:   dw 0, 0             ; the type-L path table
cd_ptlen:   dw 0, 0
%if cd_rlen - cd_rlba != 4 || cd_dbase - cd_rlba != 8 || cd_ptlba - cd_rlba != 12 || cd_ptlen - cd_rlba != 16
  %error "cdrom: cd_readvol fills cd_rlba..cd_ptlen as one run of five dwords"
%endif
cd_toc:     times CD_TOCSZ db 0 ; READ TOC's answer, raw

; --- the sector buffer, tagged by cd_stag above ------------------------------
; ON A WORD BOUNDARY: `rep insw` fills it on a 286 or better, whose 16-bit
; bus writes a word to an odd address as two cycles - a thousand extra per
; sector, on every directory and every partial read. It was odd.
    align 2, db 0
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
cd_ent:     times 32 db 0       ; the staged SPEC.md 19.1 entry
cd_nm:      times 16 db 0       ; a mangled name
cd_want:    times 16 db 0       ; FSV_STAT's name, staged and upper-cased
cd_pos:     dw 0, 0             ; cd_bytes: the offset into the file...
cd_left:    dw 0, 0             ; ...bytes still to move...
cd_boff:    dw 0                ; ...and where they go - OFFSET FIRST, so one
cd_bseg:    dw 0                ; `les` loads the pair (cd_bytes, cd_enum)
cd_tmp:     dw 0, 0             ; a parent walk's scratch, and the path table's

; --- the door (cdaudio.inc) --------------------------------------------------
cd_abuf:    times CD_MPLEN db 0 ; SUB-CHANNEL and MODE SENSE answers
cd_hcnext:  db 0                ; the next cache row to overwrite, mod CD_HC_N

%if cd_rlen - cd_rlba != 4 || cd_cwdlen - cd_cwdlba != 4 || cd_r_len - cd_r_lba != 4 || cd_bseg - cd_boff != 2
  %error "cdrom: cd_resolve and cd_setcwd copy an extent and its length as one run; les reads cd_boff then cd_bseg"
%endif

    OS88_DRV_END
