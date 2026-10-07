; The graphics lab is a separate native window owned by this package.
; Its scene includes nested window-like frames and objects; the OS itself
; manages top-level windows, so these logical children use container coordinates.
rl_canvas_select:
    mov word [rl_canvas_h], 64
    mov word [rl_labtpl+6], 114
    mov word [rl_labmin], 106
    mov bx, [rl_win]
    call OSAPI_WM_GEOM
    cmp dx, 224
    jb .scene
    mov word [rl_canvas_h], 128
    mov word [rl_labtpl+6], 178
    mov word [rl_labmin], 170
.scene:
    mov ax, [rl_canvas_h]
    mov [rl_objects+8], ax         ; root height
    sub ax, 18
    mov [rl_objects+16+8], ax      ; child height
    sub ax, 20
    mov [rl_objects+32+8], ax      ; nested height
    sub ax, 14
    mov [rl_objects+48+4], ax      ; button sits near its parent's bottom
    mov ax, [rl_canvas_h]
    sub ax, 10
    mov [rl_objects+64+8], ax      ; viewport height
    ret
rl_lab_open:
    mov bx, [rl_win]
    call OSAPI_WM_CONTENT
    add ax, 12
    add dx, 54
    mov [rl_labtpl], ax
    mov [rl_labtpl+2], dx
    mov si, rl_labtpl
    call OSAPI_WM_CREATE
    jc .out
    mov [rl_labwin], bx
    mov al, 1
    call OSAPI_WM_SIZABLE
    mov cx, 280
    mov dx, [rl_labmin]
    call OSAPI_WM_MINSIZE
    call rl_lab_origin
    mov bx, [rl_labwin]
    call OSAPI_WM_SHOW
    clc
.out:
    ret
rl_lab_origin:
    mov bx, [rl_labwin]
    call OSAPI_WM_CONTENT
    add ax, 8
    add dx, 6
    mov [rl_x], ax
    mov [rl_y], dx
    ret
rl_lab_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call rl_lab_origin
    call rl_clear_cube
    call rl_composition
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
rl_lab_key:
    ret                            ; the benchmark owns its temporary container
rl_labtpl: dw 20,80,288,114,rl_labtitle,rl_lab_paint,rl_lab_key,rl_lab_key
rl_labtitle: db 'REDLINE Graphics Lab', 0
rl_laberror: db 'No window slot/memory for the graphics lab; close other apps.', 0
rl_labwin: dw 0
rl_canvas_h: dw 64
rl_labmin: dw 106

; Three graphics tiers. The canvas is 256x64 or 256x128 for taller viewports.
rl_large_rect:
    mov ax, [rl_x]
    mov bx, [rl_y]
    mov cx, ax
    add cx, 255
    mov dx, bx
    add dx, [rl_canvas_h]
    dec dx
    ret
rl_fill_large:
    call rl_large_rect
    call OSAPI_GFX_FILL
    ret
rl_lines_many:
    mov si, [rl_canvas_h]
    shr si, 1
    shr si, 1
    mov dx, [rl_y]
.row:
    mov ax, [rl_x]
    mov bx, ax
    add bx, 255
    call OSAPI_GFX_HLINE
    add dx, 4
    dec si
    jnz .row
    ret
rl_frames_many:
    call rl_large_rect
    mov si, 8
.frame:
    call OSAPI_GFX_FRAME
    add ax, 4
    add bx, 2
    sub cx, 4
    sub dx, 2
    dec si
    jnz .frame
    ret
rl_text_grid:
    mov bp, [rl_canvas_h]
    mov cl, 3
    shr bp, cl
    mov cx, [rl_x]
    mov dx, [rl_y]
.line:
    mov si, rl_textsample
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    add dx, 8
    dec bp
    jnz .line
    ret
rl_blit1_large:
    push ds
    pop es
    mov si, rl_src
    mov bp, 16
    mov ax, [rl_x]
    add ax, 7
    and ax, 0xFFF8
    mov bx, [rl_y]
    mov cx, 128
    mov dx, [rl_canvas_h]
    call OSAPI_GFX_BLIT1
    ret
rl_blit4_large:
    push ds
    pop es
    mov si, rl_pixels
    mov bp, 64
    mov ax, [rl_x]
    mov bx, [rl_y]
    mov cx, 128
    mov dx, [rl_canvas_h]
    shr dx, 1
    call OSAPI_GFX_BLIT4
    ret
rl_patternblit:
    push ds
    pop es
    mov di, rl_pixels
    mov ax, 0x1E4B
    mov cx, 2048
    cld
    rep stosw
    call rl_blit4_large
    mov di, rl_pixels
    mov ax, 0x1111
    mov cx, 2048
    rep stosw
    ret
rl_scroll:
    call rl_large_rect
    add ax, 7
    and ax, 0xFFF8
    mov cx, ax
    add cx, 255
    mov si, [rl_canvas_h]
    shr si, 1
    shr si, 1
    shr si, 1
    shr si, 1
    call OSAPI_GFX_SCROLL
    jc .fallback
    neg si
    call OSAPI_GFX_SCROLL
    ret
.fallback:
    call OSAPI_GFX_FILL
    ret
 ; A parent-indexed scene: children inherit their parent's absolute origin.
; Window objects, buttons and a 3D viewport share one bounded container.
rl_composition:
    mov ax, [rl_x]
    mov [rl_scene_x], ax
    mov ax, [rl_y]
    mov [rl_scene_y], ax
    mov word [rl_object], 0
.object:
    mov bx, [rl_object]
    mov si, bx
    mov cl, 4
    shl si, cl
    add si, rl_objects
    mov di, [si]                   ; parent, or FFFF for the container root
    mov ax, [rl_scene_x]
    mov dx, [rl_scene_y]
    cmp di, 0xFFFF
    je .position
    shl di, 1
    shl di, 1
    mov ax, [rl_object_xy+di]
    mov dx, [rl_object_xy+di+2]
.position:
    add ax, [si+2]
    add dx, [si+4]
    shl bx, 1
    shl bx, 1
    mov [rl_object_xy+bx], ax
    mov [rl_object_xy+bx+2], dx
    mov bx, dx
    mov cx, [si+6]
    add cx, ax
    dec cx
    add dx, [si+8]
    dec dx
    mov [rl_object_x], ax
    mov [rl_object_y], bx
    cmp word [si+10], 2
    je .viewport
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    cmp word [si+10], 0
    jne .label
    mov dx, bx
    add dx, 10
    push ax
    mov al, CBLUE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.label:
    mov cx, [rl_object_x]
    add cx, 3
    mov dx, [rl_object_y]
    add dx, 2
    push si
    mov al, CBLACK
    mov ah, CWHITE
    cmp word [si+10], 0
    jne .text
    mov al, CWHITE
    mov ah, CBLUE
.text:
    mov si, [si+14]
    call OSAPI_FONT_RUN
    pop si
    jmp .next
.viewport:
    mov [rl_x], ax
    mov [rl_y], bx
    mov al, CRED
    call OSAPI_SET_COLOR
    mov word [rl_cos], 110
    mov word [rl_sin], 64
    mov word [rl_project_scale], 112
    cmp word [rl_canvas_h], 64
    je .smallview
    mov word [rl_project_scale], 192
.smallview:
    mov word [rl_centerx], 64
    mov ax, [si+8]
    shr ax, 1
    mov [rl_centery], ax
    call rl_project
    call rl_wire_edges
.next:
    inc word [rl_object]
    cmp word [rl_object], RL_OBJECTS
    jb .object
    mov ax, [rl_scene_x]
    mov [rl_x], ax
    mov ax, [rl_scene_y]
    mov [rl_y], ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    ret
RL_OBJECTS equ 5
; parent,x,y,w,h,kind,ink,label. Logical children fit inside their parents.
rl_objects:
    dw 0xFFFF,0,0,256,64,0,CBLUE,rl_scene_title
    dw 0,6,14,114,46,0,CBLUE,rl_scene_child
    dw 1,5,14,100,26,0,CBLUE,rl_scene_nested
    dw 2,5,12,74,12,1,CBLACK,rl_scene_button
    dw 0,126,10,126,54,2,CRED,0
rl_scene_title: db 'Scene container', 0
rl_scene_child: db 'Window', 0
rl_scene_nested: db 'Nested', 0
rl_scene_button: db 'Object', 0
rl_object_xy: times RL_OBJECTS dd 0
rl_object: dw 0
rl_object_x: dw 0
rl_object_y: dw 0
rl_scene_x: dw 0
rl_scene_y: dw 0

rl_windows_move:
    ; Three identical scene renders with a child window sliding horizontally.
    mov word [rl_objects+18], 6
    call rl_composition
    mov word [rl_objects+18], 10
    call rl_composition
    mov word [rl_objects+18], 14
    call rl_composition
    mov word [rl_objects+18], 6
    ret
rl_resize:
    ; Public read-only frame fields, then public resize with damage repair.
    ; This row uses method T: a complete repaint can exceed one PIT period.
    mov bx, [rl_labwin]
    mov ax, KERNEL_SEG
    mov es, ax
    mov cx, [es:bx+W_W]
    mov dx, [es:bx+W_H]
    push cx
    push dx
    sub cx, 8
    sub dx, 8
    call OSAPI_WM_RESIZE
    pop dx
    pop cx
    call OSAPI_WM_RESIZE
    ret
rl_composetitle: db 'REDLINE graphics window', 0

; Twelve Q7 Y poses, fixed X tilt, perspective projection and back-face culling.
rl_cube_setup:
    mov word [rl_project_scale], 112
    cmp word [rl_canvas_h], 64
    je .center
    mov word [rl_project_scale], 224
.center:
    mov word [rl_centerx], 128
    mov ax, [rl_canvas_h]
    shr ax, 1
    mov [rl_centery], ax
    mov bx, [rl_angle]
    shl bx, 1
    shl bx, 1
    mov ax, [rl_poses+bx]
    mov [rl_cos], ax
    mov ax, [rl_poses+bx+2]
    mov [rl_sin], ax
    inc word [rl_angle]
    cmp word [rl_angle], 12
    jb .out
    mov word [rl_angle], 0
.out:
    ret
rl_project:
    mov si, rl_vertices
    mov di, rl_projected
    mov bp, 8
.vertex:
    mov ax, [si]
    mov bx, [rl_cos]
    imul bx
    mov [rl_rx], ax
    mov ax, [si+4]
    mov bx, [rl_sin]
    imul bx
    add [rl_rx], ax
    mov ax, [si+4]
    mov bx, [rl_cos]
    imul bx
    mov [rl_rz], ax
    mov ax, [si]
    mov bx, [rl_sin]
    imul bx
    sub [rl_rz], ax
    mov cl, 7
    sar word [rl_rx], cl
    sar word [rl_rz], cl
    mov ax, [si+2]
    mov bx, 120                   ; fixed 20-degree X tilt, also Q7
    imul bx
    mov [rl_ry], ax
    mov ax, [rl_rz]
    mov bx, 44
    imul bx
    sub [rl_ry], ax
    mov ax, [rl_rz]
    mov bx, 120
    imul bx
    mov [rl_tz], ax
    mov ax, [si+2]
    mov bx, 44
    imul bx
    add ax, [rl_tz]
    sar ax, cl
    mov [rl_rz], ax
    sar word [rl_ry], cl
    add word [rl_rz], 128
    mov ax, [rl_rx]
    mov bx, [rl_project_scale]
    imul bx
    idiv word [rl_rz]
    add ax, [rl_centerx]
    add ax, [rl_x]
    mov [di], ax
    mov ax, [rl_ry]
    imul bx
    idiv word [rl_rz]
    add ax, [rl_centery]
    add ax, [rl_y]
    mov [di+2], ax
    add si, 6
    add di, 4
    dec bp
    jnz .vertex
    ret
rl_clear_cube:
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call rl_large_rect
    call OSAPI_GFX_FILL
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    ret
rl_wire:
    call rl_cube_setup
    call rl_clear_cube
    call rl_project
    call rl_wire_edges
    ret
rl_wire_edges:
    mov si, rl_edges
    mov bp, 12
.edge:
    xor bx, bx
    mov bl, [si]
    mov ax, [rl_projected+bx]
    mov dx, [rl_projected+bx+2]
    inc si
    mov bl, [si]
    mov cx, [rl_projected+bx]
    mov bx, [rl_projected+bx+2]
    inc si
    push si
    push bp
    call rl_line
    pop bp
    pop si
    dec bp
    jnz .edge
    ret

; Bresenham, AX/DX = first point, CX/BX = second. Pixel API clips safely.
rl_line:
    mov [rl_lx], ax
    mov [rl_ly], dx
    mov [rl_lendx], cx
    mov [rl_lendy], bx
    sub cx, ax
    mov word [rl_lsx], 1
    jns .dx
    neg cx
    mov word [rl_lsx], -1
.dx:
    mov [rl_ldx], cx
    sub bx, dx
    mov word [rl_lsy], 1
    jns .dy
    neg bx
    mov word [rl_lsy], -1
.dy:
    neg bx
    mov [rl_ldy], bx
    add cx, bx
    mov [rl_lerr], cx
.pixel:
    mov cx, [rl_lx]
    mov dx, [rl_ly]
    call OSAPI_GFX_PIXEL
    cmp cx, [rl_lendx]
    jne .advance
    cmp dx, [rl_lendy]
    je .out
.advance:
    mov ax, [rl_lerr]
    shl ax, 1
    cmp ax, [rl_ldy]
    jl .y
    mov bx, [rl_ldy]
    add [rl_lerr], bx
    mov bx, [rl_lsx]
    add [rl_lx], bx
.y:
    cmp ax, [rl_ldx]
    jg .pixel
    mov bx, [rl_ldx]
    add [rl_lerr], bx
    mov bx, [rl_lsy]
    add [rl_ly], bx
    jmp .pixel
.out:
    ret
rl_shaded:
    call rl_cube_setup
    call rl_clear_cube
    call rl_project
    mov si, rl_triangles
    mov bp, 12
.triangle:
    push si
    push bp
    mov di, rl_triangle
    mov cx, 3
.copy:
    xor bx, bx
    mov bl, [si]
    inc si
    mov ax, [rl_projected+bx]
    mov [di], ax
    mov ax, [rl_projected+bx+2]
    mov [di+2], ax
    add di, 4
    loop .copy
    mov al, [si]
    mov [rl_facecolor], al
    call OSAPI_SET_COLOR
    ; Cull away-facing triangles by their signed projected area.
    mov ax, [rl_triangle+4]
    sub ax, [rl_triangle]
    mov bx, [rl_triangle+10]
    sub bx, [rl_triangle+2]
    imul bx
    mov [rl_area], ax
    mov [rl_area+2], dx
    mov ax, [rl_triangle+8]
    sub ax, [rl_triangle]
    mov bx, [rl_triangle+6]
    sub bx, [rl_triangle+2]
    imul bx
    sub [rl_area], ax
    sbb [rl_area+2], dx
    cmp word [rl_area+2], 0
    jl .culled
    mov ax, [rl_area]
    or ax, [rl_area+2]
    jz .culled
    call rl_triangle_fill
.culled:
    pop bp
    pop si
    add si, 4
    dec bp
    jnz .triangle
    mov al, CBLACK
    call OSAPI_SET_COLOR
    ret
rl_triangle_fill:
    mov ax, [rl_triangle+2]
    mov bx, ax
    mov si, 6
.bounds:
    mov dx, [rl_triangle+si]
    cmp dx, ax
    jge .max
    mov ax, dx
.max:
    cmp dx, bx
    jle .next
    mov bx, dx
.next:
    add si, 4
    cmp si, 14
    jb .bounds
    mov [rl_scan_y], ax
    mov [rl_scan_end], bx
.row:
    mov word [rl_scan_min], 0x7FFF
    mov word [rl_scan_max], 0
    mov si, rl_triangle
    mov bp, 3
.edge:
    mov [rl_edge_start], si
    mov di, si
    add di, 4
    cmp di, rl_triangle+12
    jb .ordered
    mov di, rl_triangle
.ordered:
    mov ax, [si+2]
    mov bx, [di+2]
    cmp ax, bx
    jle .ascending
    xchg si, di
    xchg ax, bx
.ascending:
    push si
    push di
    mov dx, [rl_scan_y]
    cmp dx, ax
    jl .skip
    cmp dx, bx
    jge .skip                     ; top-inclusive, bottom-exclusive edges
    sub bx, ax
    sub dx, ax
    mov ax, [di]
    sub ax, [si]
    imul dx
    idiv bx
    add ax, [si]
    cmp ax, [rl_scan_min]
    jge .right
    mov [rl_scan_min], ax
.right:
    cmp ax, [rl_scan_max]
    jle .skip
    mov [rl_scan_max], ax
.skip:
    pop di
    pop si
    mov si, [rl_edge_start]
    add si, 4
    dec bp
    jnz .edge
    mov ax, [rl_scan_min]
    mov cx, [rl_scan_max]
    cmp ax, cx
    jg .nextrow
    mov dx, [rl_scan_y]
    cmp byte [rl_video], VID_VGA
    jne .mono
    mov bx, cx
    call OSAPI_GFX_HLINE
    jmp .nextrow
.mono:
    mov bx, dx
    mov si, rl_shade_dark
    cmp byte [rl_facecolor], CRED
    je .pattern
    mov si, rl_shade_mid
    cmp byte [rl_facecolor], CLRED
    je .pattern
    mov si, rl_shade_light
.pattern:
    call OSAPI_GFX_FILL_PAT
.nextrow:
    inc word [rl_scan_y]
    mov ax, [rl_scan_y]
    cmp ax, [rl_scan_end]
    jb .row
    ret
rl_vertices:
    dw -20,-16,-20, 20,-16,-20, 20,16,-20, -20,16,-20
    dw -20,-16,20, 20,-16,20, 20,16,20, -20,16,20
rl_edges: db 0,4, 4,8, 8,12, 12,0, 16,20, 20,24, 24,28, 28,16
          db 0,16, 4,20, 8,24, 12,28
rl_triangles: db 4,20,24,CRED, 4,24,8,CRED
              db 0,16,20,CLRED, 0,20,4,CLRED
              db 0,4,8,CYELLOW, 0,8,12,CYELLOW
              db 0,12,28,CRED, 0,28,16,CRED
              db 12,8,24,CLRED, 12,24,28,CLRED
              db 16,28,24,CYELLOW, 16,24,20,CYELLOW
rl_poses: dw 128,0, 110,64, 64,110, 0,128, -64,110, -110,64
          dw -128,0, -110,-64, -64,-110, 0,-128, 64,-110, 110,-64
rl_angle: dw 0
rl_cos: dw 110
rl_sin: dw 64
rl_project_scale: dw 112
rl_centerx: dw 128
rl_centery: dw 32
rl_projected: times 8 dd 0
rl_triangle: times 3 dd 0
rl_rx: dw 0
rl_ry: dw 0
rl_tz: dw 0
rl_rz: dw 0
rl_area: dd 0
rl_lx: dw 0
rl_ly: dw 0
rl_lendx: dw 0
rl_lendy: dw 0
rl_ldx: dw 0
rl_ldy: dw 0
rl_lsx: dw 0
rl_lsy: dw 0
rl_lerr: dw 0
rl_edge_start: dw 0
rl_scan_y: dw 0
rl_scan_end: dw 0
rl_scan_min: dw 0
rl_scan_max: dw 0
rl_facecolor: db 0
rl_shade_dark: db 0xEE,0xBB,0xEE,0xBB,0xEE,0xBB,0xEE,0xBB
rl_shade_mid: db 0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55
rl_shade_light: db 0x11,0x44,0x11,0x44,0x11,0x44,0x11,0x44

; Mandelbrot in signed Q8.8. 64x32 samples, at most 24 iterations per point.
; Display blocks fill the entire canvas: 4x2 on CGA, 4x4 on taller screens.
rl_fractal:
    mov ax, [rl_canvas_h]
    mov cl, 5
    shr ax, cl
    mov [rl_fr_blockh], ax
    mov word [rl_fr_row], 0
    mov word [rl_fr_ci], -320
.row:
    mov word [rl_fr_col], 0
    mov word [rl_fr_cr], -576
.point:
    mov ax, [rl_fr_cr]
    mov bx, [rl_fr_ci]
    call rl_fractal_point
    mov [rl_fr_escape], ax
    mov cx, [rl_fr_col]
    shl cx, 1
    shl cx, 1
    add cx, [rl_x]
    mov [rl_fr_px], cx
    mov ax, [rl_fr_row]
    mul word [rl_fr_blockh]
    add ax, [rl_y]
    mov [rl_fr_py], ax
    mov al, CBLACK
    cmp word [rl_fr_escape], 24
    je .ink
    mov ax, [rl_fr_escape]
    xor dx, dx
    mov bx, 14
    div bx
    mov ax, dx
    inc ax
.ink:
    call OSAPI_SET_COLOR
    mov ax, [rl_fr_px]
    mov bx, [rl_fr_py]
    mov cx, ax
    add cx, 3
    mov dx, bx
    add dx, [rl_fr_blockh]
    dec dx
    cmp byte [rl_video], VID_VGA
    je .solid
    cmp word [rl_fr_escape], 24
    je .solid
    mov si, rl_shade_light
    test word [rl_fr_escape], 1
    jz .pattern
    mov si, rl_shade_mid
.pattern:
    call OSAPI_GFX_FILL_PAT
    jmp .next
.solid:
    call OSAPI_GFX_FILL
.next:
    add word [rl_fr_cr], 13
    inc word [rl_fr_col]
    cmp word [rl_fr_col], 64
    jb .point
    add word [rl_fr_ci], 20
    inc word [rl_fr_row]
    cmp word [rl_fr_row], 32
    jb .row
    mov al, CBLACK
    call OSAPI_SET_COLOR
    ret

; AX/BX = real/imaginary c, Q8.8; AX = escape iteration or 24 (bounded).
rl_fractal_point:
    mov [rl_fr_cx], ax
    mov [rl_fr_cy], bx
    mov word [rl_fr_zx], 0
    mov word [rl_fr_zy], 0
    xor di, di
.iterate:
    mov ax, [rl_fr_zx]
    cmp ax, -512
    jl .out
    cmp ax, 512
    jg .out
    imul ax
    mov al, ah                    ; low word of signed product >> 8
    mov ah, dl
    mov [rl_fr_x2], ax
    mov ax, [rl_fr_zy]
    cmp ax, -512
    jl .out
    cmp ax, 512
    jg .out
    imul ax
    mov al, ah
    mov ah, dl
    mov [rl_fr_y2], ax
    add ax, [rl_fr_x2]
    cmp ax, 1024                  ; |z| squared >= 4
    jae .out
    mov ax, [rl_fr_zx]
    imul word [rl_fr_zy]
    mov al, ah
    mov ah, dl
    shl ax, 1
    add ax, [rl_fr_cy]
    mov [rl_fr_zy], ax
    mov ax, [rl_fr_x2]
    sub ax, [rl_fr_y2]
    add ax, [rl_fr_cx]
    mov [rl_fr_zx], ax
    inc di
    cmp di, 24
    jb .iterate
.out:
    mov ax, di
    ret
rl_fr_blockh: dw 2
rl_fr_row: dw 0
rl_fr_col: dw 0
rl_fr_cr: dw 0
rl_fr_ci: dw 0
rl_fr_escape: dw 0
rl_fr_px: dw 0
rl_fr_py: dw 0
rl_fr_cx: dw 0
rl_fr_cy: dw 0
rl_fr_zx: dw 0
rl_fr_zy: dw 0
rl_fr_x2: dw 0
rl_fr_y2: dw 0

; Keep sample 3's full method/overhead table and add all samples plus means.
rl_sample_report:
    call bl_blank
    mov si, rl_average_intro
    call bl_sline
    mov si, rl_flagkey
    call bl_sline
    mov si, rl_sample_columns
    call bl_sline
    xor bp, bp
.row:
    call bl_lclr
    mov bx, bp
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov si, [rl_table+bx]
    xor di, di
    call bl_lput
    mov bx, bp
    shl bx, 1
    shl bx, 1
    mov ax, [rl_samples+bx]
    mov dx, [rl_samples+bx+2]
    mov di, 32
    mov cx, 10
    call bl_dec
    mov ax, [rl_samples+bx+RL_ROWS*4]
    mov dx, [rl_samples+bx+RL_ROWS*4+2]
    mov di, 43
    call bl_dec
    mov ax, [rl_samples+bx+RL_ROWS*8]
    mov dx, [rl_samples+bx+RL_ROWS*8+2]
    mov di, 54
    call bl_dec
    mov ax, [rl_results+bx]
    mov dx, [rl_results+bx+2]
    mov di, 65
    call bl_dec
    mov bx, bp
    mov al, [rl_sampleflags+bx]
    mov [bl_lscr+75], al
    mov al, [rl_sampleflags+bx+RL_ROWS]
    mov [bl_lscr+76], al
    mov al, [rl_sampleflags+bx+RL_ROWS*2]
    mov [bl_lscr+77], al
    call bl_lcommit
    inc bp
    cmp bp, RL_ROWS
    jb .row
    ret
rl_lastpass: db 'Sample 3 raw timings and method flags (see all samples and means below):', 0
rl_average_intro: db 'Arithmetic mean of 3 complete runs; indices use these mean timing counts.', 0
rl_flagkey: db 'Sample flags: P=PIT, T=ticks, w=PIT-lap fallback, !=near wrap.', 0
rl_sample_columns: db 'Workload                           Run 1      Run 2      Run 3       Mean', 0
