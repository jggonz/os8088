; =============================================================================
; kernel/mouproto.inc - THE MICROSOFT SERIAL PACKET, ONCE (SPEC.md 9.5)
;
; Three bytes off a 1200-baud UART become a signed dx, a signed dy and two
; button bits. It is forty lines of bit-shuffling with two sign extensions in
; it, and it has TWO readers for the same reason `apps/dos/doscall.inc` has
; two hosts: `kernel/mouse.inc` decodes it for the desktop's pointer, and
; `kerndos/kdmouse.inc` decodes it for INT 33h on a machine where the kernel
; is not running (SPEC.md 96.45). Neither can call the other - one is a kernel
; ISR and the other is 96.40's image with no kernel under it - so what they
; share, they share as SOURCE.
;
; **IT IS THE ARITHMETIC AND NOTHING ELSE.** The phase machine, the port
; contest, the hot-plug drain and what to do with the answer are all the
; HOST's: the kernel has per-port arrays because a modem on the other port
; decodes independently (9.5), and `kern_dos` has one port because os8088
; settled which one before it handed over. Putting any of that in here would
; make the macro a second copy of the thing that differs rather than one copy
; of the thing that does not.
;
; WHY A MACRO AND NOT A PROC: the kernel's copy is inside `mou_byte`, in an
; ISR, and reached by fall-through from the phase machine above it; a `call`
; there would cost a return address on the shared mouse stack SPEC.md 9.10
; exists to keep shallow. It emits identical bytes in both hosts, and the
; kernel building BYTE-IDENTICAL across the extraction is what says so.
; =============================================================================

%ifndef MOUPROTO_INC
%define MOUPROTO_INC 1

; -----------------------------------------------------------------------------
; MOU_DECODE_MS - one complete Microsoft three-byte packet
; in:  DH = byte 0 (the one with bit 6 set), DL = byte 1, BL = byte 2
; out: AX = dx, signed; BX = dy, signed and POSITIVE IS DOWN; CL = the buttons
;      in `mouse_btn`'s own two bits - bit 0 left, bit 1 right
; clobbers: SI, flags. DH and DL are spent.
;
; The two `cbw`s are the whole of the sign extension and they are why the
; halves are assembled into AL rather than into a word: the protocol sends
; eight-bit deltas split 2+6 across two bytes, so the byte has to be whole
; before its top bit means anything.
;
; The buttons land in `mouse_btn`'s order here rather than in the host,
; because this is the whole of what THIS protocol has to say about them - and
; the PS/2 packet already carries them in that order (SPEC.md 9.9.3), so a
; shared back end never has to know which mouse it is serving.
; -----------------------------------------------------------------------------
%macro MOU_DECODE_MS 0
    ; --- dx = sign-extended {X7X6 from b0, X5..X0 from b1} -------------------
    mov al, dh
    and al, 0x03
    mov cl, 6
    shl al, cl                  ; X7X6 into bits 7:6
    mov ah, dl
    and ah, 0x3F
    or  al, ah
    cbw                         ; AX = dx, signed
    mov si, ax                  ; ...banked, because the host wants both deltas
                                ; at once. SI is the ISR's to spend - it is
                                ; pushed - and it is the one register the y
                                ; decode does not need

    ; --- dy = sign-extended {Y7Y6 from b0, Y5..Y0 from b2}; positive = down --
    mov al, dh
    and al, 0x0C
    mov cl, 4
    shl al, cl                  ; Y7Y6 into bits 7:6
    and bl, 0x3F
    or  al, bl
    cbw                         ; AX = dy, signed
    mov bx, ax

    ; --- buttons: b0 bit 5 = left, bit 4 = right -----------------------------
    xor cl, cl
    test dh, 0x20
    jz %%nol
    or  cl, 0x01
%%nol:
    test dh, 0x10
    jz %%nor
    or  cl, 0x02
%%nor:
    mov ax, si
%endmacro

%endif                          ; MOUPROTO_INC
