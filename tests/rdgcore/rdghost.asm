; =============================================================================
; os8088 - tests/rdgcore/rdghost.asm
;
; RealDMG's core on a BARE machine: a boot sector that loads this harness,
; the 64KB core (build/realdmg/rdgcore.bin) and a Game Boy ROM, then runs the
; ROM with no os8088 underneath. It is how the core is gated before there is
; a package (docs/plans/REALDMG-PLAN.md wave 1), and how its cost per
; M-cycle is measured on machines that time things - MartyPC's 8088 and
; 86Box's 286 - without the OS's own costs in the number.
;
; Built by tests/rdgcore/rdgimg.py, which patches the geometry and the three
; lengths into the boot sector and appends the harness, the core and the ROM.
;
;   MODE 0 (test)   run until FRAMES frames have passed; what the game sends
;                   on its link port goes to the screen and COM1. Blargg's
;                   test ROMs print their verdict that way.
;   MODE 1 (bench)  run FRAMES frames and print FRAMES, the BIOS ticks they
;                   took, and the core's own counts.
;   MODE 2 (trace)  as MODE 0, with an RDG_TRACE core: one line per
;                   instruction on COM1 only (tools/gbtrace.py diffs it).
;   MODE 3 (play)   as MODE 1, pressing buttons on gbprof's platformer
;                   script, with the counters and the ticks restarted at
;                   frame 800.
;   MODE 4 (idle)   as MODE 3 with no buttons pressed.
;   MODE 5 (render) as MODE 3, composing every frame with rdg_render - the
;                   picture's CPU cost, with no screen written.
;   MODE 6 (damage) as MODE 3, and every third frame composed twice: by
;                   rdg_renderd into a buffer it keeps, and whole by
;                   rdg_render. The DONE line counts the frames whose two
;                   pictures differ (REALDMG-SPEC section 13.2's gate).
;   MODE 12         as MODE 7, timed, onto VGA mode 0Dh: rdg_renderd's
;   (renderv)       changed lines to VRAM as it decides them (rdg_dvga), as
;                   the package's full screen does.
;   MODE 13 (state) as MODE 3, with a SAVE STATE taken 200 frames before the
;                   end (rdgsys's rdg_stcompose and the view's 8000-FFFF, copied
;                   to STATE_SEG) and put back at the end (rdg_stcheck,
;                   rdg_stapply), the same 200 frames then run again. The DONE
;                   line's state sums - the picture and the Game Boy's memory
;                   at the end, each time - must agree (REALDMG-SPEC 10.4).
;                   STATE_SEG is the second cache segment's: no JF_SEG2.
;                   It loads into the machine reset (rdg_init, the recompiler
;                   emptied), so a field rdg_stapply misses is wrong - or with
;                   b_ilace 1, which MODE 13 has no other use for, into the
;                   machine that ran on, its translated code kept.
;
; Memory: the boot sector at 0000:7C00, the stack below it, this harness at
; 0800:0000, the core at 1000:0000, the view at 2000:0000, the ROM from
; 3000:0000 (up to 368KB).
; =============================================================================

cpu 8086
bits 16

%ifdef FRAME_SUM
%define NO_MODE13
%endif
HOST_SEG        equ 0x0760                  ; (35,328 bytes under the core: the
                                            ;   boot sector moves itself out of the
                                            ;   way first, BOOT_SEG, and the stack
                                            ;   is HOST_STK's, under the harness)
BOOT_SEG        equ 0x2000                  ; the boot sector's copy, at 2000:7C00
                                            ;   (the view, not loaded from disk)
HOST_STK        equ 0x7600                  ; 0:7600 down, over FRAME_SEG's end
CORE_SEG        equ 0x1000
VIEW_SEG        equ 0x2000
ROM_SEG         equ 0x3000
CACHE_SEG       equ 0x9000                  ; the recompiler's 64KB, and its
HASH_SEG        equ 0x8000                  ;   block table's 64KB before it,
HASH_MAXSEC     equ (HASH_SEG - ROM_SEG) / 32 ; past a ROM of 320KB or less -
HASH16_SEG      equ 0x8C00                  ;   or its 16KB, for one to 368KB
CACHE2_SEG      equ 0x7000                  ; a second cache segment (16.2.1),
CACHE2_MAXSEC   equ (CACHE2_SEG - ROM_SEG) / 32 ;   over a ROM of 256KB or less
JIT2_LIM        equ 0x0400                  ; JF_SQZ: each segment's code ends
JOY_NO3RD       equ 01h                     ; b_joy: no Start at frame 700 - in
                                            ;   Kirby's Dream Land the game has
                                            ;   begun by then, and it pauses
JOY_FLAP        equ 02h                     ; b_joy: A 6 frames of every 12,
                                            ;   Up held, and no B
JF_MODE         equ 03h                     ; b_jit: 0 interpret, 1 translate,
JF_SEG2         equ 04h                     ;   2 Static; a second segment; both
JF_SQZ          equ 10h                     ;   squeezed to JIT2_LIM's 1KB;
JF_SMALL        equ 08h                     ;   the block table at 16KB;
JF_NOCORE       equ 20h                     ;   no core tier (REALDMG-SPEC 16.2.2)
PRF_SEG         equ 0x05A0                  ; Static's profile, 4KB (not MODES 5-9,
                                            ;   which compose into FRAME_SEG)
FRAME_SEG       equ 0x05A0                  ; rdg_render's 5,760 bytes, above the
                                            ;   damage memory's RDG_DMG_SIZE (to 5896h:
                                            ;   INTERLACE writes its end) and under the
                                            ;   harness's stack (HOST_STK): the harness
                                            ;   (with the recompiler's templates) runs
                                            ;   past A000h
FRAMED_SEG      equ 0x0060                  ; MODE 6: rdg_renderd's buffer
DMG_SEG         equ 0x01D0                  ;   and its damage memory
STATE_SEG       equ 0x7000                  ; MODE 13's copy of a save state

; --- the boot sector ---------------------------------------------------------
section boot start=0 vstart=0x7C00
boot:
    jmp short boot_go
    nop
b_spt       db 9                        ; patched: sectors a track
b_heads     db 2                        ; patched: heads
b_hostsec   dw 0                        ; patched: harness sectors
b_coresec   dw 128
b_romsec    dw 0                        ; patched: ROM sectors
b_mode      db 0                        ; patched: MODE
b_frames    dw 600                      ; patched: FRAMES
b_jit       db 1                        ; patched: translate (REALDMG-SPEC 16)
b_joy       db 0                        ; patched: JOY_ flags (joy_script)
b_ilace     db 0                        ; patched: rdg_ilace, for MODE 12
boot_go:
    cli
    cld
    xor ax,ax                           ; out of the harness's way: itself to
    mov ds,ax                           ;   BOOT_SEG:7C00, and its stack with it
    mov si,0x7C00
    mov ax,BOOT_SEG
    mov es,ax
    mov ss,ax
    mov sp,0x7C00
    mov di,si
    mov cx,256
    rep movsw
    mov ds,ax
    sti
    jmp BOOT_SEG:boot_rel
boot_rel:
    mov [b_drive],dl
    mov word [b_lba],1
    mov ax,HOST_SEG
    mov cx,[b_hostsec]
    call load
    mov ax,CORE_SEG
    mov cx,[b_coresec]
    call load
    mov ax,ROM_SEG
    mov cx,[b_romsec]
    call load
    ; hand the harness its parameters through its own header
    mov ax,HOST_SEG
    mov es,ax
    mov al,[b_mode]
    mov [es:h_mode],al
    mov ax,[b_frames]
    mov [es:h_frames],ax
    mov ax,[b_romsec]
    mov [es:h_romsec],ax
    mov al,[b_jit]
    mov ah,al
    and al,JF_MODE
    mov [es:h_jit],al
    mov [es:h_jitf],ah
    mov al,[b_joy]
    mov [es:h_joy],al
    mov al,[b_ilace]
    mov [es:rdg_ilace],al
    jmp HOST_SEG:host_main

; load CX sectors from LBA [b_lba] to AX:0000, one at a time
load:
    jcxz .r
    push ax
    push cx
    mov es,ax
    mov ax,[b_lba]
    xor dx,dx
    mov bl,[b_spt]
    xor bh,bh
    div bx                              ; AX = track, DX = sector - 1
    mov cl,dl
    inc cl
    xor dx,dx
    mov bl,[b_heads]
    div bx                              ; AX = cylinder, DX = head
    mov ch,al
    mov dh,dl
    mov dl,[b_drive]
    mov di,4
.try:
    xor bx,bx
    mov ax,0x0201
    int 0x13
    jnc .ok
    xor ax,ax
    int 0x13
    dec di
    jnz .try
    mov si,s_err
.p:
    lodsb
    or al,al
    jz $
    mov ah,0x0E
    int 0x10
    jmp .p
.ok:
    inc word [b_lba]
    pop cx
    pop ax
    add ax,0x20
    loop load
.r:
    ret
b_drive db 0
b_lba   dw 0
s_err   db "rdghost: disk error",0
    times 510-($-$$) db 0
    dw 0xAA55

; --- the harness -------------------------------------------------------------
section host follows=boot vstart=0
h_mode      db 0
h_frames    dw 0
h_romsec    dw 0
h_jit       db 0
h_jitf      db 0                        ; JF_ flags
h_joy       db 0                        ; JOY_ flags

%include "rdgdefs.inc"

host_main:
    xor ax,ax                           ; the stack under the harness, not
    mov ss,ax                           ;   in it (the boot sector's was at
    mov sp,HOST_STK                     ;   0:7C00, inside it now)
    mov ax,cs
    mov ds,ax
    mov es,ax
    cmp byte [h_mode],12                ; MODE 12: MODE 7 onto VGA
    jne .m89
    mov byte [h_hw],1
    mov byte [h_mode],7
.m89:
    cmp byte [h_mode],8                 ; MODES 8, 9 and 10: MODE 6 with
    jb .mode                            ;   Interlace On, Scrolling or Doubled
    cmp byte [h_mode],10
    ja .mode
    mov al,[h_mode]
    sub al,7
    mov [rdg_ilace],al
    mov byte [h_mode],6
.mode:
    mov ax,0x0003                       ; a clean text screen
    int 0x10
    ; COM1, 115200 8N1, polled
    mov dx,0x3FB
    mov al,0x80
    out dx,al
    mov dx,0x3F8
    mov al,1
    out dx,al
    inc dx
    xor al,al
    out dx,al
    mov dx,0x3FB
    mov al,3
    out dx,al
    mov si,s_hello
    call puts
    ; rdg_init: view, core, ROM, banks = sectors / 32
    mov dx,[h_romsec]
    mov cl,5
    shr dx,cl
    cmp dx,2
    jae .b
    mov dx,2
.b:
    mov ax,VIEW_SEG
    mov bx,CORE_SEG
    mov cx,ROM_SEG
    call rdg_init
    cmp byte [h_mode],2
    jne .nt
    call rdg_trace_on
    jmp short .nojit
.nt:
    push ds
    mov ax,VIEW_SEG
    mov ds,ax
    cmp byte [cs:h_jit],0               ; the recompiler, unless asked not:
    je .plain                           ;   then the interpreter as it was
    cmp byte [cs:h_jit],2               ; Static (REALDMG-SPEC 16.8): the
    jne .dyn                            ;   profile at PRF_SEG, recorded by
    mov word [S_PRFSEG],PRF_SEG         ;   the first pass
    mov word [S_PRFMASK],1023
    push es                             ; ...over 4KB cleared first: nothing
    mov ax,PRF_SEG                      ;   else does, and at 05A0h the BIOS
    mov es,ax                           ;   has left bytes there - rdgstatic's
    xor di,di                           ;   "empty profile" preloaded 2 blocks
    mov cx,2048                         ;   out of them
    xor ax,ax
    rep stosw
    pop es
.dyn:
    call h_jitsegs                      ;   before there was one
    call rt_init
    jmp short .jdone
.plain:
    xor al,al
    call rt_dispatch
.jdone:
    pop ds
.nojit:
    ; the BIOS tick count as we start
    push ds
    xor ax,ax
    mov ds,ax
    mov ax,[0x46C]
    mov dx,[0x46E]
    pop ds
    mov [t0],ax
    mov [t0+2],dx
.run:
    mov ax,0x8000
    call rdg_run
    cmp al,RDGX_FRAME
    je .frame
    cmp al,RDGX_BUDGET
    je .budget
    cmp al,RDGX_STOP
    je .run
    ; a bad opcode: say where and stop
    mov si,s_bad
    call puts
    push ds
    mov ds,[cs:rdg_viewseg]
    mov al,[S_BADOP]
    mov bx,[S_PC]
    pop ds
    call hex8
    mov al,' '
    call putc
    mov ax,bx
    call hex16
    jmp .end
.budget:
    inc word [nbudget]                  ; a budget is 8000h M-cycles, under two
    mov ax,[h_frames]                   ;   frames: with the LCD off no frame
    shl ax,1                            ;   ever ends, so a run stops after
    add ax,100                          ;   2 x FRAMES + 100 of them anyway
    cmp ax,[nbudget]
    ja .run
    jmp .end
.frame:
    inc word [nframes]
    cmp byte [h_mode],3
    jb .nj
    call joy_script
%ifndef NO_MODE13                       ; (NO_MODE13: room for an experiment -
    cmp byte [h_mode],13                ;   FRAME_SUM's - and no MODE 13)
    jne .n13
    call st_frame
    jmp short .nj
.n13:
%endif
    cmp byte [h_mode],5                 ; MODE 5: compose every frame too
    jne .n6
    push es
    mov ax,FRAME_SEG
    mov es,ax
    xor di,di
    call rdg_render
    pop es
    jmp short .nj
.n6:
    cmp byte [h_mode],6                 ; MODE 6: by damage and whole
    jne .n7
    call dmg_check
    jmp short .nj
.n7:
    cmp byte [h_mode],7                 ; MODE 7: by damage only, every
    jne .nj                             ;   frame - the package's composer,
    push es                             ;   timed (rdgbench --renderd)
    cmp word [cs:rdg_dmgseg],0
    jne .d7
    mov word [cs:rdg_dmgseg],DMG_SEG
    mov ax,DMG_SEG
    mov es,ax
    mov byte [es:RDG_DFULL],1
    cmp byte [cs:h_hw],0
    je .d7
    mov ax,0x000D
    int 0x10
    mov word [cs:rdg_dvga],0xA000        ; (each changed line to VGA, ppu_dvga)
.d7:
    mov ax,FRAMED_SEG
    mov es,ax
    xor di,di
    call rdg_renderd
%ifdef FRAME_SUM
    call fsum
%endif
%ifdef SCRL_STAT
    call scrl_stat
%endif
    pop es
.nj:
    mov ax,[nframes]
    cmp ax,[h_frames]
    jb .run
    cmp byte [h_jit],2                  ; Static: the first pass recorded the
    jne .end                            ;   profile; the game again from its
    cmp byte [pass],0                   ;   start, preloaded and fixed, for a
    jne .end                            ;   second pass the same length
    inc byte [pass]
%ifndef RDG_PANMODES                     ; (no Static in the pan modes' host:
    call h_static                       ;   it is short of room, and
%endif                                  ;   rt_preload is left out of it)
    jmp .run
.end:
    push ds
    xor ax,ax
    mov ds,ax
    mov ax,[0x46C]
    mov dx,[0x46E]
    pop ds
    sub ax,[t0]
    sbb dx,[t0+2]
    mov [ticks],ax
    cmp byte [h_hw],0                   ; (a VGA writer's mode 0Dh: the text
    je .tx                              ;   screen back for the DONE line,
    mov ax,0x0003                       ;   which 86Box's runs read)
    int 0x10
.tx:
    mov si,s_done
    call puts
    mov ax,[nframes]
    call dec16
%ifdef FRAME_SUM
    mov si,s_fsum
    call puts
    mov ax,[cs:fs_sum+2]
    call hex16
    mov ax,[cs:fs_sum]
    call hex16
    mov si,s_fsum
    call puts
    mov ax,[cs:fs_chg+2]
    call hex16
    mov ax,[cs:fs_chg]
    call hex16
%endif
    mov si,s_ticks
    call puts
    mov ax,[ticks]
    call dec16
    mov si,s_ev
    call puts
    push ds
    mov ds,[cs:rdg_viewseg]
    mov ax,[S_EVCOUNT]
    mov bx,[S_SKIPPED]
    mov cx,[S_SKIPPED+2]
    pop ds
    call dec16
    mov si,s_skip
    call puts
    mov ax,cx
    call hex16
    mov ax,bx
    call hex16
    mov si,s_jit                        ; the recompiler's blocks and flushes
    call puts
    push ds
    mov ds,[cs:rdg_viewseg]
    mov ax,[S_JITNB]
    mov bx,[S_JITFL]
    pop ds
    call dec16
    mov si,s_jfl
    call puts
    mov ax,bx
    call dec16
    mov si,s_jsw
    call puts
    push ds
    mov ds,[cs:rdg_viewseg]
    mov ax,[S_JITSW]
    pop ds
    call dec16
    mov si,s_jby
    call puts
    push ds
    mov ds,[cs:rdg_viewseg]
    mov ax,[S_JITBY]
    mov bx,[S_JITADV]
    pop ds
    call dec16
    mov si,s_jadv
    call puts
    mov ax,bx
    call dec16
    mov si,s_jrcy                       ; tiers recycled in turn (16.2.3)
    call puts
    push ds
    mov ds,[cs:rdg_viewseg]
    mov ax,[S_JITRCY]
    pop ds
    call dec16
    cmp byte [h_jit],2                  ; Static: the blocks preloaded, and
    jne .nst                            ;   the ROM blocks translated since,
    mov si,s_stat                       ;   which must be 0 (HRAM's may be)
    call puts
    mov ax,[npre]
    call dec16
    mov si,s_stat2
    call puts
    push ds
    mov ds,[cs:rdg_viewseg]
    mov ax,[S_JITNBR]
    pop ds
    sub ax,[nbpre]
    call dec16
    mov si,s_stat3                      ; the preload's own time
    call puts
    mov ax,[tpre]
    call dec16
.nst:
%ifdef RT_DEBUG
    push ds
    mov ds,[cs:rdg_viewseg]
    mov si,JIT_BUF+0C0h
    mov cx,16
.dbg:
    lodsw
    pop ds
    call hex16
    push ds
    mov ds,[cs:rdg_viewseg]
    loop .dbg
    pop ds
%endif
%ifdef SCRL_STAT
    mov si,s_scrl                       ; scroll frames, then the lines a
    call puts                           ;   hardware scroll would still have
    mov ax,[scrl_n]                     ;   to compose: a sprite's now or
    call dec16                          ;   the last frame's, a scroll of its
    mov si,s_scrl1                      ;   own (a status bar), either -
    call puts                           ;   each lines a frame, on average
    mov ax,[scrl_spr]
    mov dx,[scrl_spr+2]
    call .d16
    mov si,s_scrl2
    call puts
    mov ax,[scrl_hud]
    mov dx,[scrl_hud+2]
    call .d16
    mov si,s_scrl3
    call puts
    mov ax,[scrl_any]
    mov dx,[scrl_any+2]
    call .d16
    jmp short .scd
.d16:                                   ; DX:AX / the frames: a frame's
    mov cx,[scrl_n]                     ;   average
    jcxz .z
    div cx
    jmp dec16
.z:
    xor ax,ax
    jmp dec16
.scd:
%endif
    cmp byte [h_mode],6
    jne .nd
    mov si,s_dmg
    call puts
    mov ax,[dmg_bad]
    call dec16
    mov si,s_dmg1
    call puts
    mov ax,[dmg_first]
    call dec16
    mov si,s_dmg2
    call puts
    mov ax,[dmg_lines]
    call dec16
    mov si,s_dmg3
    call puts
    mov ax,[dmg_off]
    call dec16
%ifdef DMG_DEBUG
    mov si,dmg_dump
    mov cx,85
.dd:
    lodsb
    call hex8
    loop .dd
%endif
.nd:
%ifndef NO_MODE13
    cmp byte [h_mode],13
    jne .ns
    mov si,s_st
    call puts
    mov ax,[st_suma]
    call hex16
    mov al,' '
    call putc
    mov ax,[st_sumb]
    call hex16
    mov si,s_stbad
    cmp byte [st_bad],0
    je .ns
    call puts
%endif
.ns:
    mov si,s_end
    call puts
    cmp byte [h_mode],3                 ; MODES 3 and 4 end with the picture's
    jb .halt                            ; inputs, for tests/rdgcore/rdgshot.py
    mov si,s_vram
    call puts
    push ds
    mov ds,[cs:rdg_viewseg]
    mov si,8000h                        ; VRAM
    mov cx,2000h
    call dump
    mov si,0FE00h                       ; OAM
    mov cx,0A0h
    call dump
    mov si,0FF40h                       ; LCDC to WX
    mov cx,0Ch
    call dump
    mov si,0A000h                       ; and A000-A0FF, where Blargg's ROMs
    mov cx,100h                         ;   leave their verdict as text
    call dump
    mov si,S_AF                         ; and the registers, where Mooneye's
    mov cx,12                           ;   leave theirs: AF BC DE HL SP PC
    call dump
    pop ds
    push es                             ; and the frame, as rdg_render
    mov ax,FRAME_SEG                    ;   composes it
    mov es,ax
    xor di,di
    call rdg_render
    pop es
    push ds
    mov ax,FRAME_SEG
    mov ds,ax
    xor si,si
    mov cx,5760
    call dump
    pop ds
    push ds                             ; and what it was composed from: the
    mov ds,[cs:rdg_viewseg]              ;   raster log, the snapshot, and the
    mov si,RDG_RLOG                      ;   frame counter and tags
    mov cx,RDG_RLOG_MAX*4
    call dump
    mov si,RDG_SNAP
    mov cx,8
    call dump
    mov si,S_FRAMES
    mov cx,2
    call dump
    mov al,[S_SNAPTAG]
    call com1
    mov al,[S_RLOGN]
    call com1
    pop ds
.halt:
    hlt
    jmp .halt

; dmg_check - MODE 6: every third frame, rdg_renderd against rdg_render
dmg_check:
    push ax
    push cx
    push dx
    push si
    push di
    push ds
    push es
    cmp word [cs:rdg_dmgseg],0
    jne .on
    mov word [cs:rdg_dmgseg],DMG_SEG     ; the first time: everything
    mov ax,DMG_SEG
    mov es,ax
    mov byte [es:RDG_DFULL],1
.on:
    mov ax,[nframes]
    xor dx,dx
    mov cx,3
    div cx
    or dx,dx
    jz .chk
    cmp byte [cs:rdg_ilace],0            ; INTERLACE: every frame composed, as
    je .r                               ;   the package does, so what one call
    mov ax,FRAMED_SEG                   ;   leaves is carried into the next
    mov es,ax                           ;   frame's - a scroll's into a still
    xor di,di                           ;   one's (Scrolling)
    call rdg_renderd
    jmp .r
.chk:
    mov ax,FRAMED_SEG
    mov es,ax
    xor di,di
    call rdg_renderd                     ; AX = the lines it changed
    add [cs:dmg_lines],ax
    cmp byte [cs:rdg_ilace],0            ; INTERLACE: a second call, which
    je .whole                           ;   must compose what the first left
    call rdg_renderd                     ;   to the other parity
.whole:
    mov ax,FRAME_SEG
    mov es,ax
    xor di,di
    call rdg_render
    mov ax,FRAMED_SEG
    mov ds,ax
    xor si,si
    xor di,di
    mov cx,5760 / 2
    repe cmpsw
    je .r
    inc word [cs:dmg_bad]
    cmp word [cs:dmg_first],0
    jne .r
    mov ax,[cs:nframes]
    mov [cs:dmg_first],ax
    mov [cs:dmg_off],si                 ; (past the first word that differs)
%ifdef DMG_DEBUG
    mov ax,si                           ; both copies of that line
    dec ax
    dec ax
    xor dx,dx
    mov cx,40
    div cx
    mul cx
    mov si,ax
    push ds
    pop ax
    push cs
    pop es
    mov di,dmg_dump
    mov cx,40
    rep movsb
    sub si,40
    mov ax,FRAME_SEG
    mov ds,ax
    mov cx,40
    rep movsb
    mov ax,DMG_SEG
    mov ds,ax
    mov si,RDG_DTCH
    mov cx,5
    rep movsb
%endif
.r:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret
h_hw        db 0                        ; MODE 12: onto VGA

dmg_bad     dw 0
dmg_first   dw 0
dmg_lines   dw 0
dmg_off     dw 0
%ifdef DMG_DEBUG
dmg_dump    times 85 db 0
%endif

; MODE 3's input, tools/gbprof/gbprof.c's platformer script frame for frame:
; Start at 150, 400 and 700, Right held, A for 14 frames of every 50, B for
; the last 29 of every 400. At frame 800 the counters start from zero, as
; gbprof's statistics do, so the two shares compare like for like.
%ifndef NO_MODE13
; st_frame - MODE 13, after each frame: the state taken at h_frames - 200,
; and at h_frames the sum, then the state back and the frames again
st_frame:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov ax,[h_frames]
    sub ax,200
    cmp ax,[nframes]
    jne .end
    mov ds,[cs:rdg_viewseg]              ; TAKE: the record, then it and the
    call rdg_stcompose                   ;   Game Boy's 8000-FFFF - one run
    mov si,RDG_STREC
    mov ax,STATE_SEG
    mov es,ax
    xor di,di
    mov cx,(10000h - RDG_STREC) / 2
    cld
    rep movsw
    jmp short .r
.end:
    mov ax,[nframes]
    cmp ax,[h_frames]
    jne .r
    call st_sum
    cmp byte [st_pass],0
    jne .two
    mov [st_suma],ax
    inc byte [st_pass]
    cmp byte [cs:rdg_ilace],0            ; (b_ilace 1: into the machine that
    jne .keep                           ;   ran on, its cache kept, as the
    call st_reset                       ;   package's is) BACK into a machine
.keep:                                  ;   just switched
    mov es,[cs:rdg_viewseg]              ;   on, as a game opened and then
    mov ax,STATE_SEG                    ;   loaded is - every field it misses,
    mov ds,ax
    xor si,si
    mov di,RDG_STREC
    mov cx,(10000h - RDG_STREC) / 2
    cld
    rep movsw
    mov ds,[cs:rdg_viewseg]              ;   wrong: the run, then the record's
    call rdg_stcheck                     ;   checks and fields
    jnc .ok
    mov byte [cs:st_bad],1
.ok:
    call rdg_stapply
    mov ax,[cs:h_frames]
    sub ax,200
    mov [cs:nframes],ax
    push ds                             ; and the buttons that frame had: a
    mov ax,cs                           ;   state leaves the ones held NOW
    mov ds,ax
    call joy_script
    pop ds
    jmp short .r
.two:
    mov [st_sumb],ax
.r:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; st_reset - the machine as host_main started it: rdg_init, and the recompiler
; (its cache emptied with it) or the interpreter. Kept: DS, ES
st_reset:
    push ds
    push es
    push ax
    push bx
    push cx
    push dx
    mov ax,cs
    mov ds,ax
    mov dx,[h_romsec]
    mov cl,5
    shr dx,cl
    cmp dx,2
    jae .b
    mov dx,2
.b:
    mov ax,VIEW_SEG
    mov bx,CORE_SEG
    mov cx,ROM_SEG
    call rdg_init
    mov ax,VIEW_SEG
    mov ds,ax
    cmp byte [cs:h_jit],0
    je .plain
    call h_jitsegs
    call rt_init
    jmp short .r
.plain:
    xor al,al
    call rt_dispatch
.r:
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    pop ds
    ret

; st_sum - AX = a sum of the picture rdg_render composes now and the Game
; Boy's 8000-FFFF, a word at a time
st_sum:
    push bx
    push cx
    push si
    push di
    push ds
    push es
    mov ax,FRAME_SEG
    mov es,ax
    xor di,di
    call rdg_render
    xor bx,bx
    mov ax,FRAME_SEG
    mov ds,ax
    xor si,si
    mov cx,5760 / 2
    cld
.p:
    lodsw
    add bx,ax
    loop .p
    mov ds,[cs:rdg_viewseg]
    mov si,8000h
    mov cx,4000h
.m:
    lodsw
    add bx,ax
    rol bx,1
    loop .m
    mov ax,bx
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    ret

st_pass db 0
st_bad  db 0
st_suma dw 0
st_sumb dw 0
%endif

joy_script:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    mov bx,[nframes]
    xor dl,dl
    cmp byte [cs:h_mode],4              ; MODE 4: the same, no buttons
    je .nb
    mov dl,01h                          ; Right
    mov ax,bx
    sub ax,150
    cmp ax,6
    jb .st
    mov ax,bx
    sub ax,400
    cmp ax,6
    jb .st
    test byte [cs:h_joy],JOY_NO3RD      ; (a game Start at 700 would PAUSE)
    jnz .nst
    mov ax,bx
    sub ax,700
    cmp ax,6
    jae .nst
.st:
    or dl,80h
.nst:
    mov cx,50                           ; A: 14 frames of every 50, or with
    mov si,14                           ;   JOY_FLAP 6 of every 12 - Kirby
    test byte [cs:h_joy],JOY_FLAP       ;   flies, where a jump stops at the
    jz .ad                              ;   first tall wall
    mov cx,12
    mov si,6
    or dl,04h                           ; (and Up held: Kirby climbs)
.ad:
    mov ax,bx
    push dx
    xor dx,dx
    div cx
    cmp dx,si
    pop dx
    jae .na
    or dl,10h
.na:
    mov ax,bx
    push dx
    xor dx,dx
    mov cx,400
    div cx
    cmp dx,370
    pop dx
    jbe .nb
    test byte [cs:h_joy],JOY_FLAP       ; (B lets a flying Kirby's air out)
    jnz .nb
    or dl,20h
.nb:
    mov ds,[cs:rdg_viewseg]
    mov [S_JOY],dl
    cmp bx,800
    jne .r
    xor ax,ax
    mov [S_SKIPPED],ax
    mov [S_SKIPPED+2],ax
    mov [S_EVCOUNT],ax
    mov ds,ax                           ; and the ticks: the frames timed are
    mov ax,[0x46C]                      ; 800 onwards, the game in play
    mov dx,[0x46E]
    mov [cs:t0],ax
    mov [cs:t0+2],dx
.r:
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rdg_putc: what the game sends on its link port, to COM1 and the screen - or
; in MODE 2 to the screen only, COM1 carrying nothing but trace records
rdg_putc:
    push ax
    push dx
    cmp byte [cs:h_mode],2
    je .s0
    call com1
.s0:
    cmp al,10
    jne .s
    mov al,13
    call scr
    mov al,10
.s:
    call scr
    pop dx
    pop ax
    ret

; CX bytes at DS:SI, raw, to COM1
dump:
    lodsb
    call com1
    loop dump
    ret

; rdg_trbyte: a trace record's byte, raw, to COM1
rdg_trbyte:
    push dx
    call com1
    pop dx
    ret
com1:
    push ax
    mov dx,0x3FD
.w:
    in al,dx
    test al,0x20
    jz .w
    pop ax
    mov dx,0x3F8
    out dx,al
    ret

putc:
    jmp rdg_putc
scr:
    push ax
    push bx
    mov ah,0x0E
    xor bx,bx
    int 0x10
    pop bx
    pop ax
    ret
puts:
    push ax
.l:
    mov al,[cs:si]
    inc si
    or al,al
    jz .r
    call putc
    jmp .l
.r:
    pop ax
    ret
hex16:
    push ax
    mov al,ah
    call hex8
    pop ax
hex8:
    push ax
    shr al,1
    shr al,1
    shr al,1
    shr al,1
    call .n
    pop ax
.n:
    push ax
    and al,0x0F
    add al,'0'
    cmp al,'9'
    jbe .p
    add al,7
.p:
    call putc
    pop ax
    ret
dec16:
    push ax
    push bx
    push cx
    push dx
    mov bx,10
    xor cx,cx
.d:
    xor dx,dx
    div bx
    push dx
    inc cx
    or ax,ax
    jnz .d
.o:
    pop ax
    add al,'0'
    call putc
    loop .o
    pop dx
    pop cx
    pop bx
    pop ax
    ret

s_hello db "RealDMG core harness",10,0
s_bad   db 10,"RDGHOST BADOP ",0
s_done  db 10,"RDGHOST DONE frames ",0
%ifdef FRAME_SUM
s_fsum  db " fsum ",0
fs_sum  dd 0
fs_chg  dd 0
; fsum - FRAME_SUM's: every composed frame's buffer and its changed lines
; folded into two dwords, so two builds can be shown to compose the same
fsum:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    add [cs:fs_chg],ax
    adc word [cs:fs_chg+2],0
    mov ax,FRAMED_SEG
    mov ds,ax
    xor si,si
    mov cx,2880
    mov bx,[cs:fs_sum]
    mov dx,[cs:fs_sum+2]
.l:
    lodsw                               ; (Fletcher's: a word's place counts)
    add bx,ax
    add dx,bx
    loop .l
    mov [cs:fs_sum],bx
    mov [cs:fs_sum+2],dx
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif
s_dmg   db " damage-bad ",0
%ifdef SCRL_STAT
s_scrl  db " scroll-frames ",0
s_scrl1 db " spr ",0
s_scrl2 db " hud ",0
s_scrl3 db " any ",0
scrl_n      dw 0
scrl_spr    dd 0
scrl_hud    dd 0
scrl_any    dd 0
scrl_prev   times 144 db 0

; scrl_stat - MODE 7 with SCRL_STAT: after each rdg_renderd, the lines of a
; SCROLLING frame (RDG_DSTALE) a hardware scroll would still compose
scrl_stat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov ax,DMG_SEG
    mov es,ax
    mov ds,[cs:rdg_viewseg]
    mov dx,[es:RDG_DLREG + 64 * 10 + 1]  ; the main scroll: line 64's
    xor bx,bx                           ; BX = the line
    mov si,PPU_LSPR
    mov di,RDG_DLREG + 1
    cmp byte [es:RDG_DSTALE],0
    je .upd
    inc word [cs:scrl_n]
.l:
    xor cx,cx                           ; CL = sprite, CH = its own scroll
    cmp byte [si],0
    je .np
    inc cl
.np:
    cmp byte [cs:scrl_prev+bx],0
    je .nq
    inc cl
.nq:
    mov al,[si]
    mov [cs:scrl_prev+bx],al
    cmp [es:di],dx
    je .nh
    inc ch
    add word [cs:scrl_hud],1
    adc word [cs:scrl_hud+2],0
.nh:
    or cl,cl
    jz .ns
    add word [cs:scrl_spr],1
    adc word [cs:scrl_spr+2],0
.ns:
    or cl,ch
    jz .na
    add word [cs:scrl_any],1
    adc word [cs:scrl_any+2],0
.na:
    add si,41
    add di,10
    inc bx
    cmp bx,144
    jb .l
    jmp short .r
.upd:
    mov al,[si]                         ; a still frame: the sprites' lines
    mov [cs:scrl_prev+bx],al            ;   kept for the next
    add si,41
    inc bx
    cmp bx,144
    jb .upd
.r:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif
s_dmg1  db " first ",0
s_dmg2  db " lines ",0
s_dmg3  db " at ",0
s_ticks db " ticks ",0
s_ev    db " events ",0
s_skip  db " skipped ",0
s_jit   db " jit ",0
s_jfl   db " flushes ",0
s_jsw   db " seg2 ",0
s_jby   db " bytes/16 ",0
s_jadv  db " moves ",0
s_jrcy  db " recycled ",0
s_end   db " END",10,0
s_st    db " state ",0
s_stbad db " refused",0
s_stat  db " static ",0
s_stat2 db " after ",0
npre    dw 0
nbpre   dw 0
tpre    dw 0
s_stat3 db " ticks ",0

pass    db 0

; h_jitsegs - DS = the view: rt_init's AX, BX and CX from h_jitf, and the
; squeezed segments' end
h_jitsegs:
    mov ax,CACHE_SEG
    mov bx,HASH_SEG
    xor cx,cx
    mov dl,JIT_HMMAX                    ; the table's 64KB (JF_SMALL, or a
    test byte [cs:h_jitf],JF_SMALL      ;   ROM too big for it: 16KB)
    jnz .h16
    cmp word [cs:h_romsec],HASH_MAXSEC
    jbe .hm
.h16:
    mov bx,HASH16_SEG
    mov dl,7
.hm:
    mov byte [S_JITNOC],0               ; the core's holes the second tier,
    test byte [cs:h_jitf],JF_NOCORE     ;   or not: the A/B (16.2.2)
    jz .nc
    mov byte [S_JITNOC],1
.nc:
    mov word [S_JITLIM],0
    test byte [cs:h_jitf],JF_SQZ
    jz .s
    mov word [S_JITLIM],JIT2_LIM
.s:
    cmp word [cs:h_romsec],CACHE2_MAXSEC
    ja .r                               ; (a ROM over 304KB: no room)
    test byte [cs:h_jitf],JF_SEG2
    jz .r
    mov cx,CACHE2_SEG
.r:
    ret

; h_static - Static (REALDMG-SPEC 16.8), after the first pass: the game
; started again (rdg_init), the cache flushed, the profile the first pass
; recorded translated into it heaviest first, the table fixed, and the
; frames counted from 0 again. The profile is outside the view, which
; rdg_init zeroes.
%ifndef RDG_PANMODES
h_static:
    push ax
    push bx
    push cx
    push dx
    mov dx,[h_romsec]
    mov cl,5
    shr dx,cl
    cmp dx,2
    jae .b
    mov dx,2
.b:
    mov ax,VIEW_SEG
    mov bx,CORE_SEG
    mov cx,ROM_SEG
    call rdg_init
    push ds
    mov ds,[cs:rdg_viewseg]
    mov word [S_PRFSEG],PRF_SEG
    mov word [S_PRFMASK],1023
    call h_jitsegs
    call rt_init
    push es
    xor ax,ax
    mov es,ax
    mov ax,[es:0x46C]
    mov [cs:tpre],ax
    pop es
    call rt_preload
    mov [cs:npre],ax
    push es
    xor ax,ax
    mov es,ax
    mov ax,[es:0x46C]
    sub [cs:tpre],ax
    neg word [cs:tpre]
    pop es
    mov ax,[S_JITNBR]
    mov [cs:nbpre],ax
    pop ds
    mov word [nframes],0
    mov word [nbudget],0
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif
s_vram  db "RDGVRAM",0
nframes dw 0
nbudget dw 0
t0      dd 0
ticks   dw 0

%define RDG_TRHOST                       ; (rdgsys.inc: the trace's receiving half)
%include "rdgsys.inc"
%include "rdgppu.inc"
%define RT_SQUEEZE                      ; (b_jit's JF_SQZ)
%ifdef RDG_PANMODES
%define RT_NOPRELOAD                    ; (no Static in them: h_static's)
%endif
%include "rdgjit.inc"

; the harness ends under the core, at 1000:0000 (a run past it overwrites
; the core it loaded and hangs with nothing said)
host_end:
    times -((host_end - $$) > CORE_SEG * 16 - HOST_SEG * 16) db 0
