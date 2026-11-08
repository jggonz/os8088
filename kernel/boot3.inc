; =============================================================================
; boot3.inc - `.boot3`, THE THIRD BOOT-ONLY HOME (SPEC.md 2.9.15,
; docs/plans/BOOT3-PLAN.md)
;
; A HOME, NOT A STAGE. Nothing jumps here and there is no boot/boot3.asm:
; `boot/boot.asm` (stage 1) reads `.boot2` (boot/boot2.asm, the blob), `.boot2`
; reads KERNEL.SYS, and this section is KERNEL.SYS's LAST part - so it rides
; stage 2's packed read (or boothd's, or NOKZIP's flat one) with no loader
; change, is expanded with everything else by kz_all, and lands on RAM nothing
; claims until mem_unblob_x. Its bodies are far-entered, end in `retf`, and run
; with CS = B3_SEG, DS = KERNEL_SEG, SS = LOW_SEG - a `.boot2` body's contract
; exactly. They reach `.text` through the KERNEL_SEG shims, `.cold` by a plain
; `call COLD_SEG:x` (re-pointed by cfx_slide's fourth group, and on a kernel
; the ROM or COLDHMA.SYS adopts, by their rt_b3 list), and the blob by B2CALL.
; Callers use B3CALL (kernel.asm, beside BLOBCALL); a body is placed with
; B3SECT. tools/os88ovlchk.py's rules B1-B9 are the fences, all at build time.
;
; THE LIFETIME, the same on every kernel:
;
;   moment                                  what is in this section's RAM
;   stage 1, the splash's first tick,       packed bytes, or nothing yet -
;     the tail's read                         NOT CALLABLE (rule B2's cone)
;   kz_all returns (stage 2's end, after    expanded and aboard
;     the canary; boothd's kz_hd likewise)
;   kmain -> SPLGATE1 kmain_o -> its calls  live: mem_init_x raised [mem_base]
;                                             past the blob + BOOT2_RES, so no
;                                             claim, read-ahead or buffer
;                                             lands on it
;   kmain zeroes [spl_fseg], mem_unblob_x   DROPPED - the one `mov [mem_base],
;                                             ax` that gives back the blob, its
;                                             lift, and this, for 0 new bytes
;
; WHERE, per arm (BOOT3-PLAN 3.2, 5):
;
;   kern_big, kern_emu   B3_RAM is VGABUF_RAM - the paragraph past `.cold`'s
;   (COLD_SLIDE)         rung - and the section runs over `.vgabuf`'s
;                        KVGABUF_SIZE bytes, LADDER_PACK's rounding to KERN_END
;                        and the blob's lift. On a VGA or an EGA machine at
;                        home the planar decoder owns the first KVGABUF_SIZE
;                        bytes during kmain_o, so only the slide prefix
;                        (cfx_slide, cfx_grp, cfx_tab), which runs only on a
;                        machine with neither card, may sit there: every other
;                        entry follows b3_live, which is asserted past it
;   NO_COLD_SLIDE knob   just PAST `.vgabuf`'s KVGABUF_SIZE bytes, to the
;                        paragraph: there is no slide to put in them, so
;                        nothing of this section may share them
;   kern_small           the same rule, and KVGABUF_SIZE is 0 there: `.cold`'s
;                        end, to the paragraph - KERN_END's rounding and the
;                        lift
;   kern_mega and the    KERN_END, behind a zero gap in the file from `.mega`'s
;   NO_ROM_COLD knob     end (or `.ovl`'s) that packs to almost nothing: past
;                        `.mega`'s park scratch and `.vgabuf` rather than over
;                        them, so no b3_live rule applies there. Every kern_mega
;                        body is REAL mode: mega_enter runs after mem_unblob_x
;
; Before this section existed it was `.cfx`, the slide's own list and three
; bodies the blob had no room for (MOVABLE-COLD-PLAN 4); the section was
; renamed and its labels kept their names, as did tools/os88cfx.py.
; =============================================================================

; --- THE KERNEL'S OWN LIST of the words outside `.cold` that name its
; segment - in `.text`, the window half, the blob and this section - which
; the slide re-points (docs/plans/MOVABLE-COLD-PLAN.md 4). ZEROS AS ASSEMBLED:
; tools/os88cfx.py finds the words by moving COLD_CS and stamps them in, so
; the list is always this assembly's own. It is wanted once, at the boot,
; before mem_init; a resident copy would be the bytes the slide exists to give
; back, and the blob had no sector for it that did not cost 360KB and 720KB a
; whole int 13h call (tests/unit/t_blobruns.py --sectors 14).
%ifndef B3_FOOT                ; ===== THE HEAD, %included where the section
                                ; is declared (kernel.asm), so that it is the
                                ; section's FIRST bytes: `-f bin` fills a
                                ; section in the order its blocks are met, and
                                ; every module's B3SECT block is met before
                                ; the foot - `.boot2`'s `jmp boot2_entry` has
                                ; the same reason
; (A FILE offset, every arm on a paragraph, so that B3_RAM below is a segment)
%ifdef KERN_MEGA                ; kern_mega: at KERN_END
B3_START    equ BOOT2_PAD + (KERN_END - KERNEL_SEG) * 16
%elifdef COLD_SLIDE             ; kern_big, kern_emu: `.cold`'s end, which is
B3_START    equ (COLD_START + COLD_SIZE + 15) & ~15  ; VGABUF_RAM
%elifdef ROM_COLD               ; NO_COLD_SLIDE, kern_small: past `.vgabuf`'s
B3_START    equ BOOT2_PAD + (VGABUF_RAM - KERNEL_SEG) * 16 + ((KVGABUF_SIZE + 15) & ~15)
%else                           ; bytes (none on kern_small)
B3_START    equ BOOT2_PAD + (KERN_END - KERNEL_SEG) * 16 ; NO_ROM_COLD: KERN_END
%endif
B3_RAM      equ KERNEL_SEG + (B3_START - BOOT2_PAD) / 16 ; ...so a segment
B3_SEG      equ B3_RAM              ; the segment its bodies run at: constant
B3_RES      equ 0                   ; its nobits scratch, `.boot3b`: none yet
                                    ; (BOOT3-PLAN wave 2 brings ovc_buf)
section .boot3                  ; (already open where kernel.asm declares it;
                                ; said again for the source scanners)
%ifdef COLD_SLIDE
CFX_MAX     equ 256
; -----------------------------------------------------------------------------
; cfx_slide - move `.cold` down over the VGA rung (MOVABLE-COLD-PLAN 2, 4)
; in:  far-called from kmain_o (CS = B3_SEG: this is `.boot3`, transient like
;      the list beside it), DS = KERNEL_SEG, `.cold` at home, cfx_tab stamped
; out: `.cold` runs at COLD_SEG - VGA_PARA, and every word outside it that
;      named its segment - [api_coldseg] among them - names the new one;
;      every register preserved
;
; IN `.boot3` AND NOT THE BLOB: the blob had 78 bytes and this needs ~100, so
; it rides beside its own list in the RAM nothing claims until mem_unblob.
; ~105 ms on a 4.77 MHz 8088, the `rep movsw` of `.cold` (ESTIMATED off
; PERFORMANCE.md Set 117.2's 13.3 cycles a byte). Downward and overlapping,
; so DF = 0 copies it correctly. The list is read where stage 2 left it,
; above the kernel, which the copy - going DOWN - cannot reach.
; -----------------------------------------------------------------------------
cfx_slide:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov bp, [spl_fseg]          ; the blob's segment, for its own group
    mov ax, COLD_RAM
    mov ds, ax
    mov ax, COLD_RAM - VGA_PARA
    mov es, ax
    xor si, si
    xor di, di
    mov cx, (COLD_SIZE + 1) / 2
    cld
    rep movsw
    mov ax, B3_SEG
    mov ds, ax                  ; DS:SI = cfx_tab, the four groups in order
    mov si, cfx_tab
    mov dx, -VGA_PARA           ; what every listed word gains
    mov ax, KERNEL_SEG          ; `.text`...
    call cfx_grp
    mov ax, FAT_SEG             ; ...the window half...
    call cfx_grp
    mov ax, bp                  ; ...this blob...
    call cfx_grp
    mov ax, cs                  ; ...and this section, the third home's own
    call cfx_grp                ; `call COLD_SEG:` sites (BOOT3-PLAN 4.1)
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; cfx_grp - one group of cfx_tab: AX = its segment, DS:SI = its stream, DX =
; the delta. A byte 1-255 is the step to the next word, a 0 is followed by the
; step as a word, and a word of 0 ends it (tools/os88cfx.py's format). Leaves
; SI past the group; clobbers AX, BX, ES
cfx_grp:
    mov es, ax
    xor bx, bx
.n:
    lodsb
    or al, al
    jz .w
    xor ah, ah
    jmp short .a
.w:
    lodsw
    or ax, ax
    jz .done
.a:
    add bx, ax
    add [es:bx], dx
    jmp short .n
.done:
    ret
cfx_tab:    times CFX_MAX db 0
cfx_tab_end:
%endif
; --- b3_live: EVERY ENTRY BELOW THIS LINE may be called on any machine ------
; Where there is a slide (COLD_SLIDE) the first KVGABUF_SIZE bytes of this
; section are `.vgabuf`'s RAM too (VGABUF_RAM is B3_RAM): the slide is done with them before the planar
; decoder's first use, and only the slide - which runs on a machine with
; neither a VGA nor an EGA - may live there (SPEC.md 39.22). The guard below
; holds every later entry past them.
b3_live:
%else                           ; ===== THE FOOT, %included at kernel.asm's
                                ; foot, after every module's B3SECT block:
                                ; the bodies that need the modules' macros
section .boot3
%ifdef KERN_BIG                 ; (kern_big, kern_emu, kern_mega: kern_small's
                                ; blob has its own desk grid, no capture and no
                                ; SYSTEM.CFG reader)
; -----------------------------------------------------------------------------
; cfx_grid - desk_rowcalc's geometry for the BOOT (desk.inc's DESK_GRID): the
;            blob's bvw_rowcalc far-calls it and then reflows (kernel size pass
;            15). All registers preserved but flags.
;
; HERE because the blob had nine bytes and `.boot3` is the RAM nothing claims
; until mem_unblob - which is after kmain_o's last call - and past b3_live.
; It names no segment, so os88cfx has nothing to stamp in it.
; -----------------------------------------------------------------------------
cfx_grid:
    push ax
    push bx
    push cx
    push dx
    push si
    DESK_GRID
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf
; cfx_ctxcap - vid_ctx_capture for the blob's bvs_disp_init (vidsel.inc's
; VID_CTXCAP), here for cfx_grid's reason
VID_CTXCAP cfx_ctxcap, retf
; cfx_cfgname - SYSTEM.CFG's name staged for the blob's ovc_load (driver.inc's
; CFGN_STAGE), here for cfx_grid's reason (kernel size pass 16)
CFGN_STAGE cfx_cfgname, retf
%endif
b3_end:
B3_SIZE     equ b3_end - $$

; --- the guards (BOOT3-PLAN 13) ----------------------------------------------
%ifdef COLD_SLIDE
 %if b3_live - $$ < KVGABUF_SIZE
  %error "a `.boot3` entry lies in the RAM `.vgabuf` shares with it - put it after b3_live (SPEC.md 39.22)"
 %endif
%else                           ; ...and where no slide shares it, the section
 %if B3_RAM < VGABUF_RAM + (KVGABUF_SIZE + 15) / 16 && B3_RAM + (B3_SIZE + 15) / 16 > VGABUF_RAM
  %error "`.boot3` overlaps `.vgabuf`'s bytes on a kernel with no slide to put there (SPEC.md 39.22)"
 %endif
%endif
%if B3_SIZE + B3_RES > 0xFFF0
  %error "`.boot3` has outgrown one segment"
%endif
%if B3_RAM * 16 + B3_SIZE + B3_RES > BLOB_SEG_AT * 16
  %error "`.boot3` lands on stage 2's blob - the derived BLOB_LIFT (kernel.asm) should have prevented this"
%endif
%ifdef KZIP
 %if KZ_MARGIN_K != KZ_MARGIN
  %error "kernel.asm's KZ_MARGIN_K mirrors boot2.asm's KZ_MARGIN: the derived lift is wrong without it"
 %endif
%endif
%endif                          ; (B3_FOOT)
section .text
