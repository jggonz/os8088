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
; Since S14 they live in the LOADER part (SPEC.md 124.19.1): the loader and the
; census are their only readers, and each program's imports are bound into
; slots of w16_bind. A row's handler is an offset in the image or in the
; library part, and W16LIB is what tells the two apart (w16_exports, emitted
; at the end of win16.asm, after every handler is defined).
W16X_MOD  equ 0
W16X_ORD  equ 1
W16X_ARGB equ 3
W16X_PART equ 4
W16X_PROC equ 5
W16X_SIZE equ 7

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
W16EXPORT W16M_KERNEL,     50,      6,  w16k_getproc        ; GetProcAddress(word str)
W16EXPORT W16M_KERNEL,     51,      6,  w16k_mkprocinst     ; MakeProcInstance(segptr word)
W16EXPORT W16M_KERNEL,     52,      4,  w16k_freeprocinst   ; FreeProcInstance(segptr)
W16EXPORT W16M_KERNEL,     90,      4,  w16k_lstrlen        ; lstrlen(str)
W16EXPORT W16M_KERNEL,     91,      -2, w16k_inittask       ; InitTask() -register
W16EXPORT W16M_KERNEL,     127,     14, w16k_getpprofint    ; GetPrivateProfileInt(str str s_word str)
W16EXPORT W16M_KERNEL,     128,     22, w16k_getpprofstr    ; GetPrivateProfileString(str str str ptr word str)
W16EXPORT W16M_KERNEL,     129,     16, w16k_writepprof     ; WritePrivateProfileString(str str str str)
W16EXPORT W16M_KERNEL,     132,     0,  w16k_getwinflags    ; GetWinFlags()
W16EXPORT W16M_KERNEL,     207,     2,  w16k_isdbcs         ; IsDBCSLeadByte(word)
W16EXPORT W16M_KERNEL,     1,       2,  w16k_fatalexit      ; FatalExit(word)
W16EXPORT W16M_KERNEL,     115,     4,  w16k_debugstr       ; OutputDebugString(str)
W16EXPORT W16M_KERNEL,     137,     6,  w16k_fatalapp       ; FatalAppExit(word str)
W16EXPORT W16M_KERNEL,     134,     6,  w16k_windir         ; GetWindowsDirectory(ptr word)
W16EXPORT W16M_KERNEL,     135,     6,  w16k_windir         ; GetSystemDirectory(ptr word)
W16EXPORT W16M_KERNEL,     74,      10, w16k_openfile       ; OpenFile(str ptr word)
W16EXPORT W16M_KERNEL,     81,      2,  w16k_lclose         ; _lclose(word)
W16EXPORT W16M_KERNEL,     95,      4,  w16k_loadlib        ; LoadLibrary(str)
W16EXPORT W16M_KERNEL,     96,      2,  w16k_freelib        ; FreeLibrary(word)
; two bound by the loader to something other than a gate stub (w16_reloc1):
; DOS3Call to int 21h itself, __WINFLAGS to its value (SPEC.md 124.15.3)
W16_D3ROW equ W16NX
W16EXPORT W16M_KERNEL,     102,     0,  w16k_dos3call       ; DOS3Call() -register
W16_WFROW equ W16NX
W16EXPORT W16M_KERNEL,     178,     0,  w16k_getwinflags    ; __WINFLAGS, an equate
; the huge-pointer equates, real mode's values (SPEC.md 124.20.3)
W16_AHROW equ W16NX
W16EXPORT W16M_KERNEL,     114,     0,  w16x_never          ; __AHINCR = 1000h
W16EXPORT W16M_KERNEL,     113,     0,  w16x_never          ; __AHSHIFT = 12
; and __FPMATH to the emulator's or the coprocessor's (SPEC.md 124.5.2)
W16_FPROW equ W16NX
W16EXPORT W16M_WIN87EM,    1,       0,  w16x_never          ; __FPMATH() -register
W16EXPORT W16M_USER,       1,       12, w16u_msgbox         ; MessageBox(word str str word)
W16EXPORT W16M_USER,       5,       2,  w16u_initapp        ; InitApp(word)
W16EXPORT W16M_USER,       6,       2,  w16u_postquit       ; PostQuitMessage(word)
W16EXPORT W16M_USER,       10,      10, w16u_settimer       ; SetTimer(word word word segptr)
W16EXPORT W16M_USER,       12,      4,  w16u_killtimer      ; KillTimer(word word)
W16EXPORT W16M_USER,       13,      0,  w16u_tickcount      ; GetTickCount()
W16EXPORT W16M_USER,       15,      0,  w16u_tickcount      ; GetCurrentTime()
W16EXPORT W16M_USER,       18,      2,  w16u_setcapture     ; SetCapture(word)
W16EXPORT W16M_USER,       19,      0,  w16u_relcapture     ; ReleaseCapture()
W16EXPORT W16M_USER,       22,      2,  w16u_setfocus       ; SetFocus(word)
W16EXPORT W16M_USER,       23,      0,  w16u_getfocus       ; GetFocus()
W16EXPORT W16M_USER,       28,      6,  w16u_c2s            ; ClientToScreen(word ptr)
W16EXPORT W16M_USER,       29,      6,  w16u_s2c            ; ScreenToClient(word ptr)
W16EXPORT W16M_USER,       31,      2,  w16u_isiconic       ; IsIconic(word)
W16EXPORT W16M_USER,       45,      2,  w16u_totop          ; BringWindowToTop(word)
W16EXPORT W16M_USER,       50,      8,  w16u_findwnd        ; FindWindow(str str)
W16EXPORT W16M_USER,       130,     6,  w16u_setclsword     ; SetClassWord(word s_word word)
W16EXPORT W16M_USER,       175,     6,  w16u_loadbmp        ; LoadBitmap(word str)
W16EXPORT W16M_USER,       286,     0,  w16u_desktop        ; GetDesktopWindow()
W16EXPORT W16M_USER,       287,     2,  w16u_lastpopup      ; GetLastActivePopup(word)
W16EXPORT W16M_USER,       32,      6,  w16u_getwrect       ; GetWindowRect(word ptr)
W16EXPORT W16M_USER,       33,      6,  w16u_clientrect     ; GetClientRect(word ptr)
W16EXPORT W16M_USER,       34,      4,  w16u_enable         ; EnableWindow(word word)
W16EXPORT W16M_USER,       35,      2,  w16u_isenabled      ; IsWindowEnabled(word)
W16EXPORT W16M_USER,       36,      8,  w16u_getwtext       ; GetWindowText(word segptr word)
W16EXPORT W16M_USER,       37,      6,  w16u_setwtext       ; SetWindowText(word segstr)
W16EXPORT W16M_USER,       38,      2,  w16u_getwtlen       ; GetWindowTextLength(word)
W16EXPORT W16M_USER,       39,      6,  w16u_beginpaint     ; BeginPaint(word ptr)
W16EXPORT W16M_USER,       40,      6,  w16u_endpaint       ; EndPaint(word ptr)
W16EXPORT W16M_USER,       41,      30, w16u_createwnd      ; CreateWindow(str str long s_word x4 word word word segptr)
W16EXPORT W16M_USER,       42,      4,  w16u_showwnd        ; ShowWindow(word word)
W16EXPORT W16M_USER,       53,      2,  w16u_destroywnd     ; DestroyWindow(word)
W16EXPORT W16M_USER,       46,      2,  w16u_getparent      ; GetParent(word)
W16EXPORT W16M_USER,       47,      2,  w16u_iswindow       ; IsWindow(word)
W16EXPORT W16M_USER,       48,      4,  w16u_ischild        ; IsChild(word word)
W16EXPORT W16M_USER,       49,      2,  w16u_isvisible      ; IsWindowVisible(word)
W16EXPORT W16M_USER,       56,      12, w16u_movewnd        ; MoveWindow(word word word word word word)
W16EXPORT W16M_USER,       57,      4,  w16u_regclass       ; RegisterClass(ptr)
W16EXPORT W16M_USER,       66,      2,  w16u_getdc          ; GetDC(word)
W16EXPORT W16M_USER,       68,      4,  w16u_releasedc      ; ReleaseDC(word word)
W16EXPORT W16M_USER,       72,      12, w16u_setrect        ; SetRect(ptr s_word s_word s_word s_word)
W16EXPORT W16M_USER,       76,      8,  w16u_ptinrect       ; PtInRect(ptr long)
W16EXPORT W16M_USER,       81,      8,  w16u_fillrect       ; FillRect(word ptr word)
W16EXPORT W16M_USER,       87,      12, w16u_dlgbox         ; DialogBox(word str word segptr)
W16EXPORT W16M_USER,       88,      4,  w16u_enddlg         ; EndDialog(word s_word)
W16EXPORT W16M_USER,       89,      12, w16u_credlg         ; CreateDialog(word str word segptr)
W16EXPORT W16M_USER,       90,      6,  w16u_isdlgmsg       ; IsDialogMessage(word segptr)
W16EXPORT W16M_USER,       91,      4,  w16u_getdlgitem     ; GetDlgItem(word word)
W16EXPORT W16M_USER,       92,      8,  w16u_setdlgtext     ; SetDlgItemText(word word segstr)
W16EXPORT W16M_USER,       93,      10, w16u_getdlgtext     ; GetDlgItemText(word word segptr word)
W16EXPORT W16M_USER,       94,      8,  w16u_setdlgint      ; SetDlgItemInt(word word word word)
W16EXPORT W16M_USER,       95,      10, w16u_getdlgint      ; GetDlgItemInt(word s_word ptr word)
W16EXPORT W16M_USER,       96,      8,  w16u_checkradio     ; CheckRadioButton(word word word word)
W16EXPORT W16M_USER,       97,      6,  w16u_checkdlg       ; CheckDlgButton(word word word)
W16EXPORT W16M_USER,       98,      4,  w16u_isdlgcheck     ; IsDlgButtonChecked(word word)
W16EXPORT W16M_USER,       101,     12, w16u_senddlgmsg     ; SendDlgItemMessage(word word word word long)
W16EXPORT W16M_USER,       103,     6,  w16u_mapdlgrect     ; MapDialogRect(word ptr)
W16EXPORT W16M_USER,       104,     2,  w16u_beep           ; MessageBeep(word)
W16EXPORT W16M_USER,       107,     10, w16u_defproc        ; DefWindowProc(word word word long)
W16EXPORT W16M_USER,       122,     14, w16u_callwndproc    ; CallWindowProc(segptr word word word long)
W16EXPORT W16M_USER,       133,     4,  w16u_getwword       ; GetWindowWord(word s_word)
W16EXPORT W16M_USER,       134,     6,  w16u_setwword       ; SetWindowWord(word s_word word)
W16EXPORT W16M_USER,       135,     4,  w16u_getwlong       ; GetWindowLong(word s_word)
W16EXPORT W16M_USER,       136,     8,  w16u_setwlong       ; SetWindowLong(word s_word long)
W16EXPORT W16M_USER,       218,     10, w16u_dlgboxind      ; DialogBoxIndirect(word word word segptr)
W16EXPORT W16M_USER,       219,     12, w16u_credlgind      ; CreateDialogIndirect(word segptr word segptr)
W16EXPORT W16M_USER,       227,     6,  w16u_nextgroup      ; GetNextDlgGroupItem(word word word)
W16EXPORT W16M_USER,       228,     6,  w16u_nexttab        ; GetNextDlgTabItem(word word word)
W16EXPORT W16M_USER,       239,     16, w16u_dlgboxp        ; DialogBoxParam(word str word segptr long)
W16EXPORT W16M_USER,       240,     14, w16u_dlgboxindp     ; DialogBoxIndirectParam(word word word segptr long)
W16EXPORT W16M_USER,       241,     16, w16u_credlgp        ; CreateDialogParam(word str word segptr long)
W16EXPORT W16M_USER,       242,     16, w16u_credlgindp     ; CreateDialogIndirectParam(word segptr word segptr long)
W16EXPORT W16M_USER,       243,     0,  w16u_dlgunits       ; GetDialogBaseUnits()
W16EXPORT W16M_USER,       277,     2,  w16u_ctrlid         ; GetDlgCtrlID(word)
W16_XDDP equ W16NX                ; DefDlgProc's row: bound to slot W16_DDPROW
W16EXPORT W16M_USER,       308,     10, w16u_defdlgproc     ; DefDlgProc(word word word long)
W16EXPORT W16M_USER,       108,     10, w16u_getmsg         ; GetMessage(ptr word word word)
W16EXPORT W16M_USER,       109,     12, w16u_peekmsg        ; PeekMessage(ptr word word word word)
W16EXPORT W16M_USER,       110,     10, w16u_postmsg        ; PostMessage(word word word long)
W16EXPORT W16M_USER,       111,     10, w16u_sendmsg        ; SendMessage(word word word long)
W16EXPORT W16M_USER,       113,     4,  w16u_translate      ; TranslateMessage(ptr)
W16EXPORT W16M_USER,       114,     4,  w16u_dispatch       ; DispatchMessage(ptr)
W16EXPORT W16M_USER,       124,     2,  w16u_updatewnd      ; UpdateWindow(word)
W16EXPORT W16M_USER,       125,     8,  w16u_invalrect      ; InvalidateRect(word ptr word)
W16EXPORT W16M_USER,       127,     6,  w16u_validrect      ; ValidateRect(word ptr)
W16EXPORT W16M_USER,       171,     12, w16u_winhelp        ; WinHelp(word str word long)
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
W16EXPORT W16M_USER,       69,      2,  w16u_setcursor      ; SetCursor(word)
W16EXPORT W16M_USER,       71,      2,  w16u_showcursor     ; ShowCursor(word)
W16EXPORT W16M_USER,       137,     2,  w16u_opencb         ; OpenClipboard(word)
W16EXPORT W16M_USER,       138,     0,  w16u_closecb        ; CloseClipboard()
W16EXPORT W16M_USER,       139,     0,  w16u_emptycb        ; EmptyClipboard()
W16EXPORT W16M_USER,       140,     0,  w16u_getcbown       ; GetClipboardOwner()
W16EXPORT W16M_USER,       141,     4,  w16u_setcbdata      ; SetClipboardData(word word)
W16EXPORT W16M_USER,       142,     2,  w16u_getcbdata      ; GetClipboardData(word)
W16EXPORT W16M_USER,       143,     0,  w16u_countcbfmt     ; CountClipboardFormats()
W16EXPORT W16M_USER,       144,     2,  w16u_enumcbfmt      ; EnumClipboardFormats(word)
W16EXPORT W16M_USER,       193,     2,  w16u_iscbfmt        ; IsClipboardFormatAvailable(word)
W16EXPORT W16M_USER,       430,     8,  w16u_lstrcmp        ; lstrcmp(str str)
W16EXPORT W16M_USER,       431,     4,  w16u_ansiup         ; AnsiUpper(segstr)
W16EXPORT W16M_USER,       432,     4,  w16u_ansilow        ; AnsiLower(segstr)
W16EXPORT W16M_USER,       471,     8,  w16u_lstrcmpi       ; lstrcmpi(str str)
W16EXPORT W16M_USER,       472,     4,  w16u_ansinext       ; AnsiNext(segptr)
W16EXPORT W16M_USER,       473,     8,  w16u_ansiprev       ; AnsiPrev(segptr segptr)
W16EXPORT W16M_USER,       420,     0,  w16u_wsprintf       ; wsprintf(ptr str ...), cdecl
W16EXPORT W16M_USER,       421,     12, w16u_wvsprintf      ; wvsprintf(ptr str ptr)
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
W16EXPORT W16M_GDI,        34,      20, w16g_bitblt         ; BitBlt(word s_word x4 word s_word s_word long)
W16EXPORT W16M_GDI,        35,      24, w16g_stretch        ; StretchBlt(word s_word x4 word s_word x4 long)
W16EXPORT W16M_GDI,        52,      2,  w16g_cmpdc          ; CreateCompatibleDC(word)
W16EXPORT W16M_GDI,        68,      2,  w16g_deldc          ; DeleteDC(word)
W16EXPORT W16M_GDI,        82,      8,  w16g_getobj         ; GetObject(word s_word ptr)
W16EXPORT W16M_GDI,        442,     20, w16g_credib         ; CreateDIBitmap(word ptr long ptr ptr word)
W16EXPORT W16M_GDI,        45,      4,  w16g_select         ; SelectObject(word word)
W16EXPORT W16M_GDI,        61,      8,  w16g_createpen      ; CreatePen(s_word s_word long)
W16EXPORT W16M_GDI,        66,      4,  w16g_solidbrush     ; CreateSolidBrush(long)
W16EXPORT W16M_GDI,        69,      2,  w16g_delobj         ; DeleteObject(word)
W16EXPORT W16M_GDI,        80,      4,  w16g_devcaps        ; GetDeviceCaps(word s_word)
W16EXPORT W16M_GDI,        87,      2,  w16g_stock          ; GetStockObject(word)
W16EXPORT W16M_GDI,        91,      8,  w16g_textext        ; GetTextExtent(word ptr s_word)
W16EXPORT W16M_GDI,        93,      6,  w16g_textmet        ; GetTextMetrics(word ptr)
W16EXPORT W16M_GDI,        443,     28, w16g_setdib         ; SetDIBitsToDevice(word s_word x6 word word ptr ptr word)
; what CLOCK and REVERSI call (SPEC.md 124.17, w16acc.inc)
W16EXPORT W16M_KERNEL,     48,      2,  w16_ans1            ; GetModuleUsage(word)
W16EXPORT W16M_KERNEL,     54,      6,  w16_ans0            ; GetInstanceData(word word word)
W16EXPORT W16M_USER,       70,      4,  w16u_setcurpos      ; SetCursorPos(word word)
W16EXPORT W16M_USER,       77,      8,  w16u_offrect        ; OffsetRect(ptr s_word s_word)
W16EXPORT W16M_USER,       78,      8,  w16u_infrect        ; InflateRect(ptr s_word s_word)
W16EXPORT W16M_USER,       106,     2,  w16u_getkeystate    ; GetKeyState(word)
W16EXPORT W16M_USER,       249,     2,  w16u_getkeystate    ; GetAsyncKeyState(word)
W16EXPORT W16M_USER,       244,     8,  w16u_eqrect         ; EqualRect(ptr ptr)
W16EXPORT W16M_USER,       454,     14, w16u_adjrectex      ; AdjustWindowRectEx(ptr long word long)
W16EXPORT W16M_USER,       512,     12, w16u_wnetno         ; WNetGetConnection(ptr ptr ptr)
W16EXPORT W16M_USER,       513,     2,  w16u_wnetcaps       ; WNetGetCaps(word)
W16EXPORT W16M_USER,       514,     2,  w16u_wnetno         ; WNetDeviceMode(word)
W16EXPORT W16M_USER,       515,     8,  w16u_wnetno         ; WNetBrowseDialog(word word ptr)
W16EXPORT W16M_USER,       516,     8,  w16u_wnetno         ; WNetGetUser(ptr ptr)
W16EXPORT W16M_USER,       517,     12, w16u_wnetno         ; WNetAddConnection(str str str)
W16EXPORT W16M_USER,       518,     6,  w16u_wnetno         ; WNetCancelConnection(str word)
W16EXPORT W16M_USER,       519,     4,  w16u_wnetno         ; WNetGetError(ptr)
W16EXPORT W16M_USER,       520,     10, w16u_wnetno         ; WNetGetErrorText(word ptr ptr)
W16EXPORT W16M_GDI,        1,       6,  w16g_setbkcol       ; SetBkColor(word long)
W16EXPORT W16M_GDI,        2,       4,  w16g_setbkmode      ; SetBkMode(word word)
W16EXPORT W16M_GDI,        24,      10, w16g_ellipse        ; Ellipse(word s_word x4)
W16EXPORT W16M_GDI,        29,      14, w16g_patblt         ; PatBlt(word s_word x4 long)
W16EXPORT W16M_GDI,        48,      12, w16g_crebmp         ; CreateBitmap(word word word word ptr)
W16EXPORT W16M_GDI,        53,      16, w16g_credc          ; CreateDC(str str str ptr)
W16EXPORT W16M_GDI,        57,      4,  w16g_crefont        ; CreateFontIndirect(ptr)
W16EXPORT W16M_GDI,        56,      30, w16g_crefont        ; CreateFont(s_word x5 word x8 str)
W16EXPORT W16M_GDI,        60,      2,  w16g_patbrush       ; CreatePatternBrush(word)
W16EXPORT W16M_GDI,        119,     4,  w16_ans0            ; AddFontResource(str)
W16EXPORT W16M_GDI,        136,     4,  w16_ans0            ; RemoveFontResource(str)
W16EXPORT W16M_GDI,        150,     2,  w16_ans1            ; UnrealizeObject(word)
W16EXPORT W16M_GDI,        154,     6,  w16g_nearcol        ; GetNearestColor(word long)
W16EXPORT W16M_GDI,        346,     4,  w16g_settalign      ; SetTextAlign(word word)
W16EXPORT W16M_GDI,        351,     22, w16g_exttext        ; ExtTextOut(word s_word s_word word ptr ptr word ptr)
W16EXPORT W16M_USER,       84,      8,  w16u_drawicon       ; DrawIcon(word s_word s_word word)
; what Solitaire calls (SPEC.md 124.20.4, w16draw.inc)
W16EXPORT W16M_USER,       74,      8,  w16u_copyrect       ; CopyRect(ptr ptr)
W16EXPORT W16M_USER,       79,      12, w16u_isectrect      ; IntersectRect(ptr ptr ptr)
W16EXPORT W16M_USER,       82,      6,  w16u_invrect        ; InvertRect(word ptr)
W16EXPORT W16M_USER,       83,      8,  w16u_framerect      ; FrameRect(word ptr word)
W16EXPORT W16M_USER,       85,      14, w16u_drawtext       ; DrawText(word str s_word ptr word)
W16EXPORT W16M_USER,       118,     4,  w16u_regmsg         ; RegisterWindowMessage(str)
W16EXPORT W16M_USER,       102,     10, w16u_adjrect        ; AdjustWindowRect(ptr long word)
W16EXPORT W16M_USER,       59,      2,  w16u_setactive      ; SetActiveWindow(word)
W16EXPORT W16M_GDI,        100,     16, w16g_linedda        ; LineDDA(s_word s_word s_word s_word segptr long)
W16EXPORT W16M_GDI,        103,     6,  w16g_ptvisible      ; PtVisible(word s_word s_word)
W16EXPORT W16M_GDI,        148,     6,  w16g_setbrorg       ; SetBrushOrg(word s_word s_word)
W16EXPORT W16M_GDI,        51,      6,  w16g_cmpbmp         ; CreateCompatibleBitmap(word word word)
W16EXPORT W16M_GDI,        79,      2,  w16g_getdcorg       ; GetDCOrg(word)
W16EXPORT W16M_GDI,        83,      6,  w16g_getpixel       ; GetPixel(word s_word s_word)
W16EXPORT W16M_USER,       457,     2,  w16u_destroyicon    ; DestroyIcon(word)
W16EXPORT W16M_KEYBOARD,   132,     0,  w16kb_codepage      ; GetKBCodePage()
W16EXPORT W16M_KEYBOARD,   6,       8,  w16kb_oem2ansi      ; OemToAnsi(str ptr)
W16EXPORT W16M_SOUND,      1,       0,  w16s_open           ; OpenSound()
W16EXPORT W16M_SOUND,      2,       0,  w16s_close          ; CloseSound()
W16EXPORT W16M_SOUND,      4,       8,  w16s_note           ; SetVoiceNote(word word word word)
W16EXPORT W16M_SOUND,      5,       10, w16s_accent         ; SetVoiceAccent(word word word word word)
W16EXPORT W16M_SOUND,      9,       0,  w16s_start          ; StartSound()
W16EXPORT W16M_SOUND,      10,      0,  w16s_stop           ; StopSound()
W16EXPORT W16M_SHELL,      22,      12, w16sh_about         ; ShellAbout(word str str word)
W16_NX equ W16NX                  ; the rows, as a symbol: the loader part is
                                  ;   read before this file (SPEC.md 124.19.1)

; The four slots the image names its stubs by (SPEC.md 124.19.1): rows of the
; bind table no program's import table can reach, written by w16_bindinit
; before the first import is bound. Emitted with the rows (win16.asm's end).
%macro W16FIXED 2               ; argument bytes, handler
%xdefine w16f_a_%[W16NF] %1
%xdefine w16f_p_%[W16NF] %2
%assign W16NF W16NF + 1
%endmacro
%assign W16NF 0
W16FIXED 10, w16_ctlrow         ; W16_CTLROW: a control's window procedure
W16FIXED 0,  w16_cbdone         ; W16_CBROW: the callee's way back
W16FIXED 0,  w16x_lacks         ; W16_X87ROW: an x87 instruction it lacks
W16FIXED 10, w16u_defdlgproc    ; W16_DDPROW: DefDlgProc (W16_XDDP's handler)
%if W16NF != W16_NFIXED
  %error "W16FIXED: one row a fixed slot"
%endif

; The stubs: slot k is `mov ax, k / jmp near w16_gate`, six bytes, so a stub's
; address is w16_stubs + 6k. `near` is not decoration - a short jmp would make
; one stub five bytes and every address after it wrong. There are W16_NBIND of
; them whatever WIN16 exports: what a program imports is what it binds.
w16_stubs:
    ; STKBALANCE-OK: each stub is `mov ax, k / jmp near w16_gate`, entered by
    ; the program's far call - the gate's frame, not a routine of its own
%assign k 0
%rep W16_NBIND
    mov ax, k
    jmp near w16_gate
%assign k k + 1
%endrep
%if ($ - w16_stubs) != W16_NBIND * 6
  %error "w16_stubs: every stub must be six bytes"
%endif

; w16_missed - an import WIN16 does not provide, called (SPEC.md 124.12): its
; slot's part is W16B_MISS and the gate has banked the name's slot in
; [w16_missi]. It ends as AH=4Ch ends it, but WIN16's window stays, back to
; its own two lines, naming the call (w16_endshow): a toast would go with the
; window that raised it (SPEC.md 59).
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

; --- profiles: w16prof.inc (SPEC.md 124.14). The default's copy stays here
; ------------------------------------------------------------------------------

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
    or dx, dx                       ; a NULL default leaves the buffer as it
    jz .out                         ;   was and answers 0, as Windows 3.0 did
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

; WinHelp (USER.171) - +0 dwData, +4 wCommand, +6 lpHelpFile, +10 hWnd.
; There is no Help here (SPEC.md 124.15.3): HELP_QUIT, which a program sends
; as it ends whether or not it showed help, answers TRUE silently. Any other
; command says why in a message box - its arguments are rewritten in place as
; MessageBox's, which are as many bytes - and answers TRUE, so the program
; does not add a message of its own
w16u_winhelp:
    call w16_args
    mov ax, 1
    cmp word [es:bx + 4], 2         ; HELP_QUIT
    jne .say
    ret
.say:
    push si
    push di
    mov di, w16pf_ins + 128
    mov si, w16_s_nohelp1
    call w16_strcpy
    push es
    les si, [es:bx + 6]             ; the file's last component
    mov ax, es
    or ax, si
    jz .noname
    call w16_lastpart               ; ES:SI -> it
    call w16_fcopy40
    jmp short .named
.noname:
    pop es
    push es
    push ds
    pop es
    mov si, w16_s_itshelp
    call w16_strcpy
.named:
    pop es
    mov si, w16_s_nohelp2
    call w16_strcpy
    mov word [w16pf_ins], 'He'      ; the caption: "Help"
    mov word [w16pf_ins + 2], 'lp'
    mov byte [w16pf_ins + 4], 0
    pop di
    pop si
    ; fall into w16_asmsgbox

; w16_asmsgbox - ES:BX = twelve bytes of arguments whose +10 is an hWnd:
; rewritten as MessageBox(hWnd, w16pf_ins + 128, w16pf_ins, MB_OK |
; MB_ICONINFORMATION) and run as that import
w16_asmsgbox:
    mov word [es:bx], 0x0040
    mov word [es:bx + 2], w16pf_ins
    mov [es:bx + 4], ds
    mov word [es:bx + 6], w16pf_ins + 128
    mov [es:bx + 8], ds
    jmp w16u_msgbox

w16_s_nohelp1:  db 'Help is not available.', 13, 10, 'WIN16 has no WinHelp, so ', 0
w16_s_itshelp:  db 'its help', 0
w16_s_nohelp2:  db ' cannot be opened.', 0

; ShellAbout (SHELL.22) - +0 hIcon, +2 lpszOtherStuff, +6 lpszApp, +10 hWnd.
; A WIN16 message box (SPEC.md 124.15.3): "About" and the application, then
; the application (after a '#' when there is one), the other line, and what
; it is running on
w16sh_about:
    push si
    push di
    call w16_args
    mov di, w16pf_ins               ; the caption: "About " + the title part
    mov si, w16_s_about
    call w16_strcpy
    push es
    les si, [es:bx + 6]
    mov ax, es
    or ax, si
    jz .noapp
    mov cx, 40
    mov dl, '#'
    call w16_fcopyn                 ; up to a '#'
    mov byte [di], 0
    mov di, w16pf_ins + 128         ; the text: after the '#', else the same
    cmp byte [es:si], '#'
    jne .whole
    inc si
    jmp short .line1
.whole:
    pop es
    push es
    les si, [es:bx + 6]
.line1:
    mov cx, 60
    xor dl, dl
    call w16_fcopyn
    jmp short .other
.noapp:
    mov byte [di], 0
    mov di, w16pf_ins + 128
.other:
    pop es
    push es
    mov word [di], 0x0A0D
    add di, 2
    les si, [es:bx + 2]
    mov ax, es
    or ax, si
    jz .us
    mov cx, 120
    xor dl, dl
    call w16_fcopyn
    mov word [di], 0x0A0D
    add di, 2
.us:
    pop es
    mov si, w16_s_on8088
    call w16_strcpy
    pop di
    pop si
    jmp w16_asmsgbox

w16_s_about:    db 'About ', 0
w16_s_on8088:   db 13, 10, 'WIN16 on os8088', 0

; w16_fcopyn - ES:SI a far string -> DS:DI, at most CX characters, stopping
; at its NUL or at DL (0 = none). DI and SI end past what was copied; no NUL
; is written. Keeps the rest but AL.
w16_fcopyn:
    jcxz .out
.c:
    mov al, [es:si]
    or al, al
    jz .out
    or dl, dl
    jz .put
    cmp al, dl
    je .out
.put:
    mov [di], al
    inc si
    inc di
    loop .c
.out:
    ret

; w16_fcopy40 - ES:SI -> DS:DI, at most 40 characters, NUL-terminated
w16_fcopy40:
    push cx
    push dx
    mov cx, 40
    xor dl, dl
    call w16_fcopyn
    mov byte [di], 0
    pop dx
    pop cx
    ret

; w16_lastpart - ES:SI a far path -> ES:SI past its last '\', '/' or ':'
w16_lastpart:
    push di
    mov di, si
.c:
    mov al, [es:di]
    or al, al
    jz .out
    inc di
    cmp al, '\'
    je .cut
    cmp al, '/'
    je .cut
    cmp al, ':'
    jne .c
.cut:
    mov si, di
    jmp short .c
.out:
    pop di
    ret

; --- the program ends itself (SPEC.md 124.15.3) -----------------------------

; FatalAppExit (KERNEL.137) - +0 lpMessage, +4 wAction: the program ends,
; its window showing the message
w16k_fatalapp:
    push si
    push di
    call w16_args
    mov di, w16_l1
    les si, [es:bx]
    mov ax, es
    or ax, si
    jz .nomsg
    call w16_fcopy40
.nomsg:
    mov si, w16_s_fatalapp
    jmp short w16_endself

; FatalExit (KERNEL.1) - +0 the code
w16k_fatalexit:
    push si
    push di
    call w16_args
    mov ax, [es:bx]
    mov di, w16_l1
    mov si, w16_s_fatalx
    call w16_strcpy
    call w16_putdec
    mov word [di], ')'
    mov si, w16_s_empty
    ; fall into w16_endself

; w16_endself - SI = the second line: the program is over, as w16_missed
; ends one. Pops the DI and SI its callers pushed
w16_endself:
    mov di, w16_l2
    call w16_strcpy
    mov byte [w16_endmsg], 1
    mov byte [w16_state], W16S_ENDED
    pop di
    pop si
    ret

w16_s_fatalapp: db 'It ended itself (FatalAppExit).', 0
w16_s_fatalx:   db 'It ended itself (FatalExit ', 0

; OutputDebugString (KERNEL.115): nowhere to go
w16k_debugstr:
    ret

; DOS3Call (KERNEL.102): bound by the loader straight to this, not to a gate
; stub, so it is int 21h with the program's own registers and flags back
w16k_dos3call:
    ; STKBALANCE-OK: a far call's target, left by retf
    int 0x21
    retf

; =============================================================================
; USER: the window calls WinMine 3.10 makes at start-up (SPEC.md 124.15.3)
; =============================================================================

; GetDesktopWindow (USER.286)
w16u_desktop:
    mov ax, W16_HDESK
    ret

; GetLastActivePopup (USER.287) - +0 hWnd: the window itself
w16u_lastpopup:
    call w16_args
    mov ax, [es:bx]
    ret

; IsIconic (USER.31): os8088 does not minimize
w16u_isiconic:
    xor ax, ax
    ret

; BringWindowToTop (USER.45) - +0 hWnd
w16u_totop:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd2rec
    jc .no
    call w16_frontwnd
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; FindWindow (USER.50) - +0 lpWindowName, +4 lpClassName: the program's own
; top-level window whose class and text match, case blind; NULL matches all
w16u_findwnd:
    push si
    push di
    call w16_args
    mov ax, [es:bx + 4]
    mov dx, [es:bx + 6]
    mov byte [w16_fwcls], 0
    mov cx, ax
    or cx, dx
    jz .title
    call w16_farname                ; [w16_namebuf]
    jc .none
    mov byte [w16_fwcls], 1
.title:
    les si, [es:bx]
    mov [w16_fwt], si
    mov [w16_fwt + 2], es
    xor cx, cx
.slot:
    cmp cx, W16_MAXWND
    jae .none
    call w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .next
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .next
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .next
    cmp byte [w16_fwcls], 0
    je .ct
    cmp byte [bx + W16W_KIND], W16Q_PROG
    jne .next
    push cx
    mov al, [bx + W16W_CLS]
    mov ah, W16C_SIZE
    mul ah
    add ax, w16_cls
    mov di, ax
    mov si, w16_namebuf
    mov cx, 16
    push es
    push ds
    pop es
    cld
    repe cmpsb
    pop es
    pop cx
    jne .next
.ct:
    mov ax, [w16_fwt]
    or ax, [w16_fwt + 2]
    jz .hit
    call w16_fwcmp
    jne .next
.hit:
    call w16_rec2hwnd
    jmp short .out
.next:
    inc cx
    jmp short .slot
.none:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; SetClassWord (USER.130) - +0 wNewWord, +2 nIndex, +4 hWnd. The class's
; background brush (and every window of the class's, which each keep a copy)
; or style; anything else - the icon, the cursor - is kept nowhere: 0
GCW_HBRBACKGROUND equ -10
GCW_STYLE         equ -26

w16u_setclsword:
    push si
    call w16_args
    mov dx, [es:bx]
    mov cx, [es:bx + 2]
    mov ax, [es:bx + 4]
    call w16_hwnd2rec
    jc .no
    cmp byte [bx + W16W_KIND], W16Q_PROG
    jne .no
    mov al, [bx + W16W_CLS]
    mov ah, W16C_SIZE
    mul ah
    add ax, w16_cls
    mov si, ax
    cmp cx, GCW_STYLE
    je .style
    cmp cx, GCW_HBRBACKGROUND
    jne .no
    mov ax, dx
    xchg ax, [si + W16C_HBR]
    push ax
    mov al, [bx + W16W_CLS]
    xor cx, cx
.w:
    cmp cx, W16_MAXWND
    jae .wd
    call w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .wn
    cmp byte [bx + W16W_KIND], W16Q_PROG
    jne .wn
    cmp [bx + W16W_CLS], al
    jne .wn
    mov [bx + W16W_HBR], dx
.wn:
    inc cx
    jmp short .w
.wd:
    pop ax
    jmp short .out
.style:
    mov ax, dx
    xchg ax, [si + W16C_STYLE]
    jmp short .out
.no:
    xor ax, ax
.out:
    pop si
    ret

; w16_fwcmp - BX = a window: ZF=1 its text equals [w16_fwt]'s, case blind.
; Keeps BX and CX.
w16_fwcmp:
    push cx
    push si
    push di
    push es
    mov dx, ds
    mov di, w16pf_ins
    mov cx, 128
    call w16_gettext
    mov si, w16pf_ins
    les di, [w16_fwt]
.c:
    mov al, [si]
    mov ah, [es:di]
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 'a' - 'A'
.u1:
    cmp ah, 'a'
    jb .u2
    cmp ah, 'z'
    ja .u2
    sub ah, 'a' - 'A'
.u2:
    cmp al, ah
    jne .out
    or al, al
    jz .out
    inc si
    inc di
    jmp short .c
.out:
    pop es
    pop di
    pop si
    pop cx
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
%ifdef W16_X87_EMU                  ; (the test build's machine has none:
    and ah, ~CPU_F_X87              ;   the runtime takes its no-8087 path)
%endif
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

; =============================================================================
; KEYBOARD
; =============================================================================

; GetKBCodePage (KEYBOARD.132): the IBM PC's own
w16kb_codepage:
    mov ax, 437
    ret

w16_s_error: db 'Error', 0
w16_s_empty: db 0
