; =============================================================================
; os8088 - CDROM.DRV / cdico.inc - the CD-ROM's desktop picture (SPEC.md 26.4.2)
;
; A disc - its edge, the hub and a glint of the grooves - where the kernel
; would draw a hard disk: the driver sets FSCAP_ICON in DSV_CAPS and desk_pic
; asks DRV_ICONV here instead. The picture is the DRIVER'S so the kernel keeps
; no glyph for a drive most machines do not have (SPEC.md 26.7's rule), and
; it costs RAM only while CDROM.DRV is loaded.
;
; OSAPI_ICON_DRAW takes 16x16 records and nothing wider (SPEC.md 25.6.1), so
; the 32-wide picture is four quadrants, and the CGA's 14-row one two halves -
; a disc 14 rows tall is round on the CGA's 2.4:1 pixels, and a squashed
; 32-row one would not be (SPEC.md 26.4). Records: wwords 1, rows, the mask
; rows (white underlay), the data rows (black); bit 15 is the leftmost pixel.
; `#` black, `.` white, ` ` the desktop:
;
; 32 rows (VGA, EGA, Hercules):
;                 ####              
;             ####....####          
;           ##............##        
;          #................#       
;        ##..................##     
;       #......#...............#    
;       #....####..............#    
;      #....###.................#   
;     #....###...................#  
;     #...###....................#  
;    #....##......................# 
;    #...##.......................# 
;    #....#.......................# 
;    #............####............# 
;   #............######............#
;   #............##  ##............#
;   #............##  ##............#
;   #............######............#
;    #............####............# 
;    #.......................#....# 
;    #.......................##...# 
;    #......................##....# 
;     #....................###...#  
;     #...................###....#  
;      #.................###....#   
;       #..............####....#    
;       #...............#......#    
;        ##..................##     
;          #................#       
;           ##............##        
;             ####....####          
;                 ####              
;
; 14 rows (CGA):
;               ########            
;          #####........#####       
;       ###....#.............###    
;      #....###.................#   
;     #...##.....................#  
;    #....#.......................# 
;   #............##  ##............#
;   #............##  ##............#
;    #.......................#....# 
;     #.....................##...#  
;      #.................###....#   
;       ###.............#....###    
;          #####........#####       
;               ########            
; =============================================================================

; -----------------------------------------------------------------------------
; cd_v_icon - DRV_ICONV: paint the volume's picture (SPEC.md 26.4.2)
; in:  CX, DX = its top-left on the screen; gfx lock held, the UI task
; out: CF = 0; every register kept (FSCAP_ICON's contract, os88drv.inc)
; -----------------------------------------------------------------------------
cd_v_icon:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push cx                     ; OSAPI_VIDEO answers in AX, BX, CX and DX,
    push dx                     ; so the origin is banked across it
    call OSAPI_VIDEO            ; DL = VID_*
    mov bl, dl
    pop dx
    pop cx
    mov si, cd_ico_sl
    cmp bl, VID_CGA
    je .half                    ; the CGA: one band of 14 rows
    mov si, cd_ico_tl           ; the rest: two bands of 16
    call cd_ico_pair
    add dx, 16
    mov si, cd_ico_bl
.half:
    call cd_ico_pair
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; cd_ico_pair - SI = a band's LEFT record, its right one following it; CX/DX
;               = where. Draws both; CX and DX back as they were.
;               clobbers AX, SI, DI
cd_ico_pair:
    push cx
    push dx
    push si
    call OSAPI_ICON_DRAW        ; the left quadrant...
    pop si
    pop dx
    pop cx
    push cx
    push dx
    add cx, 16
    mov al, [si+1]              ; ...and the right one, the record after it:
    cbw                         ; 2 + rows x 2 x 2 bytes on
    shl ax, 1
    shl ax, 1
    add si, ax
    inc si
    inc si
    call OSAPI_ICON_DRAW
    pop dx
    pop cx
    ret

; --- the records: each band's LEFT then its RIGHT, ADJACENT (cd_ico_pair) -----
cd_ico_tl:
    db 1, 16
    dw 0x0003, 0x003F, 0x00FF, 0x01FF, 0x07FF, 0x0FFF, 0x0FFF, 0x1FFF
    dw 0x3FFF, 0x3FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0xFFFF, 0xFFFE
    dw 0x0003, 0x003C, 0x00C0, 0x0100, 0x0600, 0x0810, 0x0878, 0x10E0
    dw 0x21C0, 0x2380, 0x4300, 0x4600, 0x4200, 0x4003, 0x8007, 0x8006
cd_ico_tr:
    db 1, 16
    dw 0xC000, 0xFC00, 0xFF00, 0xFF80, 0xFFE0, 0xFFF0, 0xFFF0, 0xFFF8
    dw 0xFFFC, 0xFFFC, 0xFFFE, 0xFFFE, 0xFFFE, 0xFFFE, 0xFFFF, 0x7FFF
    dw 0xC000, 0x3C00, 0x0300, 0x0080, 0x0060, 0x0010, 0x0010, 0x0008
    dw 0x0004, 0x0004, 0x0002, 0x0002, 0x0002, 0xC002, 0xE001, 0x6001
cd_ico_bl:
    db 1, 16
    dw 0xFFFE, 0xFFFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x3FFF, 0x3FFF
    dw 0x1FFF, 0x0FFF, 0x0FFF, 0x07FF, 0x01FF, 0x00FF, 0x003F, 0x0003
    dw 0x8006, 0x8007, 0x4003, 0x4000, 0x4000, 0x4000, 0x2000, 0x2000
    dw 0x1000, 0x0800, 0x0800, 0x0600, 0x0100, 0x00C0, 0x003C, 0x0003
cd_ico_br:
    db 1, 16
    dw 0x7FFF, 0xFFFF, 0xFFFE, 0xFFFE, 0xFFFE, 0xFFFE, 0xFFFC, 0xFFFC
    dw 0xFFF8, 0xFFF0, 0xFFF0, 0xFFE0, 0xFF80, 0xFF00, 0xFC00, 0xC000
    dw 0x6001, 0xE001, 0xC002, 0x0042, 0x0062, 0x00C2, 0x01C4, 0x0384
    dw 0x0708, 0x1E10, 0x0810, 0x0060, 0x0080, 0x0300, 0x3C00, 0xC000
cd_ico_sl:
    db 1, 14
    dw 0x000F, 0x01FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFE, 0xFFFE
    dw 0x7FFF, 0x3FFF, 0x1FFF, 0x0FFF, 0x01FF, 0x000F
    dw 0x000F, 0x01F0, 0x0E10, 0x10E0, 0x2300, 0x4200, 0x8006, 0x8006
    dw 0x4000, 0x2000, 0x1000, 0x0E00, 0x01F0, 0x000F
cd_ico_sr:
    db 1, 14
    dw 0xF000, 0xFF80, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0x7FFF, 0x7FFF
    dw 0xFFFE, 0xFFFC, 0xFFF8, 0xFFF0, 0xFF80, 0xF000
    dw 0xF000, 0x0F80, 0x0070, 0x0008, 0x0004, 0x0002, 0x6001, 0x6001
    dw 0x0042, 0x00C4, 0x0708, 0x0870, 0x0F80, 0xF000
