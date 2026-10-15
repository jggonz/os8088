; =============================================================================
; os8088 - Color's eight presets as thm_tab rows (SPEC.md 76.14.14)
;
; ONE TABLE, TWO READERS, NEITHER RESIDENT. kernel/driver.inc's boot reader
; (.boot2, gone at the first mount) installs the row SYSTEM.CFG's 'TP' names
; into the kernel's one Color row, and THMPAL.DRV installs the row a click
; names - so the kernel itself carries the Color row it already had and no
; preset at all. Included by both as SOURCE, never copied (CLAUDE.md's
; WEAVE rule), so the two cannot disagree about a colour.
;
; A row is thm_tab's own ten bytes, in kernel/vga12.inc's live-block order:
;   menu bg, menu ink, disabled ink, active title bg, active title ink,
;   desktop, stripes, inactive title bg, inactive title ink, menu XOR mask
; The stripes byte is 0 here: 'TS' is installed over it separately. The XOR
; mask is what the open menu's highlight bar is XORed with (SPEC.md 76.14.14.1):
; on grey (7) paper and black (0) ink a mask m gives a (7 xor m) bar with
; m-coloured text, so 0Fh is the dark-grey bar every other theme draws.
; =============================================================================
THM_PRESET_ROWB equ 10
THM_PRESET_N    equ 8

%macro THM_PRESET_ROWS 0
    db 7, 0, 8, 1, 15,  3, 0, 7,  0, 0x0F  ; 0 Classic - the row Color always had
    db 7, 0, 8, 1, 15,  9, 0, 8, 15, 14    ; 1 Workbench: light blue bar, yellow
    db 7, 0, 8, 3, 15,  7, 0, 8, 15,  8    ; 2 Light: white bar, dark grey
    db 7, 0, 8, 2, 15, 10, 0, 8, 15, 0x0F  ; 3 Forest
    db 7, 0, 8, 5, 15, 13, 0, 8, 15, 0x0F  ; 4 Amethyst
    db 7, 0, 8, 6, 15, 14, 0, 8, 15,  9    ; 5 Amber: yellow bar, light blue
    db 7, 0, 8, 8, 15,  1, 0, 7,  0,  7    ; 6 Midnight: black bar, light grey
    db 7, 0, 8, 4, 15,  6, 0, 8, 15, 0x0F  ; 7 Desert
%endmacro
