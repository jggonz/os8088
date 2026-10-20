; =============================================================================
; os8088 - apps/imgtest/imgtest.asm
;
; The test app for apps/os88img.inc, and the reason that file can be trusted
; before WORD's Insert > Picture depends on it.
;
; A decoder is the classic thing that passes its own test: write the encoder
; and the decoder from one understanding of a format and they agree with each
; other about something neither has got right. So apps/imgtest/imgcases.inc is
; GENERATED ON THE HOST by tools/os88imgcase.py, which computes every expected
; answer from the FORMAT DOCUMENTS - ZSoft's Technical Reference Manual
; revision 5, the BITMAPINFOHEADER layout, apps/frotz/zpic.inc's own header -
; and never by running this and recording what it said. Same argument
; apps/fptest makes for the soft-float core (84).
;
; Each case reads a REAL FILE off the disk, so the path under test is the one
; a package actually uses: claim, OSAPI_FILE_READ, img_load. A case passes
; only if the width, the height, the stride, the error code AND a checksum of
; every decoded byte all match - and the geometry is compared ON A REFUSAL
; too, against zero, because img_setgeom stores it before any decoder has
; proved its pixel data is inside the file.
;
; One case is not generated at all. MAIN.PCX is 1152x90 in four planes, off
; the Dr. Dobb's File Formats disc, written by PC Paintbrush by somebody who
; had never heard of this project - the one file here that cannot share a
; misreading with the decoder.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'IMGTEST', it_entry

IT_W        equ 300
IT_ROWH     equ 9

IT_DSTKB    equ 64                  ; the whole of a segment, whatever the case
                                    ; says its picture needs - see it_entry

; -----------------------------------------------------------------------------
; it_paint - one row a case, its verdict word then its file name, and the
; whole run's verdict under them. OSAPI_FONT_RUN takes CX=x, DX=y, SI=text,
; AL=ink and AH=background and draws both in ONE pass (6.1) - the
; erase-then-letter pair is the double-draw flash PERFORMANCE.md names - and
; like every slot it preserves every register, so the pen is set once. SI is
; the window, and is handed back as it came.
; -----------------------------------------------------------------------------
it_paint:
    push si
    mov bx, si
    call OSAPI_WM_CONTENT               ; AX/DX = the content origin
    add ax, 4
    xchg cx, ax                         ; CX = the verdict column
    add dx, 4                           ; DX = the first row's top
    mov ax, CBLACK | (CWHITE << 8)
    mov bx, it_s_all                    ; the run's verdict, until a row says
    mov di, imgc_tab                    ; otherwise
.row:
    mov si, it_s_ok
    cmp word [di+2], 0
    jne .say
    mov si, it_s_bad
    mov bx, it_s_some
.say:
    call OSAPI_FONT_RUN
    add cx, 40
    mov si, [di]                        ; the file's name
    call OSAPI_FONT_RUN
    sub cx, 40
    add dx, IT_ROWH
    add di, IMGC_REC
    cmp di, imgc_tab + IMGC_N * IMGC_REC
    jb .row
    add dx, 8                           ; the verdict, 12 under the last row
    mov si, bx
    call OSAPI_FONT_RUN
    pop si
it_ret:
    ret

; -----------------------------------------------------------------------------
; it_entry - claim, run every case, then make the window that reports them.
;
; TWO CLAIMS, SIZED BY WHAT EACH ONE IS FOR.
;
; The DESTINATION is a whole 64KB, and that is a GATE property rather than a
; size: every case but BIG.BMP passes IMG_DSTMAX = 0, which tells the decoder
; it owns the segment, so a decoder broken on purpose may write anywhere in
; DSTSEG:0000-FFFF - and the claim has to be that big for such a decoder to
; show up as a FAIL row rather than as somebody else's heap.
;
; The SOURCE is only ever READ, so it is sized by the files and not by the
; worst case. It starts at NOTHING ([SI+IT_SRCKB] = 0, the bss is zeroed), so the
; first case's OSAPI_FILE_READ answers FERR_BIG with DX = the KB it needs -
; decided from the directory entry before any I/O - and the claim grows to
; that and the read is made again. The generated corpus is under 1KB a file,
; so a plain run holds 1KB where it used to hold a constant 64KB; the Dr.
; Dobb's files grow it as far as they need. The grow path runs on EVERY run,
; on the first case, so it cannot rot unseen.
;
; THE RESULT OF A CASE IS WRITTEN BACK INTO ITS OWN RECORD, over the picture
; number (+2) - which img_load has finished with by then, and nothing reads
; again. A pass stores the record's own (non-zero) address and a fail a zero,
; so it_paint reads one word a row and there is no result array in the bss.
; -----------------------------------------------------------------------------
it_entry:
    mov ax, IT_DSTKB
    call OSAPI_MEM_CLAIM
    jc it_ret                           ; CF=1: the loader says so
    mov si, it_blk
    mov [si+IMG_DSTSEG], dx
    mov word [si+IMG_ROWBUF], it_row
    mov di, imgc_tab                    ; DI = this case's record, throughout
.case:
    mov ax, 1024
    mul word [si+IT_SRCKB]
    xchg cx, ax                         ; DX:CX = what the source claim holds
    mov es, [si+IMG_SRCSEG]
    push si
    mov si, [di]                        ; the file's name
    xor bx, bx
    call OSAPI_FILE_READ                ; out DX:AX = the size, or CF=1
    pop si
    jc .grow
    or dx, dx
    jnz .fail                           ; a file past 64KB is not in the corpus
    mov [si+IMG_SRCLEN], ax
    mov ax, [di+4]                      ; the capacity this case is given, 0 =
    mov [si+IMG_DSTMAX], ax             ; the whole 64KB. PER CASE, because a
                                        ; constant 0 takes img_setgeom's own
                                        ; `jz .fits` and leaves the compare
                                        ; below it unreachable - a decoder that
                                        ; ignored IMG_DSTMAX would pass every
                                        ; case, which is not an uncovered path
                                        ; but an untestable one
    mov ax, [di+2]                      ; the picture number asked for
    mov [si+IMG_PICNO], ax
    call img_load
    mov bx, 8                           ; the geometry, WHICH IS ALSO CHECKED
.geom:                                  ; ON A REFUSAL: img_setgeom stores
    mov ax, [bx+di]                     ; W/H/STRIDE before the pixel data has
    cmp ax, [bx+si+IMG_W-8]             ; been proved to be inside the file, so
    jne .fail                           ; "the refusal left no geometry behind"
    inc bx                              ; is a claim with a case rather than a
    inc bx                              ; comment. The generator emits 0,0,0
    cmp bl, 14                          ; for a refusal. Record +8/+10/+12 are
    jb .geom                            ; W/H/STRIDE, the block's in the same
                                        ; order, and AX leaves as the stride
    mov cx, [di+6]                      ; ...and the error code expected
    cmp cx, [si+IMG_ERR]
    jne .fail
    jcxz .cksum                         ; a refusal has no picture to checksum
.pass:
    mov [di+2], di                      ; the record's address: never zero
    jmp short .next
    ; THE CHECKSUM - the LFSR-xor of stride*height decoded bytes, and it must
    ; agree to the bit with tools/os88imgcase.py's cksum().
    ;
    ; A plain sum would not notice two rows swapped, and NEITHER DID THE
    ; ROTATE THIS USED TO BE: rotating by one is a linear map of order
    ; SIXTEEN, so two rows whose byte distance is a multiple of 16 land on the
    ; same rotation and exchanging them left the answer unchanged - which on
    ; the 16-byte-stride cases is every pair of rows in the picture.
    ; Multiplying by x modulo x^16 + x^12 + x^5 + 1 has order 32767 instead,
    ; which no picture this decoder accepts can reach.
.cksum:
    mul word [si+IMG_H]                 ; DX:AX - the geometry check inside
    xchg cx, ax                         ; img_load already proved it fits
    mov es, [si+IMG_DSTSEG]
    xor bx, bx
    xor dx, dx                          ; DX = the running value
.b:
    shl dx, 1
    jnc .nofb
    xor dx, 0x1021                      ; ...x times the running value, modulo
.nofb:                                  ; the polynomial, xor the next byte
    xor dl, [es:bx]
    inc bx
    loop .b
    cmp dx, [di+14]
    je .pass
.fail:
    and word [di+2], 0
.next:
    add di, IMGC_REC
    cmp di, imgc_tab + IMGC_N * IMGC_REC
    jb .case
    mov si, it_tpl
    call OSAPI_WM_CREATE                ; CF=1 on a full window table, and the
                                        ; macro below keeps the flags for us
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    ; Nothing in the image holds a segment of the image, so the proc is
    ; a bare `ret` - it_paint's, rather than a second of the macro's own.
    OS88_REGION_MOVABLE it_ret
    ret
.grow:
    cmp ax, FERR_BIG                    ; the read refused: AX = FERR_*, and on
    jne .fail                           ; FERR_BIG DX = the KB the file needs
    cmp dx, [si+IT_SRCKB]               ; only MORE than we hold is worth a
    jbe .fail                           ; second read
    xchg ax, dx
    call OSAPI_MEM_CLAIM                ; AX = KB -> DX = the new claim
    jc .fail
    mov [si+IT_SRCKB], ax               ; ...recorded only once it is held
    xchg dx, [si+IMG_SRCSEG]
    call OSAPI_MEM_FREE                 ; ...and the old one given back. On the
    jmp .case                           ; first case DX = 0, which names no
                                        ; claim, and the slot refuses it (CF=1,
                                        ; nothing freed - mem_find_own skips
                                        ; the free records a 0 could match)


it_tpl:
    dw 30, 30, IT_W, IT_H
    dw it_title, it_paint, 0, 0
it_title:   db 'os88img self-test', 0
it_s_ok:    db 'ok', 0
it_s_bad:   db 'FAIL', 0
it_s_all:   db 'ALL PASS', 0
it_s_some:  db 'FAILURES', 0

%include "imgcases.inc"

IT_H        equ IMGC_N * IT_ROWH + 40   ; one row a case plus the verdict under
                                    ; them, DERIVED - the corpus grows by five
                                    ; when the Dr. Dobb's disc is present, and
                                    ; a constant here loses the verdict off the
                                    ; bottom without saying so. VGA-only, like
                                    ; the rest of this tool: a CGA desktop band
                                    ; is 155 rows (39.11.2) and never held it
                                    ; (below the include, because IMGC_N
                                    ; has to exist before an equ can use it)

%include "os88img.inc"

    OS88_BSS IT_BSS
    OS88_IMAGE_END

it_blk     equ os88_image_end + 2      ; OS88IMG_SZ - its IMG_SRCSEG and
                                       ; IMG_DSTSEG are where the two claims
                                       ; are kept, and the word BEFORE it is
IT_SRCKB   equ -2                      ; [SI+IT_SRCKB], the KB the source claim
                                       ; holds (0 = none), reached off the
                                       ; block pointer the loop already holds
it_row     equ it_blk + OS88IMG_SZ     ; OS88IMG_ROW
it_bss_end equ it_row + OS88IMG_ROW
IT_BSS     equ it_bss_end - os88_image_end
