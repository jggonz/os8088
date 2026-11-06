; =============================================================================
; os8088 - softgfx.inc
;
; THE 1BPP DRIVER, and the reason the kernel can drive a Hercules or a CGA
; card at all (SPEC.md 39.3). Nothing in here touches a VGA port or a latch:
; it does the ALU's work with plain CPU instructions, so pointing it at the
; framebuffer ([vid_rseg]), telling it there is one plane ([vid_planes]) and
; routing its row advances through gfx_nextrow so they follow the banked
; layout IS the Hercules/CGA renderer. No second driver exists.
;
; [vid_mono] is what routes drawing through this module, and on a 1bpp
; adapter it is set before anything draws and never changes.
;
; It was written for a RAM back buffer - four claimed planes on a VGA, with a
; dirty rect and a flush - and that is where the plane machinery below comes
; from. SPEC.md 32 records why the buffer went: it could only ever be armed
; on a VGA with 150KB of free heap, so the machine this project is calibrated
; against could never reach it, while every adapter paid the dispatch, the
; colour tracking and the dirty union on every fill and every glyph. What is
; left is the renderer, which was always the load-bearing half.
;
; RAM has no latches or Set/Reset, so these routines do the VGA ALU's work in
; software, one plane at a time: solid ops or/and the edge masks per the
; [gfx_color] plane bit (interiors are rep stosb of FF/00), XOR ops xor the
; edge masks and `not` whole interior bytes. Plane p lived at segment
; [vid_rseg] + p*<plane stride> with the same rows as VRAM, so gfx_rect_setup's
; masks and offsets serve both worlds; every solid/XOR primitive here funnels
; through it. SPEC.md 39.26 flattened every plane loop in the file, so there
; is only ever plane 0 now and [vid_rseg] IS the target - the stride and the
; end it walked to were published cells, and a constant behind the stride,
; until all three were found to have no reader left. They are gone.
; Pixel/hline/vline are degenerate fills - a rect setup costs one
; mul, same as the dedicated VRAM loops.
;
; No public API slot has its body here: gfx_scroll was the last and moved to
; vga12.inc with the back buffer (SPEC.md 5.5), so every sw_* entry below is
; reached through the [vid_mono] dispatch at a public entry in another file.
;
; ONE BODY HERE IS NOT A sw_* AND IS NOT THIS RENDERER'S: gfx_rect_setup, the
; shared rect geometry, which vga_solid_rect calls as well. It is here because
; it is not VGA-specific and so does not belong in the VGA file - see its own
; header below.
;
; Concurrency: every routine here runs with the gfx lock held (SPEC.md 1.6) -
; vga12's rect scratch needs no interrupt guard. The cursor does not come
; through here at all: mouse.inc goes through vga_save_vram/vga_restore_vram
; and draws straight to the framebuffer (SPEC.md 7.1).
; =============================================================================


; =============================================================================
; THE SHARED RECT GEOMETRY (SPEC.md 5.4) - clip a rect once, and hand BOTH
; renderers the same answers.
;
; IT LIVES HERE AND NOT IN vga12.inc, and the reason is ownership rather than
; tidiness: nothing in this body touches a VGA port, a latch or a plane. It
; computes a clipped row range, a framebuffer offset and two edge bit masks,
; and it is called by vga_solid_rect and by sw_rect alike - so it was in the
; adapter-specific file only because that is where the code it was cut out of
; happened to be. It was left behind at the last split of that file; this is
; the correction. A shared body inside a file that is about to gate its
; contents on "does this machine have a VGA" is the defect that gate would
; otherwise ship (docs/plans/MONO-RECLAIM-PLAN.md 1.1).
;
; It is HOT: PERFORMANCE.md Set 102 profiles gfx_rect_setup.x2ok at 7.3% of
; the whole machine in the screen saver, on a CGA. Nothing here may become a
; far call and nothing here may grow a dispatch.
;
; TWO OF ITS OUTPUTS STAY IN vga12.inc, deliberately - [gfx_lmsk] and
; [gfx_rmsk] are each packed BEHIND a constant 8 so the VGA fills can load the
; mask and the Graphics Controller's Bit Mask index as one word. The masks are
; shared and their PACKING is VGA's; splitting the pair costs the hot fills 3
; bytes and 4 clocks a site, which is a worse trade than a name in the wrong
; file. See the assertion there.
; =============================================================================

; The two edge masks, by x & 7. They were computed with `shr dl,cl` / `shl
; dl,cl`, which on an 8088 is 8 clocks plus 4 PER BIT and costs a push/pop of
; CX to free the count register - about a fifth of gfx_rect_setup for two
; bytes that a table answers outright (PERFORMANCE.md Part 2: the per-call
; floor is CPU-side setup, and this is setup).
; -----------------------------------------------------------------------------
gfx_lmtab:  db 0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01  ; FF >> (x1 & 7)
gfx_rmtab:  db 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF  ; FF << 7-(x2&7)

; -----------------------------------------------------------------------------
; gfx_rect_setup - internal: clip a rect and precompute its geometry
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      CF = 1 if the clipped rect is empty (nothing else valid);
;           else CF = 0 and gfx_ry1/gfx_rrows/gfx_roff/gfx_rspan/gfx_lmsk/
;           gfx_rmsk hold clipped top row, row count, VRAM offset of the
;           top-left byte, right byte - left byte, and the edge bit masks
;           (combined into gfx_lmsk when the rect spans a single byte).
; clobbers: AX, BX, CX, DX, flags
; -----------------------------------------------------------------------------
gfx_rect_setup:
    or bx, bx                   ; y1 < 0 -> 0
    jns .y1ok
    xor bx, bx
.y1ok:
    cmp dx, [vid_ch]            ; y2 past the last row -> the last row
    jl .y2ok
    mov dx, [vid_chm1]
.y2ok:
    cmp dx, bx
    jl .empty
    or ax, ax                   ; x1 < 0 -> 0
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp cx, [vid_cw]            ; x2 past the last column -> the last column
    jl .x2ok
    mov cx, [vid_cwm1]
.x2ok:
    cmp cx, ax
    jl .empty
    mov [gfx_ry1], bx            ; y1 is banked, so BX is the free register the
    sub dx, bx                  ; mask lookups below index through - which is
    inc dx                      ; what retires the two variable shifts and the
    mov [gfx_rrows], dx          ; three push/pops the old sequence needed
    mov bx, ax                  ; left mask = FF >> (x1 & 7)
    and bx, 7                   ; (BH = 0 falls out of the AND)
    mov dl, [bx + gfx_lmtab]
    mov [gfx_lmsk], dl
    mov bx, cx                  ; right mask = FF << (7 - (x2 & 7))
    and bx, 7
    mov dl, [bx + gfx_rmtab]
    mov [gfx_rmsk], dl
    mov dx, cx                  ; DX = x2; [gfx_rrows] already banked it
    mov cl, 3
    shr ax, cl                  ; ax = left byte  (x1 / 8)
    mov bx, ax                  ; ...carried over gfx_rowbase in BX, which it
                                ; PRESERVES on both its arms (viddet.inc), and
                                ; which holds `x2 & 7` here - dead since the
                                ; right-mask lookup two instructions up. It
                                ; was a .bss word whose only two references
                                ; were this store and the add below
    shr dx, cl                  ; dx = right byte (x2 / 8)
    sub dx, ax
    mov [gfx_rspan], dx          ; span = right byte - left byte (`mov` writes
    jnz .offs                   ; no flag: the `sub`'s ZF is the test - kernel
                                ; size pass 17, -2)
    mov dl, [gfx_rmsk]          ; one-byte rect: combine the masks
    and [gfx_lmsk], dl
.offs:
    mov ax, [gfx_ry1]            ; this one call re-bases every rect primitive
    call gfx_rowbase            ; in BOTH renderers - on VGA it is still just
    add ax, bx                   ; y1 * 80 (SPEC.md 39.3) - AND CF = 0: a
    mov [gfx_roff], ax           ; row base plus a byte column is inside one
    ret                          ; framebuffer (under 0x9660 on every adapter
                                 ; there is), so the ADD cannot carry and the
                                 ; clc that followed it restated its answer
.empty:
    stc
    ret

; -----------------------------------------------------------------------------
; ...AND SO IS THE PATTERN STAGING, for the same reason and found by the same
; sweep: gfx_pat_stage copies eight bytes and touches no card at all. Both
; fill_pat bodies call it - gfx_fill_pat_raw's VGA loop and sw_fill_pat - and
; it was in the VGA file on the strength of its old name.
; -----------------------------------------------------------------------------
; gfx_pat_stage - internal: copy the 8 bytes at [gfx_pat] into gfx_patbuf
;                 so the fill loops can index them ([bx+disp] addressing)
; in:       [gfx_pat]
; out:      gfx_patbuf filled
; clobbers: SI, DI, ES, flags
; -----------------------------------------------------------------------------
gfx_pat_stage:
    push ds                     ; DF=0 per SPEC.md 1
    pop es
    mov si, [gfx_pat]
    mov di, gfx_patbuf
    movsw
    movsw
    movsw
    movsw
    ret

; -----------------------------------------------------------------------------
; sw_fill / sw_fill_gray / sw_xor_fill - software twins of gfx_fill,
; gfx_fill_gray and gfx_xor_fill (same register contracts: AX = x1,
; BX = y1, CX = x2, DX = y2; clobbers flags). One shared core, sw_rect,
; parameterized by [sw_mode]:
;
;   SWM_SOLID - per plane, the [gfx_color] bit picks a 00/FF byte; edges
;               become (dest & ~mask) | (value & mask), interiors stosb.
;   SWM_GRAY  - one AA/55 pattern for all planes (color 15/0 per pixel
;               bit), toggled each row via [sw_altm] = FF; same edge math.
;   SWM_XOR   - XOR 0Fh flips every plane identically: edges xor the
;               mask, interior bytes are complemented.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
SWM_SOLID equ 0
SWM_GRAY  equ 1
SWM_XOR   equ 2

; SOLID is the one that falls through, because it is the one the machine
; spends its day on: every window background, every glyph cell's erase, every
; frame. XOR pays the jump instead - selection bands and drag outlines.
sw_xor_fill:
    mov byte [sw_mode], SWM_XOR
    jmp sw_rect

sw_fill_gray:
    mov byte [sw_mode], SWM_GRAY
    jmp sw_rect

sw_fill:
    mov byte [sw_mode], SWM_SOLID
    ; fall through to sw_rect

sw_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call gfx_rect_setup
    jc .done
    ; --- the mode staging and the pass, for the rect gfx_rect_setup has just
    ; described. It was sw_rect_pl, with this one call, until kernel size pass
    ; 17 wrote it here: its call goes, and its tail `jmp sw_plane_op` is the
    ; `call` below, whose ret comes back to the ladder (-3 on kern_big, -2 on
    ; kern_small, where the jump had been short)
    mov byte [sw_altm], 0
    cmp byte [sw_mode], SWM_GRAY ; ONLY gray starts from the dither byte. Solid
    jne .planes                  ; overwrote it per plane below (sw_ink), and
    call sw_ink.dither           ; its own dither branch stages it there; XOR
                                 ; never reads [sw_pat] at all. Staging it for
.planes:                         ; all three cost every solid fill five
                                 ; instructions it threw away.
                                 ;
                                 ; sw_ink.dither IS `mov byte [sw_altm],0xFF`
                                 ; and the AA/55 store (sw_parity's body, one
                                 ; call less since kernel size pass 16), the
                                 ; same two steps this arm spelled out. It costs
                                 ; a near call and ret - 52.13 clocks measured
                                 ; (PERFORMANCE.md Part 2), not the ~35 this
                                 ; was first priced at - on a gray fill the
                                 ; file's own accounting puts at 5,305 cycles:
                                 ; 0.98%, once per fill and not per row.
                                 ; docs/plans/LAST-DROP-BYTES.md 7.6 refused +33% on
                                 ; this body; this is a thirtieth of that.
                                 ; It clobbers AL and flags, and AX is
                                 ; dead here (gfx_rect_setup spent it and
                                 ; nothing has reloaded it) while the flags
                                 ; from the `cmp` are already consumed
    ; --- ONE PLANE, AND NO LOOP AROUND IT (SPEC.md 39.26) -------------------
    ; This walked [vid_planes] passes, stepping [vid_rpara] between them. That
    ; is the RAM back buffer's shape - four claimed planes on a VGA - and
    ; SPEC.md 32 took the buffer out; what is left is the renderer, which is
    ; reached only when [vid_mono] is set, and vid_depth_set writes [vid_mono]
    ; and [vid_planes] TOGETHER (39.5). So the counter was always 1 here and
    ; the loop always exited after one pass. sw_spans two screens up already
    ; says so in as many words and does not loop; this is the same fact
    ; applied to the body every fill goes through.
    mov es, [vid_rseg]          ; the framebuffer itself on mono (SPEC.md 39.3)
    cmp byte [sw_mode], SWM_SOLID
    jne .go                     ; INVERTED so that SOLID - the mode the machine
    call sw_ink                 ; spends its day in - falls through a 2-byte
.go:                            ; short jump instead of taking the 5-byte
                                ; expansion of `jne sw_plane_op` (an 8086 has
                                ; no near Jcc, so NASM spells that `je $+5 /
                                ; jmp near`). SOLID pays an untaken jne (8.7
                                ; clocks by the prefetch floor) where it paid
                                ; a taken je (18.19); GRAY and XOR pay those
                                ; ~9.5 clocks back, once per fill.
    call sw_plane_op
.done:
    jmp kret_es


%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; sw_spans - the 1bpp half of SPEC.md 5.10's gfx_spans
; in:       gfx_spans' walk is armed; jumped to, not called
; out:      jumps back to gfx_spans.done
;
; **IT DOES NOT GO THROUGH sw_rect_pl, AND SPEC.md 5.10.4 IS WHY.** That is the
; body for a RECT and it costs 1,806 cycles for a single row, measured - the
; mode tests, the plane loop, sw_ink's table read and two sw_col calls that
; each reload six registers, all of which a rect pays ONCE and amortises over
; its rows. A span IS one row, so a span list paid a rect's fixed cost sixteen
; times and the primitive lost to the eight fills it replaced. PERFORMANCE.md
; Set 88's trap, one layer down: reusing the shape of the machinery is not
; reusing the optimisation.
;
; So the ink, the plane and the dither's alternation are hoisted out of the
; loop and what is left per row is the two masked edge bytes and the interior.
; The ink comes from sw_ink exactly as a fill's does, [gfx_ry1] being the FIRST
; row when it runs, and a middle grey then alternates through [sw_altm] per row
; - which is what sw_col does inside a rect (SPEC.md 39.4).
;
; IT WRITES NO [sw_mode], and a line here used to, "sw_ink's, and nothing else
; reads it here". sw_ink does not read it: its whole body is the gfx_inktab
; lookup and the dither fork. The complete reader set is sw_rect_pl,
; sw_plane_op and sw_col, and none of the three is reachable from here -
; sw_rect_pl's only caller is sw_rect, whose only entries are sw_fill's
; fall-through and sw_fill_gray/sw_xor_fill's jumps, each of which writes
; [sw_mode] as its FIRST instruction. So no reader can observe a value left
; behind by this routine.
; -----------------------------------------------------------------------------
sw_spans:
    mov ax, [gfx_sp_y]
    mov [gfx_ry1], ax                ; the first row: sw_ink.dither's parity
    mov byte [sw_altm], 0
    call sw_ink                     ; [sw_pat], and [sw_altm] if it dithers
    mov ax, [vid_rseg]              ; one plane: [vid_mono] and [vid_planes]
    mov [gfx_sp_fb], ax             ; are set together (SPEC.md 39.5)
    mov es, ax
.row:
    call gfx_sp_next                ; AH/AL = the masks, BX = span, DI = the byte
    jc .next
    mov dl, [sw_pat]
    mov dh, dl                      ; left edge (the whole row if 1 byte wide)
    xor dh, [es:di]                 ; ONE STORE an edge byte - sw_col's
    and dh, ah                      ; dest ^= (pat ^ dest) & mask, and for
    xor [es:di], dh                 ; sw_col's reason
    or bx, bx
    jz .next
    add di, bx                      ; right edge
    mov dh, dl
    xor dh, [es:di]
    and dh, al
    xor [es:di], dh
    dec bx                          ; interior bytes
    jz .next
    sub di, bx                      ; DI is on the right edge: step back over
    mov cx, bx                      ; the interior rather than reload the left
    mov al, dl                      ; byte, which is no longer in memory to
    rep stosb                       ; reload (SPEC.md 5.7.1's shape)
.next:
    mov al, [sw_altm]               ; gray alternates down the rows, solid
    xor [sw_pat], al                ; leaves [sw_altm] zero and this alone
    dec word [gfx_sp_n]
    jnz .row
    jmp gfx_spans.done
%endif

; -----------------------------------------------------------------------------
; sw_ink - internal: solid mode's byte pattern for the pass about to run
;
; VGA: the plane's own bit of [gfx_color], consumed LSB-first out of BL -
; exactly what the open-coded shr/sbb did before.
;
; Mono: gfx_ink's reduction of the colour. Black and white become 00/FF; the
; middle greys become the 50% dither, which costs nothing extra because
; sw_rect has ALREADY put the row-parity AA/55 byte in [sw_pat] - arming
; [sw_altm] is all it takes to make SWM_GRAY's machinery do the work
; (SPEC.md 39.4).
;
; in:       BL = the colour's remaining bits (VGA path only)
; out:      [sw_pat], possibly [sw_altm]; BL shifted on the VGA path
; clobbers: AL, SI, flags
; -----------------------------------------------------------------------------
sw_ink:
    ; NO [vid_mono] TEST (SPEC.md 39.26). It had one, and a four-instruction
    ; arm under it that reduced a PLANE's colour bit - the RAM back buffer's
    ; again (SPEC.md 32). Nothing reaches this file with [vid_mono] clear:
    ; [vid_rseg] is 0 on a VGA precisely so that it cannot, and viddet.inc
    ; says what happens when it does. The test could only ever go one way.
    mov al, [gfx_color]         ; gfx_ink's table read, inlined through SI -
    and ax, 15                  ; it indexes through BX, which sw_rect_pl used
    mov si, ax                  ; to hold the plane counter in. SI is dead
    mov al, [si + gfx_inktab]   ; between gfx_rect_setup and sw_plane_op, which
    cmp al, 1                   ; loads it itself
    jne .set
.dither:                        ; alternate it down the rows...
    mov byte [sw_altm], 0xFF
    mov al, 0xAA                ; ...from the 50% dither byte for the rect's
    test byte [gfx_ry1], 1      ; top row. This was sw_parity, its one call,
    jz .set                     ; and it shares the solid arm's store
    mov al, 0x55                ; (kernel size pass 16, -8)
.set:
    mov [sw_pat], al
    ret

; -----------------------------------------------------------------------------
; sw_plane_op - internal: one plane of a sw_rect op - left/right masked
;               edge columns plus the whole-byte interior.
; in:       ES = plane segment; [sw_mode]/[sw_pat]/[sw_altm]; vga_* rect
;           scratch from gfx_rect_setup
; clobbers: everything but the segment registers
;
; IT USED TO BANK BX AND DX, and the header explained the pair as "ONCE PER
; PLANE" - a SPEC.md 39.26 relic, because there is one plane and no loop, so
; "once per plane" had become "once per call". BX and DX are still the
; interior loop's working registers; only the pointless save is gone. The one
; caller is sw_rect_pl (a tail `jmp`, so this routine's `ret` is its), whose
; one caller is sw_rect, which banks all eight registers in its own prologue
; and pops them at .done - and sw_rect_pl's published contract is already
; "clobbers: everything but the segment registers".
;
; sw_col has the run of the register file and saves nothing, and this
; routine's own interior loop can hold
; gfx_nextrow's two parameters in registers (SPEC.md 39.3). A row step was a
; near call plus two memory reads; it is three register instructions now, and
; a fill walks its rows THREE times.
;
; THROUGH DS, WITH NO `cs:` OVERRIDE - here and in sw_col, and the reason is
; not "DS is always KERNEL_SEG". sw_xfer is the counter-example, in this very
; file: it sets DS to the framebuffer or to the caller's buffer for its movsb
; and MUST keep its overrides, which is what gfx_nextrow's own header is about.
; The reason here is that these two routines already read a dozen kernel
; variables through DS in their own prologues - [gfx_lmsk], [gfx_roff],
; [gfx_rspan], [gfx_rmsk], [gfx_rrows], [sw_pat], [sw_mode], [sw_altm] - so a
; wrong DS would already be fetching garbage; three overrides beside them were
; protecting nothing. They were copied verbatim off gfx_nextrow when it was
; inlined. kernel.asm's mark_fill inlines the same three instructions with no
; override and says so. The caller set is closed and re-checked at each edit:
; sw_col from sw_plane_op only, sw_plane_op from sw_rect_pl only, sw_rect_pl
; from sw_rect only, and nothing on that chain writes DS.
; -----------------------------------------------------------------------------
%ifdef SW_BANKWALK
; =============================================================================
; SPEC.md 39.31 - THE RECT IS WALKED A BANK AT A TIME (kern_big)
;
; What follows replaces the three column-major passes below it on kern_big:
; left edge column, right edge column, interior rows, each stepping rows with
; gfx_nextrow's add, test and conditional wrap and toggling the dither
; pattern on every row. On a banked framebuffer rows y, y+B, y+2B... (B =
; [vid_bmask] + 1, two on CGA and four on Hercules) are [vid_stride] apart in
; ONE bank, and B is even, so they also share a dither parity. So a rect is
; walked as B "phases" - every row of bank phase j, top to bottom - and inside
; a phase the row step is one `add di, si`, the pattern is constant and the
; count is `loop`'s. No pixel is written twice and the settled picture is the
; same byte for byte; what changes is the ORDER rows land in within one call.
;
; And a phase writes the whole ROW: the masked left byte, the whole bytes,
; the masked right byte, in one pass, so the per-row loop is paid once and
; not three times. A whole-byte edge (mask FF, which SPEC.md 11.94 makes the
; common case) is folded into the run of whole bytes, so an aligned fill is
; nothing but `rep stosw` a row. One-row rects - every hline - skip the phase
; driver and jump straight into the row body.
;
; Each masked byte is still ONE store, `dest ^= (pat ^ dest) & mask`
; (SPEC.md 39.25.1): an interrupt between the read and the store cannot have
; the bits outside the mask rewritten.
;
; in/out/clobbers: sw_plane_op's contract below - everything but the segment
; registers.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; sw_walk - run the row body at [sw_kern] once per bank phase
; in:  DI = the rect's first byte (row y1), AH = phase 0's pattern, BX/SI/BP
;      the body's parameters, [sw_n0]/[sw_rp] the phase geometry
; clobbers: AL, CX, DX, DI, flags. Every body preserves AH, BX, SI, BP and
; ES, and takes its row count in DX.
; -----------------------------------------------------------------------------
sw_walk:
    mov dx, [sw_n0]
    mov cl, [sw_rp]
    xor ch, ch
    inc cx                      ; rp + 1 phases of n0 rows...
    call .seg
    mov cl, [vid_bmask]         ; ...then B - 1 - rp phases of n0 - 1 (rows
    sub cl, [sw_rp]             ; never reach 0 there: the walk is only
    jz .out                     ; taken from 9 rows, past 2B on both)
    mov dx, [sw_n0]
    dec dx
.seg:
    push cx
    push dx
    push di
    call [sw_kern]
    pop di
    add di, [vid_rowadd]        ; the next phase starts one row down: the only
    test di, [vid_wrapbit]      ; bank wrap a walk tests, once a PHASE
    jz .nw
    add di, [vid_wrapfix]
.nw:
    xor ah, [sw_altm]           ; ...and on the other dither parity
    pop dx
    pop cx
    loop .seg
.out:
    ret

sw_k_col:                       ; one masked byte column
    mov cx, dx
.r:
    mov al, ah
    xor al, [es:di]
    and al, bl
    xor [es:di], al
    add di, si
    loop .r
    ret

sw_k_colf:                      ; one whole byte column: nothing under it to
    mov cx, dx                  ; keep, so one store (SPEC.md 39.25)
.r:
    mov [es:di], ah
    add di, si
    loop .r
    ret

sw_k_xcol:                      ; one XOR column - every drag outline's two
    mov cx, dx                  ; sides, redrawn on every pointer step - TWO
    shr cx, 1                   ; rows a `loop`: the odd row first, then pairs
    jnc .r
    xor [es:di], bl
    add di, si
    jcxz .o
.r:
    xor [es:di], bl
    add di, si
    xor [es:di], bl
    add di, si
    loop .r
.o:
    ret

sw_k_i:                         ; whole bytes only: an aligned fill
    mov al, ah                  ; AX = pattern:pattern, so stosw is safe for
.r:                             ; the solid 00/FF and the AA/55 dither alike
    mov cx, bp
    shr cx, 1
    rep stosw                   ; string ops leave the shr's CF alone, so the
    adc cx, cx                  ; odd byte is CX = CF and one more rep, with
    rep stosb                   ; no branch
    add di, si
    dec dx
    jnz .r
    ret

sw_k_lir:                       ; masked left, whole bytes, masked right
.r:
    mov al, ah
    xor al, [es:di]
    and al, bl
    xor [es:di], al
    inc di
    mov al, ah
    mov cx, bp
    shr cx, 1
    rep stosw
    adc cx, cx
    rep stosb                   ; AL is still the pattern for the right edge
    xor al, [es:di]
    and al, bh
    xor [es:di], al
    add di, si
    dec dx
    jnz .r
    ret

sw_k_xrow:                      ; XOR: both edges xor their mask, the bytes
.r:                             ; between are complemented a WORD at a time
    xor [es:di], bl             ; (AH is no pattern here, so AX is free)
    inc di
    mov cx, bp
    shr cx, 1
    jnc .ev
    not byte [es:di]
    inc di
.ev:
    jcxz .rt
.w:
    mov ax, [es:di]
    not ax
    stosw
    loop .w
.rt:
    xor [es:di], bh
    add di, si
    dec dx
    jnz .r
    ret
sw_po_sel:
    mov si, [vid_stride]        ; SI = the in-bank row step, less what a row
    mov di, [gfx_roff]          ; advances DI by itself
    mov bl, [gfx_lmsk]          ; (the combined mask, for one byte column)
    mov ah, [sw_pat]            ; AH = phase 0's pattern (unused by XOR)
    mov bp, [gfx_rspan]         ; BP = the run of whole bytes, set below
    cmp byte [sw_mode], SWM_XOR
    je .xor
    or bp, bp                   ; ONE BYTE COLUMN - a pixel, a vline - first,
    jnz .rows                   ; because it is the shortest call there is
    mov cx, sw_k_colf
    cmp bl, 0xFF
    je .go
    mov cx, sw_k_col
.go:                            ; CX = the row body
    or dx, dx                   ; one row: no phases, and the row body's own
    jnz .walk                   ; `ret` is this routine's
    inc dx
    jmp cx
.walk:
    mov [sw_kern], cx
    mov al, dl                  ; the phase geometry: phase j (row y1 + j)
    and al, [vid_bmask]         ; has n0 rows for j <= rp and n0 - 1 after:
    mov [sw_rp], al             ;   rp = (N - 1) & bmask
    mov cl, [vid_bshift]        ;   n0 = ((N - 1) >> bshift) + 1
    shr dx, cl
    inc dx
    mov [sw_n0], dx
    jmp sw_walk
.rows:
    mov bh, [gfx_rmsk]
    mov cx, sw_k_i
    cmp bx, 0xFFFF              ; both edges whole: the row is S + 1 whole
    je .whole                   ; bytes from the left one
    sub si, bp                  ; else S - 1 whole bytes between two masked
    dec bp                      ; ones; DI advances S across the row
    mov cx, sw_k_lir
    jmp short .go
.whole:
    inc bp
    sub si, bp
    jmp short .go
.xor:
    mov cx, sw_k_xcol
    or bp, bp
    jz .go
    mov bh, [gfx_rmsk]
    sub si, bp
    dec bp
    mov cx, sw_k_xrow
    jmp short .go
    db 0                        ; PARITY, never executed: the hot routines
                                ; after this file keep their entry parity
                                ; (tests/unit/t_hotpar.py)

%endif
%ifdef SW_BANKWALK
sw_po_big:                      ; 9 rows or more (DX = N - 1): a rect with
    cmp word [gfx_rspan], 0     ; whole bytes in its row walks; a single byte
    jne sw_po_sel               ; column - whose per-row saving is smaller -
    cmp dx, 24                  ; from 25 rows
    jae sw_po_sel
    jmp short sw_plane_cm

; THE ENTRY. The walk pays ~250 cycles a PHASE, so a short rect keeps the
; column-major body below and pays five instructions for the question. One
; row (every hline, every pixel) takes its row body directly. Constants rather
; than multiples of B, so the test stays this short; the break-evens, per
; body and adapter, are in docs/plans/khp1/mono.md.
sw_plane_op:
    mov dx, [gfx_rrows]
    dec dx
    jz sw_po_sel
    cmp dx, 8
    jae sw_po_big
sw_plane_cm:                    ; THE COLUMN-MAJOR BODY, for those short rects
%else
sw_plane_op:
%endif
    mov ah, [gfx_lmsk]          ; left edge column (whole rect if 1 byte wide)
    mov di, [gfx_roff]
    call sw_col
    mov bp, [gfx_rspan]
    or bp, bp
    jz .out
    mov ah, [gfx_rmsk]          ; right edge column
    mov di, [gfx_roff]
    add di, bp
    call sw_col
    mov bp, [gfx_rspan]          ; sw_col owns BP too (it parks the wrap bit)
    dec bp                      ; interior bytes per row
    jz .out
    mov di, [gfx_roff]
    inc di
    mov si, [gfx_rrows]
    mov ah, [sw_pat]
    mov bx, [vid_rowadd]     ; gfx_nextrow's two parameters, hoisted out of
    sub bx, bp                  ; the row loop and inlined below - the step
                                ; carrying the interior walk's own advance, so
                                ; neither loop banks DI (SPEC.md 5.7.1)
    mov dx, [vid_wrapbit]
    cmp byte [sw_mode], SWM_XOR
    je .xrow
.irow:
    mov al, ah                  ; AX = pattern:pattern, so stosw is safe for
    mov cx, bp                  ; both the solid 00/FF and the AA/55 dither
    shr cx, 1
    rep stosw                   ; string ops leave the shr's CF alone
    jnc .ieven
    stosb                       ; odd interior width: one tail byte
.ieven:
    add di, bx                  ; gfx_nextrow, inlined (see the note above)
    test di, dx
    jz .inw
    add di, [vid_wrapfix]
.inw:
    xor ah, [sw_altm]           ; gray alternates, solid stays
    dec si
    jnz .irow
    ret
.xrow:
    mov cx, bp
.xbyte:
    not byte [es:di]
    inc di
    loop .xbyte
    add di, bx
    test di, dx
    jz .xnw
    add di, [vid_wrapfix]
.xnw:
    dec si
    jnz .xrow
.out:
    ret

; -----------------------------------------------------------------------------
; sw_col - internal: one masked byte column of [gfx_rrows] rows at ES:DI.
;          XOR mode: dest ^= mask. Else: dest = (dest & ~mask) |
;          (pattern & mask), pattern xor-stepped by [sw_altm] per row.
; in:       AH = mask, DI = top byte, ES = plane; [sw_mode]/[sw_pat]/[sw_altm]
; clobbers: AL, CX, DX, SI, DI, BP, flags (AH, BX and ES survive)
;
; **ONE STORE A BYTE.** The masked row was `and [es:di], ~mask` then
; `or [es:di], pat & mask` - two writes, and the first leaves the masked bits
; BLACK until the second lands. The CRTC scans the framebuffer while the CPU
; writes it, so a row the beam passed between the two was displayed with the
; hole in it: three alternating pixels of a dither edge black for one frame,
; on a different row each time, about one gesture in six on CGA
; (tests/deskflash.py, which had carried it as a known flake). SPEC.md 6.6's
; rule - nothing writes a pixel twice - at the scale of one byte. It is now
; dest ^= (pat ^ dest) & mask, vga12.inc's own masked-blit idiom: the same
; ten bytes, three bus accesses where there were four, and the bits outside
; the mask are never written with a stale value even if an interrupt touches
; the byte between the read and the store.
;
; It preserves NOTHING, deliberately: sw_plane_op banks BX/DX for the whole
; plane and reloads AH, DI, SI and BP itself around every call, so the five
; push/pop pairs this used to open with were saving registers their only
; caller was about to overwrite - a fifth of the cost of drawing one pixel
; (PERFORMANCE.md Part 2). SI and BP then pay for themselves by holding
; gfx_nextrow's step and wrap bit across the row loop.
; -----------------------------------------------------------------------------
sw_col:
    mov cx, [sw_altm]           ; CL = toggle, CH = running pattern - ONE word
                                ; read where two byte reads were. The two are
                                ; declared adjacent in .bss with a %if that
                                ; says so; little-endian puts [sw_altm] in CL
                                ; and the byte after it, [sw_pat], in CH
    mov dx, [gfx_rrows]
    mov si, [vid_rowadd]     ; gfx_nextrow, hoisted and inlined - its body
    mov bp, [vid_wrapbit]    ; is three instructions and the call and ret
    cmp byte [sw_mode], SWM_XOR ; around them cost as much again
    je .xrow
%ifdef GFX_COLFAST
    cmp ah, 0xFF                ; --- SPEC.md 39.25: A WHOLE COLUMN NEEDS NO
    je .frow                    ;     READ. mask = FF means the byte is ours
%endif                          ;     entire, so there is nothing under it to
                                ;     preserve and the read-modify-write
                                ;     below is doing nothing but cost. That is
                                ;     not a rare shape: gfx_rect_setup takes
                                ;     the left mask from x1 & 7 and the right
                                ;     from x2 & 7, and SPEC.md 11.94 puts every
                                ;     window's content origin on a multiple of
                                ;     8 - so an ordinary chrome fill arrives
                                ;     with BOTH edge columns whole
.row:
    mov al, ch
    xor al, [es:di]             ; AL = (pattern ^ dest) & mask...
    and al, ah
    xor [es:di], al             ; ...and ONE store (see the header)
    xor ch, cl
    add di, si
    test di, bp
    jz .nw
    add di, [vid_wrapfix]
.nw:
    dec dx
    jnz .row
    ret
%ifdef GFX_COLFAST
.frow:
    mov [es:di], ch             ; the pattern byte, whole. `and al, ah` is a
    xor ch, cl                  ; no-op at mask FF and the read is gone with
    add di, si                  ; it, so a row is one store where it was two
    test di, bp                 ; accesses and two ALU ops - 15 bytes of
    jz .fnw                     ; traffic against 25 on an 8088's 8-bit bus
    add di, [vid_wrapfix]    ; (PERFORMANCE.md Set 20's model)
.fnw:
    dec dx
    jnz .frow
    ret
%endif
.xrow:
    xor [es:di], ah
    add di, si
    test di, bp
    jz .xnw
    add di, [vid_wrapfix]
.xnw:
    dec dx
    jnz .xrow
    ret

; =============================================================================
; THE BLIT DECODER (SPEC.md 5.4.1) - a row of packed 4bpp straight into 1bpp
;
; gfx_blit4 used to emit one gfx_hline per coalesced RUN, and SPEC.md 5.4.1's
; first form wrote the run itself instead - which took ~756us of ARRIVING off
; each one and left ~371us of per-run work. This takes the runs out altogether:
; a source BYTE is two pixels and maps, through 39.4's reduction, to a two-bit
; destination pattern that depends only on its value and one parity bit, so a
; 256-byte table answers it outright and the loop is a read, an xlat and a
; shift per two pixels. It costs the same whatever is in the row.
;
; THERE IS NO HYBRID and docs/plans/LAST-DROP-PERF.md 3 is why. The run path is cheaper
; only below 1.84 runs a row (PERFORMANCE.md Set 43's two-term fit), nothing
; anyone waits on draws flat art through this - Paint's canvas, Solitaire's
; lattice card backs, ArtfulType's line of TEXT per keystroke - and keeping
; both paths costs 336 bytes and a permanent second opinion about the pixels.
;
; THREE SEGMENTS AT ONCE is what shapes the loop: the source is the CALLER's
; (ES on entry), the framebuffer is [vid_rseg], and the table is the kernel's.
; So DS becomes the source (lodsb), ES the framebuffer, and the table is read
; `ss xlat` - the tables having moved to .lowbss when SPEC.md 5.4.1.3 spent
; the segment (39.3.1's reason), where they were `cs xlat` in .bss. It is the
; same one prefix byte and the same clocks either way, because SS IS LOW_SEG
; for all kernel code (SPEC.md 1) exactly as CS is KERNEL_SEG. Nothing may
; read a [gfx_blit_*] word inside the loop.
; =============================================================================

; sw_pbit - internal: the 1bpp bit for colour SI at parity DH (SPEC.md 39.4)
; out: AL = 0 or 1; clobbers AL and flags
sw_pbit:
    mov al, [si + gfx_inktab]
    cmp al, 1
    je .dither
    and al, 1                   ; gfx_inktab is 00 / 01 / FF, so the low bit
    ret                         ; IS the answer for the two solid classes
.dither:
    mov al, dh                  ; ...and a middle grey alternates with (x+y) -
    xor al, 1                   ; INVERTED, because sw_ink.dither's 0xAA has its
    ret                         ; bit SET at x=0 of an even row, and bit 7 is
                                ; the leftmost pixel

; -----------------------------------------------------------------------------
; sw_pairbuild - fill the two pair tables from gfx_inktab (SPEC.md 5.4.1)
; out: gfx_pairtab0/1 built, [gfx_pairbuilt] = 1; every register preserved
;
; Built at run time rather than assembled, so gfx_inktab stays the ONE place
; the reduction is written down. Once per boot, ~5,000 clocks, behind a flag
; in .text because -f bin zeroes no .bss.
;
; tab0 is for a pair whose FIRST pixel is at even (x+y), tab1 for odd; the two
; pixels of a pair are always opposite, so one bit selects the table and the
; loop then costs nothing per pixel for the dither at all.
; -----------------------------------------------------------------------------
sw_pairbuild:
    call kent_di                ; (kernel size pass 15: once a boot)
    xor bx, bx                  ; BH stays 0: the loop runs BL round once
.byte:
    mov si, bx                  ; SI = the byte's high nibble (the LEFT
    mov cl, 4                   ; pixel)...
    shr si, cl
    mov di, bx                  ; ...and DI its low one
    and di, 15
    xor dh, dh                  ; --- tab0: hi at parity 0, lo at parity 1
    call .pair
    mov [ss:bx + gfx_pairtab0], dl
    mov dh, 1                   ; --- tab1: the other way round
    call .pair
    mov [ss:bx + gfx_pairtab1], dl
    inc bl
    jnz .byte                   ; 256 entries: BL wraps to 0 after 255
%ifdef GFX_PLANE
    inc bx                      ; BX = 0 here (only BL ever moved, and it has
    mov [gfx_pairbuilt], bx     ; wrapped), so this is the word 0x0001 in a
                                ; byte less (kernel size pass 16). Built - and
                                ; vga_p4built = 0, UNION: the
                                ; pair tables overwrote it (one word store,
                                ; the pair adjacent under vga12.inc's %error)
%else
    mov byte [gfx_pairbuilt], 1
%endif
    jmp kret_di                 ; once a boot: the shared epilogue, not six
                                ; pops of its own

; one table entry: DL = sw_pbit(SI, parity DH) << 1 | sw_pbit(DI, parity
; DH ^ 1). DH, SI and DI come back as they went in (size pass 9: the two
; table halves were this sequence written out twice)
.pair:
    call sw_pbit
    shl al, 1
    mov dl, al
    xchg si, di
    xor dh, 1
    call sw_pbit
    xor dh, 1
    xchg si, di
    or dl, al
    ret

; -----------------------------------------------------------------------------
; sw_blit_row - ONE ROW of packed 4bpp into the 1bpp framebuffer (SPEC.md 5.4.1)
; in:       ES:SI = the row's first source byte (pixel 0 is its HIGH nibble),
;           [gfx_blit_x] = pixel 0's destination x, [gfx_blit_w] = pixels,
;           [gfx_blit_ro] = the row's base byte, [gfx_blit_tb] = this row's
;           pair table. gfx_blit4 has already proved the row is ON SCREEN in x.
; out:      nothing; preserves BX, BP, DS and ES.
;           **CLOBBERS AX, CX, DX, SI AND DI**, which is a CONTRACT CHANGE
;           from "every register preserved" and is worth the lines it takes to
;           justify: this routine is 27.7% of the whole machine in Paint
;           (PERFORMANCE.md Set 102) and the ten stack operations it drops are
;           per ROW of every 4bpp blit on a 1bpp adapter - ~135 clocks a row,
;           and a 466x110 Paint canvas is 110 rows of a repaint measured at
;           399 ms.
;           **gfx_blit4's row loop is the ONLY caller** (vga12.inc:2714,
;           grepped over kernel/, apps/, drivers/, tools/ and tests/); it is
;           reached only when [gfx_blit_fst] == 1, and its only successor is
;           `jmp .nextrow`:
;             AX  .nextrow opens `mov ax,[gfx_blit_sv]`               dead
;             SI  .row opens `mov si,[gfx_blit_sv]`, every iteration  dead
;             DX  gfx_rowbase, called earlier in the SAME iteration, ends
;                 in a `mul` and so WRITES DX; .par then loads it with a
;                 table address it stores immediately                 dead
;             CX  gfx_rowbase clobbers CX too, before any read here    dead
;             DI  never read on the fst == 1 path, and sw_blit_row loads
;                 it itself from [gfx_blit_ro] at every entry          dead
;           and when the row loop ends all five come off gfx_blit4's OWN
;           prologue at .pops. BX (`inc bx` at .nextrow) and BP (`add ax,bp`)
;           are LIVE and stay saved; DS and ES are mandatory - the routine
;           swaps both. A second caller would inherit a contract that no
;           longer holds: read this list before adding one.
;
; The accumulator is 16-bit and carries `AH` bits, seeded with the `x & 7` bits
; of the first destination byte that belong to the SCREEN - so the first store
; needs no mask, and only the last one does. That seeding is why this handles
; an odd x with no special case: the completed byte is `DX >> (AH-8)` and
; `AH-8` is 0 or 1 by construction, fixed for the whole row.
;
; It does NOT clip in x, and that is the one thing gfx_blit4 owes it: a left
; clip would advance the source by a pixel count that may be ODD, which shifts
; the nibble phase of every pair after it. Refusing an off-screen block costs
; nothing real - Paint, ArtfulType and Solitaire all clip their own blits - and
; buys the loop its simplicity.
;
; The general loop costs 19 bytes a PAIR and eight of them are the accounting -
; `add ah,2 / cmp ah,8 / jb`, asked per pair to find a boundary that arrives
; every fourth one. Once the FIRST destination byte has been stored the phase
; is fixed for the rest of the row - nothing pending, or exactly one bit
; pending, for ever - so four pairs are always the same shape and SPEC.md
; 5.4.1.2 gives each phase a body of its own, past the `ret` below: .abyte (37
; bytes per eight pixels) and .obyte (52, the carry epilogue being the whole
; difference).
;
; NEITHER TESTS THE PHASE, because each is entered from the store that proves
; its own precondition: an odd x can never reach the whole-byte store (AH steps
; by 2 from an odd seed, so it hits 9 and carries, for ever) and an even one
; can never reach the carry. Both are gated - tools/ptcheck.py's PTNUDGE is an
; odd sideways drag, and without it a run proves half the routine.
; -----------------------------------------------------------------------------
sw_blit_row:
    push bx                     ; ...and only these four: see the contract
    push bp                     ; above
    push ds
    push es
    mov ax, [gfx_blit_x]
    mov di, [gfx_blit_ro]
    mov bx, ax
    and bx, 7                   ; BL = the bits of byte 0 we do NOT own
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax                  ; DI = the first destination byte
    mov cx, [gfx_blit_w]
    mov bp, cx
    and bp, 1                   ; BP = 1 if there is an odd trailing pixel
    shr cx, 1
    push cx                     ; ...whole pairs, banked over the shift below
    mov dx, [gfx_blit_tb]
    mov ax, [vid_rseg]
    push dx                     ; ...and the table, over the segment swap
    mov dx, es                  ; DX = the CALLER's source segment
    mov es, ax                  ; ES = the framebuffer
    mov ds, dx                  ; DS = the source: NO kernel reads past here
    xor dx, dx
    mov ah, bl                  ; AH = bits already accounted for
    or bl, bl
    jz .primed
    mov al, [es:di]             ; the screen's top `bl` bits, right-aligned, so
    mov cl, 8                   ; that the first store owes no mask
    sub cl, bl
    shr al, cl
    mov dl, al
.primed:
    pop bx                      ; BX = the pair table (ss xlat's base)
    pop cx                      ; CX = whole pairs
    jcxz .nomore
.pair:
    lodsb                       ; DS:SI - the source byte, two pixels
    ss xlat                     ; ...and its two destination bits
    shl dx, 1
    shl dx, 1
    or dl, al
    add ah, 2
    cmp ah, 8
    jb .next
    sub ah, 8
    jnz .carry
    mov [es:di], dl             ; the common case: eight bits, none over
    inc di
    xor dx, dx
    dec cx                      ; ...and the pair just consumed
    jz .nomore
    jmp .esplit                 ; SPEC.md 5.4.1.2: DI is on a byte with nothing
                                ; pending, so four pairs are one whole byte
.carry:
    mov al, dl                  ; AH = 1: one bit belongs to the NEXT byte, so
    and al, 1                   ; bank it, shift the completed byte down and
    shr dx, 1                   ; put it back
    mov [es:di], dl
    inc di
    xor dx, dx
    mov dl, al
    dec cx
    jz .nomore
    jmp .osplit                 ; ...and 5.4.1.2's other phase: four pairs are
                                ; nine bits, a byte and the next pending one
.nomore:
    jmp short .odd              ; the bodies sit past the `ret`, out of `loop`'s
.next:                          ; reach, so every exit trampolines through here
    loop .pair
.odd:
    or bp, bp                   ; a trailing pixel with no partner
    jz .tail
    lodsb
    ss xlat
    shr al, 1                   ; the HIGH nibble's bit, out of the pair
    shl dx, 1
    or dl, al
    inc ah
    cmp ah, 8
    jb .tail
    xor ah, ah
    mov [es:di], dl
    inc di
    xor dx, dx
.tail:
    or ah, ah                   ; AH bits left over: the TOP AH bits of the
    jz .done                    ; last byte, and the screen keeps the rest
    mov cl, 8
    sub cl, ah
    mov al, dl
    shl al, cl
    mov ah, 0xFF
    shl ah, cl                  ; AH = the mask of the bits we own
    mov ch, ah
    not ch
    and ch, [es:di]
    or ch, al
    mov [es:di], ch
.done:
    pop es
    pop ds
    pop bp
    pop bx
    ret

; --- SPEC.md 5.4.1.2's two aligned bodies. They live PAST the `ret` because
; `loop .pair` is a short jump and cannot reach over them, and each is entered
; from the store that proves its own precondition - so neither tests the phase.
;
; .abyte IS AT ITS FLOOR AND A THIRD PASS IS REFUSED. It is 37 instruction
; bytes and 145 execution clocks per eight pixels, so under PERFORMANCE.md
; Part 2's `max(execution, 4.34 x bytes)` it is fetch-bound at 160.6 - by
; 15.6 clocks. That gap is the WHOLE prize: removing every instruction byte
; for nothing would buy 7.7%, and the loop is execution-bound the moment four
; of them go. Two levers were costed against that and both are recorded in
; docs/plans/MONO-RECLAIM-PLAN.md 5.3:
;
;   * the six `shl dl, 1` as three `shl dl, cl` with CL = 2 (the count moving
;     to DH, which is free here). Four bytes smaller and 11% SLOWER: an 8088
;     charges 8 clocks plus 4 per bit for a shift by CL, so three of them add
;     36 execution clocks to save 17 of fetch. A shorter encoding beats a
;     cheaper instruction only while the code is fetch-bound, and this crosses
;     over;
;   * a table per byte POSITION, pre-shifted, so the shifts vanish. Worth
;     13.6% and IMPOSSIBLE as written: `xlat` takes its base in BX, so a
;     different table per pair costs at least 3 bytes to reload it - more than
;     the 4 bytes of shifts it removes.
;
; What is left is `xchg ax, dx` for `mov al, dl` - one byte, 2.1% - and it
; needs the leftover-pair count out of AH first. Not taken; the ratio does not
; justify touching the hottest loop in the tree.
.esplit:
    mov ah, cl                  ; AH = the pairs the body cannot take...
    and ah, 3
    shr cx, 1
    shr cx, 1                   ; ...and CX = the whole destination bytes
    jcxz .erest
.abyte:
    lodsb
    ss xlat
    mov dl, al
    lodsb
    ss xlat
    shl dl, 1
    shl dl, 1
    or dl, al
    lodsb
    ss xlat
    shl dl, 1
    shl dl, 1
    or dl, al
    lodsb
    ss xlat
    shl dl, 1
    shl dl, 1
    or dl, al
    mov al, dl
    stosb                       ; ES:DI, and DI walks itself
    loop .abyte
.erest:
    mov cl, ah                  ; the leftovers go back to the general loop,
    xor ch, ch                  ; accumulator empty and DI on a byte
    xor ax, ax
    xor dx, dx
    jcxz .backodd
    jmp .pair
.backodd:
    jmp .odd
.osplit:
    mov al, cl                  ; AH is 1 here and has to STAY 1, so this one's
    and al, 3                   ; leftover count rides the stack rather than AH
    push ax
    shr cx, 1
    shr cx, 1
    jcxz .orest
.obyte:
    lodsb
    ss xlat
    shl dx, 1
    shl dx, 1
    or dl, al
    lodsb
    ss xlat
    shl dx, 1
    shl dx, 1
    or dl, al
    lodsb
    ss xlat
    shl dx, 1
    shl dx, 1
    or dl, al
    lodsb
    ss xlat
    shl dx, 1
    shl dx, 1
    or dl, al
    mov al, dl                  ; nine bits: the top eight are the byte and the
    and al, 1                   ; ninth is the next one's pending bit
    shr dx, 1
    mov [es:di], dl
    inc di
    xor dx, dx
    mov dl, al
    loop .obyte
.orest:
    pop ax                      ; AL = the leftover pairs, AH = 1 again
    mov cl, al
    xor ch, ch
    jcxz .backodd
    jmp .pair

; -----------------------------------------------------------------------------
; sw_fill_pat - software twin of gfx_fill_pat (SPEC.md 5/32): the 8-row
;               pattern at [gfx_pat], staged into gfx_patbuf, written
;               identically to every plane (colors 0/15 per pixel bit).
;               sw_col's fixed-byte/xor-toggle model cannot index 8 rows,
;               so this brings its own column helper.
; in:       AX = x1, BX = y1, CX = x2, DX = y2; [gfx_pat] = pattern ptr
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
sw_fill_pat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call gfx_rect_setup
    jc .done
    call gfx_pat_stage          ; gfx_patbuf <- the 8 bytes at [gfx_pat]

    ; --- ONE PLANE, AND NO LOOP AROUND IT (SPEC.md 39.26) -------------------
    ; sw_rect_pl's note, for the body 39.26's sweep did not reach. This walked
    ; [vid_planes_w] passes stepping [vid_rpara] between them; it is entered
    ; only from gfx_fill_pat_raw's `cmp byte [vid_mono],0 / jne sw_fill_pat`
    ; (vga12.inc) and, on a build with no VGA in it at all, from the NOVGA_ARM
    ; one line above that - so [vid_mono] is set at every entry, and
    ; vid_depth_set writes [vid_mono] and [vid_planes_w] TOGETHER, so the
    ; counter was always 1 and the loop always exited after one pass.
    ; BP's double duty as plane counter AND row counter - the reason for the
    ; push/pop around the interior loop below - goes with it.
    mov es, [vid_rseg]          ; the framebuffer itself on mono (SPEC.md 39.3)
    mov di, [gfx_roff]
    mov cl, [gfx_lmsk]          ; left edge column (whole rect if 1 byte)
    call sw_patcol
    mov si, [gfx_rspan]
    or si, si
    jz .done
    mov di, [gfx_roff]
    add di, si
    mov cl, [gfx_rmsk]          ; right edge column
    call sw_patcol
    dec si                      ; SI = interior bytes per row
    jz .done
    mov di, [gfx_roff]
    inc di
    mov bx, [gfx_ry1]
    and bx, 7
    mov bp, [gfx_rrows]
    mov dx, [vid_rowadd]        ; SPEC.md 39.34: the row step less the run's
    sub dx, si                  ; own advance, so DI is never banked
.irow:
    mov al, [gfx_patbuf+bx]
    mov ah, al                  ; byte uniform across the row: stosw, and the
    mov cx, si                  ; odd byte as CX = CF and one more rep (string
    shr cx, 1                   ; ops leave the shr's CF alone)
    rep stosw
    adc cx, cx
    rep stosb
    add di, dx                  ; gfx_nextrow, inlined
    test di, [vid_wrapbit]
    jz .inw
    add di, [vid_wrapfix]
.inw:
    inc bx
    and bx, 7
    dec bp
    jnz .irow
.done:
    jmp kret_es

; -----------------------------------------------------------------------------
; sw_patcol - internal: one masked byte column of [gfx_rrows] rows at ES:DI,
;             row byte from gfx_patbuf by (y & 7):
;             dest = dest ^ ((dest ^ pat) & mask)
; in:       CL = mask, DI = top byte, ES = plane; vga_* rect scratch
; clobbers: AX, BX, BP, DI, flags (preserves SI and CX)
;
; A CONTRACT CHANGE, and there are exactly two callers, both in sw_fill_pat
; above (grepped over kernel/, apps/, drivers/, tools/, tests/). Neither reads
; BX or DI on the far side: after the left column the caller's next three
; instructions are `mov si,[gfx_rspan] / or si,si / jz .done` and then
; `mov di,[gfx_roff]`, and after the right column `dec si / jz .done /
; mov di,[gfx_roff] / inc di / mov bx,[gfx_ry1]` - DI and BX both written
; before any read. SI IS live across the second call (that `dec si`), so the
; row count is kept in BP, which neither caller reads until it loads it for
; the interior. This is what sw_col already does and documents.
; -----------------------------------------------------------------------------
sw_patcol:
    mov bp, [gfx_rrows]         ; BP counts, not SI (kernel size pass 17, -2):
                                ; the caller writes BP only after both calls
                                ; (`mov bp, [gfx_rrows]` for the interior),
                                ; and gfx_nextrow touches DI alone, so SI's
                                ; push/pop pair went
    mov bx, [gfx_ry1]
    and bx, 7
.row:
    mov al, [gfx_patbuf+bx]     ; dest ^= (pat ^ dest) & mask, ONE store
    xor al, [es:di]             ; (SPEC.md 39.25.1's idiom: the bits outside
    and al, cl                  ; the mask are never written back with a
    xor [es:di], al             ; value read before an interrupt)
    inc bx
    and bx, 7
    add di, [vid_rowadd]        ; gfx_nextrow, inlined (SPEC.md 39.34)
    test di, [vid_wrapbit]
    jz .nw
    add di, [vid_wrapfix]
.nw:
    dec bp
    jnz .row
    ret
    db 0                        ; PARITY, never executed: the hot routines
                                ; after this file keep their entry parity
                                ; (tests/unit/t_hotpar.py)


; -----------------------------------------------------------------------------
; sw_save / sw_restore - software twins of gfx_save / gfx_restore, same
; rounding and buffer layout (SPEC.md 5) - a buffer saved from the back
; buffer restores to it byte-identically. One shared body, sw_xfer, with
; [sw_dir] picking the direction; restore additionally dirties the rect.
;   sw_save:    in AX/BX/CX/DX = rect, ES:DI = buffer; DI advances
;   sw_restore: in AX/BX/CX/DX = rect, ES:SI = buffer; SI advances
; ES preserved; clobbers flags.
; -----------------------------------------------------------------------------
sw_save:
    mov byte [sw_dir], 0
    jmp sw_xfer

sw_restore:
    mov byte [sw_dir], 1
    ; fall through to sw_xfer

sw_xfer:
    push ax
    push bx
    push cx
    push dx
    push bp
    call gfx_rect_setup
    jc .done

    ; --- ONE PASS, AND NO LOOP AROUND IT (SPEC.md 39.26) --------------------
    ; The fourth body of that sweep, and THE ONE THAT DOES NOT REST ON THE
    ; PLANE COUNT: this walked `add bx,[vid_rpara] / cmp bx,[vid_rend] /
    ; jb .plane`, a SEGMENT compare, so it needs its own argument and gets
    ; one. sw_xfer is reached only from sw_save and sw_restore, and those only
    ; from vga12.inc's `cmp byte [vid_mono],0 / jne` pair or, on a build with
    ; no VGA at all, from the NOVGA_ARM beside each. vid_apply - the sole
    ; writer of all three cells (tests/unit/t_invariants.py) - sets
    ; [vid_rpara] = 1 and [vid_rend] = [vid_rseg] + 1 on every non-VGA arm, so
    ; BX started at [vid_rseg] and reached [vid_rend] after exactly one pass,
    ; every time.
    ; With one pass the two `cmp byte [sw_dir],0` tests are four instructions
    ; apart on one straight line, so the first one goes and its body - the
    ; sub-rect skip - becomes .rst's first instruction, which is still ABOVE
    ; the `pop ds` that makes DS the caller's buffer.
    mov bp, [gfx_rspan]
    inc bp                      ; BP = bytes per row
    mov bx, [vid_rseg]
    mov ax, [gfx_roff]          ; the walking offset
    mov dx, [gfx_rrows]
    push ds
    push es
    cmp byte [sw_dir], 0
    jne .rst
    push si                     ; save: plane -> buffer; caller SI untouched
    mov si, ax
    mov ax, [vid_wrapbit]       ; SPEC.md 39.32: the row step lives in BX and
    mov ds, bx                  ; AX (the plane segment and the walking
    mov bx, [cs:vid_rowadd]     ; offset are spent once DS and SI hold them),
    sub bx, bp                  ; less the bytes the copy advanced the row by
.srow:
    mov cx, bp                  ; A WORD AT A TIME: `rep movsw` is 12.5 clocks
    shr cx, 1                   ; a byte on an 8088 where `rep movsb` is 17,
    rep movsw                   ; and the odd byte is CX = CF and one more rep
    adc cx, cx                  ; (string ops leave the shr's CF alone), with
    rep movsb                   ; no branch. Buffer DI advances contiguously
    add si, bx                  ; gfx_nextrow INLINED, on SI, its step and wrap
    test si, ax                 ; bit in registers (PERFORMANCE.md Part 9 Set
    jz .snw                     ; 3: inline it in every row loop)
    add si, [cs:vid_wrapfix]
.snw:
    dec dx
    jnz .srow
    pop si
    jmp .pnext
.rst:
    add si, [gfx_sub_st]        ; the SUB-RECT restore (SPEC.md 5.8), 0 at
                                ; rest. Here, where DS is still KERNEL_SEG -
                                ; below the `pop ds` it would read the
                                ; caller's buffer
    push di                     ; restore: buffer -> plane; caller DI untouched
    mov di, ax
    mov ax, [vid_wrapbit]       ; ...and the same two registers for DI's step
    push es
    pop ds                      ; movsb source = caller's buffer
    mov es, bx                  ; movsb dest = this plane
    mov bx, [cs:vid_rowadd]
    sub bx, bp
.rrow:
    mov cx, bp
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    add si, [cs:gfx_sub_rs]     ; CS: IS MANDATORY - DS is the BUFFER in here
    add di, bx
    test di, ax
    jz .rnw
    add di, [cs:vid_wrapfix]
.rnw:
    dec dx
    jnz .rrow
    pop di
    add si, [cs:gfx_sub_ps]     ; ...past the rows this rect does not cover. DS
                                ; is the buffer until .pnext's `pop ds`, and
                                ; the SAVE path jumps straight to .pnext, so
                                ; this is the restore's alone
.pnext:
    pop es
    pop ds
.done:
    cmp byte [sw_dir], 0        ; a sub-rect is ONE restore (SPEC.md 5.8), and
    je .nosub                   ; clearing it HERE rather than at the call site
    call gfx_sub_off            ; is what makes a forgetful caller inherit
.nosub:                         ; "whole", the safe value. Restore only, or a
                                ; gfx_save between the arm and the restore
                                ; would disarm it - and wm_su_edge is exactly
                                ; that, twice
    jmp gfx_xor_rect_d.pops     ; the same five pops (bp dx cx bx ax) and ret

; -----------------------------------------------------------------------------
section .bss

gfx_patbuf      resb 8          ; gfx_fill_pat: staged pattern rows
gfx_ry1          resw 1          ; rect: clipped top row (dither parity)
gfx_rrows        resw 1          ; rect: clipped row count
gfx_rspan        resw 1          ; rect: right byte - left byte
gfx_roff         resw 1          ; rect: VRAM offset of top-left byte

sw_dir          resb 1          ; xfer: 0 = save, 1 = restore
sw_mode         resb 1          ; rect core: SWM_SOLID / SWM_GRAY / SWM_XOR
sw_altm         resb 1          ; rect core: per-row pattern toggle (0 or FF)
sw_pat          resb 1          ; rect core: plane value / dither pattern
%ifdef SW_BANKWALK
sw_n0           resw 1          ; rect walk: rows in each of the first phases
sw_rp           resw 1          ; ...the last phase index that has sw_n0 rows
sw_kern         resw 1          ; ...and the row body the phases run
%endif
%if (sw_pat - sw_altm) != 1
  %error "sw_col loads the pair as ONE word (`mov cx,[sw_altm]` -> CL = toggle, CH = pattern): they must stay adjacent, sw_altm first. Every other site reads them as bytes and does not care about the order"
%endif

section .text
