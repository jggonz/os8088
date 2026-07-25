; =============================================================================
; jop - apps/jopapi.inc
;
; The program-side SDK for .jop packages (SPEC.md 20.5). Included by package
; sources only - never by the kernel. Provides the pinned jop API jump-table
; offsets (SPEC.md 20.3), the window record and wm_create template layouts
; (SPEC.md 11), the 16 EGA colour indices, the loaded-program region
; constants (SPEC.md 3/20.1), and the packaging macros, so a package source
; can be just:
;
;   %include "jopapi.inc"
;   JOP_HEADER 'NAME', entry        ; emits cpu/bits/org + the 32-byte header
;                                   ; (optional 3rd arg = flags byte, SPEC.md
;                                   ; 20.2: bit 0 = embedded icon follows)
;   JOP_ICON16                      ; only with flags bit 0: 32 dw rows
;   ... 16 mask rows, 16 data rows  ; (bit 15 = leftmost pixel)
;   JOP_ICON16_END
;   ... code and data ...
;   JOP_BSS totalbytes              ; optional; must precede JOP_IMAGE_END
;   JOP_IMAGE_END                   ; defines JOP_IMAGE_SIZE (and bss = 0
;                                   ; when no JOP_BSS was given)
;
; JOP_BSS must come before JOP_IMAGE_END because the "default to 0" choice
; is made by JOP_IMAGE_END at preprocess time; the loader zeroes that many
; bytes after the image, so a package addresses its bss with equ offsets
; from the jop_image_end label that JOP_IMAGE_END defines.
;
; Packages run in the kernel's tiny model (CS = DS = SS = KERNEL_SEG),
; assembled at the APP_LOAD_OFF link base and RELOCATED at load time to a
; per-instance region - several packages (or several instances of one) can
; be resident at once, each a fully relocated copy with its own bss
; (SPEC.md 20.1). This imposes ONE coding rule (SPEC.md 20.2): a package
; address may only be embedded as a whole 16-bit word (imm16, disp16,
; dw label) - never byte-split, shifted or folded into other constants;
; jopkg.py's reconstruction check fails the build otherwise. Every JAPI_*
; symbol below is a fixed near-call target (4-byte jump-table slots in
; kernel.asm). Register contracts are the target routines' own (SPEC.md
; 5, 6, 8, 11, 20.4); all of them preserve every register except their
; documented outputs.
; =============================================================================

; --- environment ---------------------------------------------------------------
KERNEL_SEG   equ 0x1000     ; CS = DS = SS for kernel and packages alike
APP_LOAD_OFF equ 0xA000     ; where packages load (kernel segment offset)
APP_MAX_SIZE equ 0x5000     ; image + bss budget, 0xA000..0xEFFF
SCREEN_W     equ 640
SCREEN_H     equ 480
MBAR_H       equ 20         ; menu bar height, px
TITLE_H      equ 18         ; window title bar height, px

; --- colours (standard EGA/VGA 16-colour indices) ------------------------------
CBLACK    equ 0
CBLUE     equ 1
CGREEN    equ 2
CCYAN     equ 3
CRED      equ 4
CMAGENTA  equ 5
CBROWN    equ 6
CLGRAY    equ 7
CDGRAY    equ 8
CLBLUE    equ 9
CLGREEN   equ 10
CLCYAN    equ 11
CLRED     equ 12
CLMAGENTA equ 13
CYELLOW   equ 14
CWHITE    equ 15

; --- the jop API jump table (SPEC.md 20.3, pinned offsets) ---------------------
; Each slot is a near jmp; `call JAPI_*` from package code is a plain near
; call into the kernel routine named in the comment.
JAPI_GFX_LOCK      equ 0x0010   ; acquire drawing mutex + hide cursor
JAPI_GFX_UNLOCK    equ 0x0014   ; show cursor + release mutex
JAPI_GFX_PIXEL     equ 0x0018   ; CX=x, DX=y
JAPI_GFX_HLINE     equ 0x001C   ; AX=x1, BX=x2, DX=y
JAPI_GFX_VLINE     equ 0x0020   ; AX=x, BX=y1, DX=y2
JAPI_GFX_FILL      equ 0x0024   ; AX=x1, BX=y1, CX=x2, DX=y2
JAPI_GFX_FRAME     equ 0x0028   ; AX=x1, BX=y1, CX=x2, DX=y2
JAPI_GFX_FILL_GRAY equ 0x002C   ; AX=x1, BX=y1, CX=x2, DX=y2 (50% dither)
JAPI_GFX_XOR_RECT  equ 0x0030   ; AX=x1, BX=y1, CX=x2, DX=y2
JAPI_GFX_XOR_FILL  equ 0x0034   ; AX=x1, BX=y1, CX=x2, DX=y2
JAPI_FONT_CHAR     equ 0x0038   ; CX=x, DX=y, AL=char
JAPI_FONT_STR      equ 0x003C   ; CX=x, DX=y, SI=NUL string
JAPI_FONT_WIDTH    equ 0x0040   ; SI=NUL string; out AX = pixel width
JAPI_WM_CREATE     equ 0x0044   ; SI=template; out BX = win ptr, CF on full
JAPI_WM_SHOW       equ 0x0048   ; BX=win ptr
JAPI_WM_HIDE       equ 0x004C   ; BX=win ptr
JAPI_WM_FRONT      equ 0x0050   ; BX=win ptr
JAPI_WM_CONTENT    equ 0x0054   ; BX=win ptr; out AX=content left, DX=top
JAPI_WM_OBSCURED   equ 0x0058   ; BX=win ptr; out CF=1 if covered
JAPI_TASK_YIELD    equ 0x005C   ; give up the time slice
JAPI_TASK_SLEEP    equ 0x0060   ; AX = ticks to sleep (18 ~ 1 second)
JAPI_GET_TICKS     equ 0x0064   ; out AX = [ticks]
JAPI_SET_COLOR     equ 0x0068   ; AL -> [gfx_color]
JAPI_MOUSE         equ 0x006C   ; out CX=mouse_x, DX=mouse_y, AL=mouse_btn
JAPI_SRAND         equ 0x0070   ; AX -> [japi_seed]
JAPI_RAND          equ 0x0074   ; out AX = next pseudo-random word

; --- window record layout (SPEC.md 11, 18 bytes) -------------------------------
W_FLAGS   equ 0             ; word: bit0 = used, bit1 = visible
W_X       equ 2             ; word: frame left (screen coords)
W_Y       equ 4             ; word: frame top (y >= MBAR_H)
W_W       equ 6             ; word: frame width, outer
W_H       equ 8             ; word: frame height, includes title bar
W_TITLE   equ 10            ; word: near ptr to NUL title string
W_PAINT   equ 12            ; word: near ptr, SI = win ptr; content only
W_ONKEY   equ 14            ; word: near ptr or 0; AL=ascii, AH=scan, SI=win
W_ONCLICK equ 16            ; word: near ptr or 0; CX=x, DX=y, SI=win
WIN_SIZE  equ 18
MAX_WIN   equ 12

; --- wm_create template layout (SPEC.md 11, 16 bytes, 8 words) -----------------
WT_X       equ 0
WT_Y       equ 2
WT_W       equ 4
WT_H       equ 6
WT_TITLE   equ 8
WT_PAINT   equ 10
WT_ONKEY   equ 12
WT_ONCLICK equ 14
WT_SIZE    equ 16

; -----------------------------------------------------------------------------
; JOP_HEADER 'NAME', entry [, flags] - open the package and emit the 32-byte
; header (SPEC.md 20.2). Emits cpu 8086 / bits 16 / org APP_LOAD_OFF first,
; so this must be the first thing after the %include. The image-size and
; bss-size words are forward references resolved by JOP_IMAGE_END / JOP_BSS
; at the end of the source. The name is NUL-padded to exactly 16 bytes.
; The optional third parameter is the flags byte (default 0); flags bit 0
; declares an embedded icon, which must follow immediately as a
; JOP_ICON16 .. JOP_ICON16_END block.
; -----------------------------------------------------------------------------
%macro JOP_HEADER 2-3
    cpu 8086
    bits 16
%ifdef JOP_ORG
    org JOP_ORG             ; relocation-probe pass (Makefile passes
%else                       ; -DJOP_ORG=0xA800; jopkg.py diffs the two
    org APP_LOAD_OFF        ; assemblies into the fixup table, SPEC.md 24)
%endif
    db 'J', 'P'             ; +0  magic (word 0x504A)
    db 2                    ; +2  format version 2: relocatable (SPEC.md 20.2)
    %if %0 >= 3
    db %3                   ; +3  flags (bit 0 = embedded icon)
    %else
    db 0                    ; +3  flags
    %endif
    dw APP_LOAD_OFF         ; +4  link base (constant across both passes)
    dw (%2) - $$            ; +6  entry offset, image-relative
    dw JOP_IMAGE_SIZE       ; +8  image size (resident bytes; the reloc
                            ;     table jopkg appends is excluded)
    dw JOP_BSS_SIZE         ; +10 bytes the loader zeroes after the image
    dw 0                    ; +12 relocation count - stamped by jopkg.py
    dw 0                    ; +14 reserved
    %strlen %%len %1
    %if %%len > 15
        %error "JOP_HEADER: program name must be at most 15 characters (the 16-byte field is NUL-padded)"
    %endif
    db %1                   ; +16 program name...
    times 16 - %%len db 0   ;     ...NUL-padded to 16 bytes
%endmacro

; -----------------------------------------------------------------------------
; JOP_ICON16 / JOP_ICON16_END - bracket the embedded 16x16 icon (SPEC.md
; 20.2/20.5). Only valid when JOP_HEADER was given flags bit 0. The author
; writes exactly 32 hand-authored dw rows between the two macros: 16 mask
; words (white underlay) then 16 data words (black pixels), bit 15 =
; leftmost pixel, row-major. JOP_ICON16 asserts the block starts right
; after the header (file offset 32); JOP_ICON16_END asserts it ends at
; offset 96, i.e. exactly 64 bytes of rows were written.
; -----------------------------------------------------------------------------
%macro JOP_ICON16 0
    %if ($ - $$) != 32
        %error "JOP_ICON16 must immediately follow JOP_HEADER (file offset 32)"
    %endif
%endmacro

%macro JOP_ICON16_END 0
    %if ($ - $$) != 96
        %error "JOP_ICON16 block must be exactly 32 dw rows (16 mask + 16 data, ending at offset 96)"
    %endif
%endmacro

; -----------------------------------------------------------------------------
; JOP_BSS totalbytes - declare the package's zeroed-bss size (optional).
; Emits nothing; must appear before JOP_IMAGE_END (which supplies the 0
; default when this macro is absent). The argument must be a plain constant
; expression with no forward references.
; -----------------------------------------------------------------------------
%macro JOP_BSS 1
    %ifdef JOP__HAVE_END
        %error "JOP_BSS must appear before JOP_IMAGE_END"
    %endif
    %define JOP__HAVE_BSS
JOP_BSS_SIZE equ (%1)
%endmacro

; -----------------------------------------------------------------------------
; JOP_IMAGE_END - close the package. Defines jop_image_end (the label a
; package bases its bss offsets on), JOP_IMAGE_SIZE = $-$$ for the header,
; defaults JOP_BSS_SIZE to 0 when no JOP_BSS was given, and enforces the
; APP_MAX_SIZE budget at assembly time.
; -----------------------------------------------------------------------------
%macro JOP_IMAGE_END 0
    %define JOP__HAVE_END
jop_image_end:
JOP_IMAGE_SIZE equ jop_image_end - $$
    %ifndef JOP__HAVE_BSS
JOP_BSS_SIZE equ 0
    %endif
    %if (JOP_IMAGE_SIZE + JOP_BSS_SIZE) > APP_MAX_SIZE
        %error "package too big: image + bss must fit in APP_MAX_SIZE (0x5000)"
    %endif
%endmacro
