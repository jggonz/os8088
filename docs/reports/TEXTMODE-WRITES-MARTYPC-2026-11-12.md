# Text-mode write costs on an emulated 5150: CGA and Hercules

**A measurement, not a description.** It was taken on 2026-11-12 on branch
`multiplayer-fps`, cut from `elendilon` at `9f5c3c9c`, for
[docs/plans/MULTIPLAYER-FPS-PLAN.md](../plans/MULTIPLAYER-FPS-PLAN.md). It
answers one question that PERFORMANCE.md did not cover: what it costs to put
a text-mode frame on a 5150's screen. Every VRAM row in Part 2 was taken in a
GRAPHICS mode (Set 2 in CGA 640x200, Set 148 in mode 4), and the C160 figures
in SPEC.md 88.15 and 97.15 are for the 160x100 retime.

- **Instrument:** MartyPC's guest cycle counter.
  - Machines: `os8088_5150_cga_gla` and `os8088_5150_herc_gla`.
  - The counter runs at 4,772,727 Hz.
  - Each span runs between two `out 0E9h, al` markers, caught with an `io`
    breakpoint.
- **What the counts include:** the counts are exact for the emulator. They
  include DRAM refresh and the adapter wait states MartyPC models.
- **No kernel:** it is a bare boot sector, so no os8088 code runs.
- **Not real iron:** see *What this cannot say* before quoting a CGA figure.

## The rows

| row | RAM (baseline) | CGA 80-col text | CGA 40-col text | Hercules text (B000) |
|---|---|---|---|---|
| `rep movsw`, a whole 80x25 screen (2,000 words) | 52,451 clk, 26.2 a word | **72,092 clk, 36.0 a word** | n/a | **74,641 clk, 37.3 a word** |
| `rep movsw`, a whole 40x25 screen (1,000 words) | n/a | n/a | 36,080 clk, 36.1 a word | n/a |
| attribute byte only, `movsb`/`inc di`/`loop`, per cell | 87,029 per 2,000, 43.5 a cell | 96,076 per 2,000, **48.0 a cell** | 48,065 per 1,000, 48.1 | 96,068 per 2,000, 48.0 |
| SPARSE, `lodsw`/`xchg di,ax`/`mov [es:di],bl`/`loop` over a table of 400 scattered offsets | 25,264, 63.2 a cell | 27,172, **67.9 a cell** | n/a | 28,528, **71.3 a cell** |

In microseconds at 4.77 MHz:

- **A full 80x25 screen:** 15.1 ms on CGA and 15.6 ms on Hercules.
- **The attribute plane of 80x25:** 20.1 ms. This is the "80x50 half-block"
  frame: `▀` in every cell, with the foreground as the top pixel and the
  background as the bottom one.
- **400 scattered cells:** 5.7 ms on CGA and 6.0 ms on Hercules.

Against the frame budgets (one frame is 159,091 clk at 30 fps, 190,909 at 25
and 238,636 at 20):

| a frame presented as | clk | of a 30 fps frame |
|---|---|---|
| 80x25 char+attr, whole screen | 72,092 | 45% |
| 80x25 attribute plane (80x50 half-block), whole screen | 96,076 | 60% |
| 40x25 char+attr, whole screen | 36,080 | 23% |
| 160x100 C160 attribute plane, whole screen (8,000 x 48.0) | ~384,000 | 241%, so not a 30 fps mode |
| 600 changed cells, sparse | ~40,700 | 26% |
| 300 changed cells, sparse | ~20,400 | 13% |

## What this cannot say

1. **MartyPC charges the CGA's 80-column and 40-column text modes alike.**
   It charges 36.0 against 36.1 clk a word. A real IBM CGA gives the CPU fewer
   memory slots in 80-column ("high-resolution") text, which is why snow
   happens in that mode and not in the 40-column one. So the 80-column CGA
   row is probably OPTIMISTIC on the 1981 card.
   - Set 2 measured a real 5150's CGA in 640x200 graphics at 1.78x RAM for a
     word write. Here MartyPC's text mode is 1.37x.
   - Until the field confirms it, price 80-column CGA text at up to ~47 clk a
     word (1.78 x 26.2).
   - The Hercules row has no such doubt to the same degree, because the card
     is dual-ported enough to have no snow.
2. **Snow is invisible here.** MartyPC compares framebuffers, and snow is a
   property of what the CRTC fetched (docs/plans/CGA-SNOW-PLAN.md section 5).
3. **The `_gla` machines boot GLaBIOS rather than the IBM ROM.** The loops
   above run no BIOS code, so the ROM does not move a count. But they are
   still not field numbers: docs/FIELD-MACHINES.md says who can take those.

## Reproducing it

The bench is one boot sector. To run it:

1. Assemble it with `nasm -f bin tbench.asm -o tbench.bin`, and add
   `-DMONO` for the Hercules build.
2. Pad it to 368,640 bytes.
3. Boot it under `os88marty.launch(img, machine=..., boot=False)`.
4. Arm `breakpoints([{"type": "io", "addr": 0xE9}])`.
5. At each stop, read `status()["cycles"]` and the marker in AL.

```nasm
cpu 8086
org 7C00h
start:  cli
        xor ax,ax
        mov ds,ax
        mov ss,ax
        mov sp,7C00h
        sti
        cld
%ifdef MONO
        mov bx,0B000h
        mov al,7
        or byte [0410h],30h
%else
        mov bx,0B800h
        mov al,3
%endif
.col:   mov [vseg],bx
        xor ah,ah
        int 10h
        mov dx,3D8h
        cmp word [vseg],0B000h
        je .noblink
        mov al,09h              ; 80-col, video on, blink OFF
        out dx,al
.noblink:
        mov ax,2000h            ; source: 2000h:0, 4,000 pattern words
        mov es,ax
        xor di,di
        mov cx,4000
        mov ax,1FDFh
.fill:  stosw
        inc ah
        loop .fill
        mov di,8000h            ; 400 scattered odd offsets < 4000
        mov cx,400
        mov bx,1234h
.lcg:   mov ax,bx
        mov dx,25173
        mul dx
        add ax,13849
        mov bx,ax
        xor dx,dx
        mov si,2000
        div si
        shl dx,1
        inc dx
        mov ax,dx
        stosw
        loop .lcg
        mov ax,2000h
        mov ds,ax
        mov al,1                ; T1 rep movsw 2000 -> RAM
        out 0E9h,al
        mov ax,3000h
        mov es,ax
        xor si,si
        xor di,di
        mov cx,2000
        rep movsw
        mov al,2                ; T2 rep movsw 2000 -> video
        out 0E9h,al
        mov es,[cs:vseg]
        xor si,si
        xor di,di
        mov cx,2000
        rep movsw
        mov al,3                ; T3 attribute plane -> RAM
        out 0E9h,al
        mov ax,3000h
        mov es,ax
        xor si,si
        mov di,1
        mov cx,2000
.a1:    movsb
        inc di
        loop .a1
        mov al,4                ; T4 attribute plane -> video
        out 0E9h,al
        mov es,[cs:vseg]
        xor si,si
        mov di,1
        mov cx,2000
.a2:    movsb
        inc di
        loop .a2
        mov al,5                ; T5 400 sparse -> RAM
        out 0E9h,al
        mov ax,3000h
        mov es,ax
        mov si,8000h
        mov cx,400
        mov bl,5
.s1:    lodsw
        xchg di,ax
        mov [es:di],bl
        loop .s1
        mov al,6                ; T6 400 sparse -> video
        out 0E9h,al
        mov es,[cs:vseg]
        mov si,8000h
        mov cx,400
.s2:    lodsw
        xchg di,ax
        mov [es:di],bl
        loop .s2
        mov al,7
        out 0E9h,al
        cmp word [cs:vseg],0B000h
        je .done
        mov ax,1                ; 40x25, blink off
        int 10h
        mov dx,3D8h
        mov al,08h
        out dx,al
        mov al,8                ; T7 rep movsw 1000 -> 40-col
        out 0E9h,al
        xor si,si
        xor di,di
        mov cx,1000
        rep movsw
        mov al,9                ; T8 attribute plane, 1000 cells
        out 0E9h,al
        xor si,si
        mov di,1
        mov cx,1000
.a3:    movsb
        inc di
        loop .a3
        mov al,10
        out 0E9h,al
.done:  mov al,0FFh
        out 0E9h,al
        jmp $
vseg    dw 0
        times 510-($-$$) db 0
        dw 0AA55h
```
