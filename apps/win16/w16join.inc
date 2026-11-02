; =============================================================================
; os8088 - apps/win16/w16join.inc
;
; THE JOIN BOX (SPEC.md 124.28.12): 24 bytes of WIN16's image at a pinned
; offset, through which DOS.O88 hands a desktop launch of a Windows program
; to the WIN16 instance already running one, so the program becomes a task
; of it rather than a second instance. Equates only, included as SOURCE by
; both packages (apps/win16/win16.asm lays the box out, apps/dos/dosw16.inc
; fills it), so the two cannot disagree about where a field is.
;
; A WIN16 whose box does not carry this signature AND this version is not
; joined: DOS starts it as an instance of its own, as before S21.
; =============================================================================

W16J_AT     equ 96              ; the image offset: after the header and icon
W16J_SIG    equ 0x4A57          ; +0 word: 'WJ'
W16J_V1     equ 1               ; +2 byte: this layout
W16J_MAGIC  equ 0
W16J_VER    equ 2
W16J_OK     equ 3               ; +3 byte: 1 while a program runs and a join
                                ;   is taken (WIN16 writes it)
W16J_WIN    equ 4               ; +4 word: WIN16's os8088 window (WIN16's)
W16J_REQ    equ 6               ; +6 byte: 1 while a locator waits - set LAST
                                ;   by DOS, cleared by WIN16 as it takes it
W16J_DOC    equ 8               ; +8 16 bytes: the locator, OSAPI_ARG_FILE's
                                ;   shape (SPEC.md 21.5.3)
W16J_SIZE   equ 24
