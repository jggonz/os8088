; =============================================================================
; os8088 - apps/hello/hello.asm
;
; HELLO, the second software package (SPEC.md 27). Deliberately minimal: it
; proves the SDK surface (header macro, wm_create template, paint via the
; API table) and the no-icon fallback - no flags bit 0, so the Disk window
; shows the built-in ico_app16 for it. One window, two centred lines of
; text, no onkey, no onclick.
;
; It is also the worked example for app menus (SPEC.md 12.2): while its
; window is frontmost the bar reads chip / "Hello" / "File", and File's two
; items - Greeting and About - pick which pair of strings hl_paint draws.
; That is the whole of the menu machinery an app needs: one OS88_MENUSET
; block of data, one OSAPI_MENU_SET call in the entry proc, one command
; handler, and one byte of bss saying which page is showing.
;
; The entry point only creates the window (wm_create is lock-free), attaches
; the menu set and returns BX = window ptr / CF clear; the loader shows it.
; The paint proc runs with the gfx lock already held (SPEC.md 11) and
; fetches the content origin via wm_content each call (the window moves);
; the command handler runs under that same lock, on the UI task, exactly
; like an onclick would.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'HELLO', hl_entry

HL_CONT_W equ 238                   ; content width: 240 outer - 2px borders
HL_CONT_H equ 71                    ; content height: 90 outer - TITLE_H - 1

; -----------------------------------------------------------------------------
; hl_entry - package entry point (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here.
;
; Registering the menu set here is what makes the very first bar the loader
; draws already ours (SPEC.md 12.2). Our success contract is CF clear, and
; it holds without a re-clear: the branch below already consumed wm_create's
; CF, OSAPI_MENU_SET preserves the flags as well as the registers
; (SPEC.md 20.3), and pop does not disturb them - so an abort's CF reaches
; the loader untouched down the same path.
; -----------------------------------------------------------------------------
hl_entry:
    push si
    mov si, hl_tpl
    call OSAPI_WM_CREATE             ; BX = window ptr, CF on table full
    jc .out
    mov si, hl_menus
    call OSAPI_MENU_SET              ; BX = the window we just created
.out:
    pop si
    retf                            ; far-called by the loader (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; hl_paint - W_PAINT: two centred lines on the white content
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
;
; Which two lines is [hl_page], an index into hl_pages' 2-word entries. The
; paint proc is the only reader, so a menu command is done once it has
; stored the new page and asked for a repaint.
; -----------------------------------------------------------------------------
hl_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT            ; AX = content left, DX = content top
    mov bx, ax                      ; keep the content left in BX
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov al, [hl_page]               ; 0 or 1: hl_oncmd is its only writer
    xor ah, ah
    shl ax, 1
    shl ax, 1                       ; 4 bytes per page: two string pointers
    add ax, hl_pages                ; a plain org-0 offset (SPEC.md 20.1)
    mov si, ax
    add dx, 25                      ; content is 71px tall; two 8px lines
    push si                         ; 12px apart, centred as a 20px block
    mov si, [si]                    ; first line of the page
    call hl_line
    pop si
    add dx, 12
    mov si, [si+2]                  ; second line of the page
    call hl_line
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf                            ; far-called W_PAINT (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; hl_line - draw one line centred in the content width
; in:  SI = NUL string, BX = content left, DX = y
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
hl_line:
    push ax
    push cx
    call OSAPI_FONT_WIDTH            ; AX = pixel width
    mov cx, HL_CONT_W
    sub cx, ax
    shr cx, 1
    add cx, bx
    call OSAPI_FONT_STR
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hl_oncmd - AM_ONCMD: the File menu (SPEC.md 12.2)
; in:  AL = item index (0 = Greeting, 1 = About), AH = menu index (always 0 -
;      File is our only menu), SI = the owning window, BX = the menu set
; out: nothing; clobbers AX/BX/CX/DX like any window callback
;
; Called on the UI task with the gfx lock already held, so it draws directly
; and must never take the lock itself. The item index IS the page index -
; the two items are declared in hl_pages' order, which is the smallest
; honest wiring an app can have. The kernel does not repaint after a
; command returns, so the redraw is ours.
; -----------------------------------------------------------------------------
hl_oncmd:
    mov [hl_page], al
    call hl_repaint
    retf                            ; far-called menu handler (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; hl_repaint - erase the content and draw it again
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
;
; The white-fill idiom every self-repainting app uses: the lines differ in
; length between pages, so the old ones have to go before the new ones are
; drawn. The window is not resizable, so the content rect is the constant
; HL_CONT_W x HL_CONT_H from the wm_content origin and no OSAPI_WM_GROW is
; owed at the end.
; -----------------------------------------------------------------------------
hl_repaint:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT            ; AX = content left, DX = content top
    mov bx, dx                      ; y1 = top
    mov cx, ax
    add cx, HL_CONT_W-1             ; x2
    add dx, HL_CONT_H-1             ; y2
    call OSAPI_GFX_FILL             ; AX = x1 already
    push cs                         ; hl_paint is a kernel-called proc and
    call hl_paint                   ; returns with retf: give it the CS a
                                    ; far call would have pushed
                                    ; (SI = window ptr still)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
hl_tpl:
    dw 200, 150, 240, 90            ; x, y, w, h -> content 238 x 71
    dw hl_ttl, hl_paint, 0, 0       ; no onkey, no onclick

; --- the app menu set (SPEC.md 12.2) -------------------------------------------
    OS88_MENUSET hl_menus, hl_name, hl_oncmd
        OS88_MENU hl_m_file, hl_i_file, 2
    OS88_MENUSET_END hl_menus

hl_name:    db 'Hello', 0           ; the bar label, right of the chip menu
hl_m_file:  db 'File', 0
hl_i_file:  dw hl_it_greet, hl_it_about
hl_it_greet: db 'Greeting', 0
hl_it_about: db 'About', 0

; --- the two pages, in File's item order ---------------------------------------
hl_pages:
    dw hl_s_line1, hl_s_line2       ; 0: Greeting
    dw hl_s_abt1,  hl_s_abt2        ; 1: About

hl_ttl:     db 'Hello', 0
hl_s_line1: db 'Hello from a', 0
hl_s_line2: db '.o88 package!', 0
hl_s_abt1:  db 'HELLO - the smallest', 0
hl_s_abt2:  db 'os8088 package.', 0

    OS88_BSS 1
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; Zero = the Greeting page, which is what a freshly opened window shows.
hl_page     equ os88_image_end + 0   ; byte: page index into hl_pages
