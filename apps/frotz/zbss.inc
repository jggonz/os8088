; =============================================================================
; os8088 - apps/frotz/zbss.inc
;
; Every byte of Frotz's loader-zeroed bss, in one file, because OS88_BSS needs
; a constant with no forward references (apps/os88api.inc: the macro %errors
; otherwise) and a total scattered across ten modules is not one.
;
; The locations are `%define`s, not `equ`s, and that is the whole trick: a
; %define is expanded where it is USED, so `mov ax, [zf_sseg]` becomes
; `mov ax, [os88_image_end + 0]` at the use site and the forward reference to
; os88_image_end is an ordinary one NASM resolves on the second pass. An `equ`
; here would have to be evaluated at definition time, before OS88_IMAGE_END has
; planted the label.
;
; ALL-ZERO MUST BE A VALID FRESH STATE (SPEC.md 21 step 5: the loader zeroes
; this and nothing else does). It is: ZFS_NONE is 0, no claim is held, the
; worker is not hired, every ring is empty and every cursor is at the origin.
;
; Nothing outside a module touches that module's block. The two exceptions are
; deliberate and named here rather than discovered: the UI task writes
; [zf_req]/[zf_reqres]/[zi_*] that the worker reads (the handshake in zio.inc,
; docs/FROTZ-PLAN.md 4.1), and zexec.inc reads the whole story-header block
; because that IS the machine's configuration.
; =============================================================================

; --- 1. UI and lifecycle (frotz.asm) -----------------------------------------
%define zf_win        (os88_image_end + 0)    ; word: our window pointer
%define zf_state      (os88_image_end + 2)    ; byte: ZFS_NONE/LOAD/RUN
%define zf_hired      (os88_image_end + 3)    ; byte: the worker exists
%define zf_req        (os88_image_end + 4)    ; byte: ZREQ_* the worker posted
%define zf_reqres     (os88_image_end + 5)    ; byte: ZRES_* the UI task answered
%define zf_script     (os88_image_end + 6)    ; byte: transcript is on
%define zf_showabout  (os88_image_end + 7)    ; byte: About covers the text
%define zf_pendok     (os88_image_end + 8)    ; byte: zf_pend holds a name
%define zf_pend       (os88_image_end + 9)    ; 13 bytes: OSAPI_ARG_FILE's name
%define zf_dead       (os88_image_end + 22)   ; byte: @quit ran; the VM parks
%define zf_err        (os88_image_end + 23)   ; byte: a fatal ZERR_*

; --- 2. The story image and its header (zmem.inc) ----------------------------
; Every one of these except zf_sseg/zf_skb/zf_slen is a word read straight out
; of the story header (SS 59.2) and cached because it is read constantly.
; Byte addresses below 64KB fit a word by construction: the header's tables
; all live in dynamic or static memory, which the Standard caps at 64KB.
%define zf_sseg       (os88_image_end + 24)   ; word: story claim base segment
%define zf_skb        (os88_image_end + 26)   ; word: its size in KB
%define zf_slen_l     (os88_image_end + 28)   ; word: story length, low
%define zf_slen_h     (os88_image_end + 30)   ; word: story length, high
%define zf_ver        (os88_image_end + 32)   ; byte: 1..8
%define zf_pshift     (os88_image_end + 33)   ; byte: packed-address shift
                                              ;       1 (v1-3), 2 (v4-7), 3 (v8)
%define zf_flags1     (os88_image_end + 34)   ; byte: header byte 1, as we set it
%define zf_himem      (os88_image_end + 36)   ; word: base of high memory
%define zf_initpc     (os88_image_end + 38)   ; word: initial PC / main routine
%define zf_dict       (os88_image_end + 40)   ; word: dictionary
%define zf_objtab     (os88_image_end + 42)   ; word: object table
%define zf_gvar       (os88_image_end + 44)   ; word: global variables
%define zf_static     (os88_image_end + 46)   ; word: base of static memory
%define zf_abbrev     (os88_image_end + 48)   ; word: abbreviations table
%define zf_hdrext     (os88_image_end + 50)   ; word: header extension (v5+)
%define zf_alpha      (os88_image_end + 52)   ; word: alphabet table (v5+, 0 =
                                              ;       the built-in one)
%define zf_termtab    (os88_image_end + 54)   ; word: terminating chars (v5+)
%define zf_roff       (os88_image_end + 56)   ; word: routine offset (v6/v7)
%define zf_soff       (os88_image_end + 58)   ; word: string offset (v6/v7)
%define zf_objsz      (os88_image_end + 60)   ; word: bytes per object entry
%define zf_propdef    (os88_image_end + 62)   ; word: property defaults table

; --- 3. Execution state (zexec.inc) ------------------------------------------
; The PC lives in ES:SI while the VM runs and is spilled here across anything
; that needs ES back (a kernel call, the input wait). zf_pcseg is a SEGMENT,
; not a story address - re-forming a 20-bit address from it is zm_pcaddr's job.
%define zf_pcseg      (os88_image_end + 64)   ; word: PC segment
%define zf_pcoff      (os88_image_end + 66)   ; word: PC offset within it
%define zf_stkseg     (os88_image_end + 68)   ; word: the Z-stack claim
%define zf_sp         (os88_image_end + 70)   ; word: stack pointer, BYTE offset
%define zf_fp         (os88_image_end + 72)   ; word: current frame base
%define zf_nlocals    (os88_image_end + 74)   ; byte: locals in the current frame
%define zf_opn        (os88_image_end + 75)   ; byte: operand count this insn
%define zf_ops        (os88_image_end + 76)   ; 8 words: the decoded operands
%define zf_optype     (os88_image_end + 92)   ; 8 bytes: their types
%define zf_store      (os88_image_end + 100)  ; byte: store variable this insn
%define zf_rndlo      (os88_image_end + 102)  ; word: RNG state, low
%define zf_rndhi      (os88_image_end + 104)  ; word: RNG state, high
%define zf_rndseq     (os88_image_end + 106)  ; word: 0 = random, else the
                                              ;       predictable sequence's max
%define zf_rndctr     (os88_image_end + 108)  ; word: where that sequence is

; --- 4. The text window, v1-v5/v7/v8 (zwin.inc) ------------------------------
%define zw_x0         (os88_image_end + 110)  ; word: content rect, absolute
%define zw_y0         (os88_image_end + 112)
%define zw_w          (os88_image_end + 114)
%define zw_h          (os88_image_end + 116)
%define zw_cols       (os88_image_end + 118)  ; word: text columns that fit
%define zw_rows       (os88_image_end + 120)  ; word: text rows that fit
%define zw_upper      (os88_image_end + 122)  ; word: rows the upper window owns
%define zw_cur        (os88_image_end + 124)  ; byte: 0 = lower, 1 = upper
%define zw_col        (os88_image_end + 126)  ; word: cursor column, lower
%define zw_row        (os88_image_end + 128)  ; word: cursor row, lower
%define zw_ucol       (os88_image_end + 130)  ; word: cursor column, upper
%define zw_urow       (os88_image_end + 132)  ; word: cursor row, upper
%define zw_style      (os88_image_end + 134)  ; byte: ZST_* bits, live
%define zw_fg         (os88_image_end + 135)  ; byte: foreground colour index
%define zw_bg         (os88_image_end + 136)  ; byte: background colour index
%define zw_more       (os88_image_end + 138)  ; word: rows since the last input,
                                              ;       for the [MORE] prompt
%define zw_sbseg      (os88_image_end + 140)  ; word: scrollback claim segment
%define zw_sbkb       (os88_image_end + 142)  ; word: its size in KB
%define zw_sbhead     (os88_image_end + 144)  ; word: ring write cursor, in lines
%define zw_sbcount    (os88_image_end + 146)  ; word: lines held
%define zw_sbtop      (os88_image_end + 148)  ; word: first line on screen
%define zw_dirty      (os88_image_end + 150)  ; byte: the window needs a repaint
%define zw_lbuf       (os88_image_end + 152)  ; 256: the line being assembled
%define zw_lbn        (os88_image_end + 408)  ; word: bytes in it
%define zw_wbuf       (os88_image_end + 410)  ; 128: the word being wrapped
%define zw_wbn        (os88_image_end + 538)  ; word: bytes in it

; --- 5. v6: eight windows in pixel coordinates (zwin6.inc) -------------------
; ZW6_REC is a whole window record; there are eight, indexed by the story.
ZW6_X       equ 0                             ; word: left, pixels
ZW6_Y       equ 2                             ; word: top
ZW6_W       equ 4                             ; word: width
ZW6_H       equ 6                             ; word: height
ZW6_CX      equ 8                             ; word: cursor x within it
ZW6_CY      equ 10                            ; word: cursor y
ZW6_ML      equ 12                            ; word: left margin
ZW6_MR      equ 14                            ; word: right margin
ZW6_STYLE   equ 16                            ; byte: ZST_* bits
ZW6_FG      equ 17                            ; byte
ZW6_BG      equ 18                            ; byte
ZW6_FONT    equ 19                            ; byte
ZW6_ATTR    equ 20                            ; word: wrapping/scrolling flags
ZW6_CNT     equ 22                            ; word: line count for [MORE]
ZW6_REC     equ 24
%define zw6_wins      (os88_image_end + 540)  ; 8 * ZW6_REC = 192
%define zw6_cur       (os88_image_end + 732)  ; byte: the selected window

; --- 6. Input and the request handshake (zio.inc) ----------------------------
; zi_ring is written by the UI task's key callback and read by the worker.
; A byte ring with separate head and tail indices needs no lock on an 8086:
; each side writes exactly one of the two cursors and byte writes are atomic
; against an interrupt.
ZI_RING_N   equ 32
%define zi_ring       (os88_image_end + 734)  ; ZI_RING_N bytes: ASCII
%define zi_rings      (os88_image_end + 766)  ; ZI_RING_N bytes: scan codes
%define zi_rhead      (os88_image_end + 798)  ; byte: UI writes here
%define zi_rtail      (os88_image_end + 799)  ; byte: the worker reads here
%define zi_line       (os88_image_end + 800)  ; 256: the line being typed
%define zi_linen      (os88_image_end + 1056) ; word: characters in it
%define zi_lmax       (os88_image_end + 1058) ; word: what the story will take
%define zi_want       (os88_image_end + 1060) ; byte: ZIW_* the worker is after
%define zi_savseg     (os88_image_end + 1062) ; word: save staging claim
%define zi_savkb      (os88_image_end + 1064) ; word: its size in KB
%define zi_savlen_l   (os88_image_end + 1066) ; word: bytes staged, low
%define zi_savlen_h   (os88_image_end + 1068) ; word: bytes staged, high
%define zi_undoseg    (os88_image_end + 1070) ; word: @save_undo snapshot claim
%define zi_undokb     (os88_image_end + 1072) ; word: its size in KB
%define zi_undook     (os88_image_end + 1074) ; byte: a snapshot is held
%define zi_name       (os88_image_end + 1076) ; 13: the file the dialog named
%define zi_story      (os88_image_end + 1089) ; 13: the story's own file name,
                                              ;     which seeds the save name

; --- 7. Pictures (zpic.inc) --------------------------------------------------
%define zp_seg        (os88_image_end + 1104) ; word: .PIX claim, 0 = none
%define zp_kb         (os88_image_end + 1106) ; word: its size in KB
%define zp_count      (os88_image_end + 1108) ; word: pictures in the archive
%define zp_dirlen     (os88_image_end + 1110) ; word: bytes of directory

; --- 8. Sound (zsnd.inc) -----------------------------------------------------
%define zs_caps       (os88_image_end + 1112) ; word: OSAPI_SND_CAPS, cached
%define zs_capsok     (os88_image_end + 1114) ; byte: we have asked

; --- 9. Scratch shared by the modules ----------------------------------------
; One 64-byte scratch, owned by whoever is running: the worker's own stack is
; 256 bytes (SCH_STACK) and shared with the tick, the mouse and any sound IRQ,
; so a module that needs a buffer takes it from here rather than from SP.
%define zf_scratch    (os88_image_end + 1116) ; 64 bytes

; --- 10. Per-module private slack --------------------------------------------
; A module that needs state this file did not anticipate takes it from ITS OWN
; range and from no other. The ranges exist because the alternative - every
; module appending to the end - makes the total a thing you can only compute by
; assembling, and OS88_BSS needs it before that (SPEC.md 20.5). A module that
; overruns its range must come back HERE and say so in the same edit that
; raises ZF_BSS_TOTAL, which is exactly the review this arrangement is for.
; What a halt reports. Shared, because zmem.inc refuses a seek off the end of
; the story and zexec.inc refuses an opcode it does not know, and zmem is
; included first - a %define must precede its use even though it expands at
; the use site.
%define zx_badop   (os88_image_end + 1174)          ; byte: the opcode, or
                                                    ;   0xFD a bad seek
                                                    ;   0xFE a bad return
%define zx_badpc_l (os88_image_end + 1176)          ; word: and the address
%define zx_badpc_h (os88_image_end + 1178)
%define zx_badseg  (os88_image_end + 1172)          ; word: ES at the fault

ZMEM_SLACK   equ 1180                         ; 64
ZTEXT_SLACK  equ 1244                         ; 128
ZOBJ_SLACK   equ 1372                         ; 64
ZDICT_SLACK  equ 1436                         ; 192
ZWIN_SLACK   equ 1628                         ; 256
ZWIN6_SLACK  equ 1884                         ; 128
ZPIC_SLACK   equ 2012                         ; 128
ZSND_SLACK   equ 2140                         ; 64
ZIO_SLACK    equ 2204                         ; 256
ZEXEC_SLACK  equ 2460                         ; 512

; zio.inc's zi_yield reads this and zexec.inc owns the region, but the define
; has to be HERE: zbss is included first and a %define must precede its use.
%define zf_sdelta  (os88_image_end + ZEXEC_SLACK + 76)  ; word: the RUNNING
                                    ; total of paragraphs the heap compactor
                                    ; has slid the story by (SPEC.md 66.5.9).
                                    ; Only ever DIFFERENCED across one wait,
                                    ; so wrapping is not a case to handle

ZF_BSS_TOTAL equ 2976
