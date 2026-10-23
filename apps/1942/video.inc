; Adapter-native renderer. VGA latch restoration/page flipping; CGA packed runs.
; All ports and foreign-mode stores are confined to the FSX bracket (SPEC 101).
N_LIST_BYTES equ 1536
N_BACK equ N_PAGE_BYTES*2
N_CBUF equ 16384
; The canvas is the one claim the instance holds for its whole life, sized
; from what is composed into it (SPEC 101.1): the desktop splash cache, or
; CGA's background and composition. Everything READ is a fetched part of
; 1942.O88 in a claim of its own (n_readbank), which op_fetch sizes from the
; part's row - so a part can neither be longer than its claim nor be read
; twice into one. The cartridge build keeps 64KB for its upper-half world.
%define N_MAX(a,b) ((a)+((b)-(a))*((b)>(a)))
%if N_ROM
N_CANVAS_KB equ 64                 ; WORLD bank occupies the upper half
%else
N_CANVAS_BYTES equ N_MAX(N_FRONT_END,N_CBUF*2)
N_CANVAS_KB equ (N_CANVAS_BYTES+1023)/1024
%endif

; THE PARTS, in the order tools/1942assets.py's outputs are packed into the
; file: the Makefile's N1942PARTS names the same files in the same order, and
; tests/n1942.py checks each row's length against 1942art.inc. Every row is
; OP_COMP | OP_LAZY: only the adapter on the glass is ever read, and a part
; held is a claim (MEM_OWNER_MAX). The two sprite banks are held for the
; bracket and dropped on the way out; the cache planes, a scene and the
; effects are copied where they are used and dropped at once.
N_P_V      equ 0                   ; 1942V.GFX   VGA sprite bank
N_P_VX     equ 1                   ; 1942VX.GFX  ...its continuation
N_P_L      equ 2                   ; 1942L.GFX   VGA latch cache planes
N_P_C      equ 3                   ; 1942C.GFX   CGA sprite bank
N_P_CX     equ 4                   ; 1942CX.GFX  ...its continuation
N_P_SFX    equ 5                   ; 1942.SFX    Sound Blaster one-shots
N_P_SCENE  equ 6                   ; the scenes, VGA's then CGA's
%if N_ROM
N_PARTS    equ N_P_SCENE+2         ; WORLD.V42 WORLD.C42
%else
N_PARTS    equ N_P_SCENE+6         ; SEA REEF PORT .V42, SEA PORT REEF .C42
%endif
OS88_PARTS_BEGIN N_PARTS
%rep N_PARTS
    OS88_PART OP_ASSET, OP_LAZY | OP_COMP
%endrep
OS88_PARTS_END

n_palette:
    call n_pushall
    cmp byte [n_cga],0
    jne .cga
    mov dx,3c8h
    xor al,al
    out dx,al
    inc dx
    mov si,n_dac
    cmp word [n_stage],29
    jb .normalpal
    mov si,n_latedac
.normalpal:
    mov cx,N_COLORS*3
.loop:
    lodsb
    out dx,al
    loop .loop
    jmp .out
.cga:
    xor bx,bx
    mov bl,[n_profile]
    mov al,[n_cgaregs+bx]
    mov dx,3d9h
    out dx,al
.out:
    jmp n_popall

; Fetch part AL - claim, read, expand (SPEC 20.12.7.4). Out: CF=0 and ES =
; its segment; CF=1 and a toast has said why. The bytes are this file's own,
; built with the package, so there is no size or checksum to check against
; a stale pair: a short read and a stream that does not expand to the row's
; exact length are the standard's refusals. Clobbers AX,BX,CX,DX,SI,DI.
n_readbank:
    push ax
    call op_fetch
    pop ax
    jc .out
    call op_seg
    mov es,ax
.out:
    ret
n_loadgfx:
    cmp byte [n_cga],0
    jne .bank
    mov al,N_P_L
    call n_readbank
    jc .out
    push ds
    mov ax,es
    mov es,[n_fsi+FSI_SEG]
    mov ds,ax
    xor si,si
    mov bx,1
.cacheplane:
    mov dx,3c4h
    mov al,2
    mov ah,bl
    out dx,ax
    mov di,N_CACHE_START
    mov cx,N_CACHE_BYTES
    rep movsb
    shl bl,1
    cmp bl,16
    jb .cacheplane
    pop ds
    mov al,N_P_L                   ; in the latches now: the claim goes back
    call op_drop
.bank:
    mov al,N_P_V
    cmp byte [n_cga],0
    je .read
    mov al,N_P_C
.read:
    call n_readbank
    jc .out
    mov [n_graphics],es
    mov dx,es                      ; ...and the selector to FAR CALL its
    call OSAPI_MEM_CODESEL         ; compiled sprites through: the segment
    mov [n_graphicsc],ax           ; itself on every real-mode kernel
    mov al,N_P_VX
    cmp byte [n_cga],0
    je .extra
    mov al,N_P_CX
.extra:
    call n_readbank
    jc .out
    mov [n_graphics2],es
    mov dx,es
    call OSAPI_MEM_CODESEL
    mov [n_graphics2c],ax
.out:
    ret
; Would the heap hold what the bracket is about to fetch? DL = FSX_CAPS's
; display kind. CF=1 = no: both sprite banks plus the largest scene, each
; priced as op_fetch will claim it (op_lazykb: the read, its cluster slack and
; R). It is asked on the DESKTOP because the fetches happen after the mode is
; set, where op_fetch's refusal toast would be drawn into the game's own
; framebuffer (SPEC 53.7) - so the refusal a busy heap can cause is said here,
; in the window, and what is left to refuse inside is a disk that will not
; read. Clobbers AX,BX,CX,DX,SI,BP.
n_fsxroom:
    mov bx,N_P_V*256+N_P_SCENE
    cmp dl,VID_VGA
    je .adapter
%if N_ROM
    mov bx,N_P_C*256+N_P_SCENE+1
%else
    mov bx,N_P_C*256+N_P_SCENE+3
%endif
.adapter:
    call op_clbget                 ; (DFREE: it spends DX, so DL is read above)
    jc .out
    xor bp,bp                      ; the largest scene...
%if N_ROM
    mov cx,1
%else
    mov cx,3
%endif
    mov al,bl
.scene:
    call op_row
    push ax
    call op_lazykb
    pop si                         ; (the row index; SI is free here)
    jc .out
    cmp ax,bp
    jbe .next
    xchg ax,bp
.next:
    xchg ax,si
    inc ax
    loop .scene
    mov al,bh                      ; ...plus the bank and its continuation
    mov cl,2
.bank:
    call op_row
    push ax
    call op_lazykb
    pop si
    jc .out
    add bp,ax
    jc .out
    xchg ax,si
    inc ax
    loop .bank
    call OSAPI_MEM_AVAIL           ; AX = KB
    cmp ax,bp
.out:
    ret
; Both banks back, on every way out of the bracket. A row never fetched is a
; no-op for op_drop, so a load that failed half way unwinds through here too.
n_dropgfx:
    push ax
    mov al,N_P_V
.drop:
    call op_drop
    inc ax
    cmp al,N_P_CX
    jbe .drop
    xor ax,ax
    mov [n_graphics],ax
    mov [n_graphics2],ax
    pop ax
    ret
%if !N_ROM
; Scene -> part row, VGA's three then CGA's. REEF.C42 is the LAST part of the
; file because it pads most to its sector (510 bytes packed) and the last part
; is not padded - so CGA's row order is SEA, PORT, REEF (N1942SCENES).
n_scenerow: db N_P_SCENE,N_P_SCENE+1,N_P_SCENE+2,N_P_SCENE+3,N_P_SCENE+5,N_P_SCENE+4
n_scenecheck:
    mov ax,[n_stage]
    dec ax
    shr ax,1
    shr ax,1
    xor dx,dx
    mov bx,3
    div bx
    cmp dx,[n_scene]
    je .same
    call n_loading
    cmp word [n_scene],0ffffh
    je .resume
    mov word [n_scroll],0
.resume:
    push dx
    mov bx,dx
    cmp byte [n_cga],0
    je .index
    add bx,3
.index:
    mov al,[n_scenerow+bx]
    push ax
    call n_readbank
    pop ax
    pop dx
    jc .bad
    mov [n_scene],dx
    push ax                        ; the row, for the drop once it is copied
    cmp byte [n_cga],0
    jne .cga
    SAVE
    push ds
    mov ax,es
    mov es,[n_fsi+FSI_SEG]
    mov ds,ax
    xor si,si                      ; the part is the four planes, no header
    mov bx,1
.plane:
    mov dx,3c4h
    mov al,2
    mov ah,bl
    out dx,ax
    mov di,N_BACK+8
    mov bp,240
.row:
    mov cx,32
    rep movsw
    add di,16
    dec bp
    jnz .row
    shl bl,1
    cmp bl,16
    jb .plane
    pop ds
    RESTORE
    jmp .copied
.cga:
    push ds
    push es
    mov es,[n_canvas]
    pop ds
    xor si,si
    xor di,di
    mov cx,8000
    rep movsw
    pop ds
.copied:
    pop ax
    call op_drop                   ; the backdrop is in VRAM / the canvas
.refresh:
    call n_scrollsetup
    call n_refresh
.same:
    clc
    ret
.bad:
    mov byte [n_error],2
    stc
    ret

%endif

; Select this page's rectangle list. 256 records is more than all actors,
; bullets and message glyphs together, including old/new CGA footprints.
n_list:
    mov bx,[n_page]
    shl bx,1
    mov di,[n_counts+bx]
    mov bx,n_rects
    cmp word [n_page],0
    je .out
    add bx,N_LIST_BYTES
.out:
    ret
n_mark:
    cmp byte [n_prepare],2
    je .quick
    push ax
    push bx
    push di
    cmp byte [n_background],0
    jne .out
    mov byte [n_rendered],1
    call n_list
    cmp di,N_LIST_BYTES
    jae .out
    add bx,di
    mov ax,[n_origin]
    mov [bx],ax
    mov ax,[n_width]
    mov [bx+2],ax
    mov ax,[n_height]
    mov [bx+4],ax
    mov bx,[n_page]
    shl bx,1
    add word [n_counts+bx],6
.out:
    pop di
    pop bx
    pop ax
.quick:
    ret
; CX,DX position; BX,BP dimensions -> native offset/byte width/height/phase.
n_geometry:
    push si
    mov si,dx
    shl si,1
    mov ax,[n_yv+si]
    mov dx,cx
    mov [n_height],bp
    cmp byte [n_cga],0
    je .vga
    mov ax,[n_yc+si]
    mov si,cx
    shl si,1
    mov dx,[n_xc+si]
    ; CGA extents follow from the logical ones: BX = width (BH = 0), BP =
    ; height, both <= 64. Two 65-byte tables, not two words per sprite.
    mov si,bp
    mov cl,[n_cgaht+si]
    xor ch,ch
    mov bp,cx
    mov bl,[n_cgawt+bx]
    mov [n_height],bp
    jmp .common
.vga:
    add ax,8
.common:
    mov cx,dx
    and cx,3
    mov [n_phase],cx
    shr dx,1
    shr dx,1
    add ax,dx
    mov [n_origin],ax
    add bx,cx
    add bx,3
    shr bx,1
    shr bx,1
    mov [n_width],bx
    pop si
    ret

; SI=sprite number, CX/DX logical origin. Opaque spans are precompiled.
n_sprite:
    SAVE
    push ds
    cmp cx,256
    jae .out
    cmp dx,240
    jae .out
    mov bx,si
    shl bx,1
    mov [n_spriteid],bx
    mov ax,[n_cacheindex+bx]
    mov [n_cacheptr],ax
    cmp byte [n_cga],0
    jne .position
    or ax,ax
    jz .position
    and cx,0fffeh
.position:
    mov es,[n_graphics]
    cmp bx,N_SPRITE_SPLIT*2
    jb .bankselected
    sub bx,N_SPRITE_SPLIT*2
    mov es,[n_graphics2]
.bankselected:
    mov [n_spriteseg],es
    mov si,[es:bx+8]
    xor bx,bx
    mov bl,[es:si]
    mov [n_compiled],bl
    and bx,127
    xor ax,ax
    mov al,[es:si+1]
    mov bp,ax
    add ax,dx
    cmp ax,240
    ja .out
    mov ax,bx
    add ax,cx
    jc .out
    cmp ax,256
    ja .out
    call n_geometry
    call n_mark
    cmp byte [n_prepare],1
    jne .draw
    cmp byte [n_background],0
    jne .draw
    ; Hardware scrolling moves VRAM, not the linear composition buffer.
    ; Prepare every new actor footprint before drawing any actor so masked
    ; stores see current terrain and overlapping sprites preserve draw order.
    jmp .out
.draw:
    mov [n_record],si
    cmp byte [n_cga],0
    jne .cga
    cmp word [n_cacheptr],0
    jne .cached
    mov es,[n_fsi+FSI_SEG]
    mov ds,[n_spriteseg]
    xor bp,bp                      ; the plane. BP and, below, BX hold what
.plane:                            ; were two words stored through CS - nine
    mov bx,bp                      ; kern_mega faults a sprite - in registers
    add bx,[cs:n_phase]
    mov di,[cs:n_origin]
    add di,[cs:n_drawbase]
    cmp bx,4
    jb .carry
    inc di
.carry:
    and bx,3
    mov cl,bl
    mov ah,1
    shl ah,cl
    mov al,2
    mov dx,3c4h
    out dx,ax
    mov bx,bp
    shl bx,1
    add bx,[cs:n_record]
    mov si,[bx+2]
    mov bx,di                      ; this plane's origin
.run:
    lodsw
    cmp ax,0ffffh
    je .nextplane
    mov di,ax
    add di,bx
    xor cx,cx
    lodsb
    mov cl,al
    rep movsb
    jmp .run
.nextplane:
    inc bp
    cmp bp,4
    jb .plane
    jmp .out
.cached:
    mov bx,[n_cacheptr]
    add bx,[n_phase]
    mov bp,[bx]
    mov ax,[n_origin]
    add ax,[n_drawbase]
    mov [n_vorigin],ax
    mov dx,3ceh
    mov ax,4105h
    out dx,ax
    mov dx,3c4h
    mov ax,[n_fsi+FSI_SEG]
    mov es,ax
    mov ds,ax
    ; Groups: map-mask byte (0 ends), run-count byte, then 5-byte runs of
    ; destination word, cache word, length byte. BH and CH stay zero.
    xor bx,bx
    xor cx,cx
.group:
    mov ah,[cs:bp]
    or ah,ah
    jz .cachedone
    mov al,2
    out dx,ax
    mov bl,[cs:bp+1]
    add bp,2
.latches:
    mov di,[cs:bp]
    add di,[cs:n_vorigin]
    mov si,[cs:bp+2]
    mov cl,[cs:bp+4]
    rep movsb
    add bp,5
    dec bx
    jnz .latches
    jmp .group
.cachedone:
    mov dx,3ceh
    mov ax,4005h
    out dx,ax
    jmp .out
.cga:
    mov bx,[n_phase]
    shl bx,1
    add bx,si
    mov si,[es:bx+2]
    mov ax,[n_origin]
    cmp byte [n_background],0
    jne .back
    add ax,N_CBUF
.back:
    mov [n_vorigin],ax
    mov es,[n_canvas]
    test byte [n_compiled],128
    jnz .compiled
    mov ds,[n_spriteseg]
.crun:
    lodsw
    cmp ax,0ffffh
    je .out
    mov di,ax
    add di,[cs:n_vorigin]
    lodsw                         ; AL=count, AH=masked flag
    xor cx,cx
    mov cl,al
    or ah,ah
    jnz .masked
    rep movsb
    jmp .crun
.masked:
    lodsw
    and al,[es:di]
    or al,ah
    stosb
    loop .masked
    jmp .crun
.compiled:
    mov [n_blitptr],si
    mov ax,[n_graphicsc]           ; the bank's CODE selector (n_loadgfx): a
    cmp word [n_spriteid],N_SPRITE_SPLIT*2 ; far call through its data segment
    jb .codesel                    ; is a kern_mega fault per sprite
    mov ax,[n_graphics2c]
.codesel:
    mov [n_blitptr+2],ax
    mov di,[n_vorigin]
    call far [n_blitptr]
    jmp .out
.out:
    pop ds
    RESTORE
    ret

n_text:
    SAVE
.char:
    lodsb
    or al,al
    jz .out
    cmp al,32
    jb .char
    cmp al,90
    ja .char
    push si
    xor ah,ah
    sub ax,32
    add ax,n_font32art
    mov si,ax
    call n_sprite
    pop si
    add cx,8
    jmp .char
.out:
    RESTORE
    ret

; Restore one native rectangle from the stage cache. SI -> offset,width,height.
; Keep the row advance inline: a near call/return per scanline is costly on XT.
%macro N_RESTORE_ADVANCE 0
    add si,dx
    add di,dx
    cmp si,ax
    jb %%hud
    sub si,[cs:n_ringbytes]
    jmp %%next
%%hud:
    ; Only a full refresh crosses from the fixed HUD into the terrain.
    cmp si,[cs:n_cachestart]
    jne %%next
    add si,[cs:n_scroll]
%%next:
%endmacro
n_restore:
    SAVE
    push ds
    mov di,[si]
    mov bx,[si+2]
    mov bp,[si+4]
    mov si,di
    mov ax,[n_ringend]
    mov [n_cacheend],ax
    mov ax,[n_ringstart]
    mov [n_cachestart],ax
    cmp si,ax
    jb .position
    add si,[n_scroll]
    cmp si,[n_ringend]
    jb .position
    sub si,[n_ringbytes]
.position:
    cmp byte [n_cga],0
    jne .cga
    add si,N_BACK
    add word [n_cacheend],N_BACK
    add word [n_cachestart],N_BACK
    add di,[n_drawbase]
    mov dx,3ceh
    mov ax,4105h
    out dx,ax
    mov dx,3c4h
    mov ax,0f02h
    out dx,ax
    mov ax,[n_fsi+FSI_SEG]
    mov es,ax
    mov ds,ax
    mov dx,80
    sub dx,bx
    mov ax,[cs:n_cacheend]
    jmp .rows
.cga:
    add di,N_CBUF
    mov ax,[n_canvas]
    mov es,ax
    mov ds,ax
    mov dx,80
    sub dx,bx
    mov ax,[cs:n_cacheend]
.crows:
    mov cx,bx
    shr cx,1
    rep movsw
    jnc .ctail
    movsb
.ctail:
    N_RESTORE_ADVANCE
    dec bp
    jnz .crows
    jmp .restored
.rows:
    mov cx,bx
    rep movsb
    N_RESTORE_ADVANCE
    dec bp
    jnz .rows
.restored:
    pop ds
    cmp byte [n_cga],0
    jne .out
    mov dx,3ceh
    mov ax,4005h
    out dx,ax
.out:
    RESTORE
    ret
%unmacro N_RESTORE_ADVANCE 0
n_fullrect: dw 0,80,240
n_playrect: dw 16*80+8,64,224
n_cfullrect: dw 0,80,200
n_hudrect: dw 8,64,16
n_chudrect: dw 0,80,14
n_clear:
    call n_pushall
    mov byte [n_damagevalid],0
    mov word [n_cgastart],0
    mov word [n_counts],0
    mov word [n_counts+2],0
    mov word [n_oldcount],0
    cmp byte [n_cga],0
    jne .cga
    mov si,n_fullrect
    mov ax,[n_scroll]
    mov [n_vscrolls],ax
    mov [n_vscrolls+2],ax
    mov word [n_vbases],N_PAGE_PAD
    mov word [n_vbases+2],N_PAGE_BYTES+N_PAGE_PAD
    mov word [n_drawbase],N_PAGE_PAD
    call n_restore
    mov word [n_drawbase],N_PAGE_BYTES+N_PAGE_PAD
    call n_restore
    jmp .done
.cga:
    mov si,n_cfullrect
    call n_restore
    ; A full CGA update is needed after a scene change or menu.
    mov word [n_rects],0
    mov word [n_rects+2],80
    mov word [n_rects+4],200
    mov word [n_counts],6
    mov word [n_oldcount],6
.done:
    mov word [n_page],0
    mov word [n_drawbase],N_PAGE_PAD
    mov byte [n_rendered],1
    jmp n_popall
n_erase:
    SAVE
    mov word [n_scrollrows],0
    cmp word [n_flightphase],0
    jne .noscroll
    cmp byte [n_bosslive],0
    jne .noscroll
    call n_scrollstep
.noscroll:
    mov byte [n_damagevalid],0
    cmp byte [n_cga],0
    jne .cga
    ; Both hidden pages have a small row margin. Move the display origin
    ; through that margin instead of copying terrain. Rebase only when the
    ; page runs out of margin; the other page remains visible throughout.
    mov bx,[n_page]
    shl bx,1
    mov ax,[n_vscrolls+bx]
    sub ax,[n_scroll]
    jnc .delta
    add ax,[n_ringbytes]
.delta:
    mov [n_shift],ax
    mov dx,[n_scroll]
    mov [n_vscrolls+bx],dx
    mov dx,[n_vbases+bx]
    sub dx,ax
    jc .rebase
    xor ax,ax
    cmp bx,0
    je .floor
    mov ax,N_PAGE_BYTES
.floor:
    cmp dx,ax
    jb .rebase
    mov [n_vbases+bx],dx
    mov [n_drawbase],dx
    jmp .list
.rebase:
    mov ax,N_PAGE_PAD
    cmp bx,0
    je .base
    add ax,N_PAGE_BYTES
.base:
    mov [n_vbases+bx],ax
    mov [n_drawbase],ax
    mov word [n_counts+bx],0
    ; The side gutters stay black from mode setup. Rebase only the actual
    ; playfield and HUD, avoiding 3,840 needless VGA latch reads/writes.
    mov si,n_playrect
    call n_restore
    mov si,n_hudrect
    call n_restore
    jmp .done
.cga:
    ; MC6845 start address scrolls both 8KB banks by one pair of scanlines.
    ; This moves every visible terrain pixel without copying the playfield.
    mov word [n_shift],0
    cmp word [n_scrollrows],0
    je .list
    sub word [n_cgastart],80
    and word [n_cgastart],1fffh
    mov word [n_shift],160
.list:
    call n_list
    mov si,bx
    mov bp,di
.old:
    or bp,bp
    jz .incoming
    ; Last frame's actors moved down two native rows with the display.
    mov ax,[n_shift]
    add [si],ax
    mov ax,[si]
    xor dx,dx
    mov bx,80
    div bx
    mov bx,[n_worldheight]
    sub bx,ax
    jbe .offscreen
    cmp [si+4],bx
    jbe .restore
    mov [si+4],bx
    jmp .restore
.offscreen:
    mov word [si],0
    mov word [si+2],0
    mov word [si+4],1
.restore:
    cmp byte [n_cga],0
    jne .collected
    call n_restore
.collected:
    add si,6
    sub bp,6
    jmp .old
.incoming:
    cmp byte [n_cga],0
    jne .ctop
    mov bx,[n_page]
    shl bx,1
    mov word [n_counts+bx],0
    mov ax,[n_shift]
    xor dx,dx
    mov bx,80
    div bx
    add ax,16
    mov [n_vtoprect+4],ax
    mov si,n_vtoprect
    call n_restore
    jmp .done
.ctop:
    ; Fixed HUD + two newly exposed terrain rows. Old HUD pixels must not
    ; scroll into the playfield. The rest is already in the CGA hardware ring.
    mov word [n_origin],0
    mov word [n_width],80
    mov word [n_height],16
    call n_mark
    mov ax,[n_counts]
    mov [n_oldcount],ax
.done:
    mov byte [n_rendered],1
    RESTORE
    ret
n_vtoprect: dw 0,80,20
n_present:
    SAVE
    cmp byte [n_rendered],0
    je .out
    cmp byte [n_cga],0
    jne .cga
    mov bx,[n_drawbase]
    mov [n_showbase],bx
    mov dx,3d4h
    mov al,0ch
    mov ah,bh
    out dx,ax
    inc al
    mov ah,bl
    out dx,ax
    mov al,FSXW_VSYNC
    call OSAPI_FSX_WAIT
    xor word [n_page],1
    mov bx,[n_page]
    shl bx,1
    mov ax,[n_vbases+bx]
    mov [n_drawbase],ax
    jmp .done
.cga:
    cmp byte [n_damagevalid],0
    jne .ready
    call n_damagebuild
.ready:
    push ds
    mov bp,[n_damagecount]
    mov bx,n_rects+N_LIST_BYTES
.rect:
    or bp,bp
    jz .compact
    push bp
    push bx
    mov ax,[bx]
    xor dx,dx
    mov cx,80
    div cx
    mov di,ax
    shr ax,1
    mul cx
    test di,1
    jz .even
    add ax,2000h
.even:
    ; remainder of the original divide is recomputed without DIV.
    mov dx,[bx]
    mov si,di
    shl si,1
    mov cx,[n_yv+si]
    sub dx,cx
    mov di,ax
    add di,dx
    mov ax,di
    and ax,2000h
    add di,[n_cgastart]
    and di,1fffh
    or di,ax
    mov si,[bx]
    add si,N_CBUF
    mov bp,[bx+4]
    mov bx,[bx+2]
    mov es,[n_fsi+FSI_SEG]
    mov ds,[n_canvas]
.row:
    push di
    mov dx,di
    and dx,1fffh
    neg dx
    add dx,2000h                   ; bytes left in this physical bank
    mov cx,bx
    cmp cx,dx
    jbe .copy
    mov cx,dx
    shr cx,1
    rep movsw
    jnc .firsttail
    movsb
.firsttail:
    sub di,2000h                   ; wrap within the same even/odd bank
    mov cx,bx
    sub cx,dx
.copy:
    shr cx,1
    rep movsw
    jnc .tail
    movsb
.tail:
    pop di
    add si,80
    sub si,bx
    xor di,2000h
    test di,2000h
    jnz .bank
    add di,80
    and di,1fffh
.bank:
    dec bp
    jnz .row
    pop bx
    pop bp
    pop ds                         ; our DS, banked at .ready - not CS, which
    push ds                        ; on kern_mega is the read-only code alias
    add bx,6
    sub bp,6
    jmp .rect
.compact:
    pop ds
    ; The CGA address is in words and wraps separately in each 8KB bank.
    mov bx,[n_cgastart]
    shr bx,1
    mov dx,3d4h
    mov al,0ch
    mov ah,bh
    out dx,ax
    inc al
    mov ah,bl
    out dx,ax
    push ds
    pop es
    mov si,n_rects
    add si,[n_oldcount]
    mov di,n_rects
    mov cx,[n_counts]
    sub cx,[n_oldcount]
    mov [n_counts],cx
    shr cx,1
    rep movsw
    mov word [n_oldcount],0
.done:
    mov byte [n_rendered],0
.out:
    RESTORE
    ret

; Paint the HUD in the cache only when it changes. Clear the exact native band.
n_hudbegin:
    SAVE
    mov byte [n_background],1
    mov ax,[n_drawbase]
    mov [n_savedbase],ax
    mov word [n_drawbase],N_BACK
    cmp byte [n_cga],0
    jne .cga
    mov es,[n_fsi+FSI_SEG]
    mov dx,3c4h
    mov ax,0f02h
    out dx,ax
    mov di,N_BACK+8
    mov bp,16
    mov ax,0101h
.vrow:
    mov cx,32
    rep stosw
    add di,16
    dec bp
    jnz .vrow
    jmp .out
.cga:
    mov es,[n_canvas]
    xor di,di
    xor ax,ax
    mov cx,560
    rep stosw
.out:
    RESTORE
    ret
n_hudend:
    mov byte [n_background],0
    mov ax,[n_savedbase]
    mov [n_drawbase],ax
    ret
n_hudcopy:
    SAVE
    mov si,n_hudrect
    cmp byte [n_cga],0
    je .copy
    mov si,n_chudrect
.copy:
    cmp byte [n_prepare],1
    je .mark
    call n_restore
.mark:
    mov ax,[si]
    mov [n_origin],ax
    mov ax,[si+2]
    mov [n_width],ax
    mov ax,[si+4]
    mov [n_height],ax
    call n_mark
    RESTORE
    ret
