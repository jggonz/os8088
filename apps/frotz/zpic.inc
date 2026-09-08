; =============================================================================
; os8088 - apps/frotz/zpic.inc
;
; PICTURES: the v6 picture opcodes' back end (Standard 8.8.6, and the
; @draw_picture / @picture_data definitions in section 15).
;
; ONE DECISION SHAPES THIS WHOLE FILE: **drawing a picture must be a seek and
; a single OSAPI_GFX_BLIT4 call, with no decoder anywhere near it.** That is
; not a performance preference, it is what the task boundary allows.
; @draw_picture is executed by the VM, and the VM is a worker task
; (docs/plans/completed/FROTZ-PLAN.md 4), and a worker may not touch a file slot and may not
; call OSAPI_MEM_* (SPEC.md 20.6). So everything a draw needs - the pixels,
; in the exact layout the blitter wants - has to be resident, claimed and
; validated by the UI task before the worker ever asks. zp_load does all of
; that; zp_draw does arithmetic and one far call.
;
; -----------------------------------------------------------------------------
; 1. THE .PIX ARCHIVE - the format this file defines, and tools/os88pix.py
;    must emit byte for byte
; -----------------------------------------------------------------------------
; Built on the host from PNG, JPEG or a Blorb's picture chunks. It is not a
; compressed format and is not meant to be one: it is the pixels already
; packed the way OSAPI_GFX_BLIT4 reads them, so the guest never decodes.
;
; **It is LITTLE-ENDIAN, and it is the only file Frotz reads that is.** Every
; other word in this program is a big-endian Z-machine word that zmem.inc
; alone forms (docs/plans/completed/FROTZ-PLAN.md 3.1). This one is ours, written by our own
; tool for one machine, and matching the 8086 costs the guest nothing while
; matching the Z-machine would cost an xchg per field.
;
;   Header, 16 bytes at offset 0
;     +0   5   'O','8','P','I','X'
;     +5   1   format version - ZP_FMT_VER (1). A different value is refused
;              whole rather than read hopefully.
;     +6   2   picture count N (1..ZP_MAXPIC)
;     +8   2   release number - what @picture_data reports for picture 0
;              (Standard 15, picture_data: word 1 of the array)
;     +10  2   directory entry size in bytes; must be ZP_ENT (16). It is in
;              the file so a future tool can grow the entry and be refused by
;              THIS interpreter instead of misread by it.
;     +12  4   reserved, zero
;
;   Directory, N entries of 16 bytes, starting at offset 16, ascending by
;   picture number (ascending for the tool's determinism - the guest scans
;   linearly, so nothing here depends on the order)
;     +0   2   picture number, 1..65535. Picture 0 is never stored: the
;              Standard reserves it for the "how many pictures are there"
;              query, and numbering "is not necessarily contiguous"
;              (Standard 8.8.6.1), which is why this is a scan and not an
;              index.
;     +2   2   width in pixels
;     +4   2   height in pixels
;     +6   2   stride: bytes from one pixel row to the next, >= (width+1)/2.
;              Carried rather than derived so the tool may pad rows.
;     +8   4   byte offset of the pixel block from the start of the file.
;              **Must be a multiple of 16**, so the block is addressed as
;              claim_segment + (offset >> 4) with offset 0 and the blitter's
;              SI never has to carry a paragraph remainder.
;     +12  4   reserved, zero
;
;   Pixel blocks follow the directory, each on a 16-byte boundary, each
;   stride*height bytes of packed 4bpp - two pixels per byte, HIGH NIBBLE
;   LEFTMOST, colour indices 0..15 in os88's palette (CBLACK..CWHITE).
;
;   **stride * height must be under 64KB.** The block is reached as one
;   segment at offset 0, so a bigger one would wrap SI mid-picture. The host
;   tool must refuse an oversized picture rather than emit one; the guest
;   refuses it too (zp_find), because every byte read off a disk is hostile.
;
;   There is no transparency and no per-picture palette. OSAPI_GFX_BLIT4
;   writes every pixel it is given and os88 has one fixed 16-colour palette,
;   so a transparency flag here would be a promise the drawing path cannot
;   keep - the tool composites against the window background instead (SPEC.md
;   47: refuse, or do it properly; never half).
;
; -----------------------------------------------------------------------------
; 2. INFOCOM .MG1/.MG2 - parsed, and honestly refused
; -----------------------------------------------------------------------------
; The picture files that ship beside Zork Zero, Arthur, Journey and Shogun.
; Their container is documented and this file parses it (zp_probe_mg1):
;
;     header, 16 bytes, LITTLE-endian words
;       +0  1  file number      +1  1  flags        +2  2  unused
;       +4  2  image count      +6  2  link         +8  1  directory entry size
;       +9  1  padding         +10  2  checksum    +12  2  unused
;      +14  2  version / release
;     directory entry, `entry size` bytes, little-endian words EXCEPT the
;     3-byte offsets, which are BIG-endian
;       +0  2  picture number   +2  2  width        +4  2  height
;       +6  2  flags            +8  3  data offset  +11 3  colour-map offset
;                                                          (entry size >= 14)
;
; **The pixel data is NOT RLE.** docs/plans/completed/FROTZ-PLAN.md 6 says "RLE over a 4-bit
; palette; the decoder is small" and both halves of that sentence are wrong -
; it is an LZW variant with 9-to-12-bit codes, 256 = clear table, 257 = end,
; and a 3840-entry value/back-reference table whose chains are emitted in
; reverse through a 512-byte buffer. The plan should be corrected.
;
; So the refusal below is NOT "we could not work the format out". It is
; arithmetic, and it is the arithmetic SPEC.md 47 asks to be stated:
;
;   - the LZW dictionary is 3840 values + 3840 back-references = 11,520 bytes
;     of tables that must be live for the whole decode;
;   - the decoder emits ONE BYTE PER PIXEL and cannot pack to 4bpp until a
;     whole row exists, so a 320x200 Infocom picture needs a 64,000-byte
;     canvas as well;
;   - and the compressed stream is read from the file AT DRAW TIME, which is
;     worker time, where there is no file slot and no OSAPI_MEM_*. The only
;     way round that is to make the whole .mg1 resident - 200 to 400KB - next
;     to a 300KB v6 story on a 640KB machine.
;
; That does not fit, on the machine this OS is for, by a margin no tuning
; closes. So zp_probe_mg1 reads the header and walks the directory (cheap: it
; proves the file really is picture art and how much of it there is), records
; what it found for zp_reason to say out loud, and leaves the picture
; subsystem reporting NO DRAWABLE PICTURES.
;
; Reporting none is the right answer rather than a cop-out. @picture_data is
; required to be able to answer "unavailable" (Standard 8.8.6.1) and stories
; handle it; what a story cannot handle is being told a picture is 200 pixels
; tall and then never seeing it, because it will lay a graphical interface
; out around blank space. Truthful and useless beats plausible and wrong.
;
; -----------------------------------------------------------------------------
; 3. WHAT THIS MODULE TOUCHES THAT IS NOT ITS OWN
; -----------------------------------------------------------------------------
; [zi_story] (zio.inc), READ ONLY, to derive the art's file name - so zi_load
; must fill it before it calls zp_load - and [zf_win] (frotz.asm) for the
; content rectangle. Nothing else, and nothing writes into another module's
; block (apps/frotz/zbss.inc).
; =============================================================================

; --- the .PIX format's own constants -----------------------------------------
ZP_FMT_VER  equ 1                   ; header +5
ZP_HDR      equ 16                  ; bytes of header before the directory
ZP_ENT      equ 16                  ; bytes per directory entry
ZP_MAXPIC   equ 1024                ; a count above this is a corrupt header,
                                    ; not an ambitious artist: 1024 entries is
                                    ; already a 16KB directory
ZP_MAXKB    equ 256                 ; the largest archive we will claim for.
                                    ; A cover image is a few KB; this is the
                                    ; bound that keeps a wrong name from
                                    ; eating the heap, not a design target.

; --- where the art came from, which is what zp_reason reports ---------------
ZP_SRC_NONE equ 0
ZP_SRC_PIX  equ 1                   ; a .PIX archive is claimed and validated
ZP_SRC_MG1  equ 2                   ; Infocom art is present and undecodable

; --- the .mg1 probe ----------------------------------------------------------
ZP_PROBE    equ 8192                ; bytes claimed to read an .mg1's head.
                                    ; OSAPI_FILE_READ_AT wants a cluster
                                    ; multiple, so this also caps the cluster
                                    ; size we can probe under (16 sectors).
                                    ; 8KB covers the largest real directory:
                                    ; Shogun's ~450 images x 14 bytes + 16.
ZP_MG_HDR   equ 16                  ; .mg1 header length
ZP_MG_EMIN  equ 11                  ; smallest legal .mg1 directory entry
ZP_MG_EMAX  equ 64
ZP_MG_MAXI  equ 2048                ; sanity bound on its image count

; --- our slack in apps/frotz/zbss.inc (ZPIC_SLACK, 128 bytes; 66 used) -------
%define zp_rel        (os88_image_end + ZPIC_SLACK + 0)   ; word: .PIX release
%define zp_src        (os88_image_end + ZPIC_SLACK + 2)   ; byte: ZP_SRC_*
%define zp_mgn        (os88_image_end + ZPIC_SLACK + 4)   ; word: .mg1 images
%define zp_mgrel      (os88_image_end + ZPIC_SLACK + 6)   ; word: its version
%define zp_name       (os88_image_end + ZPIC_SLACK + 8)   ; 14: the derived 8.3
%define zp_ent        (os88_image_end + ZPIC_SLACK + 24)  ; 24: OSAPI_FIND_SZ
%define zp_fsz_l      (os88_image_end + ZPIC_SLACK + 48)  ; word: file size, low
%define zp_fsz_h      (os88_image_end + ZPIC_SLACK + 50)  ; word: ...and high
%define zp_x          (os88_image_end + ZPIC_SLACK + 52)  ; word: zp_draw's x
%define zp_y          (os88_image_end + ZPIC_SLACK + 54)  ; word: ...and y
%define zp_w          (os88_image_end + ZPIC_SLACK + 56)  ; word: zp_find's
%define zp_h          (os88_image_end + ZPIC_SLACK + 58)  ;   unpacked entry:
%define zp_stride     (os88_image_end + ZPIC_SLACK + 60)  ;   size, stride and
%define zp_bseg       (os88_image_end + ZPIC_SLACK + 62)  ;   block segment
%define zp_pseg       (os88_image_end + ZPIC_SLACK + 64)  ; word: probe claim
%define zp_pcap       (os88_image_end + ZPIC_SLACK + 66)  ; word: its read size

; =============================================================================
; zp_load - find the story's picture art, claim it and validate it
; in:  UI TASK ONLY (it claims memory and reads a file). [zi_story] holds the
;      story's own 8.3 name; the art is looked for BESIDE it, in whatever
;      folder the file dialog left this instance standing in (SPEC.md 19.2.1).
; out: CF=0 pictures are available; CF=1 none are. Preserves all registers.
;
; Tries STORY.PIX first and STORY.MG1 second, because the first is drawable
; and the second is only a fact to report. Calling it twice is safe: the
; previous claim is released first, so opening a second story cannot leak the
; first one's art.
; =============================================================================
zp_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call zp_free                    ; a second story replaces the first
    mov byte [zp_src], ZP_SRC_NONE
    mov word [zp_mgn], 0
    mov word [zp_mgrel], 0

    ; v6 ONLY, AND THE REASON IS THE CLOCK. @draw_picture and @picture_data are
    ; v6 opcodes (Standard 15), so art beside a v3 or v5 story is art no story
    ; can ask for - and looking for it is not free. OSAPI_FILE_FIND is BY
    ; ORDINAL and dsk_find re-walks the directory from the front on every call,
    ; with no sector cache under it, so a folder of eleven stories costs about
    ; eleven int 13h calls to answer "no" - and this asks twice, .PIX then
    ; .MG1. At PERFORMANCE.md's ~400 ms a call that is the better part of ten
    ; seconds added to opening EVERY story on the target machine, to find
    ; something only one version can use.
    cmp byte [zf_ver], 6
    jne .none

    mov si, zp_ext_pix
    call zp_mkname
    jc .none                        ; no story name: nothing to derive from
    call zp_findfile
    jc .mg1
    call zp_load_pix
    jnc .yes

.mg1:
    mov si, zp_ext_mg1
    call zp_mkname
    jc .none
    call zp_findfile
    jc .none
    mov byte [zp_src], ZP_SRC_MG1   ; set from the file EXISTING, so a cluster
    call zp_probe_mg1               ; too big to probe still reports honestly
.none:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop es
    pop di
    pop si                          ; SI. The prologue pushes seven registers
    pop dx                          ; and this popped six, so zp_load returned
    pop cx                          ; to whatever the caller's SI happened to
    pop bx                          ; be. It survived review because NOTHING
    pop ax                          ; CALLED IT: zi_load filled [zi_story] and
    ret                             ; never asked for the art, so every story
                                    ; ran with @picture_data answering "none"
                                    ; and the imbalance was unreachable. Two
                                    ; defects holding each other up - the gate
                                    ; in tests/frotz/zpictest.inf is what
                                    ; separated them.

; =============================================================================
; zp_info - @picture_data's answer for one picture (Standard 15, picture_data)
; in:  AX = picture number
; out: CX = height, DX = width, CF=0 the picture exists; CF=1 it does not, and
;      CX = DX = 0. Preserves every other register.
;
; CF=1 is the answer this must be able to give and give truthfully: with no
; picture file at all, EVERY picture is unavailable, the opcode branches
; false, and a v6 story with its art missing degrades instead of failing.
; =============================================================================
zp_info:
    push ax
    call zp_find
    jc .no
    mov cx, [zp_h]
    mov dx, [zp_w]
%ifdef ZHARNESS
    call zh_pdata                   ; AX = number, DX = width, CX = height
%endif
    pop ax
    clc
    ret
.no:
    xor cx, cx
    xor dx, dx
%ifdef ZHARNESS
    call zh_pdata                   ; 0 x 0 is the "unavailable" answer, and
%endif                              ; a story that lays out around it needs it
    pop ax                          ; said as loudly as a real size
    stc
    ret

; =============================================================================
; zp_count_get - @picture_data's answer for picture 0
; in:  nothing
; out: AX = the number of DRAWABLE pictures, BX = the archive's release number
;
; "Drawable" is the load-bearing word. An .mg1 that parsed answers 0 here, on
; purpose: the Standard has the story branch on whether any pictures are
; available (Standard 15, picture_data), and answering with a count we cannot
; then draw would send it down its graphical path to look at nothing.
; =============================================================================
zp_count_get:
    mov ax, [zp_count]
    mov bx, [zp_rel]
    ret

; =============================================================================
; zp_draw - @draw_picture (Standard 15: "the picture with the given number",
;           top-left at (y,x))
; in:  AX = picture number, CX = x, DX = y, both CONTENT-RELATIVE pixels with
;      the origin at the top-left of our window's content rect. The v6 window
;      the story named is zwin6.inc's to resolve - by the time this is called
;      the coordinate is ours, not the Z-machine's.
;      **The gfx lock must be HELD by the caller** (it is not reentrant, so a
;      lock here would deadlock the drawing sequence this sits inside), and ES
;      is the story segment.
; out: nothing; preserves every register including ES.
;
; It draws nothing at all rather than partially when the picture number is
; unknown, which is what makes an "illegal" picture number survivable instead
; of fatal.
;
; CLIPPING. OSAPI_GFX_BLIT4 emits one gfx_hline per coalesced run, so it
; honours an armed clip region per pixel for free (kernel/vga12.inc) - there
; is nothing to gate. What it does NOT know is where our window ends, so the
; clamp below is ours to do: a picture larger than the content would otherwise
; paint across the desktop. Clamping CX/DX crops right and bottom; a negative
; or off-content origin is refused whole by the unsigned compares, which cost
; nothing and catch a negative as an implausibly large x.
; =============================================================================
zp_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov [zp_x], cx
    mov [zp_y], dx
%ifdef ZHARNESS
    push ax                         ; what the STORY asked for, before any of
    push si                         ; our clamping - so the host can tell a
    mov si, zh_m_pic                ; picture we cropped from one we dropped.
    call zh_evbeg                   ; AX is BANKED because zp_find below reads
    call zh_evnum                   ; the picture number out of it, and an
    mov ax, [zp_x]                  ; instrument that changes the answer is
    call zh_evnum                   ; worse than no instrument at all
    mov ax, [zp_y]
    call zh_evnum
    call zh_evend
    pop si
    pop ax
%endif
    call zp_find                    ; -> [zp_w]/[zp_h]/[zp_stride]/[zp_bseg]
%ifdef ZHARNESS
    jnc .have                       ; the refusal gets a marker, so the shipped
    push si                         ; build's `jc .out` has to stay EXACTLY
    mov si, zh_m_picno              ; that: restructuring it for both builds
    call zh_ev1                     ; grew the shipped package by two bytes,
    pop si                          ; which is a harness leaking into a disk
    jmp .out
.have:
%else
    jc .out
%endif

    mov bx, [zf_win]
    or bx, bx
    jz .out
    call OSAPI_WM_GEOM              ; CX = content w, DX = content h, CF=1 hidden
    jc .out

    mov ax, [zp_x]
    cmp ax, cx
    jae .out                        ; origin is off the right edge
    sub cx, ax                      ; CX = pixels available to the right
    mov ax, [zp_y]
    cmp ax, dx
    jae .out                        ; ...or below the last row
    sub dx, ax                      ; DX = rows available below

    cmp cx, [zp_w]                  ; crop to the picture's own size
    jbe .wok
    mov cx, [zp_w]
.wok:
    cmp dx, [zp_h]
    jbe .hok
    mov dx, [zp_h]
.hok:
    push cx
    push dx
    mov bx, [zf_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, dx
    add ax, [zp_x]                  ; AX = destination x, screen coords
    add bx, [zp_y]                  ; BX = destination y
    pop dx
    pop cx

%ifdef ZHARNESS
    push ax                         ; the pixels this blit is about to write,
    push bx                         ; in SCREEN coordinates: the host can go
    push cx                         ; and photograph exactly this rectangle
    push dx
    push si
    mov si, zh_m_picat
    call zh_evbeg
    call zh_evnum                   ; AX = destination x
    mov ax, bx
    call zh_evnum                   ; ...y
    mov ax, cx
    call zh_evnum                   ; ...width, after the clamp
    mov ax, dx
    call zh_evnum                   ; ...and height
    call zh_evend
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
%endif
    mov bp, [zp_stride]             ; BP is a VALUE here, never a base - it
                                    ; addresses SS if it is dereferenced
    mov es, [zp_bseg]               ; ES is ours for this call: no stub is
    xor si, si                      ; involved, and the block starts a segment
    call OSAPI_GFX_BLIT4
.out:
    pop es                          ; the story segment back, before the VM
    pop bp                          ; touches ES:SI again (FROTZ-PLAN 3.1)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zp_reason - what to tell the reader about this story's pictures
; in:  nothing
; out: SI = a NUL string in OUR segment, or 0 when there is nothing worth
;      saying. Preserves every other register.
;
; Not part of the inter-module ABI's four routines, and here because the
; alternative is a silent refusal. Section 2 of the banner is a real fact
; about a real file on the disk, and SPEC.md 47's whole point is that a fact
; gets said. zi_load may pass this to zw_notice; ignoring it is also correct.
; =============================================================================
zp_reason:
    cmp byte [zp_src], ZP_SRC_MG1
    je .mg1
    xor si, si
    ret
.mg1:
    mov si, zp_msg_mg1
    ret

; -----------------------------------------------------------------------------
; zp_find - locate a picture's directory entry and unpack it, checking every
;           field against the file it came out of
; in:  AX = picture number
; out: CF=0 and [zp_w]/[zp_h]/[zp_stride]/[zp_bseg] set; CF=1 no such picture.
;      Preserves every register.
;
; The validation is not paranoia about our own tool. The archive is a file on
; a floppy the user may have written with anything, and the outputs feed a
; blitter that will happily read stride*height bytes from wherever we point
; it (CLAUDE.md: every byte read off a disk is hostile). Four things are
; checked, and each one is a way a bad entry would read outside the claim:
; the stride must cover the width, the block must fit one segment, the offset
; must be paragraph-aligned, and the block must end inside the file.
; -----------------------------------------------------------------------------
zp_find:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp byte [zp_src], ZP_SRC_PIX
    jne .no
    or ax, ax
    jz .no                          ; picture 0 is the count query, never an
                                    ; entry (Standard 8.8.6.1)
    mov cx, [zp_count]
    jcxz .no
    mov es, [zp_seg]
    mov bx, ax                      ; BX = the number we want
    mov di, ZP_HDR                  ; ES:DI = the first directory entry
.scan:
    cmp [es:di], bx
    je .hit
    add di, ZP_ENT
    loop .scan
    jmp short .no

.hit:
    mov ax, [es:di+2]               ; width
    or ax, ax
    jz .no
    mov [zp_w], ax
    mov dx, [es:di+4]               ; height
    or dx, dx
    jz .no
    mov [zp_h], dx
    mov cx, [es:di+6]               ; stride
    mov [zp_stride], cx
    inc ax
    shr ax, 1                       ; AX = the bytes one row of pixels needs
    cmp ax, cx
    ja .no                          ; a stride that does not cover the width

    mov ax, cx
    mul dx                          ; DX:AX = bytes in the whole block
    or dx, dx
    jnz .no                         ; over 64KB: SI would wrap mid-picture
    mov bx, ax                      ; BX = the block's length

    mov ax, [es:di+8]               ; the block's byte offset in the file
    mov dx, [es:di+10]
    test al, 15
    jnz .no                         ; not paragraph-aligned, so it cannot be
                                    ; addressed as a segment at offset 0
    push ax
    push dx
    add ax, bx
    adc dx, 0                       ; DX:AX = one past its last byte
    cmp dx, [zp_fsz_h]
    ja .no2
    jb .inside
    cmp ax, [zp_fsz_l]
    ja .no2
.inside:
    pop dx
    pop ax
    mov cl, 4
    shr ax, cl                      ; offset >> 4, in two halves because a
    mov cl, 12                      ; 20-bit shift has no 8086 instruction
    shl dx, cl
    or ax, dx
    add ax, [zp_seg]
    mov [zp_bseg], ax
    clc
    jmp short .out
.no2:
    pop dx
    pop ax
.no:
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

; -----------------------------------------------------------------------------
; zp_mkname - derive "<story base>.<ext>" into [zp_name]
; in:  SI = a 3-character extension in our segment
; out: CF=0 and [zp_name] is a NUL 8.3 name; CF=1 there is no story name.
;      Preserves every register.
;
; Uppercased on the way through because OSAPI_FILE_FIND reports the 8.3
; DISPLAY name (SPEC.md 19), which is uppercase, and this name is compared
; against that rather than passed to a case-folding resolver.
; -----------------------------------------------------------------------------
zp_mkname:
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, si                      ; BX = the extension
    mov si, zi_story
    mov di, zp_name
    mov cx, 8
.base:
    mov al, [si]
    inc si
    or al, al
    jz .dot
    cmp al, '.'
    je .dot
    cmp al, 'a'
    jb .keep
    cmp al, 'z'
    ja .keep
    sub al, 32
.keep:
    mov [di], al
    inc di
    loop .base
.dot:
    cmp di, zp_name
    je .empty
    mov byte [di], '.'
    inc di
    mov si, bx
    mov cx, 3
.ext:
    mov al, [si]
    inc si
    mov [di], al
    inc di
    loop .ext
    mov byte [di], 0
    clc
    jmp short .out
.empty:
    mov byte [zp_name], 0
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zp_findfile - is [zp_name] in this instance's current folder, and how big?
; in:  [zp_name]
; out: CF=0 and [zp_fsz_l]/[zp_fsz_h] = its size in bytes; CF=1 not there.
;      Preserves every register.
;
; The size is the whole point of asking: it is what lets the claim be exactly
; the file's size (SPEC.md 38.6) instead of a guess we would have to cap.
; OSAPI_FILE_FIND is by ORDINAL and keeps no cursor, so CX carries the walk.
; ES:DI is the caller's buffer with no stub in the way - the slot is hand
; written and never touches ES - so ES is set on every pass.
; -----------------------------------------------------------------------------
zp_findfile:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor cx, cx                      ; ordinal 0 starts the walk
.next:
    push ds
    pop es
    mov di, zp_ent
    call OSAPI_FILE_FIND            ; CX = ordinal in, the next one out
    jc .no
    cmp word [zp_ent + 14], OSAPI_FT_DIR
    jae .next                       ; a folder or the synthesised '..'
    mov si, zp_name
    mov di, zp_ent
    call zp_streq
    jne .next
    mov ax, [zp_ent + 18]           ; the full 32-bit size, not the listing's
    mov [zp_fsz_l], ax              ; word-clamped one
    mov ax, [zp_ent + 20]
    mov [zp_fsz_h], ax
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si                          ; ...and the same missing pop as zp_load,
    pop dx                          ; for the same reason: unreachable code is
    pop cx                          ; unreviewed code. Both are now on a path
    pop bx                          ; tests/frotz/zpictest.inf walks every run
    pop ax
    ret

; -----------------------------------------------------------------------------
; zp_streq - compare two NUL 8.3 names in our segment
; in:  SI, DI = the names
; out: ZF=1 equal. Preserves every register except AL.
; -----------------------------------------------------------------------------
zp_streq:
    push cx
    push si
    push di
    mov cx, 13
.loop:
    mov al, [si]
    cmp al, [di]
    jne .out
    or al, al
    jz .out                         ; both ended together
    inc si
    inc di
    loop .loop
.out:
    pop di
    pop si
    pop cx                          ; pop touches no flags, so ZF survives
    ret

; -----------------------------------------------------------------------------
; zp_load_pix - claim, read and validate a .PIX archive
; in:  [zp_name], [zp_fsz_l]/[zp_fsz_h] from zp_findfile
; out: CF=0 loaded and [zp_src] = ZP_SRC_PIX; CF=1 refused and nothing is held.
;      Module-internal: clobbers freely (zp_load holds the registers).
;
; The claim is rounded to KB the way tracker.asm does it, because the KB slots
; are the native shape and only they work before there is an instance.
; -----------------------------------------------------------------------------
zp_load_pix:
    mov ax, [zp_fsz_l]
    mov dx, [zp_fsz_h]
    cmp dx, 8
    jae .no                         ; over 512KB is not a picture archive
    add ax, 1023                    ; round up to whole KB, in 32 bits, so a
    adc dx, 0                       ; 64KB-crossing size does not wrap
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx                       ; AX = KB to claim
    jz .no
    cmp ax, ZP_MAXKB
    ja .no
    mov [zp_kb], ax
    call OSAPI_MEM_CLAIM            ; AX = KB -> DX = base segment, CF=1 refused
    jc .no
    mov [zp_seg], dx

    mov ax, [zp_kb]                 ; DX:CX = the buffer's capacity in bytes
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    mov cx, ax
    mov es, [zp_seg]
    xor bx, bx                      ; ES:BX = the claim, at its first byte
    mov si, zp_name
    call OSAPI_FILE_READ            ; out CF=0 and DX:AX = the file's size
    push ds
    pop es                          ; ES back before anything reads our data
    jc .free
    cmp dx, [zp_fsz_h]
    jne .free                       ; the disk changed under us between the
    cmp ax, [zp_fsz_l]              ; directory scan and the read
    jne .free
    call zp_check_pix
    jc .free
    mov byte [zp_src], ZP_SRC_PIX
    clc
    ret
.free:
    call zp_free
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; zp_check_pix - validate the archive now resident at [zp_seg]
; out: CF=0 the header and directory are sound, and [zp_count]/[zp_rel]/
;      [zp_dirlen] are filled; CF=1 refuse. Clobbers freely; leaves ES = DS.
; -----------------------------------------------------------------------------
zp_check_pix:
    mov es, [zp_seg]
    cmp word [es:0], 'O8'
    jne .bad
    cmp word [es:2], 'PI'
    jne .bad
    cmp byte [es:4], 'X'
    jne .bad
    cmp byte [es:5], ZP_FMT_VER
    jne .bad                        ; a version we do not know is refused
    cmp word [es:10], ZP_ENT        ; whole, not read hopefully
    jne .bad
    mov ax, [es:6]
    or ax, ax
    jz .bad
    cmp ax, ZP_MAXPIC
    ja .bad
    mov [zp_count], ax
    mov dx, [es:8]
    mov [zp_rel], dx                ; banked before mul takes DX
    mov bx, ZP_ENT
    mul bx                          ; DX:AX = the directory's length
    or dx, dx
    jnz .bad
    mov [zp_dirlen], ax
    add ax, ZP_HDR
    jc .bad
    cmp word [zp_fsz_h], 0
    jne .ok                         ; a file over 64KB certainly holds it
    cmp ax, [zp_fsz_l]
    ja .bad
.ok:
    push ds
    pop es
    clc
    ret
.bad:
    push ds
    pop es
    stc
    ret

; -----------------------------------------------------------------------------
; zp_free - release the archive claim, if there is one
; out: nothing; preserves every register. UI TASK ONLY (OSAPI_MEM_FREE).
; -----------------------------------------------------------------------------
zp_free:
    push ax
    push dx
    cmp word [zp_seg], 0
    je .out
    mov dx, [zp_seg]                ; DX, not AX (SPEC.md 50.3)
    call OSAPI_MEM_FREE
    mov word [zp_seg], 0
    mov word [zp_kb], 0
.out:
    mov word [zp_count], 0          ; cleared unconditionally: "no drawable
    mov word [zp_dirlen], 0         ; pictures" is the state every failure
    mov word [zp_rel], 0            ; path has to land in
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zp_probe_mg1 - read an Infocom picture file's head and parse its container
; in:  [zp_name] names an existing .MG1/.MG2, [zp_fsz_*] its size
; out: nothing; [zp_mgn]/[zp_mgrel] filled when the container parses.
;      Clobbers freely. UI TASK ONLY.
;
; This never makes a picture drawable and is not meant to - banner section 2
; is why. It exists so the refusal can name a number instead of shrugging,
; and so "no art on this disk" and "art this interpreter cannot decode" are
; different sentences.
;
; OSAPI_FILE_READ_AT wants BOTH the offset and the capacity to be cluster
; multiples, so the capacity is the largest multiple of the cluster size that
; fits the probe claim. A cluster bigger than the claim (a large hard-disk
; partition) simply skips the probe: [zp_src] is already ZP_SRC_MG1 from the
; file existing, so the honest answer survives.
; -----------------------------------------------------------------------------
zp_probe_mg1:
    call OSAPI_FILE_DFREE           ; BX = SECTORS per cluster; no disk I/O
    jc .out
    or bx, bx
    jz .out
    cmp bx, ZP_PROBE / 512
    ja .out                         ; a cluster we cannot read a whole one of
    mov ax, bx
    mov cl, 9
    shl ax, cl                      ; AX = bytes per cluster
    mov bx, ax
    mov ax, ZP_PROBE
    xor dx, dx
    div bx                          ; whole clusters that fit the claim
    mul bx
    mov [zp_pcap], ax

    mov ax, ZP_PROBE / 1024
    call OSAPI_MEM_CLAIM
    jc .out
    mov [zp_pseg], dx

    mov es, dx
    xor bx, bx
    mov cx, [zp_pcap]
    xor ax, ax
    xor dx, dx                      ; DX:AX = offset 0, the head of the file
    mov si, zp_name
    call OSAPI_FILE_READ_AT         ; out CF=0 and DX:AX = bytes delivered
    push ds
    pop es
    jc .free
    or dx, dx
    jz .lo
    mov cx, 0xFFFF                  ; more than 64KB: cap the byte count at
    jmp short .have                 ; what a 16-bit bound can express
.lo:
    mov cx, ax
.have:
    call zp_check_mg1
.free:
    mov dx, [zp_pseg]
    call OSAPI_MEM_FREE
    mov word [zp_pseg], 0
.out:
    ret

; -----------------------------------------------------------------------------
; zp_check_mg1 - parse the .mg1 header and walk its directory
; in:  CX = bytes delivered into the probe claim
; out: [zp_mgn] = entries whose data offset lies inside the file, [zp_mgrel] =
;      the file's version word. Clobbers freely; leaves ES = DS.
;
; Header words are LITTLE-endian; the 3-byte data and colour-map offsets in
; each entry are BIG-endian. That is not a guess - it is what Frotz's own DOS
; and X11 front ends both read, independently, and they are the two that shipped
; against real Infocom files.
; -----------------------------------------------------------------------------
zp_check_mg1:
    mov es, [zp_pseg]
    cmp cx, ZP_MG_HDR
    jb .out
    mov bl, [es:8]                  ; directory entry size
    cmp bl, ZP_MG_EMIN
    jb .out
    cmp bl, ZP_MG_EMAX
    ja .out
    xor bh, bh
    mov ax, [es:4]                  ; image count
    or ax, ax
    jz .out
    cmp ax, ZP_MG_MAXI
    ja .out
    push ax
    mul bx                          ; DX:AX = the directory's length
    or dx, dx
    jnz .pop
    add ax, ZP_MG_HDR
    jc .pop
    cmp ax, cx
    ja .pop                         ; the directory did not fit the probe
    pop cx                          ; CX = images, and the byte count is spent
    mov si, ZP_MG_HDR
    xor di, di                      ; DI = entries that check out
.ent:
    push cx
    mov al, [es:si+8]               ; the data offset, 3 bytes BIG-endian
    xor ah, ah
    mov dx, ax
    mov al, [es:si+10]
    mov ah, [es:si+9]
    cmp dx, [zp_fsz_h]
    ja .skip
    jb .in
    cmp ax, [zp_fsz_l]
    jae .skip                       ; the pixels start past the end of the file
.in:
    cmp word [es:si+2], 0           ; width
    je .skip
    cmp word [es:si+4], 0           ; height
    je .skip
    inc di
.skip:
    add si, bx
    pop cx
    loop .ent
    mov [zp_mgn], di
    mov ax, [es:14]
    mov [zp_mgrel], ax
    jmp short .out
.pop:
    pop ax
.out:
    push ds
    pop es
    ret

; --- data --------------------------------------------------------------------
zp_ext_pix: db 'PIX'
zp_ext_mg1: db 'MG1'
zp_msg_mg1: db 'Pictures found, but in Infocom .MG1 format - not shown.', 0
