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
W16EXPORT W16M_KERNEL,     4,       6,  w16k_linit          ; LocalInit(word word word)
W16EXPORT W16M_KERNEL,     5,       4,  w16k_lalloc         ; LocalAlloc(word word)
W16EXPORT W16M_KERNEL,     6,       6,  w16k_lrealloc       ; LocalReAlloc(word word word)
W16EXPORT W16M_KERNEL,     7,       2,  w16k_lfree          ; LocalFree(word)
W16EXPORT W16M_KERNEL,     8,       2,  w16k_llock          ; LocalLock(word)
W16EXPORT W16M_KERNEL,     9,       2,  w16k_lunlock        ; LocalUnlock(word)
W16EXPORT W16M_KERNEL,     10,      2,  w16k_lsize          ; LocalSize(word)
W16EXPORT W16M_KERNEL,     11,      2,  w16k_lhandle        ; LocalHandle(word)
W16EXPORT W16M_KERNEL,     13,      2,  w16k_lcompact       ; LocalCompact(word)
W16EXPORT W16M_KERNEL,     15,      6,  w16k_galloc         ; GlobalAlloc(word long)
W16EXPORT W16M_KERNEL,     16,      8,  w16k_grealloc       ; GlobalReAlloc(word long word)
W16EXPORT W16M_KERNEL,     17,      2,  w16k_gfree          ; GlobalFree(word)
W16EXPORT W16M_KERNEL,     18,      2,  w16k_glock          ; GlobalLock(word)
W16EXPORT W16M_KERNEL,     19,      2,  w16k_gunlock        ; GlobalUnlock(word)
W16EXPORT W16M_KERNEL,     20,      2,  w16k_gsize          ; GlobalSize(word)
W16EXPORT W16M_KERNEL,     21,      2,  w16k_ghandle        ; GlobalHandle(word)
W16EXPORT W16M_KERNEL,     22,      2,  w16k_gflags         ; GlobalFlags(word)
W16EXPORT W16M_KERNEL,     23,      2,  w16k_lockseg        ; LockSegment(word)
W16EXPORT W16M_KERNEL,     24,      2,  w16k_lockseg        ; UnlockSegment(word)
W16EXPORT W16M_KERNEL,     25,      4,  w16k_gcompact       ; GlobalCompact(long)
W16EXPORT W16M_KERNEL,     57,      10, w16k_getprofint     ; GetProfileInt(str str s_word)
W16EXPORT W16M_KERNEL,     58,      18, w16k_getprofstr     ; GetProfileString(str str str ptr word)
W16EXPORT W16M_KERNEL,     59,      12, w16k_writeprof      ; WriteProfileString(str str str)
W16EXPORT W16M_KERNEL,     60,      10, w16k_findres        ; FindResource(word str str)
W16EXPORT W16M_KERNEL,     61,      4,  w16k_loadres        ; LoadResource(word word)
W16EXPORT W16M_KERNEL,     62,      2,  w16k_lockres        ; LockResource(word)
W16EXPORT W16M_KERNEL,     63,      2,  w16k_freeres        ; FreeResource(word)
W16EXPORT W16M_KERNEL,     65,      4,  w16k_sizeres        ; SizeofResource(word word)
W16EXPORT W16M_KERNEL,     88,      8,  w16k_lstrcpy        ; lstrcpy(segptr str)
W16EXPORT W16M_KERNEL,     89,      8,  w16k_lstrcat        ; lstrcat(segstr str)
W16EXPORT W16M_KERNEL,     30,      2,  w16k_waitevent      ; WaitEvent(word)
W16EXPORT W16M_KERNEL,     49,      8,  w16k_getmodfn       ; GetModuleFileName(word ptr s_word)
W16EXPORT W16M_KERNEL,     90,      4,  w16k_lstrlen        ; lstrlen(str)
W16EXPORT W16M_KERNEL,     91,      -2, w16k_inittask       ; InitTask() -register
W16EXPORT W16M_KERNEL,     127,     14, w16k_getpprofint    ; GetPrivateProfileInt(str str s_word str)
W16EXPORT W16M_KERNEL,     128,     22, w16k_getpprofstr    ; GetPrivateProfileString(str str str ptr word str)
W16EXPORT W16M_KERNEL,     129,     16, w16k_writepprof     ; WritePrivateProfileString(str str str str)
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
W16EXPORT W16M_USER,       72,      12, w16u_setrect        ; SetRect(ptr s_word s_word s_word s_word)
W16EXPORT W16M_USER,       76,      8,  w16u_ptinrect       ; PtInRect(ptr long)
W16EXPORT W16M_USER,       81,      8,  w16u_fillrect       ; FillRect(word ptr word)
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
W16EXPORT W16M_USER,       176,     10, w16u_loadstr        ; LoadString(word word ptr s_word)
W16EXPORT W16M_USER,       150,     6,  w16u_loadmenu       ; LoadMenu(word str)
W16EXPORT W16M_USER,       151,     0,  w16u_createmenu     ; CreateMenu()
W16EXPORT W16M_USER,       152,     2,  w16u_destroymenu    ; DestroyMenu(word)
W16EXPORT W16M_USER,       153,     12, w16u_changemenu     ; ChangeMenu(word word segstr word word)
W16EXPORT W16M_USER,       154,     6,  w16u_checkmenu      ; CheckMenuItem(word word word)
W16EXPORT W16M_USER,       155,     6,  w16u_enablemenu     ; EnableMenuItem(word word word)
W16EXPORT W16M_USER,       156,     4,  w16u_sysmenu        ; GetSystemMenu(word word)
W16EXPORT W16M_USER,       157,     2,  w16u_getmenu        ; GetMenu(word)
W16EXPORT W16M_USER,       158,     4,  w16u_setmenu        ; SetMenu(word word)
W16EXPORT W16M_USER,       159,     4,  w16u_getsubmenu     ; GetSubMenu(word word)
W16EXPORT W16M_USER,       160,     2,  w16u_drawmenubar    ; DrawMenuBar(word)
W16EXPORT W16M_USER,       161,     12, w16u_menustring     ; GetMenuString(word word ptr s_word word)
W16EXPORT W16M_USER,       177,     6,  w16u_loadaccel      ; LoadAccelerators(word str)
W16EXPORT W16M_USER,       178,     8,  w16u_transaccel     ; TranslateAccelerator(word word ptr)
W16EXPORT W16M_USER,       179,     2,  w16u_sysmet         ; GetSystemMetrics(s_word)
W16EXPORT W16M_USER,       180,     2,  w16u_syscolor       ; GetSysColor(word)
W16EXPORT W16M_USER,       220,     4,  w16u_loadmenuind    ; LoadMenuIndirect(segptr)
W16EXPORT W16M_USER,       232,     14, w16u_setwndpos      ; SetWindowPos(word word word word word word word)
W16EXPORT W16M_USER,       250,     6,  w16u_menustate      ; GetMenuState(word word word)
W16EXPORT W16M_USER,       263,     2,  w16u_menucount      ; GetMenuItemCount(word)
W16EXPORT W16M_USER,       264,     4,  w16u_menuitemid     ; GetMenuItemID(word word)
W16EXPORT W16M_USER,       410,     12, w16u_insertmenu     ; InsertMenu(word word word word segptr)
W16EXPORT W16M_USER,       411,     10, w16u_appendmenu     ; AppendMenu(word word word segptr)
W16EXPORT W16M_USER,       412,     6,  w16u_removemenu     ; RemoveMenu(word word word)
W16EXPORT W16M_USER,       413,     6,  w16u_deletemenu     ; DeleteMenu(word word word)
W16EXPORT W16M_USER,       414,     12, w16u_modifymenu     ; ModifyMenu(word word word word segptr)
W16EXPORT W16M_USER,       415,     0,  w16u_createmenu     ; CreatePopupMenu()
W16EXPORT W16M_GDI,        4,       4,  w16g_setrop2        ; SetROP2(word word)
W16EXPORT W16M_GDI,        9,       6,  w16g_settextcol     ; SetTextColor(word long)
W16EXPORT W16M_GDI,        19,      6,  w16g_lineto         ; LineTo(word s_word s_word)
W16EXPORT W16M_GDI,        20,      6,  w16g_moveto         ; MoveTo(word s_word s_word)
W16EXPORT W16M_GDI,        27,      10, w16g_rect           ; Rectangle(word s_word x4)
W16EXPORT W16M_GDI,        28,      14, w16g_roundrect      ; RoundRect(word s_word x6)
W16EXPORT W16M_GDI,        31,      10, w16g_setpixel       ; SetPixel(word s_word s_word long)
W16EXPORT W16M_GDI,        33,      12, w16g_textout        ; TextOut(word s_word s_word str word)
W16EXPORT W16M_GDI,        45,      4,  w16g_select         ; SelectObject(word word)
W16EXPORT W16M_GDI,        61,      8,  w16g_createpen      ; CreatePen(s_word s_word long)
W16EXPORT W16M_GDI,        66,      4,  w16g_solidbrush     ; CreateSolidBrush(long)
W16EXPORT W16M_GDI,        69,      2,  w16g_delobj         ; DeleteObject(word)
W16EXPORT W16M_GDI,        80,      4,  w16g_devcaps        ; GetDeviceCaps(word s_word)
W16EXPORT W16M_GDI,        87,      2,  w16g_stock          ; GetStockObject(word)
W16EXPORT W16M_GDI,        91,      8,  w16g_textext        ; GetTextExtent(word ptr s_word)
W16EXPORT W16M_GDI,        93,      6,  w16g_textmet        ; GetTextMetrics(word ptr)
W16EXPORT W16M_GDI,        443,     28, w16g_setdib         ; SetDIBitsToDevice(word s_word x6 word word ptr ptr word)
W16EXPORT W16M_KEYBOARD,   132,     0,  w16kb_codepage      ; GetKBCodePage()
W16EXPORT W16M_SOUND,      1,       0,  w16s_open           ; OpenSound()
W16EXPORT W16M_SOUND,      2,       0,  w16s_none           ; CloseSound()
W16EXPORT W16M_SOUND,      4,       8,  w16s_none           ; SetVoiceNote(word word word word)
W16EXPORT W16M_SOUND,      5,       10, w16s_none           ; SetVoiceAccent(word word word word word)
W16EXPORT W16M_SOUND,      9,       0,  w16s_none           ; StartSound()
W16EXPORT W16M_SOUND,      10,      0,  w16s_none           ; StopSound()
; the callee's way back (SPEC.md 124.2.2): module 0FEh, which no program's
; module table can name, so the loader never binds an import to it
W16_CBROW equ W16NX
W16EXPORT 0xFE,            0,       0,  w16_cbdone
; an import WIN16 does not provide, called (SPEC.md 124.12): module 0FDh, never
; bound by the loader either - the missing stubs below reach it
W16_MISSROW equ W16NX
W16EXPORT 0xFD,            0,       0,  w16_missed

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

; The missing stubs (SPEC.md 124.12): slot k of w16_mtxt is `mov ax, k /
; jmp near w16_missgate`, which banks k and enters the gate as W16_MISSROW.
w16_mstubs:
    ; STKBALANCE-OK: entered by the program's far call, like w16_stubs
%assign k 0
%rep W16_NMISS
    mov ax, k
    jmp near w16_missgate
%assign k k + 1
%endrep
%if ($ - w16_mstubs) != W16_NMISS * 6
  %error "w16_mstubs: every stub must be six bytes"
%endif
w16_missgate:
    ; STKBALANCE-OK: the gate's frame, as w16_stubs
    mov [cs:w16_missi], ax          ; DS is the program's: CS is ours
    mov ax, W16_MISSROW
    jmp w16_gate

; w16_missed - the W16_MISSROW handler: the program called an import WIN16
; does not provide. It ends as AH=4Ch ends it, but WIN16's window stays, back
; to its own two lines, naming the call (w16_endshow): a toast would go with
; the window that raised it (SPEC.md 59).
w16_missed:
    push si
    push di
    mov si, w16_r_needs
    mov di, w16_l1
    call w16_strcpy
    push di
    mov bx, [w16_missi]
    call w16_mslot                  ; DI -> its name
    mov si, di
    pop di
    call w16_strcpy
    mov si, w16_r_notyet
    mov di, w16_l2
    call w16_strcpy
    mov byte [w16_endmsg], 1
    mov byte [w16_state], W16S_ENDED
    pop di
    pop si
    ret

w16_r_needs:   db 'Needs ', 0
w16_r_notyet:  db 'which WIN16 does not provide yet.', 0

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
    mov dx, [w16_dgsize]            ; the local heap: ne_heap bytes after the
    or dx, dx                       ;   stack, to DGROUP's end (SPEC.md 124.9)
    jnz .dgend
    mov dx, 0xFFFC
.dgend:
    mov bx, dx
    sub bx, [w16_heap]
    jbe .noheap
    cmp word [w16_heap], 0
    je .noheap
    mov ax, [w16_dgroup]
    call w16_linit
.noheap:
    mov ax, 1
    mov dx, 1                       ; SW_SHOWNORMAL
    ret

; lstrlen (KERNEL.90) - args: +0 lpString
w16k_lstrlen:
    call w16_args
    les bx, [es:bx]
    xor ax, ax
.c:
    cmp byte [es:bx], 0
    je .out
    inc ax
    inc bx
    jmp short .c
.out:
    ret

; WaitEvent (KERNEL.30): no events are ever posted to a task here
w16k_waitevent:
    xor ax, ax
    ret

; --- profiles, until S7 (SPEC.md 124.12's plan line): the reads answer as an
; empty WIN.INI does, with their defaults; the writes are accepted and not yet
; kept ---------------------------------------------------------------------------

; GetProfileInt (KERNEL.57) - args: +0 nDefault, +2 lpKey, +6 lpApp
w16k_getprofint:
    call w16_args
    mov ax, [es:bx]
    ret

; GetPrivateProfileInt (KERNEL.127) - args: +0 lpFile, +4 nDefault, ...
w16k_getpprofint:
    call w16_args
    mov ax, [es:bx + 4]
    ret

; GetProfileString (KERNEL.58) - args: +0 nSize, +2 lpBuf, +6 lpDefault,
; +10 lpKey, +14 lpApp. The default, cut to nSize - 1; AX = its length
w16k_getprofstr:
    call w16_args
    jmp short w16_profdef

; GetPrivateProfileString (KERNEL.128) - args: +0 lpFile, then as above
w16k_getpprofstr:
    call w16_args
    add bx, 4
    ; fall into w16_profdef

; w16_profdef - ES:BX = GetProfileString's arguments: copy the default
w16_profdef:
    push si
    push di
    push ds
    mov cx, [es:bx]
    xor ax, ax
    jcxz .out
    dec cx
    mov si, [es:bx + 6]
    mov dx, [es:bx + 8]
    les di, [es:bx + 2]
    or dx, dx                       ; a NULL default is ""
    jz .end
    mov ds, dx
.c:
    jcxz .end
    mov dl, [si]
    or dl, dl
    jz .end
    mov [es:di], dl
    inc si
    inc di
    inc ax
    dec cx
    jmp short .c
.end:
    mov byte [es:di], 0
.out:
    pop ds
    pop di
    pop si
    ret

; WriteProfileString (KERNEL.59), WritePrivateProfileString (KERNEL.129):
; TRUE, and nothing kept until S7 gives them a file
w16k_writeprof:
w16k_writepprof:
    mov ax, 1
    ret

; lstrcpy (KERNEL.88) - args: +0 lpSrc, +4 lpDst. DX:AX = lpDst
w16k_lstrcpy:
    call w16_args
    xor cx, cx
    jmp short w16_lstr
; lstrcat (KERNEL.89) - args: as lstrcpy, the copy at lpDst's end
w16k_lstrcat:
    call w16_args
    mov cx, 1
    ; fall into w16_lstr
w16_lstr:
    push si
    push di
    push ds
    mov ax, [es:bx + 4]
    mov dx, [es:bx + 6]
    push ax
    push dx
    lds si, [es:bx]
    les di, [es:bx + 4]
    jcxz .copy
.end:
    cmp byte [es:di], 0
    je .copy
    inc di
    jmp short .end
.copy:
    lodsb
    stosb
    or al, al
    jnz .copy
    pop dx
    pop ax
    pop ds
    pop di
    pop si
    ret

; --- SOUND, until S8 plays it through OSAPI_SND_TONE: OpenSound answers
; S_SERDVNA (-1), "the device is not available", which a Windows program
; already handles - a machine with its sound driver unloaded said the same
w16s_open:
    mov ax, 0xFFFF
    ret
w16s_none:
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
