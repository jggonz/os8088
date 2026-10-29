; =============================================================================
; os8088 - apps/win16/w16api.inc
;
; WHAT WIN16 EXPORTS: the Windows modules' functions, by ordinal, each a row
; and a six-byte stub (SPEC.md 124.2.1). The ordinals and argument sizes are
; tools/win16spec/'s (winevdm's, which are Wine's); a row's argument bytes
; are what the pascal callee pops, so they are the .spec's argument list
; summed: word/s_word 2, long/ptr/str/segptr/segstr 4.
;
; S2's set is what the two simplest fixtures need to reach a message box and
; exit: tests/win16/tiny16.asm and MSGBOX.EXE (Open Watcom's start-up adds
; GetVersion, GetModuleFileName, GetWinFlags, IsDBCSLeadByte and
; GetKBCodePage to MessageBox, InitTask, WaitEvent and InitApp).
; =============================================================================

W16M_KERNEL   equ 0             ; the module ids, in w16_modnames' order
W16M_USER     equ 1
W16M_GDI      equ 2
W16M_KEYBOARD equ 3
W16M_SOUND    equ 4
W16M_WIN87EM  equ 5
W16M_SHELL    equ 6

w16_modnames:                   ; pascal strings, then 0
    db 6, 'KERNEL'
    db 4, 'USER'
    db 3, 'GDI'
    db 8, 'KEYBOARD'
    db 5, 'SOUND'
    db 7, 'WIN87EM'
    db 5, 'SHELL'
    db 0

; --- the export rows ----------------------------------------------------------
W16X_MOD  equ 0
W16X_ORD  equ 1
W16X_ARGB equ 3
W16X_PROC equ 4
W16X_SIZE equ 6

%assign W16NX 0
%macro W16EXPORT 4              ; module, ordinal, argument bytes, handler
%xdefine w16x_m_%[W16NX] %1
%xdefine w16x_o_%[W16NX] %2
%xdefine w16x_a_%[W16NX] %3
%xdefine w16x_p_%[W16NX] %4
%assign W16NX W16NX + 1
%endmacro

;           module         ordinal  args  handler               the .spec line
W16EXPORT W16M_KERNEL,     3,       0,  w16k_getversion     ; GetVersion()
W16EXPORT W16M_KERNEL,     30,      2,  w16k_waitevent      ; WaitEvent(word)
W16EXPORT W16M_KERNEL,     49,      8,  w16k_getmodfn       ; GetModuleFileName(word ptr s_word)
W16EXPORT W16M_KERNEL,     91,      -2, w16k_inittask       ; InitTask() -register
W16EXPORT W16M_KERNEL,     132,     0,  w16k_getwinflags    ; GetWinFlags()
W16EXPORT W16M_KERNEL,     207,     2,  w16k_isdbcs         ; IsDBCSLeadByte(word)
W16EXPORT W16M_USER,       1,       12, w16u_msgbox         ; MessageBox(word str str word)
W16EXPORT W16M_USER,       5,       2,  w16u_initapp        ; InitApp(word)
W16EXPORT W16M_USER,       6,       2,  w16u_postquit       ; PostQuitMessage(word)
W16EXPORT W16M_USER,       10,      10, w16u_settimer       ; SetTimer(word word word segptr)
W16EXPORT W16M_USER,       12,      4,  w16u_killtimer      ; KillTimer(word word)
W16EXPORT W16M_USER,       13,      0,  w16u_tickcount      ; GetTickCount()
W16EXPORT W16M_USER,       15,      0,  w16u_tickcount      ; GetCurrentTime()
W16EXPORT W16M_USER,       18,      2,  w16u_setcapture     ; SetCapture(word)
W16EXPORT W16M_USER,       19,      0,  w16u_relcapture     ; ReleaseCapture()
W16EXPORT W16M_USER,       33,      6,  w16u_clientrect     ; GetClientRect(word ptr)
W16EXPORT W16M_USER,       39,      6,  w16u_beginpaint     ; BeginPaint(word ptr)
W16EXPORT W16M_USER,       40,      6,  w16u_endpaint       ; EndPaint(word ptr)
W16EXPORT W16M_USER,       41,      30, w16u_createwnd      ; CreateWindow(str str long s_word x4 word word word segptr)
W16EXPORT W16M_USER,       42,      4,  w16u_showwnd        ; ShowWindow(word word)
W16EXPORT W16M_USER,       53,      2,  w16u_destroywnd     ; DestroyWindow(word)
W16EXPORT W16M_USER,       56,      12, w16u_movewnd        ; MoveWindow(word word word word word word)
W16EXPORT W16M_USER,       57,      4,  w16u_regclass       ; RegisterClass(ptr)
W16EXPORT W16M_USER,       66,      2,  w16u_getdc          ; GetDC(word)
W16EXPORT W16M_USER,       68,      4,  w16u_releasedc      ; ReleaseDC(word word)
W16EXPORT W16M_USER,       107,     10, w16u_defproc        ; DefWindowProc(word word word long)
W16EXPORT W16M_USER,       108,     10, w16u_getmsg         ; GetMessage(ptr word word word)
W16EXPORT W16M_USER,       109,     12, w16u_peekmsg        ; PeekMessage(ptr word word word word)
W16EXPORT W16M_USER,       110,     10, w16u_postmsg        ; PostMessage(word word word long)
W16EXPORT W16M_USER,       111,     10, w16u_sendmsg        ; SendMessage(word word word long)
W16EXPORT W16M_USER,       113,     4,  w16u_translate      ; TranslateMessage(ptr)
W16EXPORT W16M_USER,       114,     4,  w16u_dispatch       ; DispatchMessage(ptr)
W16EXPORT W16M_USER,       124,     2,  w16u_updatewnd      ; UpdateWindow(word)
W16EXPORT W16M_USER,       125,     8,  w16u_invalrect      ; InvalidateRect(word ptr word)
W16EXPORT W16M_USER,       127,     6,  w16u_validrect      ; ValidateRect(word ptr)
W16EXPORT W16M_USER,       173,     6,  w16u_loadcursor     ; LoadCursor(word str)
W16EXPORT W16M_USER,       174,     6,  w16u_loadicon       ; LoadIcon(word str)
W16EXPORT W16M_USER,       232,     14, w16u_setwndpos      ; SetWindowPos(word word word word word word word)
W16EXPORT W16M_GDI,        19,      6,  w16g_lineto         ; LineTo(word s_word s_word)
W16EXPORT W16M_GDI,        20,      6,  w16g_moveto         ; MoveTo(word s_word s_word)
W16EXPORT W16M_GDI,        27,      10, w16g_rect           ; Rectangle(word s_word x4)
W16EXPORT W16M_GDI,        33,      12, w16g_textout        ; TextOut(word s_word s_word str word)
W16EXPORT W16M_GDI,        45,      4,  w16g_select         ; SelectObject(word word)
W16EXPORT W16M_GDI,        66,      4,  w16g_solidbrush     ; CreateSolidBrush(long)
W16EXPORT W16M_GDI,        69,      2,  w16g_delobj         ; DeleteObject(word)
W16EXPORT W16M_GDI,        87,      2,  w16g_stock          ; GetStockObject(word)
W16EXPORT W16M_KEYBOARD,   132,     0,  w16kb_codepage      ; GetKBCodePage()
; the callee's way back (SPEC.md 124.2.2): module 0FEh, which no program's
; module table can name, so the loader never binds an import to it
W16_CBROW equ W16NX
W16EXPORT 0xFE,            0,       0,  w16_cbdone

w16_exports:
%assign k 0
%rep W16NX
    db w16x_m_%[k]
    dw w16x_o_%[k]
    db w16x_a_%[k]
    dw w16x_p_%[k]
%assign k k + 1
%endrep

; The stubs: row k is `mov ax, k / jmp near w16_gate`, six bytes, so a stub's
; address is w16_stubs + 6k. `near` is not decoration - a short jmp would make
; one stub five bytes and every address after it wrong.
w16_stubs:
    ; STKBALANCE-OK: each stub is `mov ax, k / jmp near w16_gate`, entered by
    ; the program's far call - the gate's frame, not a routine of its own
%assign k 0
%rep W16NX
    mov ax, k
    jmp near w16_gate
%assign k k + 1
%endrep
%if ($ - w16_stubs) != W16NX * 6
  %error "w16_stubs: every stub must be six bytes"
%endif

; -----------------------------------------------------------------------------
; w16_findexp - DL = module, AX = ordinal -> AX = the row; CF=1 not ours
; -----------------------------------------------------------------------------
w16_findexp:
    push bx
    push cx
    mov bx, w16_exports
    xor cx, cx
.row:
    cmp cx, W16NX
    jae .none
    cmp [bx + W16X_MOD], dl
    jne .next
    cmp [bx + W16X_ORD], ax
    je .hit
.next:
    add bx, W16X_SIZE
    inc cx
    jmp short .row
.hit:
    mov ax, cx
    pop cx
    pop bx
    clc
    ret
.none:
    pop cx
    pop bx
    stc
    ret

; =============================================================================
; KERNEL
; =============================================================================

; InitTask (KERNEL.91), Wine's InitTask16: the INSTANCEDATA at DGROUP:0, then
; AX = 1, BX = the command line in the PSP, CX = the stack limit, DX =
; nCmdShow, SI = hPrevInstance, DI = hInstance, ES = the PSP, and a zero
; pushed. The local heap (LocalInit) arrives with LocalAlloc, in S3.
INST_STACKTOP equ 0x0A
INST_STACKMIN equ 0x0C
INST_STACKBOT equ 0x0E

w16k_inittask:
    push es
    mov ax, [w16_psp_]              ; the program's SP once this returns
    add ax, W16F_ARGS - 2
    mov es, [w16_dgroup]
    mov [es:INST_STACKMIN], ax
    mov [es:INST_STACKBOT], ax
    sub ax, [w16_rbx]               ; BX = the stack size, on the way in
    jnb .top
    xor ax, ax
.top:
    add ax, 150
    mov [es:INST_STACKTOP], ax
    pop es
    mov [w16_rcx], ax
    mov word [w16_rbx], 0x80        ; an empty command tail
    mov ax, [w16_dgroup]
    mov [w16_rdi], ax
    mov word [w16_rsi], 0
    mov ax, [w16_psp]
    mov [w16_res], ax
    or byte [w16_rflags], W16R_SIDI
    mov ax, 1
    mov dx, 1                       ; SW_SHOWNORMAL
    ret

; WaitEvent (KERNEL.30): no events are ever posted to a task here
w16k_waitevent:
    xor ax, ax
    ret

; GetVersion (KERNEL.3): Windows 3.10 in AX (AL = major), DOS 3.30 in DX -
; the same DOS int 21h AH=30h answers
w16k_getversion:
    mov ax, 0x0A03
    mov dx, 0x031E
    ret

; GetWinFlags (KERNEL.132): real mode, so never WF_PMODE; the CPU class the
; loader measured, and the coprocessor when there is one
WF_CPU286 equ 0x0002
WF_CPU086 equ 0x0040
WF_CPU186 equ 0x0080
WF_80x87  equ 0x0400

w16k_getwinflags:
    call OSAPI_CPU_INFO             ; AL = the tier, AH = CPU_F_*
    mov dx, WF_CPU286
    cmp al, CPU_8086
    jne .fpu
    mov dx, WF_CPU186
    cmp byte [w16_cpu186], 0
    jne .fpu
    mov dx, WF_CPU086
.fpu:
    test ah, CPU_F_X87
    jz .out
    or dx, WF_80x87
.out:
    mov ax, dx
    xor dx, dx
    ret

; IsDBCSLeadByte (KERNEL.207): no double-byte character sets here
w16k_isdbcs:
    xor ax, ax
    ret

; GetModuleFileName (KERNEL.49): "A:\NAME.EXE", truncated to the buffer.
; WIN16 knows the program's drive and name, not its folder's path, and the
; folder is all a runtime does with this (argv[0]).
; args: +0 nSize, +2 lpBuffer (off, seg), +6 hModule
w16k_getmodfn:
    push si
    push di
    call w16_args
    mov cx, [es:bx]                 ; nSize
    les di, [es:bx + 2]
    mov [w16_scratch + 2], di       ; where the name starts, for the length
    xor ax, ax
    jcxz .out
    dec cx                          ; room for the NUL
    mov al, [w16_vol]
    add al, 'A'
    call .put
    mov al, ':'
    call .put
    mov al, '\'
    call .put
    mov si, w16_name
.name:
    lodsb
    or al, al
    jz .end
    call .put
    jmp short .name
.end:
    mov byte [es:di], 0
    mov ax, di
    sub ax, [w16_scratch + 2]
.out:
    pop di
    pop si
    ret
.put:                               ; AL -> ES:DI if CX allows
    jcxz .full
    mov [es:di], al
    inc di
    dec cx
.full:
    ret

; =============================================================================
; USER
; =============================================================================

; InitApp (USER.5): the message queue exists from the start here
w16u_initapp:
    mov ax, 1
    ret

; MessageBox (USER.1) - S2's face for it is os88ui's standard alert (SPEC.md
; 75.3): one line of the text, the caption as the window's title while it is
; up, and the button set nearest the style. S6 replaces it with a Windows
; message box. It WAITS: the call stays pending until the alert is answered.
; args: +0 type, +2 caption (off, seg), +6 text (off, seg), +10 hWnd
MB_OKCANCEL equ 1
MB_YESNO    equ 4
IDOK        equ 1
IDCANCEL    equ 2
IDYES       equ 6
IDNO        equ 7

w16u_msgbox:
    push si
    push di
    call w16_args
    mov ax, [es:bx]
    and al, 0x0F
    mov [w16_mbtype], al
    push bx
    mov si, [es:bx + 2]             ; the caption, or "Error" for NULL
    mov dx, [es:bx + 4]
    mov di, w16_mbcap
    mov cx, 40
    mov ax, w16_s_error
    call w16_farline
    pop bx
    mov si, [es:bx + 6]             ; the text's first line
    mov dx, [es:bx + 8]
    mov di, w16_mbtext
    mov cx, OS88UI_AMAX             ; the alert's line; S6's MessageBox wraps
    mov ax, w16_s_empty
    call w16_farline
    mov al, OS88UI_AOK
    cmp byte [w16_mbtype], MB_OKCANCEL
    jne .notoc
    mov al, OS88UI_AOKCAN
.notoc:
    cmp byte [w16_mbtype], MB_YESNO
    jne .set
    mov al, OS88UI_AYESNO
.set:
    mov [w16_mbset], al
    cmp byte [w16_locked], 0
    jne .held
    call OSAPI_GFX_LOCK
.held:
    mov bx, [w16_win]
    mov ax, w16_mbcap               ; the alert is titled with ours
    call OSAPI_WM_TITLE
    mov al, [w16_mbset]
    mov si, w16_mbtext
    mov di, w16_mbdone
    call os88ui_ask
    pushf
    cmp byte [w16_locked], 0
    jne .held2
    call OSAPI_GFX_UNLOCK
.held2:
    popf
    mov ax, 0                       ; refused (one is already up): 0, which is
    jc .out                         ; MessageBox's own failure answer
    mov byte [w16_pend], 1          ; ...else wait for the answer
.out:
    pop di
    pop si
    ret

; w16_mbdone - the alert's completion: AL = the button or OS88UI_ACANCEL;
; SI = our window, the gfx lock HELD. Finish MessageBox and run on.
w16_mbdone:
    mov ah, [w16_mbset]
    mov dx, IDOK
    cmp ah, OS88UI_AOK
    je .ans                         ; MB_OK has no cancel: a dismissal is OK
    cmp ah, OS88UI_AYESNO
    jne .okcan
    mov dx, IDYES
    or al, al
    jz .ans
    mov dx, IDNO
    jmp short .ans
.okcan:
    or al, al
    jz .ans
    mov dx, IDCANCEL
.ans:
    push dx
    mov bx, [w16_win]
    mov ax, [w16_titleptr]          ; our own title back
    call OSAPI_WM_TITLE
    pop ax
    xor dx, dx
    mov byte [w16_locked], 1
    call w16_resume
    mov byte [w16_locked], 0
    jmp w16_after

; w16_farline - copy a far string's first line: DX:SI -> DI, at most CX
; characters, stopping at NUL, CR or LF; a NULL pointer copies the string at
; AX instead. DI ends at the NUL.
w16_farline:
    push es
    mov es, dx
    or dx, si
    jnz .far
    mov si, ax
    call w16_strcpy
    pop es
    ret
.far:
    mov al, [es:si]
    or al, al
    jz .end
    cmp al, 13
    je .end
    cmp al, 10
    je .end
    mov [di], al
    inc si
    inc di
    loop .far
.end:
    mov byte [di], 0
    pop es
    ret

; =============================================================================
; KEYBOARD
; =============================================================================

; GetKBCodePage (KEYBOARD.132): the IBM PC's own
w16kb_codepage:
    mov ax, 437
    ret

w16_mbtype: db 0
w16_mbset:  db 0
w16_s_error: db 'Error', 0
w16_s_empty: db 0
