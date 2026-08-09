; =============================================================================
; os8088 - HDD.DRV / hdsec.inc - the sector buffers
;
; INCLUDED LAST BY BOTH IMAGES, and that placement is the whole file's reason.
;
; Both are int 13h transfer targets on the HDK_BIOS rung, and int 13h moves a
; sector per call but does NOT stop that sector straddling a 64KB physical
; boundary - only starting 512-aligned does, and the DMA controller answers a
; straddle with error 09h (SPEC.md 50.3). A heap claim's base is 512-aligned
; in linear terms - the heap floor is, and every claim is a whole number of
; KB - so a 512-aligned OFFSET is the whole of the protection, in either
; image.
;
; Last, because `align 512` in the middle of an image pads up to 511 bytes of
; nothing into it; at the end it pads only what the alignment actually needs.
; And hd_sec0 costs its align NOTHING, because hd_mbr is exactly 512 bytes
; long and already aligned - which is how the pair sat in hdd.asm before the
; split, and why keeping them adjacent gives those bytes back.
; =============================================================================
    align 512
hd_mbr:      times 512 db 0     ; the partition table being read or edited
%ifdef HD_TOOL
hd_sec0:     times 512 db 0     ; the formatter's sector under construction
%endif
hd_mbrok:    db 0               ; HMB_*: whether hd_mbr is the DISK's table
